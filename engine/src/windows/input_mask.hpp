#pragma once

#include <thread>

#include <qt_windows.h>

#include <qlist.h>
#include <qobject.h>
#include <qpoint.h>
#include <qpointer.h>
#include <qregion.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qwindow.h>

namespace qs::windows {

void noteHookDelay(bool keyboard, DWORD eventTime);

class InputMaskTracker: public QObject {
	Q_OBJECT;

public:
	~InputMaskTracker() override;
	Q_DISABLE_COPY_MOVE(InputMaskTracker);

	static InputMaskTracker* instance();

	void setMask(QWindow* window, const QRegion& region);
	void remove(QWindow* window);

	void refresh();

	void acquireButtonEvents();
	void releaseButtonEvents();

	void acquireCursorEvents();
	void releaseCursorEvents();

	void acquireMonitorEvents();
	void releaseMonitorEvents();
	void resetCursorMonitor();

	void acquireHook();
	void releaseHook();
	[[nodiscard]] bool hookActive() const { return this->hookRunning; }

signals:
	void buttonPressed(QPoint position, quint32 time);
	void cursorMoved(QPoint position);
	void cursorMonitorChanged(quintptr monitor);

private:
	explicit InputMaskTracker(QObject* parent);

	struct Entry {
		QPointer<QWindow> window;
		QRegion region;
	};

	void evaluate(POINT cursor);
	void publish();
	void emitCursor(QPoint position);
	void onCursorTrail();
	void updateHookState();
	bool startHook();
	void stopHook();
	void onHookFailed(DWORD error, quint32 generation);
	void onCursorMoved();
	void onButtonPressed(QPoint position, quint32 time);

	static LRESULT CALLBACK messageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK mouseHookProc(int code, WPARAM wParam, LPARAM lParam);
	static void hookThreadMain(quint32 generation);

	QList<Entry> entries;
	HWND messageWindow = nullptr;
	std::thread hookThread;
	bool hookRunning = false;
	bool hookFailed = false;
	quint32 hookGeneration = 0;
	int cursorWatchers = 0;
	int monitorWatchers = 0;
	int hookHolders = 0;
	QPoint lastCursor;
	QTimer pollTimer;
	QTimer cursorTrailTimer;
	QTimer lateReportTimer;
};

} // namespace qs::windows
