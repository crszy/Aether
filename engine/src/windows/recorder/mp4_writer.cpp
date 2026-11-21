#include "mp4_writer.hpp"

#include <algorithm>
#include <cstring>

#include <qt_windows.h>

#include <codecapi.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>

#include <qdir.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qsize.h>
#include <qstring.h>
#include <qtypes.h>
#include <winrt/base.h>

namespace qs::windows::recorder {

namespace {
Q_LOGGING_CATEGORY(logMp4Writer, "quickshell.windows.recorder", QtWarningMsg);

constexpr double BITS_PER_PIXEL = 0.15;
constexpr UINT32 MIN_BITRATE = 1'000'000;
constexpr UINT32 MAX_BITRATE = 50'000'000;
constexpr UINT32 AAC_BYTES_PER_SECOND = 24000;
constexpr UINT32 AAC_PROFILE_LEVEL = 0x29;

QString hrMessage(const QString& what, HRESULT hr) {
	return QString("%1 (0x%2)").arg(what).arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
}

} // namespace

Mp4Writer::~Mp4Writer() { this->close(); }

bool Mp4Writer::open(
    const QString& path,
    QSize size,
    int fps,
    int audioRate,
    ID3D11Device* device,
    QString* error
) {
	this->size = size;
	this->fps = fps;
	this->audioRate = audioRate;
	this->device.copy_from(device);
	this->context = nullptr;
	device->GetImmediateContext(this->context.put());

	if (this->openWith(true, path, error)) return true;

	qCInfo(logMp4Writer) << "GPU encoding unavailable:" << *error << "- using the software encoder";
	this->close();
	DeleteFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()));

	return this->openWith(false, path, error);
}

bool Mp4Writer::openWith(bool gpu, const QString& path, QString* error) {
	auto fail = [error](const QString& what, HRESULT hr) {
		*error = hrMessage(what, hr);
		return false;
	};

	auto width = static_cast<UINT32>(this->size.width());
	auto height = static_cast<UINT32>(this->size.height());
	auto fps = static_cast<UINT32>(this->fps);

	winrt::com_ptr<IMFAttributes> attributes;
	auto hr = MFCreateAttributes(attributes.put(), 4);
	if (FAILED(hr)) return fail("MFCreateAttributes failed", hr);

	attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, gpu ? TRUE : FALSE);
	attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
	attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);

	if (gpu) {
		UINT token = 0;
		hr = MFCreateDXGIDeviceManager(&token, this->manager.put());
		if (FAILED(hr)) return fail("MFCreateDXGIDeviceManager failed", hr);

		hr = this->manager->ResetDevice(this->device.get(), token);
		if (FAILED(hr)) return fail("IMFDXGIDeviceManager::ResetDevice failed", hr);

		attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, this->manager.get());
	}

	auto nativePath = QDir::toNativeSeparators(path);
	hr = MFCreateSinkWriterFromURL(
	    reinterpret_cast<LPCWSTR>(nativePath.utf16()),
	    nullptr,
	    attributes.get(),
	    this->writer.put()
	);
	if (FAILED(hr)) return fail("cannot create the video file", hr);

	auto bitrate = static_cast<UINT32>(std::clamp(
	    static_cast<double>(width) * height * fps * BITS_PER_PIXEL,
	    static_cast<double>(MIN_BITRATE),
	    static_cast<double>(MAX_BITRATE)
	));

	winrt::com_ptr<IMFMediaType> videoOut;
	hr = MFCreateMediaType(videoOut.put());
	if (FAILED(hr)) return fail("MFCreateMediaType failed", hr);
	videoOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	videoOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	videoOut->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
	videoOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	videoOut->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main);
	MFSetAttributeSize(videoOut.get(), MF_MT_FRAME_SIZE, width, height);
	MFSetAttributeRatio(videoOut.get(), MF_MT_FRAME_RATE, fps, 1);
	MFSetAttributeRatio(videoOut.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

	hr = this->writer->AddStream(videoOut.get(), &this->videoStream);
	if (FAILED(hr)) return fail("no H.264 encoder for this size", hr);

	winrt::com_ptr<IMFMediaType> videoIn;
	hr = MFCreateMediaType(videoIn.put());
	if (FAILED(hr)) return fail("MFCreateMediaType failed", hr);
	videoIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	videoIn->SetGUID(MF_MT_SUBTYPE, gpu ? MFVideoFormat_ARGB32 : MFVideoFormat_RGB32);
	videoIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	videoIn->SetUINT32(MF_MT_DEFAULT_STRIDE, width * 4);
	videoIn->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	videoIn->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
	videoIn->SetUINT32(MF_MT_SAMPLE_SIZE, width * height * 4);
	MFSetAttributeSize(videoIn.get(), MF_MT_FRAME_SIZE, width, height);
	MFSetAttributeRatio(videoIn.get(), MF_MT_FRAME_RATE, fps, 1);
	MFSetAttributeRatio(videoIn.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

	hr = this->writer->SetInputMediaType(this->videoStream, videoIn.get(), nullptr);
	if (FAILED(hr)) return fail("the H.264 encoder doesn't take the captured frames", hr);

	if (this->audioRate > 0) {
		auto rate = static_cast<UINT32>(this->audioRate);

		winrt::com_ptr<IMFMediaType> audioOut;
		hr = MFCreateMediaType(audioOut.put());
		if (FAILED(hr)) return fail("MFCreateMediaType failed", hr);
		audioOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
		audioOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
		audioOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
		audioOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
		audioOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
		audioOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, AAC_BYTES_PER_SECOND);
		audioOut->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, AAC_PROFILE_LEVEL);

		hr = this->writer->AddStream(audioOut.get(), &this->audioStream);
		if (FAILED(hr)) return fail("no AAC encoder", hr);

		winrt::com_ptr<IMFMediaType> audioIn;
		hr = MFCreateMediaType(audioIn.put());
		if (FAILED(hr)) return fail("MFCreateMediaType failed", hr);
		audioIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
		audioIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
		audioIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
		audioIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
		audioIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
		audioIn->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
		audioIn->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * 4);

		hr = this->writer->SetInputMediaType(this->audioStream, audioIn.get(), nullptr);
		if (FAILED(hr)) return fail("the AAC encoder doesn't take the captured sound", hr);
	}

	hr = this->writer->BeginWriting();
	if (FAILED(hr)) return fail("cannot start encoding", hr);

	if (gpu) {
		hr = MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(this->allocator.put()));
		if (FAILED(hr)) return fail("MFCreateVideoSampleAllocatorEx failed", hr);

		hr = this->allocator->SetDirectXManager(this->manager.get());
		if (FAILED(hr)) return fail("IMFVideoSampleAllocatorEx::SetDirectXManager failed", hr);

		winrt::com_ptr<IMFAttributes> allocatorAttributes;
		hr = MFCreateAttributes(allocatorAttributes.put(), 3);
		if (FAILED(hr)) return fail("MFCreateAttributes failed", hr);
		allocatorAttributes->SetUINT32(MF_SA_D3D11_USAGE, D3D11_USAGE_DEFAULT);
		allocatorAttributes->SetUINT32(
		    MF_SA_D3D11_BINDFLAGS,
		    D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
		);
		allocatorAttributes->SetUINT32(MF_SA_BUFFERS_PER_SAMPLE, 1);

		hr = this->allocator
		         ->InitializeSampleAllocatorEx(4, 16, allocatorAttributes.get(), videoIn.get());
		if (FAILED(hr)) return fail("cannot allocate encoder surfaces", hr);

		winrt::com_ptr<IMFSample> probe;
		hr = this->allocator->AllocateSample(probe.put());
		if (FAILED(hr)) return fail("cannot allocate an encoder surface", hr);

		winrt::com_ptr<IMFMediaBuffer> buffer;
		probe->GetBufferByIndex(0, buffer.put());
		auto dxgiBuffer = buffer ? buffer.try_as<IMFDXGIBuffer>() : nullptr;
		winrt::com_ptr<ID3D11Texture2D> texture;
		if (dxgiBuffer) dxgiBuffer->GetResource(IID_PPV_ARGS(texture.put()));

		D3D11_TEXTURE2D_DESC desc {};
		if (texture) texture->GetDesc(&desc);
		if (!texture || desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || desc.Width != width
		    || desc.Height != height)
		{
			return fail("encoder surfaces don't match the capture format", E_UNEXPECTED);
		}
	}

	this->mGpu = gpu;
	qCInfo(logMp4Writer) << "Encoding" << this->size << "at" << fps << "fps," << bitrate
	                     << "bit/s," << (gpu ? "GPU path" : "software path")
	                     << (this->audioRate > 0 ? "with" : "without") << "audio";
	return true;
}

bool Mp4Writer::encoderBehind() const {
	MF_SINK_WRITER_STATISTICS stats {};
	stats.cb = sizeof(stats);
	if (FAILED(this->writer->GetStatistics(this->videoStream, &stats))) return false;

	if (stats.qwNumSamplesEncoded == 0) return false;
	return stats.qwNumSamplesReceived > stats.qwNumSamplesEncoded + static_cast<ULONGLONG>(this->fps);
}

bool Mp4Writer::writeVideo(ID3D11Texture2D* frame, qint64 time, qint64 duration, QString* error) {
	if (!this->writer) return false;

	auto fail = [error](const QString& what, HRESULT hr) {
		*error = hrMessage(what, hr);
		return false;
	};

	winrt::com_ptr<IMFSample> sample;

	if (this->mGpu) {
		auto hr = this->allocator->AllocateSample(sample.put());
		if (hr == MF_E_SAMPLEALLOCATOR_EMPTY) {
			this->mFramesDropped++;
			return true;
		}
		if (FAILED(hr)) return fail("cannot allocate an encoder surface", hr);

		winrt::com_ptr<IMFMediaBuffer> buffer;
		hr = sample->GetBufferByIndex(0, buffer.put());
		if (FAILED(hr)) return fail("encoder surface has no buffer", hr);

		auto dxgiBuffer = buffer.try_as<IMFDXGIBuffer>();
		if (!dxgiBuffer) return fail("encoder surface is not a texture", E_NOINTERFACE);

		winrt::com_ptr<ID3D11Texture2D> texture;
		UINT subresource = 0;
		hr = dxgiBuffer->GetResource(IID_PPV_ARGS(texture.put()));
		if (SUCCEEDED(hr)) hr = dxgiBuffer->GetSubresourceIndex(&subresource);
		if (FAILED(hr)) return fail("encoder surface has no texture", hr);

		this->context->CopySubresourceRegion(texture.get(), subresource, 0, 0, 0, frame, 0, nullptr);

		DWORD length = 0;
		if (SUCCEEDED(buffer->GetMaxLength(&length)) && length > 0) buffer->SetCurrentLength(length);
	} else {
		if (this->encoderBehind()) {
			this->mFramesDropped++;
			return true;
		}

		if (!this->staging) {
			D3D11_TEXTURE2D_DESC desc {};
			frame->GetDesc(&desc);
			desc.Usage = D3D11_USAGE_STAGING;
			desc.BindFlags = 0;
			desc.MiscFlags = 0;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

			auto hr = this->device->CreateTexture2D(&desc, nullptr, this->staging.put());
			if (FAILED(hr)) return fail("cannot create the readback texture", hr);
		}

		this->context->CopyResource(this->staging.get(), frame);

		D3D11_MAPPED_SUBRESOURCE mapped {};
		auto hr = this->context->Map(this->staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
		if (FAILED(hr)) return fail("cannot read the frame back", hr);

		auto rowBytes = static_cast<DWORD>(this->size.width() * 4);
		auto bytes = rowBytes * static_cast<DWORD>(this->size.height());

		winrt::com_ptr<IMFMediaBuffer> buffer;
		hr = MFCreateMemoryBuffer(bytes, buffer.put());

		BYTE* data = nullptr;
		if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, nullptr);
		if (SUCCEEDED(hr)) {
			hr = MFCopyImage(
			    data,
			    static_cast<LONG>(rowBytes),
			    static_cast<const BYTE*>(mapped.pData),
			    static_cast<LONG>(mapped.RowPitch),
			    rowBytes,
			    static_cast<DWORD>(this->size.height())
			);
			buffer->Unlock();
		}

		this->context->Unmap(this->staging.get(), 0);

		if (SUCCEEDED(hr)) hr = buffer->SetCurrentLength(bytes);
		if (SUCCEEDED(hr)) hr = MFCreateSample(sample.put());
		if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.get());
		if (FAILED(hr)) return fail("cannot prepare a frame", hr);
	}

	sample->SetSampleTime(time);
	sample->SetSampleDuration(duration);

	auto hr = this->writer->WriteSample(this->videoStream, sample.get());
	if (FAILED(hr)) return fail("encoding a frame failed", hr);

	this->mFramesWritten++;
	return true;
}

bool Mp4Writer::writeAudio(const qint16* samples, qsizetype frames, QString* error) {
	if (!this->writer || this->audioRate <= 0) return false;
	if (frames <= 0) return true;

	auto bytes = static_cast<DWORD>(frames * 2 * sizeof(qint16));

	winrt::com_ptr<IMFMediaBuffer> buffer;
	auto hr = MFCreateMemoryBuffer(bytes, buffer.put());

	BYTE* data = nullptr;
	if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, nullptr);
	if (SUCCEEDED(hr)) {
		std::memcpy(data, samples, bytes);
		buffer->Unlock();
		hr = buffer->SetCurrentLength(bytes);
	}

	winrt::com_ptr<IMFSample> sample;
	if (SUCCEEDED(hr)) hr = MFCreateSample(sample.put());
	if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.get());

	if (SUCCEEDED(hr)) {
		auto start = this->audioFrames * 10'000'000 / this->audioRate;
		auto end = (this->audioFrames + frames) * 10'000'000 / this->audioRate;
		sample->SetSampleTime(start);
		sample->SetSampleDuration(end - start);
		hr = this->writer->WriteSample(this->audioStream, sample.get());
	}

	if (FAILED(hr)) {
		*error = hrMessage("encoding sound failed", hr);
		return false;
	}

	this->audioFrames += frames;
	return true;
}

bool Mp4Writer::finalize(QString* error) {
	if (!this->writer) {
		*error = "the video file was never opened";
		return false;
	}

	auto hr = this->writer->Finalize();
	this->close();

	if (hr == MF_E_SINK_NO_SAMPLES_PROCESSED) {
		*error = "nothing was recorded";
		return false;
	}

	if (FAILED(hr)) {
		*error = hrMessage("finishing the video file failed", hr);
		return false;
	}

	return true;
}

void Mp4Writer::close() {
	this->writer = nullptr;
	this->allocator = nullptr;
	this->staging = nullptr;
	this->manager = nullptr;
}

} // namespace qs::windows::recorder
