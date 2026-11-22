#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <qt_windows.h>

#include <qbytearray.h>
#include <qevent.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpointer.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qquickitem.h>
#include <qquickwindow.h>
#include <qrect.h>
#include <qregion.h>
#include <qscreen.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>
#include <qvariantanimation.h>

#include "../core/doc.hpp"
#include "../core/util.hpp"
#include "../window/panelinterface.hpp"
#include "../window/proxywindow.hpp"
#include "appbar.hpp"
#include "panel_animation.hpp"
#include "panel_effects.hpp"

namespace qs::wayland::layershell {
class WlrLayershell;
}

namespace qs::windows {

enum class PanelLayer : quint8 {
	Background = 0,
	Bottom = 1,
	Top = 2,
	Overlay = 3,
};

enum class PanelKeyboardFocus : quint8 {
	None = 0,
	Exclusive = 1,
	OnDemand = 2,
};

class WinPanelWindow;
class WinPanelStack;
class PanelBlur;

class WinProxiedWindow: public ProxiedWindow {
	Q_OBJECT;

public:
	using ProxiedWindow::ProxiedWindow;

	void setPanel(WinPanelWindow* panel);

signals:
	void surfaceCreated();

protected:
	bool event(QEvent* event) override;
	bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
	QPointer<WinPanelWindow> mPanel;
};

class WinPanelWindow: public ProxyWindowBase {
	QSDOC_BASECLASS(PanelWindowInterface);
	Q_OBJECT;
	// clang-format off
	QSDOC_HIDE Q_PROPERTY(Anchors anchors READ anchors WRITE setAnchors NOTIFY anchorsChanged);
	QSDOC_HIDE Q_PROPERTY(qint32 exclusiveZone READ exclusiveZone WRITE setExclusiveZone NOTIFY exclusiveZoneChanged);
	QSDOC_HIDE Q_PROPERTY(ExclusionMode::Enum exclusionMode READ exclusionMode WRITE setExclusionMode NOTIFY exclusionModeChanged);
	QSDOC_HIDE Q_PROPERTY(Margins margins READ margins WRITE setMargins NOTIFY marginsChanged);
	QSDOC_HIDE Q_PROPERTY(bool aboveWindows READ aboveWindows WRITE setAboveWindows NOTIFY layerChanged);
	QSDOC_HIDE Q_PROPERTY(bool focusable READ focusable WRITE setFocusable NOTIFY keyboardFocusChanged);
	// clang-format on
	QML_ELEMENT;

public:
	explicit WinPanelWindow(QObject* parent = nullptr);
	~WinPanelWindow() override;
	Q_DISABLE_COPY_MOVE(WinPanelWindow);

	void boundInputMask();

	ProxiedWindow* retrieveWindow(QObject* oldInstance) override;
	ProxiedWindow* createQQuickWindow() override;
	void connectWindow() override;
	ProxiedWindow* disownWindow(bool keepItemOwnership = false) override;

	void trySetWidth(qint32 implicitWidth) override;
	void trySetHeight(qint32 implicitHeight) override;

	void onReload(QObject* oldInstance) override;

	void setScreen(QuickshellScreenInfo* screen) override;
	[[nodiscard]] bool isVisibleDirect() const override;
	void setVisibleDirect(bool visible) override;

	[[nodiscard]] Anchors anchors() const { return this->bAnchors; }
	void setAnchors(Anchors anchors) { this->bAnchors = anchors; }

	[[nodiscard]] qint32 exclusiveZone() const { return this->bExclusiveZone; }
	void setExclusiveZone(qint32 exclusiveZone) {
		Qt::beginPropertyUpdateGroup();
		this->bExclusiveZone = exclusiveZone;
		this->bExclusionMode = ExclusionMode::Normal;
		Qt::endPropertyUpdateGroup();
	}

	[[nodiscard]] ExclusionMode::Enum exclusionMode() const { return this->bExclusionMode; }
	void setExclusionMode(ExclusionMode::Enum exclusionMode) { this->bExclusionMode = exclusionMode; }

	[[nodiscard]] Margins margins() const { return this->bMargins; }
	void setMargins(Margins margins) { this->bMargins = margins; }

	[[nodiscard]] bool aboveWindows() const;
	void setAboveWindows(bool aboveWindows);

	[[nodiscard]] bool focusable() const;
	void setFocusable(bool focusable);

	[[nodiscard]] PanelLayer layer() const { return this->bLayer; }
	void setLayer(PanelLayer layer) { this->bLayer = layer; }

	[[nodiscard]] QString ns() const { return this->bNamespace; }
	void setNamespace(const QString& ns) { this->bNamespace = ns; }

	[[nodiscard]] PanelKeyboardFocus keyboardFocus() const { return this->bKeyboardFocus; }
	void setKeyboardFocus(PanelKeyboardFocus focus) { this->bKeyboardFocus = focus; }

	[[nodiscard]] QObject* layershellAttached() const { return this->mLayershellAttached; }
	void setLayershellAttached(QObject* attached) { this->mLayershellAttached = attached; }

	bool handleNativeMessage(MSG* msg, qintptr* result);

	[[nodiscard]] bool isEmbedded() const { return this->mEmbedParent != nullptr; }

signals:
	void layerChanged();
	void namespaceChanged();
	void keyboardFocusChanged();
	QSDOC_HIDE void anchorsChanged();
	QSDOC_HIDE void exclusiveZoneChanged();
	QSDOC_HIDE void exclusionModeChanged();
	QSDOC_HIDE void marginsChanged();

protected:
	void applyInputMask(const QRegion& region, bool hasMask) override;
	void onPolished() override;

private slots:
	void onSurfaceCreated();
	void onWindowVisibleChanged();
	void onDesktopAccessorReady();
	void updateScreen();
	void followFocusedScreen();
	void scheduleUpdateDimensions();
	void onScreenScaleChanged();
	void updateDimensions();
	void onWindowExposed();
	void onAnimationTick(const QVariant& value);
	void onAnimationFinished();

private:
	[[nodiscard]] HWND hwnd() const;
	void nativeInit();
	void releaseNativeState();
	void applyNativeStyles();
	void updateLayer();
	void updateLayerCb() { this->updateLayer(); }
	void updateFocus();
	void updateFocusCb() { this->updateFocus(); }
	void updateDimensionsCb() { this->updateDimensions(); }
	void updateFocusFlag();
	void applyFocusFlag();
	void focusWhenExposed();
	void takeFocusOnExpose();
	void grabKeyboardFocus();
	void scheduleFocusGrab();
	void stickToAllDesktops();
	[[nodiscard]] bool reservesSpace() const;
	[[nodiscard]] bool wantsEmbedding() const;
	[[nodiscard]] bool wantsAboveIcons() const;
	void updateEmbedding();
	void embedInto(HWND parent, HWND insertAfter, bool aboveIcons);
	void unembed();
	void placeEmbedded();
	void keepAboveIcons();
	void routeInputMask();
	void applyBoundedMask(bool hasMask);
	void applyInputRegion();
	void setButtonHeld(bool held);
	[[nodiscard]] QRect embeddedRect() const;
	void onWindowScreenChanged();
	void recreateDestroyedWindow();
	[[nodiscard]] bool animationsAllowed() const;
	[[nodiscard]] std::optional<PanelAnimationChoice> animationChoice() const;
	[[nodiscard]] Qt::Edge slideEdge(const PanelAnimationStyle& style) const;
	[[nodiscard]] PanelAnimationTiming animationTiming() const;
	[[nodiscard]] std::optional<qreal> animationTravel() const;
	void prepareShowAnimation(const PanelAnimationChoice& choice);
	void prepareEffectLayer();
	void runAnimation(bool show);
	void startAnimationClock();
	void runAnimationClock();
	void beginCascade();
	void applyAnimationFrame();
	void applyCascadeFrame();
	void setAnimationTransformsBypassed(bool bypassed);
	void resetCascade();
	void resetAnimatedItem();
	void resetAnimation();
	void beginClose(const PanelAnimationChoice& choice);
	void reopen();
	void waitForFrames(int frames, int timeoutMs, void (WinPanelWindow::*done)());
	void cancelFrameWait();
	void hideAfterLastFrames();
	void finishClose();
	void releaseForegroundForClose();

	QPointer<QScreen> mTrackedScreen;
	WinAppBar appBar;
	PanelBlur* blur = nullptr;
	QObject* mLayershellAttached = nullptr;
	bool dimensionsUpdatePending = false;
	bool scaleRecreatePending = false;
	bool focusGrabPending = false;
	bool focusFlagPending = false;
	bool focusOnExpose = false;
	quint32 focusRequest = 0;
	bool maskRouted = false;
	bool appBarDeferred = false;
	bool pinnedToAllDesktops = false;
	HWND pinFailedHwnd = nullptr;
	HWND stuckHwnd = nullptr;
	qsizetype stuckDesktop = -1;

	HWND mEmbedParent = nullptr;
	HWND mEmbedInsertAfter = nullptr;
	HWND embedRefusedBy = nullptr;
	bool mEmbedAboveIcons = false;
	QRegion mInputMask;
	bool mHasInputMask = false;
	QRegion mRequestedMask;
	QRegion mAppliedRegion;
	bool mRegionApplied = false;
	bool mButtonHeld = false;
	int restackCount = 0;
	DWORD restackWindowStart = 0;
	QRect mEmbedRect;
	bool placingEmbedded = false;
	bool screenRestorePending = false;
	HWND destroyedHwnd = nullptr;
	bool visibleWhenDestroyed = false;

	QVariantAnimation* mAnimation = nullptr;
	std::unique_ptr<PanelContentTransform> mContentTransform;
	QPointer<QQuickItem> mAnimatedItem;
	PanelAnimation mAnimationState;
	PanelAnimationStyle mAnimationStyle;
	PanelEffect mAnimationEffect = PanelEffect::Standard;
	Qt::Edge mAnimationEdge = Qt::TopEdge;
	std::unique_ptr<PanelEffectLayer> mEffectLayer;

	struct CascadeSection {
		QPointer<QQuickItem> item;
		std::unique_ptr<PanelContentTransform> transform;
		qreal appliedOpacity = -1;
	};

	std::vector<CascadeSection> mCascade;
	SpringCurve mCascadeSpring;
	qreal mCascadeDistance = 0;
	bool mCascadePending = false;
	QMetaObject::Connection mFrameWait;
	quint32 mFrameWaitRequest = 0;
	int mFramesLeft = 0;
	bool mClosing = false;
	bool mInstantVisibility = false;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(WinPanelWindow, PanelLayer, bLayer, PanelLayer::Top, &WinPanelWindow::layerChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(WinPanelWindow, QString, bNamespace, "quickshell", &WinPanelWindow::namespaceChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WinPanelWindow, PanelKeyboardFocus, bKeyboardFocus, &WinPanelWindow::keyboardFocusChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WinPanelWindow, Anchors, bAnchors, &WinPanelWindow::anchorsChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WinPanelWindow, Margins, bMargins, &WinPanelWindow::marginsChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WinPanelWindow, qint32, bExclusiveZone, &WinPanelWindow::exclusiveZoneChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(WinPanelWindow, ExclusionMode::Enum, bExclusionMode, ExclusionMode::Auto, &WinPanelWindow::exclusionModeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WinPanelWindow, qint32, bcExclusiveZone);
	Q_OBJECT_BINDABLE_PROPERTY(WinPanelWindow, Qt::Edge, bcExclusionEdge);

	QS_BINDING_SUBSCRIBE_METHOD(WinPanelWindow, bLayer, updateLayerCb, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(WinPanelWindow, bNamespace, updateLayerCb, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(WinPanelWindow, bKeyboardFocus, updateFocusCb, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(WinPanelWindow, bAnchors, updateDimensionsCb, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(WinPanelWindow, bMargins, updateDimensionsCb, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(WinPanelWindow, bExclusionMode, updateDimensionsCb, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(WinPanelWindow, bcExclusiveZone, updateDimensionsCb, onValueChanged);
	// clang-format on

	friend class WinPanelStack;
	friend class PanelBlur;
};

class WinPanelInterface: public PanelWindowInterface {
	Q_OBJECT;

public:
	explicit WinPanelInterface(QObject* parent = nullptr);

	void onReload(QObject* oldInstance) override;

	[[nodiscard]] ProxyWindowBase* proxyWindow() const override;

	// NOLINTBEGIN
	[[nodiscard]] Anchors anchors() const override;
	void setAnchors(Anchors anchors) override;

	[[nodiscard]] Margins margins() const override;
	void setMargins(Margins margins) override;

	[[nodiscard]] qint32 exclusiveZone() const override;
	void setExclusiveZone(qint32 exclusiveZone) override;

	[[nodiscard]] ExclusionMode::Enum exclusionMode() const override;
	void setExclusionMode(ExclusionMode::Enum exclusionMode) override;

	[[nodiscard]] bool aboveWindows() const override;
	void setAboveWindows(bool aboveWindows) override;

	[[nodiscard]] bool focusable() const override;
	void setFocusable(bool focusable) override;
	// NOLINTEND

private:
	WinPanelWindow* panel;

	friend class qs::wayland::layershell::WlrLayershell;
};

} // namespace qs::windows
