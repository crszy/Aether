#pragma once

#include <memory>
#include <mutex>

#include <qt_windows.h>

#include <d3d11_1.h>
#include <dxgi1_2.h>

#include <qimage.h>
#include <qobject.h>
#include <qsize.h>
#include <qtclasshelpermacros.h>
#include <qthread.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::windows::capture {

class CaptureSession;
class CaptureWorker;
struct SessionCore;

struct CaptureTarget {
	HWND window = nullptr;
	HMONITOR monitor = nullptr;

	[[nodiscard]] bool valid() const { return this->window != nullptr || this->monitor != nullptr; }
};

struct CaptureOptions {
	bool cursor = false;
	bool live = false;
	int maxFps = 30;
};

struct SharedFrame {
	~SharedFrame();
	Q_DISABLE_COPY_MOVE(SharedFrame);
	SharedFrame() = default;

	QSize size;
	HANDLE handle = nullptr;
	ID3D11Texture2D* texture = nullptr;
	IDXGIKeyedMutex* mutex = nullptr;
};

class SharedFrameReader {
public:
	SharedFrameReader() = default;
	~SharedFrameReader();
	Q_DISABLE_COPY_MOVE(SharedFrameReader);

	bool open(ID3D11Device* device, const std::shared_ptr<SharedFrame>& frame);
	void close();

	[[nodiscard]] const std::shared_ptr<SharedFrame>& frame() const { return this->mFrame; }

	bool copyTo(ID3D11DeviceContext* context, ID3D11Texture2D* dest);

private:
	std::shared_ptr<SharedFrame> mFrame;
	ID3D11Texture2D* opened = nullptr;
	IDXGIKeyedMutex* mutex = nullptr;
};

class CaptureHandle: public QObject {
	Q_OBJECT;

public:
	explicit CaptureHandle(
	    const CaptureTarget& target,
	    const CaptureOptions& options,
	    QObject* parent = nullptr
	);
	~CaptureHandle() override;
	Q_DISABLE_COPY_MOVE(CaptureHandle);

	void start();
	void stop();
	[[nodiscard]] bool running() const { return this->mRunning; }

	void setCursor(bool cursor);
	void setLive(bool live);

	[[nodiscard]] std::shared_ptr<SharedFrame> latestFrame(quint64* serial = nullptr) const;

signals:
	void frameReady();
	void stopped(bool error);

private:
	friend class CaptureSession;
	void onSessionStopped(bool error);
	void onFrame(bool live);

	std::shared_ptr<SessionCore> core;
	CaptureSession* session = nullptr;
	bool mRunning = false;
};

class CaptureThread: public QObject {
	Q_OBJECT;

public:
	static CaptureThread* instance();

	[[nodiscard]] CaptureWorker* worker() const { return this->mWorker; }
	[[nodiscard]] QThread* thread() { return &this->mThread; }

	[[nodiscard]] QImage grabMonitor(HMONITOR monitor, int timeoutMs = 2000);

private:
	explicit CaptureThread();
	~CaptureThread() override;
	Q_DISABLE_COPY_MOVE(CaptureThread);

	QThread mThread;
	CaptureWorker* mWorker = nullptr;
};

} // namespace qs::windows::capture
