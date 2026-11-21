#include "audio_visualizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qnamespace.h>
#include <qscopeguard.h>
#include <qt_windows.h>
#include <qtypes.h>

#include <mmdeviceapi.h>
#include <winrt/base.h>

#include "../../core/logcat.hpp"
#include "../recorder/clock.hpp"
#include "../visualizer/loopback_source.hpp"
#include "../visualizer/spectrum.hpp"

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logVisualizer, "quickshell.windows.visualizer", QtWarningMsg);

constexpr int DEFAULT_SAMPLE_RATE = 48000;
constexpr int MAX_BARS = 1024;
constexpr int MAX_FRAMERATE = 360;
constexpr qint64 TICKS_PER_SECOND = 10'000'000;
constexpr qint64 RETRY_INTERVAL = 10'000'000;
constexpr qint64 LOST_RETRY_INTERVAL = 2'000'000;
constexpr qint64 IDLE_INTERVAL = 1'000'000;
constexpr qint64 SILENCE_GAP = 500'000;

} // namespace

AudioVisualizer::AudioVisualizer(QObject* parent): QObject(parent) {
	this->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (this->stopEvent == nullptr) {
		qCWarning(logVisualizer) << "CreateEvent failed:" << GetLastError();
	}
	this->storeSettings();
}

AudioVisualizer::~AudioVisualizer() {
	this->joinWorker();
	if (this->stopEvent != nullptr) CloseHandle(this->stopEvent);
}

void AudioVisualizer::setRunning(bool running) {
	if (running == this->mRunning) return;
	this->mRunning = running;
	if (running) {
		this->start();
	} else {
		this->stop();
	}
	emit this->runningChanged();
}

void AudioVisualizer::setBars(int bars) {
	bars = std::clamp(bars, 1, MAX_BARS);
	if (bars == this->mBars) return;
	this->mBars = bars;
	this->storeSettings();
	emit this->barsChanged();
}

void AudioVisualizer::setFramerate(int framerate) {
	framerate = std::clamp(framerate, 1, MAX_FRAMERATE);
	if (framerate == this->mFramerate) return;
	this->mFramerate = framerate;
	this->storeSettings();
	emit this->framerateChanged();
}

void AudioVisualizer::setLowerCutoff(qreal cutoff) {
	cutoff = std::max(cutoff, 1.0);
	if (cutoff == this->mLowerCutoff) return;
	this->mLowerCutoff = cutoff;
	this->storeSettings();
	emit this->lowerCutoffChanged();
}

void AudioVisualizer::setUpperCutoff(qreal cutoff) {
	cutoff = std::max(cutoff, 1.0);
	if (cutoff == this->mUpperCutoff) return;
	this->mUpperCutoff = cutoff;
	this->storeSettings();
	emit this->upperCutoffChanged();
}

void AudioVisualizer::setNoiseReduction(int noiseReduction) {
	noiseReduction = std::clamp(noiseReduction, 0, 100);
	if (noiseReduction == this->mNoiseReduction) return;
	this->mNoiseReduction = noiseReduction;
	this->storeSettings();
	emit this->noiseReductionChanged();
}

void AudioVisualizer::setMaxValue(int maxValue) {
	maxValue = std::max(maxValue, 1);
	if (maxValue == this->mMaxValue) return;
	this->mMaxValue = maxValue;
	this->storeSettings();
	emit this->maxValueChanged();
}

void AudioVisualizer::storeSettings() {
	{
		std::lock_guard lock(this->mutex);
		this->settings.spectrum = visualizer::SpectrumSettings {
		    .bars = this->mBars,
		    .framerate = this->mFramerate,
		    .lowerCutoff = this->mLowerCutoff,
		    .upperCutoff = this->mUpperCutoff,
		    .noiseReduction = this->mNoiseReduction / 100.0,
		};
		this->settings.maxValue = this->mMaxValue;
	}
	this->settingsSerial.fetch_add(1, std::memory_order_release);
}

AudioVisualizer::Settings AudioVisualizer::loadSettings() {
	std::lock_guard lock(this->mutex);
	return this->settings;
}

void AudioVisualizer::start() {
	if (this->worker.joinable() || this->stopEvent == nullptr) return;
	ResetEvent(this->stopEvent);
	auto generation = ++this->generation;
	this->worker = std::thread([this, generation] { this->run(generation); });
}

void AudioVisualizer::joinWorker() {
	if (!this->worker.joinable()) return;
	SetEvent(this->stopEvent);
	this->worker.join();
}

void AudioVisualizer::stop() {
	this->joinWorker();
	++this->generation;

	{
		std::lock_guard lock(this->mutex);
		this->pendingValues.clear();
		this->pendingFresh = false;
	}

	this->setCapturing(false);

	if (!this->mValues.isEmpty()) {
		this->mValues.clear();
		emit this->valuesChanged();
	}
}

void AudioVisualizer::publish(quint64 generation, const QList<qreal>& values) {
	{
		std::lock_guard lock(this->mutex);
		this->pendingValues = values;
		this->pendingGeneration = generation;
		this->pendingFresh = true;
	}

	if (!this->framePosted.exchange(true)) {
		QMetaObject::invokeMethod(this, [this] { this->applyFrame(); }, Qt::QueuedConnection);
	}
}

void AudioVisualizer::applyFrame() {
	this->framePosted.store(false);

	QList<qreal> values;
	{
		std::lock_guard lock(this->mutex);
		if (!this->pendingFresh || this->pendingGeneration != this->generation) return;
		this->pendingFresh = false;
		values = this->pendingValues;
	}

	if (!this->mRunning) return;
	this->mValues = values;
	emit this->valuesChanged();
}

void AudioVisualizer::reportCapturing(quint64 generation, bool capturing) {
	QMetaObject::invokeMethod(
	    this,
	    [this, generation, capturing] {
		    if (generation == this->generation) this->setCapturing(capturing);
	    },
	    Qt::QueuedConnection
	);
}

void AudioVisualizer::setCapturing(bool capturing) {
	if (capturing == this->mCapturing) return;
	this->mCapturing = capturing;
	emit this->capturingChanged();
}

void AudioVisualizer::run(quint64 generation) {
	auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	auto uninit = qScopeGuard([com] {
		if (SUCCEEDED(com)) CoUninitialize();
	});

	auto* timer = CreateWaitableTimerExW(
	    nullptr,
	    nullptr,
	    CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
	    TIMER_ALL_ACCESS
	);
	if (timer == nullptr) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
	auto* deviceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	auto closeHandles = qScopeGuard([timer, deviceEvent] {
		if (timer != nullptr) CloseHandle(timer);
		if (deviceEvent != nullptr) CloseHandle(deviceEvent);
	});

	winrt::com_ptr<IMMDeviceEnumerator> enumerator;
	auto hr = CoCreateInstance(
	    __uuidof(MMDeviceEnumerator),
	    nullptr,
	    CLSCTX_ALL,
	    IID_PPV_ARGS(enumerator.put())
	);

	if (timer == nullptr || deviceEvent == nullptr || FAILED(hr)) {
		qCWarning(logVisualizer) << "Visualizer cannot start:" << Qt::hex << hr << GetLastError();
		WaitForSingleObject(this->stopEvent, INFINITE);
		return;
	}

	visualizer::DefaultDeviceWatcher watcher;
	auto watcherActive = watcher.start(enumerator.get(), deviceEvent);

	auto serial = this->settingsSerial.load(std::memory_order_acquire);
	auto settings = this->loadSettings();

	visualizer::Spectrum spectrum(settings.spectrum, DEFAULT_SAMPLE_RATE);
	visualizer::LoopbackSource source;
	std::vector<float> samples;
	std::vector<double> frame;
	QList<qreal> scaled;
	QList<qreal> published;
	auto capturing = false;

	auto now = recorder::qpc100ns();
	auto retryAt = now;
	auto watcherRetryAt = watcherActive ? now + RETRY_INTERVAL : now;
	auto lastAudio = now;
	auto lastTick = now;
	auto nextTick = now;

	const std::array<HANDLE, 3> handles {this->stopEvent, deviceEvent, timer};

	while (true) {
		now = recorder::qpc100ns();

		auto latest = this->settingsSerial.load(std::memory_order_acquire);
		if (latest != serial) {
			serial = latest;
			settings = this->loadSettings();
			spectrum.setSettings(settings.spectrum);
		}

		if (!source.isOpen() && now >= retryAt) {
			if (source.open(enumerator.get())) {
				spectrum.setSampleRate(source.sampleRate());
				lastAudio = now;
			} else {
				retryAt = now + RETRY_INTERVAL;
			}
		}

		if (!watcherActive && now >= watcherRetryAt) {
			watcherActive = watcher.start(enumerator.get(), deviceEvent);
			watcherRetryAt = now + RETRY_INTERVAL;
		}

		samples.clear();
		auto gotPackets = false;
		if (source.isOpen() && !source.read(samples, &gotPackets)) {
			source.close();
			retryAt = now + LOST_RETRY_INTERVAL;
		}

		if (capturing != source.isOpen()) {
			capturing = source.isOpen();
			this->reportCapturing(generation, capturing);
		}

		if (gotPackets) {
			spectrum.push(samples.data(), samples.size());
			lastAudio = now;
		} else if (now - lastAudio > SILENCE_GAP) {
			spectrum.pushSilence(
			    static_cast<size_t>((now - lastTick) * spectrum.sampleRate() / TICKS_PER_SECOND)
			);
		}
		lastTick = now;

		auto active = spectrum.update(frame);

		scaled.resize(static_cast<qsizetype>(frame.size()));
		for (size_t i = 0; i < frame.size(); ++i) {
			scaled[static_cast<qsizetype>(i)] = std::round(frame[i] * settings.maxValue);
		}

		if (scaled != published) {
			published = scaled;
			this->publish(generation, published);
		}

		if (active) {
			auto interval = TICKS_PER_SECOND / std::max(1, settings.spectrum.framerate);
			nextTick += interval;
			if (nextTick <= now) nextTick = now + interval;
		} else {
			nextTick = now + IDLE_INTERVAL;
		}

		LARGE_INTEGER due {};
		due.QuadPart = -std::max<qint64>(nextTick - recorder::qpc100ns(), 1);
		SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);

		auto result = WaitForMultipleObjects(
		    static_cast<DWORD>(handles.size()),
		    handles.data(),
		    FALSE,
		    INFINITE
		);

		if (result == WAIT_OBJECT_0) break;

		if (result == WAIT_OBJECT_0 + 1) {
			source.close();
			retryAt = 0;
		} else if (result != WAIT_OBJECT_0 + 2) {
			qCWarning(logVisualizer) << "Visualizer wait failed:" << GetLastError();
			WaitForSingleObject(this->stopEvent, INFINITE);
			break;
		}
	}

	CancelWaitableTimer(timer);
	watcher.stop();
	source.close();
}

} // namespace qs::windows::sys
