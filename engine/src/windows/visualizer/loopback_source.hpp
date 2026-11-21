#pragma once

#include <vector>

#include <qt_windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <qtclasshelpermacros.h>
#include <qtypes.h>
#include <winrt/base.h>

namespace qs::windows::visualizer {

class LoopbackSource {
public:
	LoopbackSource() = default;
	~LoopbackSource();
	Q_DISABLE_COPY_MOVE(LoopbackSource);

	bool open(IMMDeviceEnumerator* enumerator);
	void close();

	[[nodiscard]] bool isOpen() const { return this->capture != nullptr; }
	[[nodiscard]] int sampleRate() const { return this->rate; }

	bool read(std::vector<float>& mono, bool* gotPackets);

private:
	enum class SampleType : quint8 { Float32, Int16, Int24, Int32 };

	void append(const BYTE* data, UINT32 frames, DWORD flags, std::vector<float>& mono) const;

	winrt::com_ptr<IAudioClient> client;
	winrt::com_ptr<IAudioCaptureClient> capture;
	SampleType type = SampleType::Float32;
	int channels = 0;
	int rate = 0;
	int bytesPerSample = 0;
};

class DefaultDeviceWatcher {
public:
	DefaultDeviceWatcher() = default;
	~DefaultDeviceWatcher();
	Q_DISABLE_COPY_MOVE(DefaultDeviceWatcher);

	bool start(IMMDeviceEnumerator* enumerator, HANDLE event);
	void stop();

private:
	winrt::com_ptr<IMMDeviceEnumerator> enumerator;
	IMMNotificationClient* client = nullptr;
};

} // namespace qs::windows::visualizer
