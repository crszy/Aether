#include "panel_animation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

#include <qlist.h>
#include <qmatrix4x4.h>
#include <qnamespace.h>
#include <qpoint.h>
#include <qquickitem.h>
#include <qrect.h>
#include <qsize.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtypes.h>

namespace qs::windows {

namespace {

constexpr qreal SOLVE_EPSILON = 1e-6;
constexpr int NEWTON_STEPS = 8;
constexpr int BISECTION_STEPS = 40;
constexpr qreal MAX_POPIN_PERCENT = 1000;
constexpr qreal MAX_MOTION = 1.5;

constexpr qreal SPRING_SETTLE = 0.001;
constexpr qreal MIN_SPRING_OVERSHOOT = 1e-4;
constexpr qreal MAX_SPRING_OVERSHOOT = 0.3;
constexpr int CRITICAL_SPRING_STEPS = 24;
constexpr qreal CRITICAL_SPRING_GUESS = 10;

constexpr qreal MAX_OVERSHOOT = 0.06;
constexpr qreal MAX_OVERSHOOT_PX = 6;

constexpr qreal REVEAL_TRAVEL_RATIO = 0.15;
constexpr qreal REVEAL_MIN_TRAVEL = 12;
constexpr qreal REVEAL_MAX_TRAVEL = 48;
constexpr qreal REVEAL_RADIUS_RATIO = 0.15;
constexpr qreal REVEAL_MIN_RADIUS = 12;
constexpr qreal REVEAL_MAX_RADIUS = 40;
constexpr qreal REVEAL_START_WIDTH = 0.6;

constexpr qreal BLUR_RATIO = 0.035;
constexpr qreal BLUR_MIN_RADIUS = 4;
constexpr qreal BLUR_MAX_RADIUS = 14;
constexpr qreal BLUR_VISIBLE_RADIUS = 0.25;

constexpr qreal CASCADE_DISTANCE_RATIO = 0.08;
constexpr qreal CASCADE_MIN_DISTANCE = 16;
constexpr qreal CASCADE_MAX_DISTANCE = 36;
constexpr qreal CASCADE_MAX_OVERSHOOT = 0.1;
constexpr qreal CASCADE_MAX_OVERSHOOT_PX = 4;
constexpr qreal CASCADE_OPACITY_FLOOR = 0.002;
constexpr int CASCADE_MAX_DEPTH = 10;
constexpr int CASCADE_CONTENT_DEPTH = 6;
constexpr int CASCADE_WEIGHT_DEPTH = 5;
constexpr int CASCADE_WEIGHT_BUDGET = 512;
constexpr qsizetype CASCADE_MIN_SECTIONS = 3;
constexpr qsizetype CASCADE_MAX_SECTIONS = 16;
constexpr qreal CASCADE_COVER_RATIO = 0.8;
constexpr qreal CASCADE_OVERLAP_RATIO = 0.25;

constexpr auto EMPHASIZED_DECEL = CubicBezier(0.05, 0.7, 0.1, 1);
constexpr auto EMPHASIZED_ACCEL = CubicBezier(0.3, 0, 0.8, 0.15);
constexpr auto STANDARD_DECEL = CubicBezier(0.2, 0, 0, 1);
constexpr auto STANDARD_ACCEL = CubicBezier(0.3, 0, 1, 1);
constexpr auto MENU_DECEL = CubicBezier(0.1, 1, 0, 1);
constexpr auto STALL = CubicBezier(1, -0.1, 0.7, 0.85);

const auto HYPRLAND_TIMING = PanelAnimationTiming {
    .showMotionMs = 270,
    .showFadeMs = 50,
    .hideMotionMs = 240,
    .hideFadeMs = 240,
    .showMotion = EMPHASIZED_DECEL,
    .showFade = MENU_DECEL,
    .hideMotion = CubicBezier(0.52, 0.03, 0.72, 0.08),
    .hideFade = STALL,
};

const auto CASCADE_TIMING = PanelCascadeTiming {
    .stepMs = 24,
    .maxDelayMs = 120,
    .sectionMs = 280,
    .fadeMs = 160,
};

std::optional<Qt::Edge> edgeFromName(const QString& name) {
	if (name == "top") return Qt::TopEdge;
	if (name == "bottom") return Qt::BottomEdge;
	if (name == "left") return Qt::LeftEdge;
	if (name == "right") return Qt::RightEdge;
	return std::nullopt;
}

QString edgeName(Qt::Edge edge) {
	switch (edge) {
	case Qt::TopEdge: return "top";
	case Qt::BottomEdge: return "bottom";
	case Qt::LeftEdge: return "left";
	case Qt::RightEdge: return "right";
	}

	return QString();
}

qreal snap(qreal value, qreal dpr) {
	if (dpr <= 0) return value;
	return std::round(value * dpr) / dpr;
}

bool verticalEdge(Qt::Edge edge) { return edge == Qt::TopEdge || edge == Qt::BottomEdge; }

qreal extentAlong(Qt::Edge edge, const QSizeF& size) {
	return verticalEdge(edge) ? size.height() : size.width();
}

QPointF towardEdge(Qt::Edge edge, qreal amount) {
	switch (edge) {
	case Qt::LeftEdge: return {-amount, 0};
	case Qt::RightEdge: return {amount, 0};
	case Qt::TopEdge: return {0, -amount};
	case Qt::BottomEdge: return {0, amount};
	}

	return {};
}

qreal area(const QRectF& rect) { return rect.isEmpty() ? 0 : rect.width() * rect.height(); }

bool shows(const QQuickItem* item) {
	return item->isVisible() && item->opacity() > 0 && item->width() > 0 && item->height() > 0;
}

bool hasContent(const QQuickItem* item, int depth) {
	if (item->flags().testFlag(QQuickItem::ItemHasContents)) return true;
	if (depth <= 0) return false;

	const auto children = item->childItems();
	return std::ranges::any_of(children, [depth](const QQuickItem* child) {
		return child->isVisible() && child->opacity() > 0 && hasContent(child, depth - 1);
	});
}

int contentWeight(const QQuickItem* item, int depth, int& budget) {
	if (budget-- <= 0) return 0;

	auto weight = item->flags().testFlag(QQuickItem::ItemHasContents) ? 1 : 0;
	if (depth <= 0) return weight;

	for (const auto* child: item->childItems()) {
		if (!child->isVisible() || child->opacity() <= 0) continue;
		weight += contentWeight(child, depth - 1, budget);
	}

	return weight;
}

QRectF boundsInParent(const QQuickItem* item) { return {item->position(), item->size()}; }

bool mostlyDisjoint(const QList<QQuickItem*>& items) {
	for (auto i = 0; i < items.size(); i++) {
		auto a = boundsInParent(items.at(i));

		for (auto j = i + 1; j < items.size(); j++) {
			auto b = boundsInParent(items.at(j));
			auto shared = area(a.intersected(b));
			if (shared > CASCADE_OVERLAP_RATIO * std::min(area(a), area(b))) return false;
		}
	}

	return true;
}

} // namespace

std::optional<PanelAnimationStyle> PanelAnimationStyle::parse(const QString& text) {
	auto words = text.simplified().toLower().split(' ', Qt::SkipEmptyParts);
	if (words.isEmpty()) return std::nullopt;

	auto style = PanelAnimationStyle();
	const auto& name = words.first();

	if (name == "noanim" || name == "none") {
		if (words.size() != 1) return std::nullopt;
		return style;
	}

	if (name == "fade") {
		if (words.size() != 1) return std::nullopt;
		style.kind = Kind::Fade;
		return style;
	}

	if (name == "slide") {
		style.kind = Kind::Slide;
		if (words.size() == 1) return style;
		if (words.size() != 2) return std::nullopt;

		style.edge = edgeFromName(words.at(1));
		if (!style.edge.has_value()) return std::nullopt;
		return style;
	}

	if (name == "popin") {
		style.kind = Kind::Popin;
		if (words.size() == 1) return style;
		if (words.size() != 2) return std::nullopt;

		auto number = words.at(1);
		if (number.endsWith('%')) number.chop(1);

		auto ok = false;
		auto percent = number.toDouble(&ok);
		if (!ok || !std::isfinite(percent) || percent < 0) return std::nullopt;

		style.popinScale = std::min(percent, MAX_POPIN_PERCENT) / 100.0;
		return style;
	}

	return std::nullopt;
}

QString PanelAnimationStyle::toString() const {
	switch (this->kind) {
	case Kind::None: return "noAnim";
	case Kind::Fade: return "fade";
	case Kind::Slide: return this->edge.has_value() ? "slide " + edgeName(*this->edge) : "slide";
	case Kind::Popin: return QString("popin %1%").arg(this->popinScale * 100.0);
	}

	return QString();
}

std::optional<PanelEffect> parsePanelEffect(const QString& text) {
	auto name = text.trimmed().toLower();
	if (name == "auto") return PanelEffect::Auto;
	if (name == "standard") return PanelEffect::Standard;
	if (name == "spring") return PanelEffect::Spring;
	if (name == "cascade") return PanelEffect::Cascade;
	if (name == "reveal") return PanelEffect::Reveal;
	if (name == "blur") return PanelEffect::Blur;
	if (name == "dissolve") return PanelEffect::Dissolve;
	return std::nullopt;
}

QString panelEffectName(PanelEffect effect) {
	switch (effect) {
	case PanelEffect::Auto: return "auto";
	case PanelEffect::Standard: return "standard";
	case PanelEffect::Spring: return "spring";
	case PanelEffect::Cascade: return "cascade";
	case PanelEffect::Reveal: return "reveal";
	case PanelEffect::Blur: return "blur";
	case PanelEffect::Dissolve: return "dissolve";
	}

	return QString();
}

PanelEffect resolvePanelEffect(const PanelAnimationStyle& style, PanelEffect requested) {
	if (!style.animates()) return PanelEffect::Standard;
	if (requested != PanelEffect::Auto) return requested;

	switch (style.kind) {
	case PanelAnimationStyle::Kind::Slide:
		if (style.edge.has_value() && verticalEdge(*style.edge)) return PanelEffect::Reveal;
		return PanelEffect::Cascade;
	case PanelAnimationStyle::Kind::Popin: return PanelEffect::Blur;
	case PanelAnimationStyle::Kind::Fade: return PanelEffect::Dissolve;
	case PanelAnimationStyle::Kind::None: break;
	}

	return PanelEffect::Standard;
}

bool panelEffectUsesShader(PanelEffect effect) {
	return effect == PanelEffect::Reveal || effect == PanelEffect::Blur
	    || effect == PanelEffect::Dissolve;
}

qreal CubicBezier::xAt(qreal t) const {
	auto cx = 3.0 * this->mX1;
	auto bx = 3.0 * (this->mX2 - this->mX1) - cx;
	auto ax = 1.0 - cx - bx;
	return ((ax * t + bx) * t + cx) * t;
}

qreal CubicBezier::yAt(qreal t) const {
	auto cy = 3.0 * this->mY1;
	auto by = 3.0 * (this->mY2 - this->mY1) - cy;
	auto ay = 1.0 - cy - by;
	return ((ay * t + by) * t + cy) * t;
}

qreal CubicBezier::xSlopeAt(qreal t) const {
	auto cx = 3.0 * this->mX1;
	auto bx = 3.0 * (this->mX2 - this->mX1) - cx;
	auto ax = 1.0 - cx - bx;
	return (3.0 * ax * t + 2.0 * bx) * t + cx;
}

qreal CubicBezier::valueAt(qreal progress) const {
	if (progress <= 0) return 0;
	if (progress >= 1) return 1;

	auto t = progress;
	for (auto i = 0; i < NEWTON_STEPS; i++) {
		auto error = this->xAt(t) - progress;
		if (std::abs(error) < SOLVE_EPSILON) return this->yAt(t);

		auto slope = this->xSlopeAt(t);
		if (std::abs(slope) < SOLVE_EPSILON) break;
		t -= error / slope;
	}

	auto low = 0.0;
	auto high = 1.0;
	t = progress;

	for (auto i = 0; i < BISECTION_STEPS; i++) {
		auto x = this->xAt(t);
		if (std::abs(x - progress) < SOLVE_EPSILON) break;
		if (x < progress) low = t;
		else high = t;
		t = (low + high) / 2.0;
	}

	return this->yAt(t);
}

SpringCurve SpringCurve::withOvershoot(qreal overshoot) {
	auto curve = SpringCurve();
	auto target = std::clamp(overshoot, 0.0, MAX_SPRING_OVERSHOOT);

	if (target < MIN_SPRING_OVERSHOOT) {
		auto frequency = CRITICAL_SPRING_GUESS;

		for (auto i = 0; i < CRITICAL_SPRING_STEPS; i++) {
			auto decay = std::exp(-frequency);
			auto error = (1.0 + frequency) * decay - SPRING_SETTLE;
			auto slope = -frequency * decay;
			if (std::abs(error) < SOLVE_EPSILON * SPRING_SETTLE || slope == 0) break;
			frequency -= error / slope;
		}

		curve.mDamping = 1;
		curve.mFrequency = frequency;
	} else {
		auto logOvershoot = std::log(target);
		curve.mDamping = -logOvershoot
		               / std::sqrt(std::numbers::pi * std::numbers::pi + logOvershoot * logOvershoot);
		curve.mFrequency = -std::log(SPRING_SETTLE) / curve.mDamping;
	}

	curve.mEndError = 1.0 - curve.rawAt(1.0);
	return curve;
}

qreal SpringCurve::rawAt(qreal progress) const {
	auto frequency = this->mFrequency;
	auto damping = this->mDamping;

	if (damping >= 1) {
		return 1.0 - (1.0 + frequency * progress) * std::exp(-frequency * progress);
	}

	auto damped = frequency * std::sqrt(1.0 - damping * damping);
	auto envelope = std::exp(-damping * frequency * progress);
	return 1.0
	     - envelope
	           * (std::cos(damped * progress)
	              + (damping * frequency / damped) * std::sin(damped * progress));
}

qreal SpringCurve::valueAt(qreal progress) const {
	if (progress <= 0) return 0;
	if (progress >= 1) return 1;
	if (this->mFrequency <= 0) return progress;
	return this->rawAt(progress) + this->mEndError * progress;
}

qreal PanelAnimationTrack::valueAt(qreal elapsed) const {
	if (this->duration <= 0 || elapsed >= this->duration) return this->to;
	if (elapsed <= 0) return this->from;

	auto progress = elapsed / this->duration;
	auto eased =
	    this->spring.has_value() ? this->spring->valueAt(progress) : this->curve.valueAt(progress);
	return this->from + (this->to - this->from) * eased;
}

const PanelAnimationTiming& PanelAnimationTiming::hyprland() { return HYPRLAND_TIMING; }

PanelAnimationTiming PanelAnimationTiming::expressive(PanelEffect effect, qreal overshoot) {
	auto spring = SpringCurve::withOvershoot(overshoot);

	switch (effect) {
	case PanelEffect::Reveal:
		return PanelAnimationTiming {
		    .showMotionMs = 400,
		    .showFadeMs = 80,
		    .hideMotionMs = 240,
		    .hideFadeMs = 240,
		    .showMotion = EMPHASIZED_DECEL,
		    .showFade = MENU_DECEL,
		    .hideMotion = EMPHASIZED_ACCEL,
		    .hideFade = STALL,
		    .showEffectMs = 400,
		    .hideEffectMs = 240,
		    .showEffect = EMPHASIZED_DECEL,
		    .hideEffect = EMPHASIZED_ACCEL,
		    .showSpring = spring,
		};
	case PanelEffect::Blur:
		return PanelAnimationTiming {
		    .showMotionMs = 360,
		    .showFadeMs = 160,
		    .hideMotionMs = 220,
		    .hideFadeMs = 220,
		    .showMotion = EMPHASIZED_DECEL,
		    .showFade = STANDARD_DECEL,
		    .hideMotion = EMPHASIZED_ACCEL,
		    .hideFade = STANDARD_ACCEL,
		    .showEffectMs = 320,
		    .hideEffectMs = 220,
		    .showEffect = STANDARD_DECEL,
		    .hideEffect = STANDARD_ACCEL,
		    .showSpring = spring,
		};
	case PanelEffect::Dissolve:
		return PanelAnimationTiming {
		    .showMotionMs = 340,
		    .showFadeMs = 50,
		    .hideMotionMs = 240,
		    .hideFadeMs = 240,
		    .showMotion = EMPHASIZED_DECEL,
		    .showFade = MENU_DECEL,
		    .hideMotion = EMPHASIZED_ACCEL,
		    .hideFade = STALL,
		    .showEffectMs = 340,
		    .hideEffectMs = 240,
		    .showEffect = STANDARD_DECEL,
		    .hideEffect = STANDARD_ACCEL,
		    .showSpring = spring,
		};
	case PanelEffect::Auto:
	case PanelEffect::Standard:
	case PanelEffect::Spring:
	case PanelEffect::Cascade: break;
	}

	return PanelAnimationTiming {
	    .showMotionMs = 380,
	    .showFadeMs = 90,
	    .hideMotionMs = 220,
	    .hideFadeMs = 220,
	    .showMotion = EMPHASIZED_DECEL,
	    .showFade = MENU_DECEL,
	    .hideMotion = EMPHASIZED_ACCEL,
	    .hideFade = STALL,
	    .showSpring = spring,
	};
}

void PanelAnimation::start(bool show, const PanelAnimationStyle& style) {
	this->start(show, style, PanelAnimationTiming::hyprland());
}

void PanelAnimation::start(
    bool show,
    const PanelAnimationStyle& style,
    const PanelAnimationTiming& timing
) {
	auto target = show ? 1.0 : 0.0;
	auto motionMs = show ? timing.showMotionMs : timing.hideMotionMs;
	auto fadeMs = show ? timing.showFadeMs : timing.hideFadeMs;
	auto effectMs = show ? timing.showEffectMs : timing.hideEffectMs;

	this->mShowing = show;
	if (!style.moves()) this->mMotion = target;
	if (effectMs <= 0) this->mEffect = target;
	this->mCascadeBase = this->mCascadeTime;

	this->motionTrack = PanelAnimationTrack {
	    .from = this->mMotion,
	    .to = target,
	    .duration = std::abs(target - this->mMotion) * motionMs,
	    .curve = show ? timing.showMotion : timing.hideMotion,
	    .spring = show ? timing.showSpring : std::nullopt,
	};

	this->opacityTrack = PanelAnimationTrack {
	    .from = this->mOpacity,
	    .to = target,
	    .duration = std::abs(target - this->mOpacity) * fadeMs,
	    .curve = show ? timing.showFade : timing.hideFade,
	};

	this->effectTrack = PanelAnimationTrack {
	    .from = this->mEffect,
	    .to = target,
	    .duration = std::abs(target - this->mEffect) * effectMs,
	    .curve = show ? timing.showEffect : timing.hideEffect,
	};
}

void PanelAnimation::startCascade(qreal duration) {
	this->mCascadeBase = 0;
	this->mCascadeTime = 0;
	this->mCascadeDuration = std::max(duration, 0.0);
}

bool PanelAnimation::advance(qreal elapsed) {
	this->mMotion = this->motionTrack.valueAt(elapsed);
	this->mOpacity = this->opacityTrack.valueAt(elapsed);
	this->mEffect = this->effectTrack.valueAt(elapsed);
	this->mCascadeTime = this->mCascadeBase + std::max(elapsed, 0.0);
	return elapsed >= this->duration();
}

void PanelAnimation::jumpTo(bool shown) {
	auto target = shown ? 1.0 : 0.0;
	this->mShowing = shown;
	this->mMotion = target;
	this->mOpacity = target;
	this->mEffect = target;
	this->mCascadeBase = 0;
	this->mCascadeTime = 0;
	this->mCascadeDuration = 0;
	this->motionTrack = PanelAnimationTrack {.from = target, .to = target};
	this->opacityTrack = PanelAnimationTrack {.from = target, .to = target};
	this->effectTrack = PanelAnimationTrack {.from = target, .to = target};
}

qreal PanelAnimation::duration() const {
	auto value = std::max(
	    {this->motionTrack.duration, this->opacityTrack.duration, this->effectTrack.duration}
	);

	if (this->mShowing) value = std::max(value, this->mCascadeDuration - this->mCascadeBase);
	return value;
}

bool PanelAnimation::settled() const {
	auto target = this->mShowing ? 1.0 : 0.0;
	return this->mMotion == target && this->mOpacity == target && this->mEffect == target;
}

bool PanelAnimationFrame::isIdentity() const {
	return this->offset.isNull() && this->scale == 1 && this->opacity >= 1;
}

Qt::Edge panelSlideEdge(
    std::optional<Qt::Edge> forced,
    const QRectF& panel,
    const QRectF& screen,
    Qt::Edges anchors
) {
	if (forced.has_value()) return *forced;

	struct Candidate {
		Qt::Edge edge;
		qreal distance;
	};

	auto center = panel.center();
	auto candidates = std::array<Candidate, 4> {{
	    {.edge = Qt::TopEdge, .distance = center.y() - screen.top()},
	    {.edge = Qt::BottomEdge, .distance = screen.bottom() - center.y()},
	    {.edge = Qt::LeftEdge, .distance = center.x() - screen.left()},
	    {.edge = Qt::RightEdge, .distance = screen.right() - center.x()},
	}};

	auto best = Qt::TopEdge;
	auto bestDistance = std::numeric_limits<qreal>::max();

	for (const auto& candidate: candidates) {
		if (anchors != Qt::Edges() && !anchors.testFlag(candidate.edge)) continue;
		if (candidate.distance >= bestDistance) continue;
		best = candidate.edge;
		bestDistance = candidate.distance;
	}

	return best;
}

PanelAnimationFrame panelAnimationFrame(
    const PanelAnimationStyle& style,
    Qt::Edge edge,
    const QSizeF& size,
    qreal motion,
    qreal opacity,
    qreal dpr,
    std::optional<qreal> travel
) {
	auto frame = PanelAnimationFrame();
	frame.opacity = std::clamp(opacity, 0.0, 1.0);
	auto hidden = 1.0 - std::clamp(motion, 0.0, MAX_MOTION);

	switch (style.kind) {
	case PanelAnimationStyle::Kind::Slide: {
		auto distance = travel.value_or(extentAlong(edge, size));
		frame.offset = towardEdge(edge, snap(hidden * distance, dpr));
		if (frame.offset.x() == 0) frame.offset.setX(0);
		if (frame.offset.y() == 0) frame.offset.setY(0);
		break;
	}
	case PanelAnimationStyle::Kind::Popin:
		frame.scale = style.popinScale + (1.0 - style.popinScale) * (1.0 - hidden);
		break;
	case PanelAnimationStyle::Kind::Fade:
	case PanelAnimationStyle::Kind::None: break;
	}

	return frame;
}

qreal panelRevealTravel(Qt::Edge edge, const QSizeF& size) {
	auto along = extentAlong(edge, size);
	auto travel = std::clamp(REVEAL_TRAVEL_RATIO * along, REVEAL_MIN_TRAVEL, REVEAL_MAX_TRAVEL);
	return std::max(std::min(travel, along), 0.0);
}

qreal panelSpringOvershoot(
    const PanelAnimationStyle& style,
    PanelEffect effect,
    Qt::Edge edge,
    const QSizeF& size
) {
	auto travel = 0.0;

	switch (style.kind) {
	case PanelAnimationStyle::Kind::Slide:
		travel = effect == PanelEffect::Reveal ? panelRevealTravel(edge, size)
		                                       : extentAlong(edge, size);
		break;
	case PanelAnimationStyle::Kind::Popin:
		if (style.popinScale < 1) {
			travel = (1.0 - style.popinScale) * std::max(size.width(), size.height()) / 2.0;
		}
		break;
	case PanelAnimationStyle::Kind::Fade:
	case PanelAnimationStyle::Kind::None: break;
	}

	if (travel <= 0) return 0;
	return std::min(MAX_OVERSHOOT, MAX_OVERSHOOT_PX / travel);
}

PanelEffectFrame panelEffectFrame(
    PanelEffect effect,
    Qt::Edge edge,
    const QSizeF& size,
    qreal progress,
    const QPointF& offset
) {
	auto frame = PanelEffectFrame();
	auto shown = std::clamp(progress, 0.0, 1.0);

	switch (effect) {
	case PanelEffect::Blur: {
		auto shortest = std::min(size.width(), size.height());
		auto radius = std::clamp(BLUR_RATIO * shortest, BLUR_MIN_RADIUS, BLUR_MAX_RADIUS);
		frame.blurRadius = radius * (1.0 - shown);
		break;
	}
	case PanelEffect::Dissolve: frame.dissolve = shown; break;
	case PanelEffect::Reveal: {
		auto along = extentAlong(edge, size);
		auto across = verticalEdge(edge) ? size.width() : size.height();
		auto radius = std::clamp(
		    REVEAL_RADIUS_RATIO * std::min(along, across),
		    REVEAL_MIN_RADIUS,
		    REVEAL_MAX_RADIUS
		);

		auto depth = shown * (along + radius);
		auto behind = radius + 1.0;
		auto halfAcross =
		    across / 2.0 * (REVEAL_START_WIDTH + (1.0 - REVEAL_START_WIDTH) * shown) + radius * shown;
		auto acrossStart = across / 2.0 - halfAcross;
		auto length = depth + behind;

		auto rect = QRectF();
		switch (edge) {
		case Qt::TopEdge: rect = QRectF(acrossStart, -behind, halfAcross * 2.0, length); break;
		case Qt::BottomEdge:
			rect = QRectF(acrossStart, size.height() - depth, halfAcross * 2.0, length);
			break;
		case Qt::LeftEdge: rect = QRectF(-behind, acrossStart, length, halfAcross * 2.0); break;
		case Qt::RightEdge:
			rect = QRectF(size.width() - depth, acrossStart, length, halfAcross * 2.0);
			break;
		}

		frame.revealRect = rect.translated(-offset);
		frame.revealRadius = std::min({radius, rect.width() / 2.0, rect.height() / 2.0});
		break;
	}
	case PanelEffect::Auto:
	case PanelEffect::Standard:
	case PanelEffect::Spring:
	case PanelEffect::Cascade: break;
	}

	return frame;
}

PanelBlurSampling panelBlurSampling(qreal radius, const QSizeF& size, qreal dpr) {
	auto sampling = PanelBlurSampling();
	if (radius < BLUR_VISIBLE_RADIUS || size.width() <= 0 || size.height() <= 0) return sampling;

	auto half = radius / 2.0;
	sampling.step = QPointF(half / size.width(), half / size.height());
	sampling.lod = std::log2(std::max(half * std::max(dpr, 1.0), 1.0));
	return sampling;
}

qreal PanelCascadeTiming::delayFor(qsizetype index) const {
	auto steps = static_cast<qreal>(std::max<qsizetype>(index, 0));
	return std::min(steps * this->stepMs, this->maxDelayMs);
}

qreal PanelCascadeTiming::durationFor(qsizetype count) const {
	if (count <= 0) return 0;
	return this->delayFor(count - 1) + this->sectionMs;
}

const PanelCascadeTiming& PanelCascadeTiming::expressive() { return CASCADE_TIMING; }

qreal panelCascadeDistance(Qt::Edge edge, const QSizeF& size) {
	return std::clamp(
	    CASCADE_DISTANCE_RATIO * extentAlong(edge, size),
	    CASCADE_MIN_DISTANCE,
	    CASCADE_MAX_DISTANCE
	);
}

qreal panelCascadeOvershoot(qreal distance) {
	if (distance <= 0) return 0;
	return std::min(CASCADE_MAX_OVERSHOOT, CASCADE_MAX_OVERSHOOT_PX / distance);
}

PanelCascadeFrame panelCascadeFrame(
    const PanelCascadeTiming& timing,
    const SpringCurve& spring,
    qsizetype index,
    qreal elapsed,
    Qt::Edge edge,
    qreal distance,
    qreal dpr
) {
	auto frame = PanelCascadeFrame();
	auto local = elapsed - timing.delayFor(index);

	auto progress = timing.sectionMs > 0 ? std::clamp(local / timing.sectionMs, 0.0, 1.0) : 1.0;
	auto fade = timing.fadeMs > 0 ? std::clamp(local / timing.fadeMs, 0.0, 1.0) : 1.0;

	if (fade < 1) {
		frame.opacity =
		    CASCADE_OPACITY_FLOOR + (1.0 - CASCADE_OPACITY_FLOOR) * STANDARD_DECEL.valueAt(fade);
	}

	if (progress < 1) {
		frame.offset = towardEdge(edge, snap((1.0 - spring.valueAt(progress)) * distance, dpr));
		if (frame.offset.x() == 0) frame.offset.setX(0);
		if (frame.offset.y() == 0) frame.offset.setY(0);
	}

	return frame;
}

QList<QQuickItem*> panelCascadeSections(QQuickItem* root, Qt::Edge edge) {
	auto* level = root;

	for (auto depth = 0; level != nullptr && depth < CASCADE_MAX_DEPTH; depth++) {
		auto bounds = QRectF(QPointF(), level->size());
		auto levelArea = area(bounds);

		QList<QQuickItem*> covers;
		QList<QQuickItem*> others;

		for (auto* child: level->childItems()) {
			if (!shows(child) || !hasContent(child, CASCADE_CONTENT_DEPTH)) continue;

			auto covered = area(boundsInParent(child).intersected(bounds));
			if (levelArea > 0 && covered >= CASCADE_COVER_RATIO * levelArea) covers.append(child);
			else others.append(child);
		}

		if (others.size() >= CASCADE_MIN_SECTIONS) {
			if (others.size() > CASCADE_MAX_SECTIONS || !mostlyDisjoint(others)) return {};

			auto horizontal = !verticalEdge(edge);
			std::ranges::stable_sort(others, [horizontal](const QQuickItem* a, const QQuickItem* b) {
				auto ca = boundsInParent(a).center();
				auto cb = boundsInParent(b).center();
				return horizontal ? ca.y() < cb.y() : ca.x() < cb.x();
			});

			return others;
		}

		QQuickItem* next = nullptr;
		auto nextWeight = 0;
		auto nextArea = 0.0;

		for (auto* child: others + covers) {
			auto budget = CASCADE_WEIGHT_BUDGET;
			auto weight = 0;

			for (const auto* grandchild: child->childItems()) {
				if (!grandchild->isVisible() || grandchild->opacity() <= 0) continue;
				weight += contentWeight(grandchild, CASCADE_WEIGHT_DEPTH, budget);
			}

			auto childArea = area(boundsInParent(child));
			if (weight > nextWeight || (weight == nextWeight && weight > 0 && childArea > nextArea)) {
				next = child;
				nextWeight = weight;
				nextArea = childArea;
			}
		}

		level = next;
	}

	return {};
}

void PanelContentTransform::setFrame(const PanelAnimationFrame& frame, const QSizeF& size) {
	auto center = QPointF(size.width() / 2.0, size.height() / 2.0);
	if (frame.offset == this->mOffset && frame.scale == this->mScale && center == this->mOrigin) {
		return;
	}

	this->mOffset = frame.offset;
	this->mScale = frame.scale;
	this->mOrigin = center;
	this->update();
}

void PanelContentTransform::applyTo(QMatrix4x4* matrix) const {
	if (this->mBypassed || (this->mOffset.isNull() && this->mScale == 1)) return;

	matrix->translate(
	    static_cast<float>(this->mOffset.x() + this->mOrigin.x()),
	    static_cast<float>(this->mOffset.y() + this->mOrigin.y())
	);

	if (this->mScale != 1) {
		matrix->scale(static_cast<float>(this->mScale), static_cast<float>(this->mScale));
	}

	matrix->translate(static_cast<float>(-this->mOrigin.x()), static_cast<float>(-this->mOrigin.y()));
}

} // namespace qs::windows
