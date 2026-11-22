#pragma once

#include <memory>

#include <qt_windows.h>

#include <qbytearray.h>
#include <qfilesystemwatcher.h>
#include <qlist.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlengine.h>
#include <qqmlintegration.h>
#include <qquickwindow.h>
#include <qrect.h>
#include <qregion.h>
#include <qsize.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

#include "blur_shapes.hpp"
#include "layer_rules.hpp"

class QWinEventNotifier;

namespace qs::windows {

class WinPanelWindow;

class BackdropWindow;
class PanelBlur;

class BlurManager: public QObject {
	Q_OBJECT;

public:
	~BlurManager() override;
	Q_DISABLE_COPY_MOVE(BlurManager);

	static BlurManager* instance();

	void setShellDir(const QString& shellDir);
	void reload();

	[[nodiscard]] LayerRule ruleFor(const QString& ns) const;

	[[nodiscard]] bool enabled() const { return this->mEnabled; }
	void setEnabled(bool enabled);

	[[nodiscard]] bool available() const;

	[[nodiscard]] bool panelAnimations() const { return this->mPanelAnimations; }
	void setPanelAnimations(bool enabled);

	[[nodiscard]] bool expressiveAnimations() const { return this->mExpressiveAnimations; }
	void setExpressiveAnimations(bool expressive);

	[[nodiscard]] QString configPath() const { return this->mConfigPath; }
	[[nodiscard]] QVariantList rulesInfo() const;

	enum class Backend : quint8 {
		HostBackdrop,
		Accent,
	};

	[[nodiscard]] Backend backend() const { return this->mBackend; }

	[[nodiscard]] bool layeredBackdrops() const;
	[[nodiscard]] bool debugTint() const { return this->mDebugTint; }

	void scheduleSystemCheck(bool recreate = false);

	[[nodiscard]] bool shutDown() const { return this->mShutDown; }

	struct Composition;
	[[nodiscard]] Composition* ensureComposition();
	[[nodiscard]] bool deferComposition();
	void markUnsupported(const QString& reason);

	void registerPanel(PanelBlur* panel);
	void unregisterPanel(PanelBlur* panel);

signals:
	void rulesChanged();
	void stateChanged();
	void panelAnimationsChanged();

private:
	explicit BlurManager(QObject* parent);

	void detectBackend();
	void loadFile();
	void parse(const QByteArray& data, const QString& path);
	void updateWatches();
	void scheduleReload();
	void checkSystem();
	void watchRegistry();
	void armRegistryNotification();
	void notifyPanels();
	void scheduleNotifyPanels();
	void shutdown();
	void closeCompositor();

	[[nodiscard]] QString userFilePath() const;
	[[nodiscard]] QString defaultFilePath() const;
	[[nodiscard]] QString readUserFile() const;

	QString shellDir;
	QString userFile;
	QString mConfigPath;
	QByteArray loadedData;
	bool loaded = false;
	bool parsed = false;
	LayerRules rules;
	bool envEnabled = true;
	bool envLayered = false;
	bool mDebugTint = false;
	bool mEnabled = true;
	bool mPanelAnimations = true;
	bool mExpressiveAnimations = false;
	Backend mBackend = Backend::HostBackdrop;

	bool transparencyEffects = true;
	bool highContrast = false;
	bool energySaver = false;
	bool unsupported = false;
	bool systemCheckPending = false;
	bool recreatePending = false;
	bool notifyPending = false;
	bool mShutDown = false;
	bool compositionWaiting = false;

	QFileSystemWatcher watcher;
	QTimer reloadTimer;

	HKEY personalizeKey = nullptr;
	HANDLE registryEvent = nullptr;
	QWinEventNotifier* registryNotifier = nullptr;

	std::unique_ptr<Composition> composition;

	QList<PanelBlur*> panels;
};

class PanelBlur: public QObject {
	Q_OBJECT;

public:
	explicit PanelBlur(WinPanelWindow* panel);
	~PanelBlur() override;
	Q_DISABLE_COPY_MOVE(PanelBlur);

	void adopt(PanelBlur* other);
	void attach();
	void release();

	void setInputMask(const QRegion& region, bool hasMask);

	void syncPlacement();
	void recreate();
	void setSuspended(bool suspended);

	void updateActive();

private slots:
	void onFrame();
	void updateShapes();

private:
	struct ShapeInputs {
		bool shown = false;
		QSize size;
		qreal alpha = -1;
		qreal dpr = 0;

		[[nodiscard]] bool operator==(const ShapeInputs& other) const = default;
	};

	void connectFrames();
	void disconnectFrames();
	void scheduleShapes();
	[[nodiscard]] ShapeInputs shapeInputs(bool shown) const;
	void collectShapes(QList<BlurShape>& shapes);
	bool ensureBackdrop();
	void destroyBackdrop();
	[[nodiscard]] bool panelShown() const;

	WinPanelWindow* panel;
	QPointer<QQuickWindow> mWindow;
	QMetaObject::Connection frameConnection;
	QPointer<QQuickWindow> framesWindow;
	std::unique_ptr<BackdropWindow> backdrop;
	LayerRule rule;
	bool active = false;
	bool suspended = false;
	bool shapesPending = false;
	bool shapesForced = true;
	ShapeInputs lastInputs;
	bool stale = true;
	bool panelWasShown = false;
	QRegion mask;
	bool hasMask = false;
	QList<BlurShape> shapes;
	bool truncatedWarned = false;
};

class BackdropBlur: public QObject {
	Q_OBJECT;
	Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY stateChanged);
	Q_PROPERTY(bool available READ available NOTIFY stateChanged);
	Q_PROPERTY(
	    bool panelAnimations READ panelAnimations WRITE setPanelAnimations
	        NOTIFY panelAnimationsChanged
	);
	Q_PROPERTY(
	    QString panelAnimationStyle READ panelAnimationStyle WRITE setPanelAnimationStyle
	        NOTIFY panelAnimationsChanged
	);
	Q_PROPERTY(QString configPath READ configPath NOTIFY rulesChanged);
	Q_PROPERTY(QVariantList rules READ rules NOTIFY rulesChanged);
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit BackdropBlur(QObject* parent);

	static BackdropBlur* create(QQmlEngine* engine, QJSEngine* jsEngine);

	[[nodiscard]] static bool enabled();
	static void setEnabled(bool enabled);
	[[nodiscard]] static bool available();
	[[nodiscard]] static bool panelAnimations();
	static void setPanelAnimations(bool enabled);
	[[nodiscard]] static QString panelAnimationStyle();
	static void setPanelAnimationStyle(const QString& style);
	[[nodiscard]] static QString configPath();
	[[nodiscard]] static QVariantList rules();

	Q_INVOKABLE static void reload();

signals:
	void stateChanged();
	void rulesChanged();
	void panelAnimationsChanged();
};

} // namespace qs::windows
