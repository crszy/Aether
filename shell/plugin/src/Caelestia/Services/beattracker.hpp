#pragma once

#include <qqmlintegration.h>

#include <vector>

typedef float smpl_t;   // Windows build: aubio is not used

#include "audioprovider.hpp"
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_SERVICES_API


namespace caelestia::shell::services {

class CAE_SERVICES_API BeatProcessor : public AudioProcessor {
    Q_OBJECT

public:
    explicit BeatProcessor(QObject* parent = nullptr);
    ~BeatProcessor() override;

signals:
    void beat(smpl_t bpm);

protected:
    void process() override;

private:
    std::vector<float> m_in;
    float m_avg = 0.0f;          // running mean of chunk energy
    qint64 m_lastBeatMs = 0;
    float m_bpm = 120.0f;
};

class CAE_SERVICES_API BeatTracker : public AudioProvider {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(smpl_t bpm READ bpm NOTIFY bpmChanged)

public:
    explicit BeatTracker(QObject* parent = nullptr);

    [[nodiscard]] smpl_t bpm() const;

signals:
    void bpmChanged();
    void beat(smpl_t bpm);

private:
    smpl_t m_bpm;

    void updateBpm(smpl_t bpm);
};

} // namespace caelestia::shell::services
