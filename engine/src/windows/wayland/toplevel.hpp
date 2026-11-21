#pragma once

#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qrect.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>

#include "../../core/doc.hpp"
#include "../../core/model.hpp"
#include "../../core/qmlscreen.hpp"
#include "../../core/util.hpp"
#include "../window_tracker.hpp"

namespace qs::wayland::toplevel {

class ToplevelManager;

class Toplevel: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString appId READ appId NOTIFY appIdChanged);
	Q_PROPERTY(QString title READ title NOTIFY titleChanged);
	Q_PROPERTY(qs::wayland::toplevel::Toplevel* parent READ parent NOTIFY parentChanged);
	Q_PROPERTY(bool activated READ activated NOTIFY activatedChanged);
	Q_PROPERTY(QList<QuickshellScreenInfo*> screens READ screens NOTIFY screensChanged);
	Q_PROPERTY(bool maximized READ maximized WRITE setMaximized NOTIFY maximizedChanged);
	Q_PROPERTY(bool minimized READ minimized WRITE setMinimized NOTIFY minimizedChanged);
	Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen NOTIFY fullscreenChanged);
	QML_ELEMENT;
	QML_UNCREATABLE("Toplevels must be acquired from the ToplevelManager.");

public:
	explicit Toplevel(qs::windows::TrackedWindow* window, QObject* parent);

	Q_INVOKABLE void activate();

	Q_INVOKABLE void close();

	Q_INVOKABLE void fullscreenOn(QuickshellScreenInfo* screen);

	Q_INVOKABLE void setRectangle(QObject* window, QRect rect);
	Q_INVOKABLE void unsetRectangle();

	[[nodiscard]] QString appId() const;
	[[nodiscard]] QString title() const;
	[[nodiscard]] Toplevel* parent() const;
	[[nodiscard]] bool activated() const;
	[[nodiscard]] QList<QuickshellScreenInfo*> screens() const;

	[[nodiscard]] bool maximized() const;
	void setMaximized(bool maximized);

	[[nodiscard]] bool minimized() const;
	void setMinimized(bool minimized);

	[[nodiscard]] bool fullscreen() const;
	void setFullscreen(bool fullscreen);

	[[nodiscard]] qs::windows::TrackedWindow* window() const { return this->mWindow; }

signals:
	void closed();
	void appIdChanged();
	void titleChanged();
	void parentChanged();
	void activatedChanged();
	void screensChanged();
	void maximizedChanged();
	void minimizedChanged();
	void fullscreenChanged();

private:
	qs::windows::TrackedWindow* mWindow;

	friend class ToplevelManager;
};

class ToplevelManager: public QObject {
	Q_OBJECT;

public:
	static ToplevelManager* instance();

	[[nodiscard]] Toplevel* forWindow(qs::windows::TrackedWindow* window) const;
	[[nodiscard]] Toplevel* forAddress(quint64 address) const;

	[[nodiscard]] ObjectModel<Toplevel>* toplevels();

signals:
	void activeToplevelChanged();

private slots:
	void onWindowAdded(qs::windows::TrackedWindow* window);
	void onWindowRemoved(qs::windows::TrackedWindow* window);
	void onActiveWindowChanged();

private:
	explicit ToplevelManager();

	ObjectModel<Toplevel> mToplevels {this};
	QHash<qs::windows::TrackedWindow*, Toplevel*> byWindow;
	Toplevel* mActiveToplevel = nullptr;

	DECLARE_PRIVATE_MEMBER(
	    ToplevelManager,
	    activeToplevel,
	    setActiveToplevel,
	    mActiveToplevel,
	    activeToplevelChanged
	);
};

class ToplevelManagerQml: public QObject {
	Q_OBJECT;
	// clang-format off
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::wayland::toplevel::Toplevel>*);
	Q_PROPERTY(UntypedObjectModel* toplevels READ toplevels CONSTANT);
	Q_PROPERTY(qs::wayland::toplevel::Toplevel* activeToplevel READ activeToplevel NOTIFY activeToplevelChanged);
	// clang-format on
	QML_NAMED_ELEMENT(ToplevelManager);
	QML_SINGLETON;

public:
	explicit ToplevelManagerQml(QObject* parent = nullptr);

	[[nodiscard]] static ObjectModel<Toplevel>* toplevels();
	[[nodiscard]] static Toplevel* activeToplevel();

signals:
	void activeToplevelChanged();
};

} // namespace qs::wayland::toplevel
