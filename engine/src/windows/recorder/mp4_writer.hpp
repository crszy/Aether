#pragma once

#include <qt_windows.h>

#include <d3d11.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>

#include <qsize.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtypes.h>
#include <winrt/base.h>

namespace qs::windows::recorder {

class Mp4Writer {
public:
	Mp4Writer() = default;
	~Mp4Writer();
	Q_DISABLE_COPY_MOVE(Mp4Writer);

	bool open(
	    const QString& path,
	    QSize size,
	    int fps,
	    int audioRate,
	    ID3D11Device* device,
	    QString* error
	);

	bool writeVideo(ID3D11Texture2D* frame, qint64 time, qint64 duration, QString* error);
	bool writeAudio(const qint16* samples, qsizetype frames, QString* error);

	bool finalize(QString* error);
	void close();

	[[nodiscard]] bool gpu() const { return this->mGpu; }
	[[nodiscard]] qint64 framesWritten() const { return this->mFramesWritten; }
	[[nodiscard]] qint64 framesDropped() const { return this->mFramesDropped; }

private:
	bool openWith(bool gpu, const QString& path, QString* error);
	[[nodiscard]] bool encoderBehind() const;

	QSize size;
	int fps = 30;
	int audioRate = 0;
	bool mGpu = false;

	winrt::com_ptr<ID3D11Device> device;
	winrt::com_ptr<ID3D11DeviceContext> context;
	winrt::com_ptr<IMFDXGIDeviceManager> manager;
	winrt::com_ptr<IMFSinkWriter> writer;
	winrt::com_ptr<IMFVideoSampleAllocatorEx> allocator;
	winrt::com_ptr<ID3D11Texture2D> staging;

	DWORD videoStream = 0;
	DWORD audioStream = 0;
	qint64 audioFrames = 0;
	qint64 mFramesWritten = 0;
	qint64 mFramesDropped = 0;
};

} // namespace qs::windows::recorder
