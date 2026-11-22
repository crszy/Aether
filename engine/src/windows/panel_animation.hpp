#pragma once

#include <optional>

#include <qlist.h>
#include <qmatrix4x4.h>
#include <qnamespace.h>
#include <qpoint.h>
#include <qquickitem.h>
#include <qrect.h>
#include <qsize.h>
#include <qstring.h>
#include <qtypes.h>

namespace qs::windows {

struct PanelAnimationStyle {
	enum class Kind : quint8 {
		None,
		Slide,
		Popin,
		Fade,
	};

	Kind kind = Kind::None;
	std::optional<Qt::Edge> edge;
	qreal popinScale = 0;

	[[nodiscard]] static std::optional<PanelAnimationStyle> parse(const QString& text);
	[[nodiscard]] QString toString() const;
	[[nodiscard]] bool animates() const { return this->kind != Kind::None; }
	[[nodiscard]] bool moves() const {
		return this->kind == Kind::Slide || this->kind == Kind::Popin;
	}
	[[nodiscard]] bool operator==(const PanelAnimationStyle& other) const = default;
};

enum class PanelEffect : quint8 {
	Auto,
	Standard,
	Spring,
	Cascade,
	Reveal,
	Blur,
	Dissolve,
};

[[nodiscard]] std::optional<PanelEffect> parsePanelEffect(const QString& text);
[[nodiscard]] QString panelEffectName(PanelEffect effect);
[[nodiscard]] PanelEffect
resolvePanelEffect(const PanelAnimationStyle& style, PanelEffect requested);
[[nodiscard]] bool panelEffectUsesShader(PanelEffect effect);

struct PanelAnimationChoice {
	PanelAnimationStyle style;
	PanelEffect effect = PanelEffect::Standard;
};

class CubicBezier {
public:
	constexpr CubicBezier(qreal x1, qreal y1, qreal x2, qreal y2)
	    : mX1(x1)
	    , mY1(y1)
	    , mX2(x2)
	    , mY2(y2) {}

	[[nodiscard]] qreal valueAt(qreal progress) const;

private:
	[[nodiscard]] qreal xAt(qreal t) const;
	[[nodiscard]] qreal yAt(qreal t) const;
	[[nodiscard]] qreal xSlopeAt(qreal t) const;

	qreal mX1;
	qreal mY1;
	qreal mX2;
	qreal mY2;
};

class SpringCurve {
public:
	SpringCurve() = default;

	[[nodiscard]] static SpringCurve withOvershoot(qreal overshoot);

	[[nodiscard]] qreal valueAt(qreal progress) const;
	[[nodiscard]] qreal dampingRatio() const { return this->mDamping; }
	[[nodiscard]] qreal frequency() const { return this->mFrequency; }

private:
	[[nodiscard]] qreal rawAt(qreal progress) const;

	qreal mDamping = 1;
	qreal mFrequency = 0;
	qreal mEndError = 0;
};

struct PanelAnimationTrack {
	qreal from = 1;
	qreal to = 1;
	qreal duration = 0;
	CubicBezier curve {0, 0, 1, 1};
	std::optional<SpringCurve> spring {};

	[[nodiscard]] qreal valueAt(qreal elapsed) const;
};

struct PanelAnimationTiming {
	qreal showMotionMs;
	qreal showFadeMs;
	qreal hideMotionMs;
	qreal hideFadeMs;
	CubicBezier showMotion;
	CubicBezier showFade;
	CubicBezier hideMotion;
	CubicBezier hideFade;
	qreal showEffectMs = 0;
	qreal hideEffectMs = 0;
	CubicBezier showEffect {0, 0, 1, 1};
	CubicBezier hideEffect {0, 0, 1, 1};
	std::optional<SpringCurve> showSpring {};

	[[nodiscard]] static const PanelAnimationTiming& hyprland();
	[[nodiscard]] static PanelAnimationTiming expressive(PanelEffect effect, qreal overshoot);
};

class PanelAnimation {
public:
	void start(bool show, const PanelAnimationStyle& style);
	void start(bool show, const PanelAnimationStyle& style, const PanelAnimationTiming& timing);
	void startCascade(qreal duration);
	bool advance(qreal elapsed);
	void jumpTo(bool shown);

	[[nodiscard]] bool showing() const { return this->mShowing; }
	[[nodiscard]] qreal duration() const;
	[[nodiscard]] qreal motion() const { return this->mMotion; }
	[[nodiscard]] qreal opacity() const { return this->mOpacity; }
	[[nodiscard]] qreal effect() const { return this->mEffect; }
	[[nodiscard]] qreal cascadeTime() const { return this->mCascadeTime; }
	[[nodiscard]] bool settled() const;

private:
	bool mShowing = true;
	qreal mMotion = 1;
	qreal mOpacity = 1;
	qreal mEffect = 1;
	qreal mCascadeBase = 0;
	qreal mCascadeTime = 0;
	qreal mCascadeDuration = 0;
	PanelAnimationTrack motionTrack;
	PanelAnimationTrack opacityTrack;
	PanelAnimationTrack effectTrack;
};

struct PanelAnimationFrame {
	QPointF offset;
	qreal scale = 1;
	qreal opacity = 1;

	[[nodiscard]] bool isIdentity() const;
};

[[nodiscard]] Qt::Edge panelSlideEdge(
    std::optional<Qt::Edge> forced,
    const QRectF& panel,
    const QRectF& screen,
    Qt::Edges anchors
);

[[nodiscard]] PanelAnimationFrame panelAnimationFrame(
    const PanelAnimationStyle& style,
    Qt::Edge edge,
    const QSizeF& size,
    qreal motion,
    qreal opacity,
    qreal dpr,
    std::optional<qreal> travel = std::nullopt
);

[[nodiscard]] qreal panelRevealTravel(Qt::Edge edge, const QSizeF& size);

[[nodiscard]] qreal panelSpringOvershoot(
    const PanelAnimationStyle& style,
    PanelEffect effect,
    Qt::Edge edge,
    const QSizeF& size
);

struct PanelEffectFrame {
	qreal blurRadius = 0;
	qreal dissolve = 1;
	QRectF revealRect;
	qreal revealRadius = -1;
};

[[nodiscard]] PanelEffectFrame panelEffectFrame(
    PanelEffect effect,
    Qt::Edge edge,
    const QSizeF& size,
    qreal progress,
    const QPointF& offset
);

struct PanelBlurSampling {
	QPointF step;
	qreal lod = 0;
};

[[nodiscard]] PanelBlurSampling panelBlurSampling(qreal radius, const QSizeF& size, qreal dpr);

struct PanelCascadeTiming {
	qreal stepMs;
	qreal maxDelayMs;
	qreal sectionMs;
	qreal fadeMs;

	[[nodiscard]] qreal delayFor(qsizetype index) const;
	[[nodiscard]] qreal durationFor(qsizetype count) const;

	[[nodiscard]] static const PanelCascadeTiming& expressive();
};

struct PanelCascadeFrame {
	QPointF offset;
	qreal opacity = 1;
};

[[nodiscard]] qreal panelCascadeDistance(Qt::Edge edge, const QSizeF& size);
[[nodiscard]] qreal panelCascadeOvershoot(qreal distance);

[[nodiscard]] PanelCascadeFrame panelCascadeFrame(
    const PanelCascadeTiming& timing,
    const SpringCurve& spring,
    qsizetype index,
    qreal elapsed,
    Qt::Edge edge,
    qreal distance,
    qreal dpr
);

[[nodiscard]] QList<QQuickItem*> panelCascadeSections(QQuickItem* root, Qt::Edge edge);

class PanelContentTransform: public QQuickTransform {
public:
	explicit PanelContentTransform(QObject* parent = nullptr): QQuickTransform(parent) {}

	void setFrame(const PanelAnimationFrame& frame, const QSizeF& size);
	void setBypassed(bool bypassed) { this->mBypassed = bypassed; }

	void applyTo(QMatrix4x4* matrix) const override;

private:
	QPointF mOffset;
	qreal mScale = 1;
	QPointF mOrigin;
	bool mBypassed = false;
};

} // namespace qs::windows
