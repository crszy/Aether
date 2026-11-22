#include "audio_dsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

namespace qs::windows::recorder {

namespace {

constexpr float HALF_POWER = 0.70710678F;

namespace speaker {
constexpr std::uint32_t FrontLeft = 0x1;
constexpr std::uint32_t FrontRight = 0x2;
constexpr std::uint32_t FrontCenter = 0x4;
constexpr std::uint32_t BackLeft = 0x10;
constexpr std::uint32_t BackRight = 0x20;
constexpr std::uint32_t FrontLeftOfCenter = 0x40;
constexpr std::uint32_t FrontRightOfCenter = 0x80;
constexpr std::uint32_t BackCenter = 0x100;
constexpr std::uint32_t SideLeft = 0x200;
constexpr std::uint32_t SideRight = 0x400;
constexpr std::uint32_t TopCenter = 0x800;
constexpr std::uint32_t TopFrontLeft = 0x1000;
constexpr std::uint32_t TopFrontCenter = 0x2000;
constexpr std::uint32_t TopFrontRight = 0x4000;
constexpr std::uint32_t TopBackLeft = 0x8000;
constexpr std::uint32_t TopBackCenter = 0x10000;
constexpr std::uint32_t TopBackRight = 0x20000;
} // namespace speaker

constexpr int BASE_HALF_WIDTH = 12;
constexpr int PHASES = 256;
constexpr double KAISER_BETA = 7.0;
constexpr double CUTOFF = 0.475;

constexpr double SNAP_SECONDS = 0.05;
constexpr double FILTER_SECONDS = 0.5;
constexpr double PROPORTIONAL_GAIN = 0.5;
constexpr double INTEGRAL_GAIN = 0.0625;
constexpr double MAX_CORRECTION = 0.005;

constexpr double LIMITER_LOOKAHEAD_SECONDS = 0.0015;
constexpr double LIMITER_RELEASE_SECONDS = 0.2;

std::array<float, 2> speakerWeights(std::uint32_t bit) {
	using namespace speaker;

	switch (bit) {
	case FrontLeft: return {1.0F, 0.0F};
	case FrontRight: return {0.0F, 1.0F};
	case FrontCenter:
	case BackCenter:
	case TopCenter:
	case TopFrontCenter:
	case TopBackCenter: return {HALF_POWER, HALF_POWER};
	case BackLeft:
	case SideLeft:
	case FrontLeftOfCenter:
	case TopFrontLeft:
	case TopBackLeft: return {HALF_POWER, 0.0F};
	case BackRight:
	case SideRight:
	case FrontRightOfCenter:
	case TopFrontRight:
	case TopBackRight: return {0.0F, HALF_POWER};
	default: return {0.0F, 0.0F};
	}
}

float decodeSample(const std::uint8_t* p, SampleType type) {
	switch (type) {
	case SampleType::Float32: {
		float v = 0;
		std::memcpy(&v, p, sizeof(v));
		return v;
	}
	case SampleType::Int16: {
		std::int16_t v = 0;
		std::memcpy(&v, p, sizeof(v));
		return static_cast<float>(v) / 32768.0F;
	}
	case SampleType::Int24: {
		auto v = static_cast<std::int32_t>(
		    static_cast<std::uint32_t>(p[0]) << 8 | static_cast<std::uint32_t>(p[1]) << 16
		    | static_cast<std::uint32_t>(p[2]) << 24
		);
		return static_cast<float>(v >> 8) / 8388608.0F;
	}
	case SampleType::Int32: {
		std::int32_t v = 0;
		std::memcpy(&v, p, sizeof(v));
		return static_cast<float>(v) / 2147483648.0F;
	}
	}
	return 0.0F;
}

double besselI0(double x) {
	auto sum = 1.0;
	auto term = 1.0;
	auto half = x / 2.0;

	for (auto k = 1; k < 64; ++k) {
		auto factor = half / k;
		term *= factor * factor;
		sum += term;
		if (term < sum * 1e-12) break;
	}

	return sum;
}

std::int16_t toInt16(float sample) {
	return static_cast<std::int16_t>(std::lrintf(std::clamp(sample, -1.0F, 1.0F) * 32767.0F));
}

} // namespace

StereoWeights stereoWeights(int channels, std::uint32_t mask) {
	StereoWeights weights(static_cast<std::size_t>(std::max(channels, 0)), {0.0F, 0.0F});
	if (channels <= 0) return weights;

	if (channels == 1) {
		weights[0] = {1.0F, 1.0F};
		return weights;
	}

	if (mask == 0 || channels == 2) {
		weights[0] = {1.0F, 0.0F};
		weights[1] = {0.0F, 1.0F};
		return weights;
	}

	auto channel = 0;
	for (std::uint32_t bit = 1; bit != 0 && channel < channels; bit <<= 1) {
		if ((mask & bit) == 0) continue;
		weights[static_cast<std::size_t>(channel++)] = speakerWeights(bit);
	}

	std::array<float, 2> sums {};
	for (const auto& w: weights) {
		sums[0] += w[0];
		sums[1] += w[1];
	}
	for (auto& w: weights) {
		if (sums[0] > 1.0F) w[0] /= sums[0];
		if (sums[1] > 1.0F) w[1] /= sums[1];
	}

	return weights;
}

void convertToStereo(
    const std::uint8_t* data,
    std::size_t frames,
    SampleType type,
    int bytesPerSample,
    const StereoWeights& weights,
    std::vector<float>& out
) {
	out.assign(frames * 2, 0.0F);
	if (data == nullptr || weights.empty() || bytesPerSample <= 0) return;

	auto channels = weights.size();
	auto blockAlign = channels * static_cast<std::size_t>(bytesPerSample);

	for (std::size_t f = 0; f < frames; ++f) {
		const auto* frame = data + f * blockAlign;
		auto left = 0.0F;
		auto right = 0.0F;

		for (std::size_t c = 0; c < channels; ++c) {
			auto sample = decodeSample(frame + c * static_cast<std::size_t>(bytesPerSample), type);
			left += sample * weights[c][0];
			right += sample * weights[c][1];
		}

		out[f * 2] = left;
		out[f * 2 + 1] = right;
	}
}

void Resampler::configure(int inRate, int outRate) {
	this->nominalStep = static_cast<double>(inRate) / outRate;
	this->mStep = this->nominalStep;

	auto scale = std::max(1.0, this->nominalStep);
	this->halfWidth = static_cast<int>(std::ceil(BASE_HALF_WIDTH * scale));
	this->taps = this->halfWidth * 2;
	this->buildTable();
	this->reset();
}

void Resampler::buildTable() {
	auto scale = std::max(1.0, this->nominalStep);
	auto cutoff = CUTOFF / scale;
	auto width = static_cast<double>(this->halfWidth);
	auto norm = besselI0(KAISER_BETA);
	auto tapCount = static_cast<std::size_t>(this->taps);

	this->table.assign(static_cast<std::size_t>(PHASES + 1) * tapCount, 0.0F);
	std::vector<double> row(tapCount);

	for (auto j = 0; j <= PHASES; ++j) {
		auto phase = static_cast<double>(j) / PHASES;
		auto total = 0.0;

		for (std::size_t m = 0; m < tapCount; ++m) {
			auto t = static_cast<double>(m) - width + 1.0 - phase;
			auto u = t / width;
			auto value = 0.0;

			if (std::abs(u) < 1.0) {
				auto x = 2.0 * cutoff * t;
				auto sinc = x == 0.0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
				value = 2.0 * cutoff * sinc * besselI0(KAISER_BETA * std::sqrt(1.0 - u * u)) / norm;
			}

			row[m] = value;
			total += value;
		}

		auto* dest = &this->table[static_cast<std::size_t>(j) * tapCount];
		for (std::size_t m = 0; m < tapCount; ++m) dest[m] = static_cast<float>(row[m] / total);
	}

	this->coefficients.assign(tapCount, 0.0F);
}

void Resampler::reset(double delay, const float* before, std::size_t beforeFrames) {
	auto history = static_cast<std::size_t>(this->halfWidth - 1);
	this->left.assign(history, 0.0F);
	this->right.assign(history, 0.0F);

	auto kept = std::min(history, beforeFrames);
	const auto* from = before + (beforeFrames - kept) * 2;
	for (std::size_t f = 0; f < kept; ++f) {
		this->left[history - kept + f] = from[f * 2];
		this->right[history - kept + f] = from[f * 2 + 1];
	}

	this->pos = static_cast<double>(history) + std::max(delay, 0.0);
}

void Resampler::setCorrection(double correction) {
	this->mStep = this->nominalStep * (1.0 + correction);
}

double Resampler::lookahead() const { return static_cast<double>(this->left.size()) - this->pos; }

void Resampler::process(const float* stereo, std::size_t frames, std::vector<float>& out) {
	for (std::size_t f = 0; f < frames; ++f) {
		this->left.push_back(stereo[f * 2]);
		this->right.push_back(stereo[f * 2 + 1]);
	}

	auto available = static_cast<std::int64_t>(this->left.size());
	auto tapCount = static_cast<std::size_t>(this->taps);
	out.reserve(out.size() + (static_cast<std::size_t>(static_cast<double>(frames) / this->mStep) + 2) * 2);

	while (true) {
		auto i = static_cast<std::int64_t>(this->pos);
		if (i + this->halfWidth >= available) break;

		auto phase = (this->pos - static_cast<double>(i)) * PHASES;
		auto j = std::min(static_cast<int>(phase), PHASES - 1);
		auto t = static_cast<float>(phase - j);

		const auto* a = &this->table[static_cast<std::size_t>(j) * tapCount];
		const auto* b = a + tapCount;
		auto* c = this->coefficients.data();
		for (std::size_t m = 0; m < tapCount; ++m) c[m] = a[m] + (b[m] - a[m]) * t;

		auto base = static_cast<std::size_t>(i - this->halfWidth + 1);
		const auto* l = &this->left[base];
		const auto* r = &this->right[base];
		auto sumLeft = 0.0F;
		auto sumRight = 0.0F;
		for (std::size_t m = 0; m < tapCount; ++m) {
			sumLeft += c[m] * l[m];
			sumRight += c[m] * r[m];
		}

		out.push_back(sumLeft);
		out.push_back(sumRight);
		this->pos += this->mStep;
	}

	auto drop = static_cast<std::int64_t>(this->pos) - this->halfWidth + 1;
	if (drop > 0) {
		drop = std::min(drop, static_cast<std::int64_t>(this->left.size()));
		this->left.erase(this->left.begin(), this->left.begin() + drop);
		this->right.erase(this->right.begin(), this->right.begin() + drop);
		this->pos -= static_cast<double>(drop);
	}
}

void Resampler::flush(std::vector<float>& out) {
	std::vector<float> zeros(static_cast<std::size_t>(this->halfWidth) * 2, 0.0F);
	this->process(zeros.data(), static_cast<std::size_t>(this->halfWidth), out);
}

void AlignedStream::configure(int inRate, int outRate) {
	this->inRate = inRate;
	this->outRate = outRate;
	this->nominalStep = static_cast<double>(inRate) / outRate;
	this->snapFrames = std::llround(SNAP_SECONDS * outRate);
	this->resampler.configure(inRate, outRate);

	this->anchored = false;
	this->mWritten = 0;
	this->filteredError = 0.0;
	this->integral = 0.0;
	this->mCorrection = 0.0;
	this->mLastError = 0.0;
	this->mSnaps = 0;
}

void AlignedStream::push(
    const float* stereo,
    std::size_t frames,
    std::optional<double> position,
    std::vector<float>& out
) {
	if (frames == 0 || this->outRate <= 0) return;

	if (position) {
		auto at = *position;

		if (this->anchored) {
			auto landing = static_cast<double>(this->mWritten)
			             + this->resampler.lookahead() / this->resampler.step();
			auto error = at - landing;
			this->mLastError = error;

			if (std::abs(error) > static_cast<double>(this->snapFrames)) {
				this->unanchor(error > 0, out);
				this->mSnaps++;
			} else {
				auto seconds = static_cast<double>(frames) / this->inRate;
				auto weight = std::min(1.0, seconds / FILTER_SECONDS);
				this->filteredError += (error / this->outRate - this->filteredError) * weight;
				this->integral = std::clamp(
				    this->integral - INTEGRAL_GAIN * this->filteredError * seconds,
				    -MAX_CORRECTION,
				    MAX_CORRECTION
				);
				this->mCorrection = std::clamp(
				    this->integral - PROPORTIONAL_GAIN * this->filteredError,
				    -MAX_CORRECTION,
				    MAX_CORRECTION
				);
				this->resampler.setCorrection(this->mCorrection);
			}
		}

		if (!this->anchored) {
			this->resampler.setCorrection(this->mCorrection);
			auto step = this->resampler.step();
			auto written = static_cast<double>(this->mWritten);

			const float* before = nullptr;
			std::size_t skip = 0;

			if (at < written) {
				skip = static_cast<std::size_t>(std::ceil((written - at) * step));
				if (skip >= frames) return;
				before = stereo;
				stereo += skip * 2;
				frames -= skip;
				at += static_cast<double>(skip) / step;
			}

			auto target = static_cast<std::int64_t>(std::ceil(at));
			if (target > this->mWritten) this->outputSilence(target - this->mWritten, out);

			this->resampler.reset((static_cast<double>(target) - at) * step, before, skip);
			this->filteredError = 0.0;
			this->anchored = true;
		}
	}

	this->output(stereo, frames, out);
}

void AlignedStream::padTo(std::int64_t frame, std::vector<float>& out) {
	if (frame <= this->mWritten) return;

	this->unanchor(true, out);
	if (frame > this->mWritten) this->outputSilence(frame - this->mWritten, out);
}

void AlignedStream::output(const float* stereo, std::size_t frames, std::vector<float>& out) {
	auto before = out.size();
	this->resampler.process(stereo, frames, out);
	this->mWritten += static_cast<std::int64_t>((out.size() - before) / 2);
}

void AlignedStream::outputSilence(std::int64_t frames, std::vector<float>& out) {
	if (frames <= 0) return;
	out.insert(out.end(), static_cast<std::size_t>(frames) * 2, 0.0F);
	this->mWritten += frames;
}

void AlignedStream::unanchor(bool flush, std::vector<float>& out) {
	if (flush && this->anchored) {
		auto before = out.size();
		this->resampler.flush(out);
		this->mWritten += static_cast<std::int64_t>((out.size() - before) / 2);
	}

	this->anchored = false;
}

void FrameQueue::append(const std::vector<float>& stereo) {
	if (stereo.size() < 2) return;

	std::lock_guard lock(this->mutex);

	if (this->readPos > 0 && this->readPos * 2 >= this->data.size()) {
		this->data.erase(
		    this->data.begin(),
		    this->data.begin() + static_cast<std::ptrdiff_t>(this->readPos)
		);
		this->readPos = 0;
	}

	this->data.insert(this->data.end(), stereo.begin(), stereo.end());
	this->mTotal += static_cast<std::int64_t>(stereo.size() / 2);
}

std::int64_t FrameQueue::total() {
	std::lock_guard lock(this->mutex);
	return this->mTotal;
}

std::size_t FrameQueue::read(float* out, std::size_t frames) {
	std::size_t count = 0;

	{
		std::lock_guard lock(this->mutex);
		auto available = (this->data.size() - this->readPos) / 2;
		count = std::min(frames, available);

		if (count > 0) {
			std::memcpy(out, this->data.data() + this->readPos, count * 2 * sizeof(float));
			this->readPos += count * 2;
		}

		if (this->readPos == this->data.size()) {
			this->data.clear();
			this->readPos = 0;
		}
	}

	if (count < frames) std::fill(out + count * 2, out + frames * 2, 0.0F);
	return count;
}

void PeakLimiter::configure(int rate) {
	this->window = static_cast<std::size_t>(std::max(1L, std::lround(rate * LIMITER_LOOKAHEAD_SECONDS)));
	this->releaseCoefficient = 1.0 - std::exp(-1.0 / (rate * LIMITER_RELEASE_SECONDS));
	this->primed = 0;
	this->minimum.clear();
	this->index = 0;
	this->released = 1.0F;
	this->averageRing.assign(this->window, 1.0F);
	this->averagePos = 0;
	this->averageSum = static_cast<double>(this->window);
	this->delayRing.assign(this->window * 2, 0.0F);
	this->delayPos = 0;
}

void PeakLimiter::pushFrame(float left, float right, std::vector<float>& out) {
	auto peak = std::max(std::abs(left), std::abs(right));
	auto required = peak > THRESHOLD ? THRESHOLD / peak : 1.0F;

	while (!this->minimum.empty() && this->minimum.back().second >= required) {
		this->minimum.pop_back();
	}
	this->minimum.emplace_back(this->index, required);
	if (this->minimum.front().first + this->window <= this->index) this->minimum.pop_front();
	auto lowest = this->minimum.front().second;

	if (lowest < this->released) this->released = lowest;
	else {
		this->released += static_cast<float>((lowest - this->released) * this->releaseCoefficient);
	}

	this->averageSum += static_cast<double>(this->released) - this->averageRing[this->averagePos];
	this->averageRing[this->averagePos] = this->released;
	this->averagePos = (this->averagePos + 1) % this->window;
	if (this->averagePos == 0) {
		this->averageSum = 0.0;
		for (auto value: this->averageRing) this->averageSum += value;
	}
	auto gain = std::min(1.0F, static_cast<float>(this->averageSum / static_cast<double>(this->window)));

	this->delayRing[this->delayPos * 2] = left;
	this->delayRing[this->delayPos * 2 + 1] = right;
	auto oldest = (this->delayPos + 1) % this->window;
	this->delayPos = oldest;
	this->index++;

	if (this->primed + 1 < this->window) {
		this->primed++;
		return;
	}

	out.push_back(this->delayRing[oldest * 2] * gain);
	out.push_back(this->delayRing[oldest * 2 + 1] * gain);
}

void PeakLimiter::process(const float* stereo, std::size_t frames, std::vector<float>& out) {
	out.reserve(out.size() + frames * 2);
	for (std::size_t f = 0; f < frames; ++f) this->pushFrame(stereo[f * 2], stereo[f * 2 + 1], out);
}

void PeakLimiter::flush(std::vector<float>& out) {
	for (std::size_t f = 0; f + 1 < this->window; ++f) this->pushFrame(0.0F, 0.0F, out);
	this->primed = 0;
}

AudioMixer::AudioMixer(int rate) { this->limiter.configure(rate); }

void AudioMixer::mix(bool drain, std::vector<std::int16_t>& out) {
	out.clear();
	if (this->sources.empty()) return;

	auto target = drain ? std::numeric_limits<std::int64_t>::min()
	                    : std::numeric_limits<std::int64_t>::max();
	for (auto* source: this->sources) {
		auto total = source->total();
		target = drain ? std::max(target, total) : std::min(target, total);
	}

	this->limited.clear();
	auto count = target - this->mMixed;

	if (count > 0) {
		auto frames = static_cast<std::size_t>(count);
		this->sum.assign(frames * 2, 0.0F);
		this->scratch.resize(frames * 2);

		for (auto* source: this->sources) {
			source->read(this->scratch.data(), frames);
			for (std::size_t i = 0; i < frames * 2; ++i) this->sum[i] += this->scratch[i];
		}

		this->limiter.process(this->sum.data(), frames, this->limited);
		this->mMixed = target;
	}

	if (drain) this->limiter.flush(this->limited);

	out.resize(this->limited.size());
	std::ranges::transform(this->limited, out.begin(), toInt16);
}

} // namespace qs::windows::recorder
