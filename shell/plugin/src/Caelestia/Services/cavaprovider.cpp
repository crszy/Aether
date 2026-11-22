#include "cavaprovider.hpp"

#include <qloggingcategory.h>

#include <cmath>
#include <complex>
#include <vector>

#include <cstddef>

#include "audiocollector.hpp"
#include "audioprovider.hpp"

namespace {

Q_LOGGING_CATEGORY(lcCava, "caelestia.services.cava", QtInfoMsg)
Q_LOGGING_CATEGORY(lcCavaProcessor, "caelestia.services.cava.processor", QtInfoMsg)

// Hann window + radix-2 FFT of the latest chunk, magnitudes grouped into log-spaced bars (50 Hz - 10 kHz),
// auto-gained and smoothed like cava's noise reduction 0.85. Output 0..1.
void fftBars(const double* in, int count, double* out, int bars, std::vector<double>& smooth) {
    int n = 1;
    while (n * 2 <= count) n *= 2;
    if (n < 64 || bars <= 0) { for (int i = 0; i < bars; ++i) out[i] = 0; return; }
    std::vector<std::complex<double>> a(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) a[i] = in[count - n + i] * (0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1)));
    for (int i = 1, j = 0; i < n; ++i) { int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (int len = 2; len <= n; len <<= 1) {
        const std::complex<double> w = std::polar(1.0, -2 * M_PI / len);
        for (int i = 0; i < n; i += len) { std::complex<double> wn(1);
            for (int j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * wn; a[i + j] = u + v; a[i + j + len / 2] = u - v; wn *= w; } }
    }
    const double binHz = 44100.0 / n, lo = 50.0, hi = 10000.0;
    static double peak = 1e-3;
    double frameMax = 0;
    for (int b = 0; b < bars; ++b) {
        const double f0 = lo * std::pow(hi / lo, double(b) / bars), f1 = lo * std::pow(hi / lo, double(b + 1) / bars);
        int k0 = std::max(1, int(f0 / binHz)), k1 = std::max(k0 + 1, int(f1 / binHz));
        k1 = std::min(k1, n / 2);
        double m = 0;
        for (int k = k0; k < k1; ++k) m = std::max(m, std::abs(a[k]));
        out[b] = m;
        frameMax = std::max(frameMax, m);
    }
    peak = std::max(frameMax, peak * 0.995);
    for (int b = 0; b < bars; ++b) {
        const double v = std::min(1.0, out[b] / peak);
        smooth[b] = std::max(v, smooth[b] * 0.85);
        out[b] = smooth[b];
    }
}

} // namespace

namespace caelestia::shell::services {

CavaProcessor::CavaProcessor(QObject* parent)
    : AudioProcessor(parent)
    , m_plan(nullptr)
    , m_in(new double[ac::k_chunkSize])
    , m_out(nullptr)
    , m_bars(0) {};

CavaProcessor::~CavaProcessor() {
    cleanup();
    delete[] m_in;
}

void CavaProcessor::process() {
    if (!m_plan || m_bars == 0 || !m_out) {
        return;
    }

    const int count = static_cast<int>(AudioCollector::instance().readChunk(m_in));

    // Process in data via cava
    fftBars(m_in, count, m_out, m_bars, m_smooth);

    // Apply monstercat filter
    QVector<double> values(m_bars);

    // Left to right pass
    const double inv = 1.0 / 1.5;
    double carry = 0.0;
    for (int i = 0; i < m_bars; ++i) {
        carry = std::max(m_out[i], carry * inv);
        values[i] = carry;
    }

    // Right to left pass and combine
    carry = 0.0;
    for (int i = m_bars - 1; i >= 0; --i) {
        carry = std::max(m_out[i], carry * inv);
        values[i] = std::max(values[i], carry);
    }

    // Update values
    if (values != m_values) {
        m_values = std::move(values);
        emit valuesChanged(m_values);
    }
}

void CavaProcessor::setBars(int bars) {
    if (bars < 0) {
        qCWarning(lcCavaProcessor) << "setBars: bars must be greater than 0. Setting to 0.";
        bars = 0;
    }

    if (m_bars != bars) {
        m_bars = bars;
        reload();
    }
}

void CavaProcessor::reload() {
    cleanup();
    initCava();
}

void CavaProcessor::cleanup() {
    if (m_plan) {
        m_plan = nullptr;
    }

    if (m_out) {
        delete[] m_out;
        m_out = nullptr;
    }
}

void CavaProcessor::initCava() {
    if (m_plan || m_bars == 0) {
        return;
    }

    m_plan = this;   // marker: ready
    m_smooth.assign(static_cast<size_t>(m_bars), 0.0);
    m_out = new double[static_cast<size_t>(m_bars)];
}

CavaProvider::CavaProvider(QObject* parent)
    : AudioProvider(parent)
    , m_bars(0)
    , m_values(m_bars, 0.0) {
    m_processor = new CavaProcessor();
    init();

    connect(static_cast<CavaProcessor*>(m_processor), &CavaProcessor::valuesChanged, this, &CavaProvider::updateValues);
}

int CavaProvider::bars() const {
    return m_bars;
}

void CavaProvider::setBars(int bars) {
    if (bars < 0) {
        qCWarning(lcCava) << "setBars: bars must be greater than 0. Setting to 0.";
        bars = 0;
    }

    if (m_bars == bars) {
        return;
    }

    m_values.resize(bars, 0.0);
    m_bars = bars;
    emit barsChanged();
    emit valuesChanged();

    QMetaObject::invokeMethod(
        static_cast<CavaProcessor*>(m_processor), &CavaProcessor::setBars, Qt::QueuedConnection, bars);
}

QVector<double> CavaProvider::values() const {
    return m_values;
}

void CavaProvider::updateValues(const QVector<double>& values) {
    if (values != m_values) {
        m_values = values;
        emit valuesChanged();
    }
}

} // namespace caelestia::shell::services
