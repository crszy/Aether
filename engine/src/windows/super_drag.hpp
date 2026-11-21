#pragma once

#include <thread>

#include <qt_windows.h>

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>

namespace qs::windows {

class SuperDragManager: public QObject {
	Q_OBJECT;

public:
	~SuperDragManager() override;
	Q_DISABLE_COPY_MOVE(SuperDragManager);

	static SuperDragManager* instance();

	[[nodiscard]] bool enabled() const { return this->mEnabled; }
	void setEnabled(bool enabled);
	void setWheelColumns(bool enabled);

	static bool onMouseHook(WPARAM message, const MSLLHOOKSTRUCT* info);
	static void onHookStopped();

signals:
	void enabledChanged();

private:
	explicit SuperDragManager(QObject* parent);

	struct GuiDrag {
		HWND hwnd = nullptr;
		bool tiledResize = false;
		bool notified = false;
	};

	bool startWorker();
	void stopWorker();

	LRESULT onStarted(HWND hwnd, LPARAM flags);
	void onTiledResize();
	void onEnded(HWND hwnd, LPARAM delta);
	static bool onWheel(WPARAM message, const MSLLHOOKSTRUCT* info);
	void finishGuiDrag(const POINT* delta);

	static LRESULT CALLBACK messageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static void workerMain();

	HWND messageWindow = nullptr;
	std::thread worker;
	bool workerRunning = false;
	bool mEnabled = false;
	bool wheelColumns = false;
	GuiDrag guiDrag;
};

class SuperDrag: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged);
	// "super" (default) or "alt": the key held for drag-to-move (left button) and drag-to-resize (right button)
	Q_PROPERTY(QString modifier READ modifier WRITE setModifier NOTIFY modifierChanged);

public:
	explicit SuperDrag(QObject* parent = nullptr);

	[[nodiscard]] bool enabled() const;
	void setEnabled(bool enabled);
	[[nodiscard]] QString modifier() const;
	void setModifier(const QString& modifier);

signals:
	void enabledChanged();
	void modifierChanged();
};

} // namespace qs::windows
