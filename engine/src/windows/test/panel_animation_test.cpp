#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <set>

#include <qbytearray.h>
#include <qcolor.h>
#include <qfile.h>
#include <qimage.h>
#include <qmatrix4x4.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpoint.h>
#include <qpointer.h>
#include <qprocess.h>
#include <qqmlcomponent.h>
#include <qqmlengine.h>
#include <qquickitem.h>
#include <qquickwindow.h>
#include <qrect.h>
#include <qregularexpression.h>
#include <qsgrendererinterface.h>
#include <qsignalspy.h>
#include <qsize.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtemporarydir.h>
#include <qtenvironmentvariables.h>
#include <qtest.h>
#include <qtestcase.h>
#include <qurl.h>
#include <qvectornd.h>
#include <rhi/qshader.h>

#include "layer_rules.hpp"
#include "panel_animation.hpp"
#include "panel_effects.hpp"

using qs::windows::CubicBezier;
using qs::windows::LayerRules;
using qs::windows::PanelAnimation;
using qs::windows::PanelAnimationStyle;
using qs::windows::PanelAnimationTiming;
using qs::windows::PanelCascadeTiming;
using qs::windows::PanelContentTransform;
using qs::windows::PanelEffect;
using qs::windows::PanelEffectFrame;
using qs::windows::PanelEffectKeepAlive;
using qs::windows::PanelEffects;
using qs::windows::SpringCurve;
using Kind = qs::windows::PanelAnimationStyle::Kind;

namespace {

bool near(qreal a, qreal b, qreal tolerance = 1e-3) { return std::abs(a - b) <= tolerance; }

qreal effectiveOpacity(const QQuickItem* item) {
	auto opacity = 1.0;
	for (; item != nullptr; item = item->parentItem()) opacity *= item->opacity();
	return opacity;
}

qreal sampledBezier(qreal x1, qreal y1, qreal x2, qreal y2, qreal x) {
	auto bestT = 0.0;
	auto bestError = 2.0;

	for (auto i = 0; i <= 200000; i++) {
		auto t = i / 200000.0;
		auto u = 1.0 - t;
		auto bx = 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t;
		auto error = std::abs(bx - x);
		if (error < bestError) {
			bestError = error;
			bestT = t;
		}
	}

	auto u = 1.0 - bestT;
	return 3 * u * u * bestT * y1 + 3 * u * bestT * bestT * y2 + bestT * bestT * bestT;
}

PanelAnimationStyle style(Kind kind, std::optional<Qt::Edge> edge = std::nullopt, qreal scale = 0) {
	auto result = PanelAnimationStyle();
	result.kind = kind;
	result.edge = edge;
	result.popinScale = scale;
	return result;
}

QString name(PanelEffect effect) { return qs::windows::panelEffectName(effect); }

qreal roundedBoxDistance(const QPointF& point, const QRectF& rect, qreal radius) {
	auto center = rect.center();
	auto qx = std::abs(point.x() - center.x()) - rect.width() / 2.0 + radius;
	auto qy = std::abs(point.y() - center.y()) - rect.height() / 2.0 + radius;
	auto outside = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0));
	return outside + std::min(std::max(qx, qy), 0.0) - radius;
}

template <typename T>
T* findObject(QObject* root, const char* objectName) {
	return root->findChild<T*>(QString::fromLatin1(objectName));
}

QStringList objectNames(const QList<QQuickItem*>& items) {
	QStringList names;
	for (const auto* item: items) names.append(item->objectName());
	return names;
}

struct QmlScene {
	QQmlEngine engine;
	std::unique_ptr<QObject> root;

	bool load(const QByteArray& qml) {
		auto component = QQmlComponent(&this->engine);
		component.setData(qml, QUrl());
		this->root.reset(component.create());
		if (this->root == nullptr) qWarning().noquote() << component.errorString();
		return this->root != nullptr;
	}
};

const auto EFFECT_WINDOW_QML = QByteArray(R"(
	import QtQuick
	import QtQuick.Window
	Window {
		width: 64
		height: 48
		visible: true
		color: "transparent"
		property Item content: content
		Item {
			id: content
			anchors.fill: parent
			Rectangle { x: 4; y: 4; width: 24; height: 40; color: "#ff2050" }
			Rectangle { x: 34; y: 8; width: 26; height: 30; radius: 6; color: "#20a0ff" }
			Rectangle { x: 10; y: 30; width: 40; height: 12; color: "#40ff60"; opacity: 0.6 }
		}
	}
)");

int maxChannelDifference(const QImage& a, const QImage& b) {
	auto first = a.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	auto second = b.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	if (first.size() != second.size()) return 256;

	auto worst = 0;
	for (auto y = 0; y < first.height(); y++) {
		for (auto x = 0; x < first.width(); x++) {
			auto pa = first.pixel(x, y);
			auto pb = second.pixel(x, y);
			worst = std::max(
			    {worst,
			     std::abs(qRed(pa) - qRed(pb)),
			     std::abs(qGreen(pa) - qGreen(pb)),
			     std::abs(qBlue(pa) - qBlue(pb)),
			     std::abs(qAlpha(pa) - qAlpha(pb))}
			);
		}
	}

	return worst;
}

qreal alphaSum(const QImage& image, const QRect& area = QRect()) {
	auto converted = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	auto rect = area.isNull() ? converted.rect() : area.intersected(converted.rect());
	auto sum = 0.0;
	for (auto y = rect.top(); y <= rect.bottom(); y++) {
		for (auto x = rect.left(); x <= rect.right(); x++) sum += qAlpha(converted.pixel(x, y));
	}
	return sum;
}

} // namespace

class TestPanelAnimation: public QObject {
	Q_OBJECT;

private slots:
	void parseStyles();
	void stylesRoundTrip();
	void bezierMatchesSampling();
	void showAndHideTimeline();
	void fadeOnlyTimeline();
	void reverseMidway();
	void frames();
	void slideEdges();
	void contentTransform();
	void maskBypass();
	void overlayFollowsRoot();
	void ruleMerging();
	void iiRules();
	void springCurve();
	void springOvershootLimits();
	void effectNames();
	void effectResolution();
	void expressiveRules();
	void expressiveTimeline();
	void expressiveReverse();
	void revealShape();
	void blurAndDissolveFrames();
	void cascadeTiming();
	void cascadeAcrossReversal();
	void cascadeSections();
	void cascadeBypass();
	void keepAlive();
	void effectLifecycle();
	void effectEngineGone();
	void missingShader();
	void shaderReflection();
	void shaderUpToDate();
	void effectRendering();
};

void TestPanelAnimation::parseStyles() {
	QCOMPARE(PanelAnimationStyle::parse("slide"), style(Kind::Slide));
	QCOMPARE(PanelAnimationStyle::parse("slide left"), style(Kind::Slide, Qt::LeftEdge));
	QCOMPARE(PanelAnimationStyle::parse("  Slide   RIGHT "), style(Kind::Slide, Qt::RightEdge));
	QCOMPARE(PanelAnimationStyle::parse("slide top"), style(Kind::Slide, Qt::TopEdge));
	QCOMPARE(PanelAnimationStyle::parse("slide bottom"), style(Kind::Slide, Qt::BottomEdge));
	QCOMPARE(PanelAnimationStyle::parse("popin"), style(Kind::Popin, std::nullopt, 0));
	QCOMPARE(PanelAnimationStyle::parse("popin 93%"), style(Kind::Popin, std::nullopt, 0.93));
	QCOMPARE(PanelAnimationStyle::parse("popin 120%"), style(Kind::Popin, std::nullopt, 1.2));
	QCOMPARE(PanelAnimationStyle::parse("popin 80"), style(Kind::Popin, std::nullopt, 0.8));
	QCOMPARE(PanelAnimationStyle::parse("fade"), style(Kind::Fade));
	QCOMPARE(PanelAnimationStyle::parse("noAnim"), style(Kind::None));
	QCOMPARE(PanelAnimationStyle::parse("none"), style(Kind::None));

	QVERIFY(!PanelAnimationStyle::parse("").has_value());
	QVERIFY(!PanelAnimationStyle::parse("slide up").has_value());
	QVERIFY(!PanelAnimationStyle::parse("slide left now").has_value());
	QVERIFY(!PanelAnimationStyle::parse("popin -5%").has_value());
	QVERIFY(!PanelAnimationStyle::parse("popin big").has_value());
	QVERIFY(!PanelAnimationStyle::parse("fade 50").has_value());
	QVERIFY(!PanelAnimationStyle::parse("slidevert").has_value());
	QVERIFY(!PanelAnimationStyle::parse("bounce").has_value());
}

void TestPanelAnimation::stylesRoundTrip() {
	const auto texts = {
	    "slide",
	    "slide left",
	    "slide right",
	    "slide top",
	    "slide bottom",
	    "popin 93%",
	    "popin 120%",
	    "fade",
	    "noAnim",
	};

	for (const auto* text: texts) {
		auto parsed = PanelAnimationStyle::parse(text);
		QVERIFY2(parsed.has_value(), text);
		QCOMPARE(parsed->toString(), QString(text));
	}
}

void TestPanelAnimation::bezierMatchesSampling() {
	struct Curve {
		qreal x1, y1, x2, y2;
	};

	const auto curves = {
	    Curve {0.05, 0.7, 0.1, 1},
	    Curve {0.1, 1, 0, 1},
	    Curve {0.52, 0.03, 0.72, 0.08},
	    Curve {1, -0.1, 0.7, 0.85},
	    Curve {0, 0, 1, 1},
	};

	for (const auto& curve: curves) {
		auto bezier = CubicBezier(curve.x1, curve.y1, curve.x2, curve.y2);
		QCOMPARE(bezier.valueAt(0), 0.0);
		QCOMPARE(bezier.valueAt(1), 1.0);

		for (auto i = 1; i < 40; i++) {
			auto x = i / 40.0;
			auto got = bezier.valueAt(x);
			auto want = sampledBezier(curve.x1, curve.y1, curve.x2, curve.y2, x);
			auto message = QString("bezier(%1, %2, %3, %4) at %5: %6, want %7")
			                   .arg(curve.x1)
			                   .arg(curve.y1)
			                   .arg(curve.x2)
			                   .arg(curve.y2)
			                   .arg(x)
			                   .arg(got)
			                   .arg(want);
			QVERIFY2(near(got, want, 2e-3), qPrintable(message));
		}
	}

	auto decel = CubicBezier(0.05, 0.7, 0.1, 1);
	auto previous = 0.0;
	for (auto i = 1; i <= 100; i++) {
		auto value = decel.valueAt(i / 100.0);
		QVERIFY(value >= previous - 1e-9);
		previous = value;
	}
}

void TestPanelAnimation::showAndHideTimeline() {
	const auto& timing = PanelAnimationTiming::hyprland();
	auto slide = style(Kind::Slide, Qt::RightEdge);
	auto animation = PanelAnimation();

	animation.jumpTo(false);
	QCOMPARE(animation.motion(), 0.0);
	QCOMPARE(animation.opacity(), 0.0);

	animation.start(true, slide);
	QCOMPARE(animation.duration(), timing.showMotionMs);
	QVERIFY(!animation.advance(0));
	QCOMPARE(animation.motion(), 0.0);
	QCOMPARE(animation.opacity(), 0.0);

	QVERIFY(!animation.advance(timing.showFadeMs));
	QCOMPARE(animation.opacity(), 1.0);
	QVERIFY(animation.motion() > 0.5 && animation.motion() < 1.0);

	QVERIFY(animation.advance(timing.showMotionMs));
	QCOMPARE(animation.motion(), 1.0);
	QVERIFY(animation.settled());

	animation.start(false, slide);
	QCOMPARE(animation.duration(), timing.hideMotionMs);
	QVERIFY(!animation.advance(timing.hideMotionMs / 2));
	QVERIFY(animation.motion() > 0.5);
	QVERIFY(animation.opacity() > 0.9);
	QVERIFY(animation.advance(timing.hideMotionMs));
	QCOMPARE(animation.motion(), 0.0);
	QCOMPARE(animation.opacity(), 0.0);
	QVERIFY(!animation.showing());
}

void TestPanelAnimation::fadeOnlyTimeline() {
	const auto& timing = PanelAnimationTiming::hyprland();
	auto animation = PanelAnimation();
	animation.jumpTo(false);

	animation.start(true, style(Kind::Fade));
	QCOMPARE(animation.duration(), timing.showFadeMs);
	QCOMPARE(animation.motion(), 1.0);
	QVERIFY(animation.advance(timing.showFadeMs));

	animation.start(false, style(Kind::Fade));
	QCOMPARE(animation.duration(), timing.hideFadeMs);
	QCOMPARE(animation.motion(), 0.0);
}

void TestPanelAnimation::reverseMidway() {
	const auto& timing = PanelAnimationTiming::hyprland();
	auto slide = style(Kind::Slide, Qt::LeftEdge);
	auto animation = PanelAnimation();
	animation.jumpTo(false);

	animation.start(true, slide);
	animation.advance(30);
	auto motion = animation.motion();
	auto opacity = animation.opacity();
	QVERIFY(motion > 0 && motion < 1);

	animation.start(false, slide);
	QVERIFY(!animation.advance(0));
	QCOMPARE(animation.motion(), motion);
	QCOMPARE(animation.opacity(), opacity);
	QVERIFY(near(animation.duration(), timing.hideMotionMs * std::max(motion, opacity), 1e-6));

	animation.advance(10);
	auto closing = animation.motion();
	QVERIFY(closing <= motion);

	animation.start(true, slide);
	QVERIFY(!animation.advance(0));
	QCOMPARE(animation.motion(), closing);
	QVERIFY(near(animation.duration(), timing.showMotionMs * (1 - closing), 1e-6));
	QVERIFY(animation.advance(animation.duration()));
	QVERIFY(animation.settled());
	QVERIFY(animation.showing());
}

void TestPanelAnimation::frames() {
	auto size = QSizeF(400, 1000);

	using qs::windows::panelAnimationFrame;

	auto right = panelAnimationFrame(style(Kind::Slide, Qt::RightEdge), Qt::RightEdge, size, 0, 0, 1);
	QCOMPARE(right.offset, QPointF(400, 0));
	QCOMPARE(right.opacity, 0.0);
	QCOMPARE(right.scale, 1.0);

	auto half = panelAnimationFrame(style(Kind::Slide), Qt::LeftEdge, size, 0.5, 1, 1);
	QCOMPARE(half.offset, QPointF(-200, 0));

	auto top = panelAnimationFrame(style(Kind::Slide), Qt::TopEdge, size, 0.25, 1, 1);
	QCOMPARE(top.offset, QPointF(0, -750));

	auto odd = QSizeF(100, 101);
	auto bottom = panelAnimationFrame(style(Kind::Slide), Qt::BottomEdge, odd, 0.5, 1, 1.5);
	QVERIFY(near(bottom.offset.y() * 1.5, std::round(bottom.offset.y() * 1.5), 1e-9));
	QVERIFY(near(bottom.offset.y(), 50.5, 0.34));

	auto shown = panelAnimationFrame(style(Kind::Slide), Qt::BottomEdge, size, 1, 1, 1);
	QVERIFY(shown.isIdentity());

	auto popin =
	    panelAnimationFrame(style(Kind::Popin, std::nullopt, 0.93), Qt::TopEdge, size, 0, 0.5, 1);
	QVERIFY(popin.offset.isNull());
	QVERIFY(near(popin.scale, 0.93, 1e-9));
	QCOMPARE(popin.opacity, 0.5);

	auto grow =
	    panelAnimationFrame(style(Kind::Popin, std::nullopt, 1.2), Qt::TopEdge, size, 0.5, 1, 1);
	QVERIFY(near(grow.scale, 1.1, 1e-9));

	auto fade = panelAnimationFrame(style(Kind::Fade), Qt::TopEdge, size, 0, 0.25, 1);
	QVERIFY(fade.offset.isNull());
	QCOMPARE(fade.scale, 1.0);
	QCOMPARE(fade.opacity, 0.25);

	auto clamped = panelAnimationFrame(style(Kind::Fade), Qt::TopEdge, size, 1, 1.05, 1);
	QCOMPARE(clamped.opacity, 1.0);
}

void TestPanelAnimation::slideEdges() {
	using qs::windows::panelSlideEdge;
	auto screen = QRectF(0, 0, 1920, 1080);
	auto second = QRectF(1920, 0, 1280, 1024);
	auto top = Qt::Edges(Qt::TopEdge);
	auto topLeft = Qt::TopEdge | Qt::LeftEdge;
	auto horizontalTop = Qt::TopEdge | Qt::LeftEdge | Qt::RightEdge;
	auto horizontalBottom = Qt::BottomEdge | Qt::LeftEdge | Qt::RightEdge;
	auto verticalLeft = Qt::TopEdge | Qt::BottomEdge | Qt::LeftEdge;
	auto verticalRight = Qt::TopEdge | Qt::BottomEdge | Qt::RightEdge;
	auto all = Qt::TopEdge | Qt::BottomEdge | Qt::LeftEdge | Qt::RightEdge;
	auto none = std::optional<Qt::Edge>();

	QCOMPARE(panelSlideEdge(none, QRectF(0, 0, 1920, 40), screen, horizontalTop), Qt::TopEdge);
	QCOMPARE(
	    panelSlideEdge(none, QRectF(0, 1040, 1920, 40), screen, horizontalBottom),
	    Qt::BottomEdge
	);
	QCOMPARE(panelSlideEdge(none, QRectF(0, 0, 50, 1080), screen, verticalLeft), Qt::LeftEdge);
	QCOMPARE(panelSlideEdge(none, QRectF(1460, 0, 460, 1080), screen, verticalRight), Qt::RightEdge);
	QCOMPARE(panelSlideEdge(none, QRectF(860, 0, 200, 60), screen, top), Qt::TopEdge);
	QCOMPARE(panelSlideEdge(none, QRectF(10, 10, 300, 80), screen, topLeft), Qt::TopEdge);
	QCOMPARE(panelSlideEdge(none, QRectF(10, 10, 80, 300), screen, topLeft), Qt::LeftEdge);
	QCOMPARE(panelSlideEdge(none, QRectF(560, 140, 800, 800), screen, Qt::Edges()), Qt::TopEdge);
	QCOMPARE(panelSlideEdge(none, QRectF(0, 0, 1920, 1080), screen, all), Qt::TopEdge);
	QCOMPARE(panelSlideEdge(none, QRectF(2740, 0, 460, 1024), second, verticalRight), Qt::RightEdge);
	QCOMPARE(panelSlideEdge(none, QRectF(1920, 0, 460, 1024), second, verticalLeft), Qt::LeftEdge);
	QCOMPARE(
	    panelSlideEdge(Qt::BottomEdge, QRectF(0, 0, 50, 1080), screen, verticalLeft),
	    Qt::BottomEdge
	);
}

void TestPanelAnimation::contentTransform() {
	auto transform = PanelContentTransform();
	auto size = QSizeF(200, 100);

	auto frame = qs::windows::PanelAnimationFrame();
	frame.offset = QPointF(100, -20);
	transform.setFrame(frame, size);

	auto matrix = QMatrix4x4();
	transform.applyTo(&matrix);
	QCOMPARE(matrix.map(QPointF(0, 0)), QPointF(100, -20));
	QCOMPARE(matrix.map(QPointF(200, 100)), QPointF(300, 80));

	frame.offset = QPointF();
	frame.scale = 0.5;
	transform.setFrame(frame, size);
	matrix = QMatrix4x4();
	transform.applyTo(&matrix);
	QCOMPARE(matrix.map(QPointF(0, 0)), QPointF(50, 25));
	QCOMPARE(matrix.map(QPointF(100, 50)), QPointF(100, 50));
	QCOMPARE(matrix.map(QPointF(200, 100)), QPointF(150, 75));

	transform.setBypassed(true);
	matrix = QMatrix4x4();
	transform.applyTo(&matrix);
	QVERIFY(matrix.isIdentity());
}

void TestPanelAnimation::maskBypass() {
	auto window = QQuickWindow();
	window.resize(400, 300);

	auto content = QQuickItem();
	content.setParentItem(window.contentItem());
	content.setSize(QSizeF(400, 300));

	auto child = QQuickItem();
	child.setParentItem(&content);
	child.setPosition(QPointF(10, 20));
	child.setSize(QSizeF(50, 60));

	QCOMPARE(child.mapToScene(QPointF(0, 0)), QPointF(10, 20));

	{
		auto transform = PanelContentTransform();
		transform.appendToItem(&content);

		auto frame = qs::windows::PanelAnimationFrame();
		frame.offset = QPointF(400, 0);
		transform.setFrame(frame, content.size());
		QCOMPARE(child.mapToScene(QPointF(0, 0)), QPointF(410, 20));

		transform.setBypassed(true);
		QCOMPARE(child.mapToScene(QPointF(0, 0)), QPointF(10, 20));
		transform.setBypassed(false);
		QCOMPARE(child.mapToScene(QPointF(0, 0)), QPointF(410, 20));
	}

	QCOMPARE(child.mapToScene(QPointF(0, 0)), QPointF(10, 20));
}

void TestPanelAnimation::overlayFollowsRoot() {
	auto engine = QQmlEngine();
	auto component = QQmlComponent(&engine);
	component.setData(
	    R"(
		import QtQuick
		import QtQuick.Controls
		Window {
			width: 400
			height: 300
			visible: true
			property Item content: content
			property Item anchorItem: anchorItem
			property Popup popup: popup
			Item {
				id: content
				anchors.fill: parent
				Item { id: anchorItem; x: 10; y: 20; width: 50; height: 60 }
				Popup { id: popup; parent: anchorItem; x: 5; y: 5; width: 30; height: 30; visible: true }
			}
		}
	)",
	    QUrl()
	);

	auto* object = component.create();
	QVERIFY2(object != nullptr, qPrintable(component.errorString()));
	auto* window = qobject_cast<QQuickWindow*>(object);
	QVERIFY(window != nullptr);
	QVERIFY(QTest::qWaitForWindowExposed(window));

	auto* root = window->contentItem();
	auto* content = object->property("content").value<QQuickItem*>();
	auto* anchorItem = object->property("anchorItem").value<QQuickItem*>();
	auto* popup = object->property("popup").value<QObject*>();
	QVERIFY(content != nullptr && anchorItem != nullptr && popup != nullptr);
	QTRY_VERIFY(popup->property("opened").toBool());

	auto* popupContent = popup->property("contentItem").value<QQuickItem*>();
	QVERIFY(popupContent != nullptr);

	auto underContent = false;
	auto underRoot = false;
	for (auto* item = popupContent->parentItem(); item != nullptr; item = item->parentItem()) {
		if (item == content) underContent = true;
		if (item == root) underRoot = true;
	}

	QVERIFY(!underContent);
	QVERIFY(underRoot);

	auto popupBefore = popupContent->mapToScene(QPointF(0, 0));
	auto anchorBefore = anchorItem->mapToScene(QPointF(0, 0));
	auto relativeBefore = content->mapFromItem(anchorItem, QPointF(0, 0));

	{
		auto transform = PanelContentTransform();
		transform.appendToItem(content);

		auto frame = qs::windows::PanelAnimationFrame();
		frame.offset = QPointF(400, 0);
		transform.setFrame(frame, QSizeF(window->size()));

		QCOMPARE(anchorItem->mapToScene(QPointF(0, 0)), anchorBefore + QPointF(400, 0));
		QCOMPARE(popupContent->mapToScene(QPointF(0, 0)), popupBefore);
	}

	{
		auto transform = PanelContentTransform();
		transform.appendToItem(root);

		auto frame = qs::windows::PanelAnimationFrame();
		frame.offset = QPointF(400, 0);
		transform.setFrame(frame, QSizeF(window->size()));

		QCOMPARE(popupContent->mapToScene(QPointF(0, 0)), popupBefore + QPointF(400, 0));
		QCOMPARE(anchorItem->mapToScene(QPointF(0, 0)), anchorBefore + QPointF(400, 0));
		QCOMPARE(content->mapFromItem(anchorItem, QPointF(0, 0)), relativeBefore);

		transform.setBypassed(true);
		QCOMPARE(anchorItem->mapToScene(QPointF(0, 0)), anchorBefore);
		QCOMPARE(popupContent->mapToScene(QPointF(0, 0)), popupBefore);
		transform.setBypassed(false);

		root->setOpacity(0.25);
		QVERIFY(qFuzzyCompare(effectiveOpacity(popupContent), 0.25));
		root->setOpacity(1);
	}

	QCOMPARE(anchorItem->mapToScene(QPointF(0, 0)), anchorBefore);
	delete object;
}

void TestPanelAnimation::ruleMerging() {
	auto data = QByteArray(R"({
		"enable": false,
		"layered": true,
		"rules": [
			{"namespace": ".*", "animation": "popin 93%", "blur": true},
			{"namespace": "quickshell:.*", "ignoreAlpha": 0.79},
			{"namespace": "quickshell:bar", "animation": "slide"},
			{"namespace": "quickshell:session", "animation": null},
			{"namespace": "quickshell:overview", "animation": "noAnim", "ignoreAlpha": null},
			{"namespace": "quickshell:bad", "animation": "wobble"},
			{"namespace": "quickshell:number", "animation": 3},
			{"namespace": "quickshell:(", "animation": "fade"},
			{"animation": "fade"}
		]
	})");

	auto warnings = QStringList();
	auto rules = LayerRules::parse(data, "test.json", &warnings);
	QVERIFY(rules.valid());
	QVERIFY(!rules.enabled());
	QVERIFY(rules.layered());
	QCOMPARE(rules.size(), qsizetype(7));
	QCOMPARE(warnings.size(), qsizetype(4));

	QCOMPARE(rules.ruleFor("quickshell:bar").animation, style(Kind::Slide));
	QCOMPARE(rules.ruleFor("quickshell:bar").ignoreAlpha, std::optional<qreal>(0.79));
	QVERIFY(rules.ruleFor("quickshell:bar").blur);
	QCOMPARE(rules.ruleFor("quickshell:barx").animation, style(Kind::Popin, std::nullopt, 0.93));
	QCOMPARE(rules.ruleFor("xquickshell:bar").animation, style(Kind::Popin, std::nullopt, 0.93));
	QCOMPARE(rules.ruleFor("quickshell:session").animation, style(Kind::None));
	QCOMPARE(rules.ruleFor("quickshell:overview").animation, style(Kind::None));
	QVERIFY(!rules.ruleFor("quickshell:overview").ignoreAlpha.has_value());
	QCOMPARE(rules.ruleFor("quickshell:bad").animation, style(Kind::Popin, std::nullopt, 0.93));
	QCOMPARE(rules.ruleFor("quickshell:number").animation, style(Kind::Popin, std::nullopt, 0.93));

	auto info = rules.info();
	QCOMPARE(info.size(), qsizetype(7));
	QCOMPARE(info.at(0).toMap().value("animation").toString(), QString("popin 93%"));
	QCOMPARE(info.at(3).toMap().value("animation").toString(), QString("noAnim"));

	auto broken = LayerRules::parse("{", "broken.json", &warnings);
	QVERIFY(!broken.valid());
	QVERIFY(broken.enabled());
	QCOMPARE(broken.ruleFor("quickshell:bar").animation, style(Kind::None));

	auto empty = LayerRules::parse(QByteArray(), "empty.json");
	QVERIFY(!empty.valid());
	QCOMPARE(empty.ruleFor("anything").animation, style(Kind::None));
}

void TestPanelAnimation::iiRules() {
	auto path = qEnvironmentVariable("IIW_LAYER_RULES");
	if (path.isEmpty()) QSKIP("IIW_LAYER_RULES is not set to ii's defaults/windows/layerrules.json");

	auto file = QFile(path);
	QVERIFY2(file.open(QFile::ReadOnly), qPrintable(path));

	auto warnings = QStringList();
	auto rules = LayerRules::parse(file.readAll(), path, &warnings);
	QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
	QVERIFY(rules.valid());
	QVERIFY(rules.enabled());

	struct Expect {
		const char* ns;
		PanelAnimationStyle animation;
	};

	const auto expected = {
	    Expect {"quickshell:sidebarRight", style(Kind::Slide, Qt::RightEdge)},
	    Expect {"quickshell:sidebarLeft", style(Kind::Slide, Qt::LeftEdge)},
	    Expect {"quickshell:bar", style(Kind::Slide)},
	    Expect {"quickshell:verticalBar", style(Kind::Slide)},
	    Expect {"quickshell:dock", style(Kind::Slide)},
	    Expect {"quickshell:cheatsheet", style(Kind::Slide, Qt::BottomEdge)},
	    Expect {"quickshell:osk", style(Kind::Slide, Qt::BottomEdge)},
	    Expect {"quickshell:wallpaperSelector", style(Kind::Slide, Qt::TopEdge)},
	    Expect {"quickshell:reloadPopup", style(Kind::Slide)},
	    Expect {"quickshell:screenCorners", style(Kind::Popin, std::nullopt, 1.2)},
	    Expect {"quickshell:notificationPopup", style(Kind::Fade)},
	    Expect {"quickshell:overview", style(Kind::None)},
	    Expect {"quickshell:session", style(Kind::None)},
	    Expect {"quickshell:overlay", style(Kind::None)},
	    Expect {"quickshell:regionSelector", style(Kind::None)},
	    Expect {"quickshell:screenshot", style(Kind::None)},
	    Expect {"quickshell:polkit", style(Kind::None)},
	    Expect {"quickshell:actionCenter", style(Kind::None)},
	    Expect {"quickshell:wStartMenu", style(Kind::None)},
	    Expect {"quickshell:wTaskView", style(Kind::None)},
	    Expect {"quickshell:popup", style(Kind::None)},
	    Expect {"quickshell:background", style(Kind::None)},
	    Expect {"quickshell:spotlight", style(Kind::None)},
	    Expect {"quickshell:colorPicker", style(Kind::None)},
	    Expect {"quickshell:nowPlaying", style(Kind::None)},
	    Expect {"quickshell:mediaControls", style(Kind::Popin, std::nullopt, 0.93)},
	    Expect {"quickshell:equalizer", style(Kind::Popin, std::nullopt, 0.93)},
	    Expect {"osk", style(Kind::None)},
	    Expect {"sideright", style(Kind::Slide, Qt::RightEdge)},
	};

	for (const auto& expect: expected) {
		auto got = rules.ruleFor(expect.ns).animation;
		auto message =
		    QString("%1: %2, want %3").arg(expect.ns, got.toString(), expect.animation.toString());
		QVERIFY2(got == expect.animation, qPrintable(message));
	}

	QVERIFY(rules.ruleFor("quickshell:sidebarRight").blur);
	QCOMPARE(rules.ruleFor("quickshell:sidebarRight").ignoreAlpha, std::optional<qreal>(0.79));
	QVERIFY(!rules.ruleFor("quickshell:background").blur);
	QCOMPARE(rules.ruleFor("quickshell:overlay").ignoreAlpha, std::optional<qreal>(1));
	QVERIFY(rules.ruleFor("quickshell:session").blur);
	QCOMPARE(rules.ruleFor("quickshell:session").ignoreAlpha, std::optional<qreal>(0));
	QCOMPARE(rules.ruleFor("quickshell:wTaskView").ignoreAlpha, std::optional<qreal>(0));
	QCOMPARE(rules.ruleFor("quickshell:popup").ignoreAlpha, std::optional<qreal>(1));
	QCOMPARE(rules.ruleFor("bar0").ignoreAlpha, std::optional<qreal>(0.6));

	struct ExpectEffect {
		const char* ns;
		PanelEffect effect;
	};

	const auto effects = {
	    ExpectEffect {"quickshell:sidebarRight", PanelEffect::Cascade},
	    ExpectEffect {"quickshell:sidebarLeft", PanelEffect::Cascade},
	    ExpectEffect {"quickshell:bar", PanelEffect::Cascade},
	    ExpectEffect {"quickshell:verticalBar", PanelEffect::Cascade},
	    ExpectEffect {"quickshell:reloadPopup", PanelEffect::Cascade},
	    ExpectEffect {"quickshell:dock", PanelEffect::Cascade},
	    ExpectEffect {"quickshell:cheatsheet", PanelEffect::Reveal},
	    ExpectEffect {"quickshell:osk", PanelEffect::Reveal},
	    ExpectEffect {"quickshell:wallpaperSelector", PanelEffect::Reveal},
	    ExpectEffect {"quickshell:screenCorners", PanelEffect::Blur},
	    ExpectEffect {"quickshell:notificationPopup", PanelEffect::Dissolve},
	    ExpectEffect {"quickshell:overview", PanelEffect::Standard},
	    ExpectEffect {"quickshell:session", PanelEffect::Standard},
	    ExpectEffect {"quickshell:background", PanelEffect::Standard},
	    ExpectEffect {"quickshell:mediaControls", PanelEffect::Blur},
	};

	for (const auto& expect: effects) {
		auto rule = rules.ruleFor(expect.ns);
		auto got = qs::windows::resolvePanelEffect(rule.animation, rule.effect);
		auto message = QString("%1: %2, want %3").arg(expect.ns, name(got), name(expect.effect));
		QVERIFY2(got == expect.effect, qPrintable(message));
	}
}

void TestPanelAnimation::springCurve() {
	for (auto overshoot: {0.0, 0.005, 0.013, 0.03, 0.06, 0.1}) {
		auto spring = SpringCurve::withOvershoot(overshoot);
		QCOMPARE(spring.valueAt(0), 0.0);
		QCOMPARE(spring.valueAt(1), 1.0);
		QCOMPARE(spring.valueAt(-0.5), 0.0);
		QCOMPARE(spring.valueAt(1.5), 1.0);

		auto peak = 0.0;
		auto peakAt = 0.0;
		for (auto i = 1; i < 2000; i++) {
			auto progress = i / 2000.0;
			auto value = spring.valueAt(progress);
			if (value > peak) {
				peak = value;
				peakAt = progress;
			}
		}

		auto previous = 0.0;
		for (auto i = 1; i < 2000 && i / 2000.0 <= peakAt; i++) {
			auto value = spring.valueAt(i / 2000.0);
			auto dip = QString("overshoot %1 dips at %2").arg(overshoot).arg(i);
			QVERIFY2(value >= previous, qPrintable(dip));
			previous = value;
		}

		for (auto i = 0; i <= 100; i++) {
			auto progress = 0.9 + i / 1000.0;
			QVERIFY2(
			    std::abs(spring.valueAt(progress) - 1.0) < 0.004,
			    qPrintable(QString("overshoot %1 has not settled at %2").arg(overshoot).arg(progress))
			);
		}

		if (overshoot == 0) {
			QCOMPARE(spring.dampingRatio(), 1.0);
			QVERIFY(peak <= 1.0 + 1e-9);
		} else {
			auto message = QString("overshoot %1 peaks at %2 (at %3)")
			                   .arg(overshoot)
			                   .arg(peak - 1)
			                   .arg(peakAt);
			QVERIFY2(near(peak - 1.0, overshoot, 0.002 + overshoot * 0.02), qPrintable(message));
			QVERIFY2(peakAt > 0.15 && peakAt < 0.85, qPrintable(message));
			QVERIFY(spring.dampingRatio() > 0 && spring.dampingRatio() < 1);
		}
	}

	QVERIFY(near(SpringCurve::withOvershoot(0.06).dampingRatio(), 0.667, 1e-3));
	QCOMPARE(SpringCurve::withOvershoot(-1).dampingRatio(), 1.0);
	QVERIFY(SpringCurve::withOvershoot(5).dampingRatio() > 0.35);

	auto linear = SpringCurve();
	QCOMPARE(linear.valueAt(0.25), 0.25);
}

void TestPanelAnimation::springOvershootLimits() {
	using qs::windows::panelSpringOvershoot;

	auto sidebar = QSizeF(460, 1080);
	auto right = style(Kind::Slide, Qt::RightEdge);
	auto sidebarOvershoot = panelSpringOvershoot(right, PanelEffect::Cascade, Qt::RightEdge, sidebar);
	QVERIFY(sidebarOvershoot > 0);
	QVERIFY(sidebarOvershoot * sidebar.width() <= 6 + 1e-9);

	auto bar = QSizeF(1920, 45);
	QCOMPARE(panelSpringOvershoot(style(Kind::Slide), PanelEffect::Cascade, Qt::TopEdge, bar), 0.06);

	auto selector = QSizeF(1200, 400);
	QCOMPARE(qs::windows::panelRevealTravel(Qt::TopEdge, selector), 48.0);
	QCOMPARE(qs::windows::panelRevealTravel(Qt::BottomEdge, QSizeF(1920, 60)), 12.0);
	QCOMPARE(qs::windows::panelRevealTravel(Qt::BottomEdge, QSizeF(100, 8)), 8.0);
	auto top = style(Kind::Slide, Qt::TopEdge);
	QCOMPARE(panelSpringOvershoot(top, PanelEffect::Reveal, Qt::TopEdge, selector), 0.06);

	auto grow = style(Kind::Popin, std::nullopt, 0.93);
	auto growOvershoot = panelSpringOvershoot(grow, PanelEffect::Blur, Qt::TopEdge, QSizeF(800, 600));
	QVERIFY(growOvershoot > 0);
	QVERIFY(growOvershoot * 0.07 * 400 <= 6 + 1e-9);

	auto shrink = style(Kind::Popin, std::nullopt, 1.2);
	QCOMPARE(panelSpringOvershoot(shrink, PanelEffect::Blur, Qt::TopEdge, QSizeF(30, 30)), 0.0);
	QCOMPARE(
	    panelSpringOvershoot(style(Kind::Fade), PanelEffect::Dissolve, Qt::TopEdge, QSizeF(400, 800)),
	    0.0
	);

	auto timing = PanelAnimationTiming::expressive(PanelEffect::Cascade, sidebarOvershoot);
	auto animation = PanelAnimation();
	animation.jumpTo(false);
	animation.start(true, right, timing);

	auto furthest = 0.0;
	for (auto elapsed = 0.0; elapsed <= animation.duration(); elapsed += 1) {
		animation.advance(elapsed);
		auto frame = qs::windows::panelAnimationFrame(
		    right,
		    Qt::RightEdge,
		    sidebar,
		    animation.motion(),
		    animation.opacity(),
		    1
		);
		furthest = std::min(furthest, frame.offset.x());
	}

	QVERIFY2(furthest < 0, "the sidebar should overshoot a little");
	QVERIFY2(furthest >= -6.5, qPrintable(QString("overshoot of %1 px").arg(-furthest)));
}

void TestPanelAnimation::effectNames() {
	const auto all = {
	    PanelEffect::Auto,
	    PanelEffect::Standard,
	    PanelEffect::Spring,
	    PanelEffect::Cascade,
	    PanelEffect::Reveal,
	    PanelEffect::Blur,
	    PanelEffect::Dissolve,
	};

	for (auto effect: all) {
		auto parsed = qs::windows::parsePanelEffect(name(effect));
		QVERIFY(parsed.has_value());
		QCOMPARE(name(*parsed), name(effect));
	}

	QCOMPARE(name(*qs::windows::parsePanelEffect(" Reveal ")), QString("reveal"));
	QVERIFY(!qs::windows::parsePanelEffect("sparkle").has_value());
	QVERIFY(!qs::windows::parsePanelEffect("").has_value());
}

void TestPanelAnimation::effectResolution() {
	using qs::windows::panelEffectUsesShader;
	using qs::windows::resolvePanelEffect;

	QCOMPARE(name(resolvePanelEffect(style(Kind::Slide), PanelEffect::Auto)), QString("cascade"));
	QCOMPARE(
	    name(resolvePanelEffect(style(Kind::Slide, Qt::LeftEdge), PanelEffect::Auto)),
	    QString("cascade")
	);
	QCOMPARE(
	    name(resolvePanelEffect(style(Kind::Slide, Qt::RightEdge), PanelEffect::Auto)),
	    QString("cascade")
	);
	QCOMPARE(
	    name(resolvePanelEffect(style(Kind::Slide, Qt::TopEdge), PanelEffect::Auto)),
	    QString("reveal")
	);
	QCOMPARE(
	    name(resolvePanelEffect(style(Kind::Slide, Qt::BottomEdge), PanelEffect::Auto)),
	    QString("reveal")
	);
	QCOMPARE(
	    name(resolvePanelEffect(style(Kind::Popin, std::nullopt, 0.93), PanelEffect::Auto)),
	    QString("blur")
	);
	QCOMPARE(name(resolvePanelEffect(style(Kind::Fade), PanelEffect::Auto)), QString("dissolve"));
	QCOMPARE(name(resolvePanelEffect(style(Kind::None), PanelEffect::Auto)), QString("standard"));
	QCOMPARE(name(resolvePanelEffect(style(Kind::None), PanelEffect::Dissolve)), QString("standard"));
	QCOMPARE(
	    name(resolvePanelEffect(style(Kind::Slide), PanelEffect::Dissolve)),
	    QString("dissolve")
	);
	QCOMPARE(name(resolvePanelEffect(style(Kind::Fade), PanelEffect::Standard)), QString("standard"));

	QVERIFY(panelEffectUsesShader(PanelEffect::Reveal));
	QVERIFY(panelEffectUsesShader(PanelEffect::Blur));
	QVERIFY(panelEffectUsesShader(PanelEffect::Dissolve));
	QVERIFY(!panelEffectUsesShader(PanelEffect::Cascade));
	QVERIFY(!panelEffectUsesShader(PanelEffect::Spring));
	QVERIFY(!panelEffectUsesShader(PanelEffect::Standard));
	QVERIFY(!panelEffectUsesShader(PanelEffect::Auto));
}

void TestPanelAnimation::expressiveRules() {
	auto data = QByteArray(R"({
		"rules": [
			{"namespace": ".*", "animation": "popin 93%", "expressive": "dissolve"},
			{"namespace": "quickshell:.*", "expressive": "blur"},
			{"namespace": "quickshell:bar", "animation": "slide", "expressive": null},
			{"namespace": "quickshell:dock", "expressive": "STANDARD"},
			{"namespace": "quickshell:bad", "expressive": "sparkle"},
			{"namespace": "quickshell:number", "expressive": 3}
		]
	})");

	auto warnings = QStringList();
	auto rules = LayerRules::parse(data, "expressive.json", &warnings);
	QVERIFY(rules.valid());
	QCOMPARE(rules.size(), qsizetype(6));
	QCOMPARE(warnings.size(), qsizetype(2));

	QCOMPARE(name(rules.ruleFor("other").effect), QString("dissolve"));
	QCOMPARE(name(rules.ruleFor("quickshell:x").effect), QString("blur"));
	QCOMPARE(name(rules.ruleFor("quickshell:bar").effect), QString("auto"));
	QCOMPARE(rules.ruleFor("quickshell:bar").animation, style(Kind::Slide));
	QCOMPARE(name(rules.ruleFor("quickshell:dock").effect), QString("standard"));
	QCOMPARE(name(rules.ruleFor("quickshell:bad").effect), QString("blur"));
	QCOMPARE(name(rules.ruleFor("quickshell:number").effect), QString("blur"));

	auto info = rules.info();
	QCOMPARE(info.at(0).toMap().value("expressive").toString(), QString("dissolve"));
	QCOMPARE(info.at(2).toMap().value("expressive").toString(), QString("auto"));
	QCOMPARE(info.at(3).toMap().value("expressive").toString(), QString("standard"));
	QVERIFY(!info.at(4).toMap().contains("expressive"));

	auto plainData = QByteArray(R"({"rules": [{"namespace": "a", "animation": "fade"}]})");
	auto plain = LayerRules::parse(plainData, "plain.json");
	QCOMPARE(name(plain.ruleFor("a").effect), QString("auto"));
	QCOMPARE(name(LayerRules().ruleFor("a").effect), QString("auto"));
}

void TestPanelAnimation::expressiveTimeline() {
	struct Case {
		PanelEffect effect;
		PanelAnimationStyle style;
	};

	const auto cases = {
	    Case {PanelEffect::Cascade, style(Kind::Slide, Qt::RightEdge)},
	    Case {PanelEffect::Spring, style(Kind::Slide, Qt::LeftEdge)},
	    Case {PanelEffect::Reveal, style(Kind::Slide, Qt::TopEdge)},
	    Case {PanelEffect::Blur, style(Kind::Popin, std::nullopt, 0.93)},
	    Case {PanelEffect::Dissolve, style(Kind::Fade)},
	};

	for (const auto& test: cases) {
		auto timing = PanelAnimationTiming::expressive(test.effect, 0.03);
		auto label = name(test.effect);
		auto animation = PanelAnimation();
		animation.jumpTo(false);

		animation.start(true, test.style, timing);
		auto showMs = animation.duration();
		QVERIFY2(showMs >= 320 && showMs <= 420, qPrintable(label + QString(" in %1").arg(showMs)));
		QVERIFY(!animation.advance(0));
		QCOMPARE(animation.opacity(), 0.0);
		QCOMPARE(animation.motion(), test.style.moves() ? 0.0 : 1.0);
		QCOMPARE(animation.effect(), qs::windows::panelEffectUsesShader(test.effect) ? 0.0 : 1.0);

		for (auto elapsed = 0.0; elapsed < showMs; elapsed += 5) {
			animation.advance(elapsed);
			QVERIFY(animation.motion() >= 0 && animation.motion() <= 1.1);
			QVERIFY(animation.opacity() >= 0 && animation.opacity() <= 1);
			QVERIFY(animation.effect() >= 0 && animation.effect() <= 1);
		}

		QVERIFY(animation.advance(showMs));
		QVERIFY(animation.settled());
		QCOMPARE(animation.effect(), 1.0);

		animation.start(false, test.style, timing);
		auto hideMs = animation.duration();
		QVERIFY2(hideMs >= 200 && hideMs <= 260, qPrintable(label + QString(" out %1").arg(hideMs)));
		QVERIFY(!animation.advance(hideMs / 2));
		QVERIFY(animation.advance(hideMs));
		QVERIFY(animation.settled());
		QVERIFY(!animation.showing());
		QCOMPARE(animation.opacity(), 0.0);
		QCOMPARE(animation.effect(), 0.0);
	}

	auto standard = PanelAnimationTiming::hyprland();
	QCOMPARE(standard.showEffectMs, 0.0);
	QVERIFY(!standard.showSpring.has_value());
}

void TestPanelAnimation::expressiveReverse() {
	auto slide = style(Kind::Slide, Qt::RightEdge);
	auto timing = PanelAnimationTiming::expressive(PanelEffect::Cascade, 0.06);
	auto animation = PanelAnimation();
	animation.jumpTo(false);
	animation.start(true, slide, timing);

	auto peak = 0.0;
	auto peakAt = 0.0;
	for (auto elapsed = 0.0; elapsed < animation.duration(); elapsed += 1) {
		animation.advance(elapsed);
		if (animation.motion() > peak) {
			peak = animation.motion();
			peakAt = elapsed;
		}
	}

	QVERIFY(peak > 1.0);
	animation.jumpTo(false);
	animation.start(true, slide, timing);
	animation.advance(peakAt);
	auto motion = animation.motion();
	auto opacity = animation.opacity();
	QVERIFY(motion > 1.0);

	animation.start(false, slide, timing);
	QVERIFY(!animation.advance(0));
	QCOMPARE(animation.motion(), motion);
	QCOMPARE(animation.opacity(), opacity);
	QVERIFY(near(animation.duration(), timing.hideMotionMs * motion, 1e-6));

	animation.advance(60);
	auto closing = animation.motion();
	QVERIFY(closing < motion);

	animation.start(true, slide, timing);
	QVERIFY(!animation.advance(0));
	QCOMPARE(animation.motion(), closing);
	QVERIFY(near(animation.duration(), timing.showMotionMs * std::abs(1 - closing), 1e-6));
	QVERIFY(animation.advance(animation.duration()));
	QVERIFY(animation.settled());

	auto reveal = PanelAnimationTiming::expressive(PanelEffect::Reveal, 0.06);
	auto top = style(Kind::Slide, Qt::TopEdge);
	animation.jumpTo(false);
	animation.start(true, top, reveal);
	animation.advance(120);
	auto effect = animation.effect();
	QVERIFY(effect > 0 && effect < 1);
	animation.start(false, top, reveal);
	animation.advance(0);
	QCOMPARE(animation.effect(), effect);
	auto expected = std::max(
	    {reveal.hideMotionMs * animation.motion(),
	     reveal.hideEffectMs * effect,
	     reveal.hideFadeMs * animation.opacity()}
	);
	QVERIFY(near(animation.duration(), expected, 1e-6));
}

void TestPanelAnimation::revealShape() {
	using qs::windows::panelEffectFrame;
	auto size = QSizeF(1200, 400);
	auto window = QRectF(QPointF(), size);
	const auto edges = {Qt::TopEdge, Qt::BottomEdge, Qt::LeftEdge, Qt::RightEdge};

	for (auto edge: edges) {
		auto hidden = panelEffectFrame(PanelEffect::Reveal, edge, size, 0, QPointF());
		QVERIFY(hidden.revealRadius >= 0);
		QVERIFY(hidden.revealRect.intersected(window).isEmpty());

		auto shown = panelEffectFrame(PanelEffect::Reveal, edge, size, 1, QPointF());
		QVERIFY(shown.revealRadius > 0);
		for (auto y = 0.5; y < size.height(); y += 13) {
			for (auto x = 0.5; x < size.width(); x += 17) {
				QVERIFY(roundedBoxDistance({x, y}, shown.revealRect, shown.revealRadius) <= -0.5);
			}
		}

		const auto corners =
		    {window.topLeft(), window.topRight(), window.bottomLeft(), window.bottomRight()};
		for (const auto& corner: corners) {
			QVERIFY(roundedBoxDistance(corner, shown.revealRect, shown.revealRadius) <= -0.5);
		}

		auto previous = 0.0;
		for (auto i = 0; i <= 20; i++) {
			auto frame = panelEffectFrame(PanelEffect::Reveal, edge, size, i / 20.0, QPointF());
			auto covered = frame.revealRect.intersected(window);
			auto area = covered.isEmpty() ? 0.0 : covered.width() * covered.height();
			QVERIFY(area >= previous);
			previous = area;
			QVERIFY(frame.revealRadius <= frame.revealRect.width() / 2 + 1e-9);
			QVERIFY(frame.revealRadius <= frame.revealRect.height() / 2 + 1e-9);
		}

		auto offset = QPointF();
		if (edge == Qt::LeftEdge) offset.setX(-20);
		if (edge == Qt::RightEdge) offset.setX(20);
		if (edge == Qt::TopEdge) offset.setY(-20);
		if (edge == Qt::BottomEdge) offset.setY(20);
		auto moved = panelEffectFrame(PanelEffect::Reveal, edge, size, 0.5, offset);
		auto still = panelEffectFrame(PanelEffect::Reveal, edge, size, 0.5, QPointF());
		QCOMPARE(moved.revealRect, still.revealRect.translated(-offset));
		QCOMPARE(moved.revealRadius, still.revealRadius);
	}

	auto half = panelEffectFrame(PanelEffect::Reveal, Qt::TopEdge, size, 0.5, QPointF());
	QVERIFY(roundedBoxDistance({600, 10}, half.revealRect, half.revealRadius) < 0);
	QVERIFY(roundedBoxDistance({600, 390}, half.revealRect, half.revealRadius) > 0);
	QVERIFY(roundedBoxDistance({5, 5}, half.revealRect, half.revealRadius) > 0);

	auto bottom = panelEffectFrame(PanelEffect::Reveal, Qt::BottomEdge, size, 0.5, QPointF());
	QVERIFY(roundedBoxDistance({600, 390}, bottom.revealRect, bottom.revealRadius) < 0);
	QVERIFY(roundedBoxDistance({600, 10}, bottom.revealRect, bottom.revealRadius) > 0);
}

void TestPanelAnimation::blurAndDissolveFrames() {
	using qs::windows::panelBlurSampling;
	using qs::windows::panelEffectFrame;

	auto large = QSizeF(800, 600);
	QCOMPARE(panelEffectFrame(PanelEffect::Blur, Qt::TopEdge, large, 0, QPointF()).blurRadius, 14.0);
	auto small = panelEffectFrame(PanelEffect::Blur, Qt::TopEdge, QSizeF(30, 30), 0, QPointF());
	QCOMPARE(small.blurRadius, 4.0);
	QCOMPARE(panelEffectFrame(PanelEffect::Blur, Qt::TopEdge, large, 1, QPointF()).blurRadius, 0.0);

	auto previous = 15.0;
	for (auto i = 0; i <= 10; i++) {
		auto frame = panelEffectFrame(PanelEffect::Blur, Qt::TopEdge, large, i / 10.0, QPointF());
		QVERIFY(frame.blurRadius <= previous);
		QCOMPARE(frame.dissolve, 1.0);
		QVERIFY(frame.revealRadius < 0);
		previous = frame.blurRadius;
	}

	auto none = panelBlurSampling(0, large, 1);
	QVERIFY(none.step.isNull());
	QCOMPARE(none.lod, 0.0);
	QVERIFY(panelBlurSampling(0.2, large, 1).step.isNull());

	auto sharp = panelBlurSampling(14, large, 1);
	QVERIFY(near(sharp.step.x(), 7.0 / 800, 1e-12));
	QVERIFY(near(sharp.step.y(), 7.0 / 600, 1e-12));
	QVERIFY(near(sharp.lod, std::log2(7.0), 1e-9));
	QVERIFY(near(panelBlurSampling(14, large, 2).lod, std::log2(14.0), 1e-9));
	QCOMPARE(panelBlurSampling(1, large, 1).lod, 0.0);

	auto dissolveAt = [&large](qreal progress) {
		auto frame = panelEffectFrame(PanelEffect::Dissolve, Qt::TopEdge, large, progress, QPointF());
		return frame.dissolve;
	};

	QCOMPARE(dissolveAt(0), 0.0);
	QCOMPARE(dissolveAt(0.4), 0.4);
	QCOMPARE(dissolveAt(1.2), 1.0);

	for (auto effect: {PanelEffect::Cascade, PanelEffect::Spring, PanelEffect::Standard}) {
		auto frame = panelEffectFrame(effect, Qt::TopEdge, large, 0, QPointF());
		QCOMPARE(frame.blurRadius, 0.0);
		QCOMPARE(frame.dissolve, 1.0);
		QVERIFY(frame.revealRadius < 0);
	}
}

void TestPanelAnimation::cascadeTiming() {
	using qs::windows::panelCascadeDistance;
	using qs::windows::panelCascadeFrame;

	const auto& timing = PanelCascadeTiming::expressive();
	QVERIFY(timing.stepMs >= 20 && timing.stepMs <= 30);
	QCOMPARE(timing.delayFor(-1), 0.0);
	QCOMPARE(timing.delayFor(0), 0.0);
	QCOMPARE(timing.delayFor(1), timing.stepMs);
	QCOMPARE(timing.delayFor(5), 120.0);
	QCOMPARE(timing.delayFor(9), 120.0);
	QCOMPARE(timing.durationFor(0), 0.0);
	QCOMPARE(timing.durationFor(1), timing.sectionMs);
	QCOMPARE(timing.durationFor(6), 400.0);
	QCOMPARE(timing.durationFor(12), 400.0);

	QCOMPARE(panelCascadeDistance(Qt::RightEdge, QSizeF(460, 1080)), 36.0);
	QCOMPARE(panelCascadeDistance(Qt::TopEdge, QSizeF(1920, 45)), 16.0);
	QCOMPARE(panelCascadeDistance(Qt::LeftEdge, QSizeF(300, 1080)), 24.0);
	QCOMPARE(qs::windows::panelCascadeOvershoot(36), 0.1);
	QCOMPARE(qs::windows::panelCascadeOvershoot(80), 0.05);

	auto spring = SpringCurve::withOvershoot(qs::windows::panelCascadeOvershoot(36));
	auto start = panelCascadeFrame(timing, spring, 0, 0, Qt::RightEdge, 36, 1);
	QCOMPARE(start.offset, QPointF(36, 0));
	QVERIFY(start.opacity > 0 && start.opacity < 0.01);

	auto waiting = panelCascadeFrame(timing, spring, 3, 50, Qt::RightEdge, 36, 1);
	QCOMPARE(waiting.offset, QPointF(36, 0));
	QCOMPARE(waiting.opacity, start.opacity);

	auto done = panelCascadeFrame(timing, spring, 5, 400, Qt::RightEdge, 36, 1);
	QCOMPARE(done.offset, QPointF(0, 0));
	QCOMPARE(done.opacity, 1.0);

	QCOMPARE(panelCascadeFrame(timing, spring, 0, 0, Qt::LeftEdge, 36, 1).offset, QPointF(-36, 0));
	QCOMPARE(panelCascadeFrame(timing, spring, 0, 0, Qt::TopEdge, 16, 1).offset, QPointF(0, -16));
	QCOMPARE(panelCascadeFrame(timing, spring, 0, 0, Qt::BottomEdge, 16, 1).offset, QPointF(0, 16));

	for (auto index = 0; index < 6; index++) {
		auto previousOpacity = 0.0;
		auto furthest = 0.0;

		for (auto elapsed = 0.0; elapsed <= 420; elapsed += 2) {
			auto frame = panelCascadeFrame(timing, spring, index, elapsed, Qt::RightEdge, 36, 1);
			QVERIFY(frame.opacity >= previousOpacity);
			QVERIFY(frame.opacity > 0);
			previousOpacity = frame.opacity;
			furthest = std::min(furthest, frame.offset.x());
			if (elapsed < timing.delayFor(index)) QCOMPARE(frame.offset.x(), 36.0);
		}

		auto message = QString("section %1 overshoots %2 px").arg(index).arg(-furthest);
		QVERIFY2(furthest >= -4.5, qPrintable(message));
	}

	auto later = panelCascadeFrame(timing, spring, 1, 60, Qt::RightEdge, 36, 1);
	auto earlier = panelCascadeFrame(timing, spring, 0, 60, Qt::RightEdge, 36, 1);
	QVERIFY(later.offset.x() > earlier.offset.x());
	QVERIFY(later.opacity < earlier.opacity);

	auto fractional = panelCascadeFrame(timing, spring, 0, 40, Qt::BottomEdge, 36, 1.5);
	QVERIFY(near(fractional.offset.y() * 1.5, std::round(fractional.offset.y() * 1.5), 1e-9));
}

void TestPanelAnimation::cascadeAcrossReversal() {
	auto slide = style(Kind::Slide, Qt::RightEdge);
	auto timing = PanelAnimationTiming::expressive(PanelEffect::Cascade, 0.01);
	auto animation = PanelAnimation();
	animation.jumpTo(false);

	animation.start(true, slide, timing);
	animation.startCascade(400);
	QCOMPARE(animation.duration(), 400.0);
	animation.advance(100);
	QCOMPARE(animation.cascadeTime(), 100.0);

	animation.start(false, slide, timing);
	QVERIFY(animation.duration() <= timing.hideMotionMs + 1e-9);
	animation.advance(50);
	QCOMPARE(animation.cascadeTime(), 150.0);

	animation.start(true, slide, timing);
	animation.advance(0);
	QCOMPARE(animation.cascadeTime(), 150.0);
	QVERIFY(animation.duration() >= 250 - 1e-9);
	QVERIFY(animation.advance(animation.duration()));
	QVERIFY(animation.cascadeTime() >= 400);
	QVERIFY(animation.settled());

	animation.jumpTo(false);
	QCOMPARE(animation.cascadeTime(), 0.0);
	animation.start(true, slide, timing);
	QCOMPARE(animation.duration(), timing.showMotionMs);
}

void TestPanelAnimation::cascadeSections() {
	using qs::windows::panelCascadeSections;

	auto sidebar = QmlScene();
	QVERIFY(sidebar.load(R"(
		import QtQuick
		Item {
			width: 460; height: 1080
			Item {
				anchors.fill: parent; anchors.margins: 5; anchors.leftMargin: 10
				Item {
					anchors.fill: parent
					Rectangle {
						objectName: "shadow"; anchors.fill: background; color: "black"
						Rectangle { anchors.fill: parent; color: "transparent" }
					}
					Rectangle {
						id: background; objectName: "background"; anchors.fill: parent; color: "gray"
						Item {
							anchors.fill: parent; anchors.margins: 10
							Rectangle { objectName: "s3"; y: 600; width: parent.width; height: 400 }
							Rectangle { objectName: "s0"; y: 0; width: parent.width; height: 180; color: "red" }
							Rectangle { objectName: "s1"; y: 190; width: parent.width; height: 60; color: "red" }
							Item {
								objectName: "s2"; y: 260; width: parent.width; height: 200
								Rectangle { anchors.fill: parent; color: "blue" }
							}
							Rectangle { objectName: "hidden"; y: 470; width: 300; height: 50; visible: false }
							Rectangle { objectName: "empty"; y: 530; width: parent.width; height: 0 }
							Rectangle { objectName: "clear"; y: 540; width: parent.width; height: 20; opacity: 0 }
							Item { objectName: "noContent"; y: 565; width: parent.width; height: 30 }
						}
					}
				}
			}
		}
	)"));

	auto* sidebarRoot = qobject_cast<QQuickItem*>(sidebar.root.get());
	QCOMPARE(
	    objectNames(panelCascadeSections(sidebarRoot, Qt::RightEdge)),
	    QStringList({"s0", "s1", "s2", "s3"})
	);

	auto bar = QmlScene();
	QVERIFY(bar.load(R"(
		import QtQuick
		Item {
			width: 1920; height: 70
			MouseArea {
				anchors.fill: parent
				Item { objectName: "maskRegion"; width: 1920; height: 60 }
				Item {
					width: 1920; height: 45
					Rectangle { objectName: "barBackground"; anchors.fill: parent; color: "black" }
					Item { objectName: "right"; x: 1500; width: 400; height: 45; Rectangle {} }
					Row { objectName: "middle"; x: 800; width: 320; height: 45; Rectangle {} }
					Item { objectName: "left"; x: 10; width: 400; height: 45; Rectangle {} }
				}
				Item {
					objectName: "decorators"; y: 45; width: 1920; height: 25
					Rectangle { width: 25; height: 25 }
					Rectangle { x: 1895; width: 25; height: 25 }
				}
			}
		}
	)"));

	auto* barRoot = qobject_cast<QQuickItem*>(bar.root.get());
	QCOMPARE(
	    objectNames(panelCascadeSections(barRoot, Qt::TopEdge)),
	    QStringList({"left", "middle", "right"})
	);

	auto stacked = QmlScene();
	QVERIFY(stacked.load(R"(
		import QtQuick
		Item {
			width: 400; height: 400
			Rectangle { width: 200; height: 300; color: "red" }
			Rectangle { x: 20; width: 200; height: 300; color: "green" }
			Rectangle { x: 40; width: 200; height: 300; color: "blue" }
		}
	)"));
	auto* stackedRoot = qobject_cast<QQuickItem*>(stacked.root.get());
	QVERIFY(panelCascadeSections(stackedRoot, Qt::LeftEdge).isEmpty());

	auto grid = QmlScene();
	QVERIFY(grid.load(R"(
		import QtQuick
		Item {
			width: 1000; height: 100
			Repeater { model: 20; Rectangle { x: index * 50; width: 40; height: 40 } }
		}
	)"));
	auto* gridRoot = qobject_cast<QQuickItem*>(grid.root.get());
	QVERIFY(panelCascadeSections(gridRoot, Qt::TopEdge).isEmpty());

	auto pair = QmlScene();
	QVERIFY(pair.load(R"(
		import QtQuick
		Item {
			width: 400; height: 400
			Rectangle { width: 400; height: 100 }
			Rectangle { y: 120; width: 400; height: 100 }
		}
	)"));
	auto* pairRoot = qobject_cast<QQuickItem*>(pair.root.get());
	QVERIFY(panelCascadeSections(pairRoot, Qt::LeftEdge).isEmpty());
	QVERIFY(panelCascadeSections(nullptr, Qt::LeftEdge).isEmpty());
}

void TestPanelAnimation::cascadeBypass() {
	auto window = QQuickWindow();
	window.resize(400, 300);

	auto column = QQuickItem();
	column.setParentItem(window.contentItem());
	column.setSize(QSizeF(400, 300));

	auto section = QQuickItem();
	section.setParentItem(&column);
	section.setPosition(QPointF(10, 120));
	section.setSize(QSizeF(380, 60));

	auto child = QQuickItem();
	child.setParentItem(&section);
	child.setPosition(QPointF(5, 6));
	child.setSize(QSizeF(20, 20));

	auto before = child.mapToScene(QPointF(0, 0));
	QCOMPARE(before, QPointF(15, 126));

	{
		auto transform = PanelContentTransform();
		transform.appendToItem(&section);

		auto frame = qs::windows::PanelAnimationFrame();
		frame.offset = QPointF(36, 0);
		transform.setFrame(frame, section.size());
		QCOMPARE(child.mapToScene(QPointF(0, 0)), before + QPointF(36, 0));
		QCOMPARE(section.position(), QPointF(10, 120));
		QCOMPARE(section.size(), QSizeF(380, 60));

		transform.setBypassed(true);
		QCOMPARE(child.mapToScene(QPointF(0, 0)), before);
		transform.setBypassed(false);

		section.setOpacity(0.25);
		QCOMPARE(section.position(), QPointF(10, 120));
		section.setOpacity(1);
	}

	QCOMPARE(child.mapToScene(QPointF(0, 0)), before);
}

void TestPanelAnimation::keepAlive() {
	auto lifetime = PanelEffectKeepAlive(100);
	auto expired = QSignalSpy(&lifetime, &PanelEffectKeepAlive::expired);

	QVERIFY(!lifetime.armed());
	QCOMPARE(lifetime.interval(), 100);

	lifetime.arm();
	QVERIFY(lifetime.armed());

	lifetime.acquire();
	QVERIFY(!lifetime.armed());
	QCOMPARE(lifetime.users(), 1);
	QTest::qWait(150);
	QCOMPARE(expired.count(), 0);

	lifetime.acquire();
	lifetime.release();
	QVERIFY(!lifetime.armed());
	QCOMPARE(lifetime.users(), 1);

	lifetime.release();
	QVERIFY(lifetime.armed());
	QCOMPARE(lifetime.users(), 0);

	QTest::qWait(60);
	lifetime.acquire();
	lifetime.release();
	QTest::qWait(60);
	QCOMPARE(expired.count(), 0);
	QTRY_COMPARE(expired.count(), 1);
	QVERIFY(!lifetime.armed());

	lifetime.release();
	QCOMPARE(lifetime.users(), 0);
	QVERIFY(lifetime.armed());
	lifetime.acquire();
	QTest::qWait(150);
	QCOMPARE(expired.count(), 1);
	lifetime.release();
	QTRY_COMPARE(expired.count(), 2);
}

void TestPanelAnimation::effectLifecycle() {
	using qs::windows::panelEffectFrame;

	auto scene = QmlScene();
	QVERIFY(scene.load(EFFECT_WINDOW_QML));
	auto* window = qobject_cast<QQuickWindow*>(scene.root.get());
	QVERIFY(window != nullptr);
	QVERIFY(QTest::qWaitForWindowExposed(window));

	auto* root = window->contentItem();
	auto* content = window->property("content").value<QQuickItem*>();
	auto rootChildren = root->childItems();
	auto guard = QObject();

	auto effects = PanelEffects(PanelEffects::defaultShader(), 100);
	auto unloaded = QSignalSpy(&effects, &PanelEffects::unloaded);
	QVERIFY(!effects.loaded());
	QVERIFY(!effects.unloadPending());
	QCOMPARE(effects.liveLayers(), 0);
	QCOMPARE(effects.hostWindows(), qsizetype(0));

	QPointer<QQuickItem> watched;

	{
		auto layer = effects.createLayer(&scene.engine, &guard, root, content, true);
		QVERIFY(layer != nullptr);
		QVERIFY(effects.loaded());
		QCOMPARE(effects.liveLayers(), 1);
		QCOMPARE(effects.hostWindows(), qsizetype(1));
		QVERIFY(!effects.unloadPending());

		auto* item = layer->item();
		watched = item;
		QVERIFY(item != nullptr);
		QCOMPARE(item->parentItem(), root);
		QCOMPARE(item->parent(), static_cast<QObject*>(root));
		QCOMPARE(item->size(), QSizeF(window->size()));
		QCOMPARE(item->property("sourceItem").value<QQuickItem*>(), content);
		QCOMPARE(item->property("mipmapped").toBool(), true);
		QCOMPARE(item->property("fragmentShader").toUrl(), PanelEffects::defaultShader());
		QVERIFY(item->property("status").toInt() != 2);
		QVERIFY(!item->acceptedMouseButtons());
		QVERIFY(!item->hasActiveFocus());

		auto size = QSizeF(window->size());
		auto reveal = panelEffectFrame(PanelEffect::Reveal, Qt::TopEdge, size, 0.5, QPointF());
		layer->apply(reveal, size, 1);
		QCOMPARE(item->property("revealRect").toRectF(), reveal.revealRect);
		QCOMPARE(item->property("revealRadius").toReal(), reveal.revealRadius);
		QCOMPARE(item->property("dissolve").toReal(), 1.0);
		QCOMPARE(item->property("blurLod").toReal(), 0.0);

		auto blur = panelEffectFrame(PanelEffect::Blur, Qt::TopEdge, size, 0, QPointF());
		layer->apply(blur, size, 1);
		QVERIFY(item->property("blurLod").toReal() > 0);
		QVERIFY(item->property("blurStep").value<QVector2D>().x() > 0);
		QVERIFY(item->property("revealRadius").toReal() < 0);

		auto second = effects.createLayer(&scene.engine, &guard, root, content, false);
		QVERIFY(second != nullptr);
		QCOMPARE(effects.liveLayers(), 2);
		QCOMPARE(effects.hostWindows(), qsizetype(1));
		QCOMPARE(second->item()->property("mipmapped").toBool(), false);
		second.reset();
		QCOMPARE(effects.liveLayers(), 1);
		QVERIFY(!effects.unloadPending());
	}

	QVERIFY(watched == nullptr);
	QCOMPARE(root->childItems(), rootChildren);
	QCOMPARE(effects.liveLayers(), 0);
	QVERIFY(effects.unloadPending());
	QVERIFY(effects.loaded());

	QTRY_COMPARE(unloaded.count(), 1);
	QVERIFY(!effects.loaded());
	QVERIFY(!effects.unloadPending());
	QCOMPARE(effects.hostWindows(), qsizetype(0));

	auto again = effects.createLayer(&scene.engine, &guard, root, content, false);
	QVERIFY(again != nullptr);
	QVERIFY(effects.loaded());
	effects.unload();
	QVERIFY(effects.loaded());
	QCOMPARE(effects.hostWindows(), qsizetype(1));
	again.reset();
	effects.unload();
	QVERIFY(!effects.loaded());
	QCOMPARE(effects.hostWindows(), qsizetype(0));
	QCOMPARE(unloaded.count(), 2);
	QVERIFY(!effects.failed());

	effects.unload();
	QCOMPARE(unloaded.count(), 2);

	auto other = QmlScene();
	QVERIFY(other.load(EFFECT_WINDOW_QML));
	auto* otherWindow = qobject_cast<QQuickWindow*>(other.root.get());
	auto* otherContent = otherWindow->property("content").value<QQuickItem*>();
	auto first = effects.createLayer(&scene.engine, &guard, root, content, false);
	auto elsewhere =
	    effects.createLayer(&other.engine, &guard, otherWindow->contentItem(), otherContent, false);
	QVERIFY(first != nullptr && elsewhere != nullptr);
	QCOMPARE(effects.hostWindows(), qsizetype(2));
	first.reset();
	elsewhere.reset();
	other.root.reset();
	QCOMPARE(effects.hostWindows(), qsizetype(1));
	effects.unload();
	QCOMPARE(effects.hostWindows(), qsizetype(0));
	QCOMPARE(unloaded.count(), 3);
}

void TestPanelAnimation::effectEngineGone() {
	auto scene = QmlScene();
	QVERIFY(scene.load(EFFECT_WINDOW_QML));
	auto* window = qobject_cast<QQuickWindow*>(scene.root.get());
	QVERIFY(window != nullptr);
	auto* root = window->contentItem();
	auto* content = window->property("content").value<QQuickItem*>();

	auto effects = PanelEffects(PanelEffects::defaultShader(), 60000);
	auto guard = std::make_unique<QObject>();

	auto layer = effects.createLayer(&scene.engine, guard.get(), root, content, false);
	QVERIFY(layer != nullptr);
	layer.reset();
	QVERIFY(effects.loaded());

	guard.reset();
	QVERIFY(!effects.loaded());
	QVERIFY(!effects.failed());
}

void TestPanelAnimation::missingShader() {
	auto scene = QmlScene();
	QVERIFY(scene.load(EFFECT_WINDOW_QML));
	auto* window = qobject_cast<QQuickWindow*>(scene.root.get());
	QVERIFY(window != nullptr);
	auto* root = window->contentItem();
	auto* content = window->property("content").value<QQuickItem*>();

	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	auto path = dir.filePath("broken.frag.qsb");
	{
		auto file = QFile(path);
		QVERIFY(file.open(QFile::WriteOnly));
		file.write("not a shader");
	}

	auto broken = PanelEffects(QUrl::fromLocalFile(path), 100);
	QTest::ignoreMessage(QtWarningMsg, "qUncompress: Input data is corrupted");
	QTest::ignoreMessage(QtWarningMsg, "Attempted to deserialize QShader with unknown version 0.");
	QTest::ignoreMessage(
	    QtWarningMsg,
	    QRegularExpression("Expressive panel effects are unavailable.*not a valid shader")
	);
	QVERIFY(broken.createLayer(&scene.engine, nullptr, root, content, false) == nullptr);
	QVERIFY(broken.failed());

	{
		auto effects = PanelEffects(QUrl("qrc:/Quickshell/Windows/shaders/missing.frag.qsb"), 100);
		QTest::ignoreMessage(
		    QtWarningMsg,
		    QRegularExpression("Expressive panel effects are unavailable.*missing\\.frag\\.qsb")
		);
		QVERIFY(effects.createLayer(&scene.engine, nullptr, root, content, false) == nullptr);
		QVERIFY(effects.failed());
		QVERIFY(!effects.loaded());

		QTest::failOnWarning(QRegularExpression("Expressive panel effects"));
		QVERIFY(effects.createLayer(&scene.engine, nullptr, root, content, false) == nullptr);
		QCOMPARE(effects.liveLayers(), 0);
		QCOMPARE(root->childItems().size(), qsizetype(1));
	}
}

void TestPanelAnimation::shaderReflection() {
	auto file = QFile(":/Quickshell/Windows/shaders/panel_effect.frag.qsb");
	QVERIFY(file.open(QFile::ReadOnly));

	auto shader = QShader::fromSerialized(file.readAll());
	QVERIFY(shader.isValid());
	QCOMPARE(shader.stage(), QShader::FragmentStage);
	QVERIFY(!shader.shader(QShaderKey(QShader::HlslShader, QShaderVersion(50))).shader().isEmpty());
	QVERIFY(!shader.shader(QShaderKey(QShader::SpirvShader, QShaderVersion(100))).shader().isEmpty());
	QVERIFY(!shader.shader(QShaderKey(QShader::GlslShader, QShaderVersion(150))).shader().isEmpty());

	auto description = shader.description();
	auto blocks = description.uniformBlocks();
	QCOMPARE(blocks.size(), qsizetype(1));

	auto members = std::set<QByteArray>();
	for (const auto& member: blocks.first().members) members.insert(member.name);

	const auto expected = std::set<QByteArray> {
	    "qt_Matrix",
	    "qt_Opacity",
	    "itemSize",
	    "blurStep",
	    "blurLod",
	    "dissolve",
	    "noiseScale",
	    "revealRect",
	    "revealRadius",
	};
	QVERIFY(members == expected);

	auto samplers = description.combinedImageSamplers();
	QCOMPARE(samplers.size(), qsizetype(1));
	QCOMPARE(samplers.first().name, QByteArray("source"));

	auto scene = QmlScene();
	QVERIFY(scene.load(EFFECT_WINDOW_QML));
	auto* window = qobject_cast<QQuickWindow*>(scene.root.get());
	auto* content = window->property("content").value<QQuickItem*>();
	auto effects = PanelEffects(PanelEffects::defaultShader(), 60000);
	auto layer = effects.createLayer(&scene.engine, nullptr, window->contentItem(), content, false);
	QVERIFY(layer != nullptr);

	for (const auto& member: expected) {
		if (member.startsWith("qt_")) continue;
		auto index = layer->item()->metaObject()->indexOfProperty(member.constData());
		QVERIFY2(index >= 0, member.constData());
	}

	QVERIFY(layer->item()->metaObject()->indexOfProperty("source") >= 0);
}

void TestPanelAnimation::shaderUpToDate() {
	auto qsb = qEnvironmentVariable("IIW_QSB");
	if (qsb.isEmpty()) QSKIP("IIW_QSB is not set to the host qsb tool");

	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	auto output = dir.filePath("panel_effect.frag.qsb");

	auto process = QProcess();
	process.start(qsb, {"--qt6", "-o", output, QS_WINDOWS_SHADER_SOURCE});
	QVERIFY(process.waitForFinished(60000));
	QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

	auto rebuilt = QFile(output);
	auto committed = QFile(QS_WINDOWS_SHADER_QSB);
	QVERIFY(rebuilt.open(QFile::ReadOnly));
	QVERIFY(committed.open(QFile::ReadOnly));
	QVERIFY2(
	    rebuilt.readAll() == committed.readAll(),
	    "shaders/panel_effect.frag.qsb is stale, rebuild it"
	);
}

void TestPanelAnimation::effectRendering() {
	auto scene = QmlScene();
	QVERIFY(scene.load(EFFECT_WINDOW_QML));
	auto* window = qobject_cast<QQuickWindow*>(scene.root.get());
	QVERIFY(window != nullptr);
	QVERIFY(QTest::qWaitForWindowExposed(window));

	auto* renderer = window->rendererInterface();
	if (renderer == nullptr || !QSGRendererInterface::isApiRhiBased(renderer->graphicsApi())) {
		QSKIP("needs an RHI scene graph (QT_QUICK_BACKEND=rhi)");
	}

	auto* root = window->contentItem();
	auto* content = window->property("content").value<QQuickItem*>();
	auto size = QSizeF(window->size());
	auto baseline = window->grabWindow();
	QVERIFY(alphaSum(baseline) > 0);

	auto effects = PanelEffects(PanelEffects::defaultShader(), 60000);
	auto layer = effects.createLayer(&scene.engine, nullptr, root, content, true);
	QVERIFY(layer != nullptr);
	QVERIFY(layer->item()->property("status").toInt() != 2);

	layer->apply(PanelEffectFrame(), size, 1);
	auto identity = window->grabWindow();
	QVERIFY2(
	    maxChannelDifference(identity, baseline) <= 2,
	    qPrintable(QString("identity differs by %1").arg(maxChannelDifference(identity, baseline)))
	);

	auto dissolve = PanelEffectFrame();
	dissolve.dissolve = 0;
	layer->apply(dissolve, size, 1);
	QCOMPARE(alphaSum(window->grabWindow()), 0.0);

	dissolve.dissolve = 0.5;
	layer->apply(dissolve, size, 1);
	auto ratio = alphaSum(window->grabWindow()) / alphaSum(baseline);
	QVERIFY2(ratio > 0.1 && ratio < 0.9, qPrintable(QString("half dissolved keeps %1").arg(ratio)));

	using qs::windows::panelEffectFrame;
	layer->apply(panelEffectFrame(PanelEffect::Reveal, Qt::TopEdge, size, 0, QPointF()), size, 1);
	QCOMPARE(alphaSum(window->grabWindow()), 0.0);

	layer->apply(panelEffectFrame(PanelEffect::Reveal, Qt::TopEdge, size, 0.45, QPointF()), size, 1);
	auto half = window->grabWindow();
	QVERIFY(alphaSum(half, QRect(0, 0, 64, 12)) > 0);
	QCOMPARE(alphaSum(half, QRect(0, 36, 64, 12)), 0.0);

	layer->apply(panelEffectFrame(PanelEffect::Reveal, Qt::TopEdge, size, 1, QPointF()), size, 1);
	QVERIFY(maxChannelDifference(window->grabWindow(), baseline) <= 2);

	layer->apply(panelEffectFrame(PanelEffect::Blur, Qt::TopEdge, size, 0, QPointF()), size, 1);
	auto blurred = window->grabWindow();
	QVERIFY(maxChannelDifference(blurred, baseline) > 20);
	auto kept = alphaSum(blurred) / alphaSum(baseline);
	QVERIFY2(kept > 0.85 && kept < 1.15, qPrintable(QString("blur keeps %1 of the alpha").arg(kept)));

	layer.reset();
	QVERIFY(maxChannelDifference(window->grabWindow(), baseline) <= 2);

	QCOMPARE(effects.hostWindows(), qsizetype(1));
	auto invalidated = QSignalSpy(window, &QQuickWindow::sceneGraphInvalidated);
	effects.unload();
	QCOMPARE(effects.hostWindows(), qsizetype(0));
	QVERIFY(!effects.loaded());
	QVERIFY(maxChannelDifference(window->grabWindow(), baseline) <= 2);
	QCOMPARE(invalidated.count(), 0);

	layer = effects.createLayer(&scene.engine, nullptr, root, content, false);
	QVERIFY(layer != nullptr);
	layer->apply(panelEffectFrame(PanelEffect::Reveal, Qt::TopEdge, size, 0, QPointF()), size, 1);
	QCOMPARE(alphaSum(window->grabWindow()), 0.0);
	layer->apply(PanelEffectFrame(), size, 1);
	QVERIFY(maxChannelDifference(window->grabWindow(), baseline) <= 2);
	layer.reset();
}

QTEST_MAIN(TestPanelAnimation)

#include "panel_animation_test.moc"
