#include "beattracker.hpp"

#include <qdatetime.h>
#include <algorithm>

#include "audiocollector.hpp"
#include "audioprovider.hpp"

namespace caelestia::shell::services {

BeatProcessor::BeatProcessor(QObject* parent)
    : AudioProcessor(parent)
    , m_in(ac::k_chunkSize) {}

BeatProcessor::~BeatProcessor() = default;

// A beat is a chunk whose energy jumps well above the recent average; bpm is smoothed from beat spacing.
void BeatProcessor::process() {
    AudioCollector::instance().readChunk(m_in.data());
    float e = 0;
    for (float s : m_in) e += s * s;
    e /= static_cast<float>(m_in.size());
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool onset = e > 1e-5f && e > m_avg * 1.6f && now - m_lastBeatMs > 250;
    m_avg = m_avg * 0.92f + e * 0.08f;
    if (!onset) return;
    if (m_lastBeatMs) {
        const float gap = static_cast<float>(now - m_lastBeatMs);
        if (gap < 1500.0f) m_bpm = std::clamp(m_bpm * 0.8f + (60000.0f / gap) * 0.2f, 60.0f, 200.0f);
    }
    m_lastBeatMs = now;
    emit beat(m_bpm);
}

BeatTracker::BeatTracker(QObject* parent)
    : AudioProvider(parent)
    , m_bpm(120) {
    m_processor = new BeatProcessor();
    init();

    connect(static_cast<BeatProcessor*>(m_processor), &BeatProcessor::beat, this, &BeatTracker::updateBpm);
}

smpl_t BeatTracker::bpm() const {
    return m_bpm;
}

void BeatTracker::updateBpm(smpl_t bpm) {
    if (!qFuzzyCompare(bpm + 1.0f, m_bpm + 1.0f)) {
        m_bpm = bpm;
        emit bpmChanged();
    }
}

} // namespace caelestia::shell::services
