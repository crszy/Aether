#pragma once

#include <atomic>

#include <qt_windows.h>

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>
#include <qtmetamacros.h>

struct IDataObject;
struct IDropTarget;

namespace qs::windows {

class DesktopDropManager: public QObject {
	Q_OBJECT;

public:
	~DesktopDropManager() override;
	Q_DISABLE_COPY_MOVE(DesktopDropManager);

	static DesktopDropManager* instance();

	[[nodiscard]] bool enabled() const { return this->mEnabled; }
	void setEnabled(bool enabled);

	[[nodiscard]] bool active() const { return this->armedState; }
	[[nodiscard]] QString activeScreen() const { return this->mActiveScreen; }

	static void onMouseHook(WPARAM message, const MSLLHOOKSTRUCT* info);

	void notifyDragEntered();
	void notifyDropped(const QStringList& paths, POINTL pt);
	void requestDisarm();


signals:
	void enabledChanged();
	void activeChanged();
	void filesDropped(QStringList paths, QString screenName);

private:
	explicit DesktopDropManager(QObject* parent);

	void onArmRequested();
	void disarm();
	bool ensureOverlay(HWND parent);
	bool registerTarget();

	static LRESULT CALLBACK messageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	bool mEnabled = false;
	bool armedState = false;
	bool targetRegistered = false;
	bool oleInited = false;
	QString mActiveScreen;
	HWND messageWindow = nullptr;
	HWND overlay = nullptr;
	IDropTarget* dropTarget = nullptr;
	QTimer enterWaitTimer;
	QTimer safetyTimer;
};

class DesktopDrop: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	// clang-format off
	Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged);
	Q_PROPERTY(bool active READ active NOTIFY activeChanged);
	Q_PROPERTY(QString activeScreen READ activeScreen NOTIFY activeChanged);
	// clang-format on

public:
	explicit DesktopDrop(QObject* parent = nullptr);

	[[nodiscard]] bool enabled() const;
	void setEnabled(bool enabled);

	[[nodiscard]] bool active() const;
	[[nodiscard]] QString activeScreen() const;


signals:
	void enabledChanged();
	void activeChanged();
	void filesDropped(QStringList paths, QString screenName);
};

} // namespace qs::windows
