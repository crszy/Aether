#include "audiocollector.hpp"

#include <qloggingcategory.h>


#include <algorithm>
#include <mutex>
#include <stop_token>
#include <utility>
#include <vector>

#include "service.hpp"

namespace {

Q_LOGGING_CATEGORY(lcAcWorker, "caelestia.services.ac.worker", QtInfoMsg)

} // namespace


#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <algorithm>
#include <vector>
#include <chrono>

namespace caelestia::shell::services {

// Captures what the speakers play (loopback), downmixes to mono int16 and feeds AudioCollector::loadChunk,
// the same shape the PipeWire worker produced. Retries every second if there is no output device.
WasapiWorker::WasapiWorker(std::stop_token token, AudioCollector* collector) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::vector<qint16> mono;
    while (!token.stop_requested()) {
        IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr; IAudioClient* ac = nullptr;
        IAudioCaptureClient* cap = nullptr; WAVEFORMATEX* wf = nullptr;
        bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en))
            && SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))
            && SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac))
            && SUCCEEDED(ac->GetMixFormat(&wf))
            && SUCCEEDED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 200000, 0, wf, nullptr))
            && SUCCEEDED(ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cap))
            && SUCCEEDED(ac->Start());
        bool isFloat = false;
        if (ok) {
            isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
                || (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wf)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
            ok = isFloat || wf->wBitsPerSample == 16;
        }
        const int ch = ok ? wf->nChannels : 1;
        while (ok && !token.stop_requested()) {
            UINT32 pkt = 0;
            if (FAILED(cap->GetNextPacketSize(&pkt))) break;
            if (pkt == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(8)); continue; }
            BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
            if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            mono.resize(frames);
            for (UINT32 f = 0; f < frames; ++f) {
                float s = 0;
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                    for (int k = 0; k < ch; ++k)
                        s += isFloat ? reinterpret_cast<const float*>(data)[f * ch + k]
                                     : reinterpret_cast<const qint16*>(data)[f * ch + k] / 32768.0f;
                    s /= static_cast<float>(ch);
                }
                mono[f] = static_cast<qint16>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
            }
            cap->ReleaseBuffer(frames);
            if (frames) collector->loadChunk(mono.data(), frames);
        }
        if (ac) ac->Stop();
        if (wf) CoTaskMemFree(wf);
        if (cap) cap->Release();
        if (ac) ac->Release();
        if (dev) dev->Release();
        if (en) en->Release();
        for (int i = 0; i < 10 && !token.stop_requested(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    CoUninitialize();
}

AudioCollector& AudioCollector::instance() {
    static AudioCollector s_instance;
    return s_instance;
}

void AudioCollector::clearBuffer() {
    const std::scoped_lock lock(m_samplesLock);
    m_samples.clear();
    m_discardPending.store(true, std::memory_order_release);
}

void AudioCollector::loadChunk(const qint16* samples, quint32 count) {
    if (m_discardPending.exchange(false, std::memory_order_acquire)) {
        m_pending.clear();
    }

    const auto toFloat = [](qint16 sample) {
        return static_cast<float>(sample) / 32768.0f;
    };

    // Called on the RT thread, so stash the chunk until the next callback rather than wait on a reader
    const std::unique_lock lock(m_samplesLock, std::try_to_lock);
    if (!lock.owns_lock()) {
        m_pending.push(std::span(samples, count), toFloat);
        return;
    }

    m_pending.pushTo(m_samples);
    m_pending.clear();
    m_samples.push(std::span(samples, count), toFloat);
}

template <typename T> quint32 AudioCollector::readLatest(T* out, quint32 count) {
    if (count == 0 || count > ac::k_chunkSize) {
        count = ac::k_chunkSize;
    }

    const std::span dest(out, count);
    const std::scoped_lock lock(m_samplesLock);

    // Pad the front with silence until the window has filled after a clear
    const auto available = static_cast<size_t>(std::min(m_samples.count(), static_cast<qsizetype>(count)));
    std::ranges::fill(dest.first(count - available), T(0));
    m_samples.copyLatest(dest.last(available), [](float sample) {
        return static_cast<T>(sample);
    });

    return count;
}

quint32 AudioCollector::readChunk(float* out, quint32 count) {
    return readLatest(out, count);
}

quint32 AudioCollector::readChunk(double* out, quint32 count) {
    return readLatest(out, count);
}

AudioCollector::AudioCollector(QObject* parent)
    : Service(parent)
    , m_samples(ac::k_chunkSize)
    , m_pending(ac::k_chunkSize)
    , m_discardPending(false) {}

AudioCollector::~AudioCollector() {
    AudioCollector::stop();
}

void AudioCollector::start() {
    if (m_thread.joinable()) {
        return;
    }

    clearBuffer();

    m_thread = std::jthread([this](std::stop_token token) {
        const WasapiWorker worker(std::move(token), this);
    });
}

void AudioCollector::stop() {
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
}

} // namespace caelestia::shell::services
