#pragma once

#include <atomic>
#include <thread>
#include <vector>

#include <qt_windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtypes.h>
#include <winrt/base.h>

#include "audio_dsp.hpp"

namespace qs::windows::recorder {

class AudioCapture {
public:
	enum class Source : quint8 { SystemAudio, Microphone };

	explicit AudioCapture(Source source): source(source) {}
	~AudioCapture();
	Q_DISABLE_COPY_MOVE(AudioCapture);

	bool open(QString* error, bool* blocked = nullptr);
	[[nodiscard]] int nativeRate() const { return this->inRate; }

	void start(qint64 origin, int outputRate);
	void stop();
	void finish(qint64 until);

	[[nodiscard]] FrameQueue* queue() { return &this->mQueue; }
	[[nodiscard]] Source kind() const { return this->source; }
	[[nodiscard]] bool lost() const { return this->mLost.load(std::memory_order_relaxed); }

	static QString microphoneBlockedReason();

private:
	void run();
	void drain();
	[[nodiscard]] double framesAt(qint64 time) const;

	const Source source;
	winrt::com_ptr<IAudioClient> client;
	winrt::com_ptr<IAudioCaptureClient> capture;
	std::thread thread;
	HANDLE stopEvent = nullptr;
	HANDLE packetEvent = nullptr;
	qint64 origin = 0;
	int outRate = 0;

	SampleType type = SampleType::Float32;
	int inRate = 0;
	int bytesPerSample = 0;
	StereoWeights weights;

	AlignedStream stream;
	std::vector<float> converted;
	std::vector<float> produced;
	FrameQueue mQueue;

	std::atomic<bool> mLost {false};
};

} // namespace qs::windows::recorder
