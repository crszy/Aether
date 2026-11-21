#include "spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <vector>

namespace qs::windows::visualizer {

namespace {

constexpr double BASS_WINDOW_SECONDS = 0.17;
constexpr size_t MIN_BASS_SIZE = 1024;
constexpr size_t MAX_BASS_SIZE = 65536;
constexpr size_t TREBLE_DIVISOR = 4;
constexpr double TREBLE_MIN_BINS = 3.0;
constexpr double WEIGHT_SCALE = 1.0 / 30.0;
constexpr float QUIET_LEVEL = 1e-7F;
constexpr double ZERO_SNAP = 1e-5;
constexpr double FALL_STEP = 0.028;
constexpr double GRAVITY_BASE = 1.54;
constexpr double GRAVITY_FRAMERATE = 60.0;
constexpr double GRAVITY_EXPONENT = 2.5;
constexpr double SENS_DOWN = 0.98;
constexpr double SENS_UP = 1.001;
constexpr double SENS_INIT_UP = 1.1;
constexpr double SENS_MIN = 1e-6;
constexpr double SENS_MAX = 1e9;

size_t nextPowerOfTwo(size_t value) {
	size_t result = 1;
	while (result < value) result <<= 1;
	return result;
}

std::complex<float> unitRoot(size_t k, size_t n) {
	auto angle = -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n);
	return {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
}

} // namespace

void RealFft::reset(size_t size) {
	if (size == this->mSize) return;
	this->mSize = size;
	auto half = size / 2;

	auto bits = 0;
	while ((size_t {1} << bits) < half) ++bits;

	this->reversed.resize(half);
	for (size_t i = 0; i < half; ++i) {
		size_t r = 0;
		for (auto b = 0; b < bits; ++b) {
			if (((i >> b) & 1) != 0) r |= size_t {1} << (bits - 1 - b);
		}
		this->reversed[i] = r;
	}

	this->halfTwiddles.resize(half / 2);
	for (size_t k = 0; k < half / 2; ++k) this->halfTwiddles[k] = unitRoot(k, half);

	this->splitTwiddles.resize(half + 1);
	for (size_t k = 0; k <= half; ++k) this->splitTwiddles[k] = unitRoot(k, size);

	this->work.resize(half);
}

void RealFft::transform(const std::vector<float>& input, std::vector<std::complex<float>>& output) {
	auto half = this->mSize / 2;
	if (half == 0) return;

	for (size_t i = 0; i < half; ++i) {
		this->work[this->reversed[i]] = {input[2 * i], input[2 * i + 1]};
	}

	for (size_t length = 2; length <= half; length <<= 1) {
		auto span = length / 2;
		auto step = half / length;
		for (size_t start = 0; start < half; start += length) {
			for (size_t j = 0; j < span; ++j) {
				auto& a = this->work[start + j];
				auto& b = this->work[start + j + span];
				auto t = b * this->halfTwiddles[j * step];
				b = a - t;
				a += t;
			}
		}
	}

	output.resize(half + 1);
	for (size_t k = 0; k <= half; ++k) {
		auto zk = this->work[k % half];
		auto zc = std::conj(this->work[(half - k) % half]);
		auto even = (zk + zc) * 0.5F;
		auto odd = (zk - zc) * std::complex<float>(0.0F, -0.5F);
		output[k] = even + this->splitTwiddles[k] * odd;
	}
}

Spectrum::Spectrum(const SpectrumSettings& settings, int sampleRate)
    : settings(settings)
    , rate(sampleRate) {
	this->rebuild();
	this->resetBars();
}

void Spectrum::setSettings(const SpectrumSettings& settings) {
	if (settings == this->settings) return;
	auto barsChanged = settings.bars != this->settings.bars;
	this->settings = settings;
	this->rebuild();
	if (barsChanged) this->resetBars();
}

void Spectrum::setSampleRate(int sampleRate) {
	if (sampleRate == this->rate) return;
	this->rate = sampleRate;
	this->rebuild();
}

void Spectrum::rebuild() {
	auto bars = static_cast<size_t>(std::max(1, this->settings.bars));
	auto rate = static_cast<double>(std::max(1, this->rate));

	auto bassSize = std::clamp(
	    nextPowerOfTwo(static_cast<size_t>(rate * BASS_WINDOW_SECONDS)),
	    MIN_BASS_SIZE,
	    MAX_BASS_SIZE
	);

	if (this->ring.size() != bassSize) {
		this->ring.assign(bassSize, 0.0F);
		this->ringPos = 0;
		this->quietSamples = bassSize;
	}

	auto setup = [rate](Analysis& analysis, size_t size) {
		analysis.fft.reset(size);
		analysis.window.resize(size);
		analysis.frame.resize(size);

		double sumSquares = 0.0;
		for (size_t i = 0; i < size; ++i) {
			auto phase = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(size);
			auto w = 0.5 - 0.5 * std::cos(phase);
			analysis.window[i] = static_cast<float>(w);
			sumSquares += w * w;
		}

		analysis.powerScale = 2.0 / (static_cast<double>(size) * sumSquares);
		analysis.binHz = rate / static_cast<double>(size);
	};

	setup(this->bass, bassSize);
	setup(this->treble, bassSize / TREBLE_DIVISOR);

	auto nyquist = rate / 2.0;
	auto lower = std::clamp(this->settings.lowerCutoff, 1.0, nyquist / 2.0);
	auto upper = std::min(this->settings.upperCutoff, nyquist);
	if (upper <= lower) upper = std::min(lower * 2.0, nyquist);

	std::vector<double> edges(bars + 1);
	for (size_t k = 0; k <= bars; ++k) {
		edges[k] =
		    lower * std::pow(upper / lower, static_cast<double>(k) / static_cast<double>(bars));
	}

	auto split = bars;
	for (size_t n = 0; n < bars; ++n) {
		if (edges[n + 1] - edges[n] >= TREBLE_MIN_BINS * this->treble.binHz) {
			split = n;
			break;
		}
	}

	this->bands.assign(bars, Band());
	this->mapBands(0, split, edges, false);
	this->mapBands(split, bars, edges, true);
	this->bass.used = split > 0;
	this->treble.used = split < bars;

	auto reduction = this->settings.noiseReduction;
	if (reduction > 0.0) {
		auto framerate = static_cast<double>(std::max(1, this->settings.framerate));
		this->gravity = std::max(
		    1.0,
		    std::pow(GRAVITY_FRAMERATE / framerate, GRAVITY_EXPONENT) * GRAVITY_BASE / reduction
		);
	} else {
		this->gravity = 0.0;
	}
}

void Spectrum::mapBands(size_t first, size_t last, const std::vector<double>& edges, bool treble) {
	if (first >= last) return;

	const auto& analysis = treble ? this->treble : this->bass;
	auto top = analysis.fft.size() / 2;

	std::vector<size_t> index(last - first + 1);
	for (auto k = first; k <= last; ++k) {
		auto i = static_cast<size_t>(std::floor(edges[k] / analysis.binHz));
		if (k > first) i = std::max(i, index[k - first - 1] + 1);
		index[k - first] = std::min(i, top);
	}

	for (auto n = first; n < last; ++n) {
		auto lo = index[n - first];
		auto next = index[n - first + 1];
		auto hi = next > lo ? next - 1 : lo;

		auto center = std::max(0.5 * static_cast<double>(lo + hi) * analysis.binHz, analysis.binHz);
		auto width = static_cast<double>(hi - lo + 1) * analysis.binHz;

		this->bands[n] = Band {
		    .treble = treble,
		    .lo = lo,
		    .hi = hi,
		    .scale = center * WEIGHT_SCALE / std::sqrt(width),
		};
	}
}

void Spectrum::resetBars() {
	auto bars = this->bands.size();
	this->peak.assign(bars, 0.0);
	this->fall.assign(bars, 0.0);
	this->previous.assign(bars, 0.0);
	this->memory.assign(bars, 0.0);
	this->sensitivity = 1.0;
	this->sensitivityInit = true;
}

void Spectrum::push(const float* samples, size_t count) {
	auto mask = this->ring.size() - 1;

	for (size_t i = 0; i < count; ++i) {
		auto sample = samples[i];
		this->ring[this->ringPos] = sample;
		this->ringPos = (this->ringPos + 1) & mask;

		if (std::abs(sample) > QUIET_LEVEL) {
			this->quietSamples = 0;
			this->heardAudio = true;
		} else if (this->quietSamples < this->ring.size()) {
			++this->quietSamples;
		}
	}
}

void Spectrum::pushSilence(size_t count) {
	auto size = this->ring.size();
	auto mask = size - 1;

	for (size_t i = 0; i < std::min(count, size); ++i) {
		this->ring[this->ringPos] = 0.0F;
		this->ringPos = (this->ringPos + 1) & mask;
	}

	this->quietSamples = std::min(size, this->quietSamples + count);
}

bool Spectrum::idle() const {
	auto zero = [](double v) { return v == 0.0; };
	return this->quietSamples >= this->ring.size() && std::ranges::all_of(this->previous, zero)
	    && std::ranges::all_of(this->memory, zero);
}

void Spectrum::analyze(Analysis& analysis) {
	auto size = analysis.fft.size();
	auto mask = this->ring.size() - 1;
	auto start = (this->ringPos + this->ring.size() - size) & mask;

	for (size_t i = 0; i < size; ++i) {
		analysis.frame[i] = this->ring[(start + i) & mask] * analysis.window[i];
	}

	analysis.fft.transform(analysis.frame, analysis.bins);
}

bool Spectrum::update(std::vector<double>& out) {
	auto bars = this->bands.size();
	out.assign(bars, 0.0);

	if (this->idle()) {
		this->heardAudio = false;
		return false;
	}

	if (this->bass.used) this->analyze(this->bass);
	if (this->treble.used) this->analyze(this->treble);

	auto reduction = this->settings.noiseReduction;
	auto overshoot = false;

	for (size_t n = 0; n < bars; ++n) {
		const auto& band = this->bands[n];
		const auto& analysis = band.treble ? this->treble : this->bass;

		double power = 0.0;
		for (auto k = band.lo; k <= band.hi; ++k) power += std::norm(analysis.bins[k]);

		auto value = std::sqrt(power * analysis.powerScale) * band.scale * this->sensitivity;

		if (this->gravity > 0.0) {
			if (value < this->previous[n]) {
				auto drop = this->fall[n] * this->fall[n] * this->gravity;
				value = std::max(0.0, this->peak[n] * (1.0 - drop));
				this->fall[n] += FALL_STEP;
			} else {
				this->peak[n] = value;
				this->fall[n] = 0.0;
			}
			this->previous[n] = value;
		}

		value += this->memory[n] * reduction;
		if (value < ZERO_SNAP) value = 0.0;
		this->memory[n] = value;

		if (value > 1.0) overshoot = true;
		out[n] = std::min(value, 1.0);
	}

	if (overshoot) {
		this->sensitivity *= SENS_DOWN;
		this->sensitivityInit = false;
	} else if (this->heardAudio) {
		this->sensitivity *= SENS_UP;
		if (this->sensitivityInit) this->sensitivity *= SENS_INIT_UP;
	}

	this->sensitivity = std::clamp(this->sensitivity, SENS_MIN, SENS_MAX);
	this->heardAudio = false;
	return true;
}

} // namespace qs::windows::visualizer
