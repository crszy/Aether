#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace qs::windows::recorder {

enum class SampleType : std::uint8_t { Float32, Int16, Int24, Int32 };

using StereoWeights = std::vector<std::array<float, 2>>;

StereoWeights stereoWeights(int channels, std::uint32_t mask);

void convertToStereo(
    const std::uint8_t* data,
    std::size_t frames,
    SampleType type,
    int bytesPerSample,
    const StereoWeights& weights,
    std::vector<float>& out
);

class Resampler {
public:
	void configure(int inRate, int outRate);
	void reset(double delay = 0.0, const float* before = nullptr, std::size_t beforeFrames = 0);
	void setCorrection(double correction);
	void process(const float* stereo, std::size_t frames, std::vector<float>& out);
	void flush(std::vector<float>& out);

	[[nodiscard]] double step() const { return this->mStep; }
	[[nodiscard]] double lookahead() const;

private:
	void buildTable();

	int halfWidth = 0;
	int taps = 0;
	double nominalStep = 1.0;
	double mStep = 1.0;
	std::vector<float> table;

	std::vector<float> left;
	std::vector<float> right;
	double pos = 0.0;
	std::vector<float> coefficients;
};

class AlignedStream {
public:
	void configure(int inRate, int outRate);

	void push(
	    const float* stereo,
	    std::size_t frames,
	    std::optional<double> position,
	    std::vector<float>& out
	);
	void padTo(std::int64_t frame, std::vector<float>& out);

	[[nodiscard]] std::int64_t written() const { return this->mWritten; }
	[[nodiscard]] double correction() const { return this->mCorrection; }
	[[nodiscard]] std::int64_t snaps() const { return this->mSnaps; }
	[[nodiscard]] double lastError() const { return this->mLastError; }

private:
	void output(const float* stereo, std::size_t frames, std::vector<float>& out);
	void outputSilence(std::int64_t frames, std::vector<float>& out);
	void unanchor(bool flush, std::vector<float>& out);

	Resampler resampler;
	int inRate = 0;
	int outRate = 0;
	double nominalStep = 1.0;
	std::int64_t snapFrames = 0;

	bool anchored = false;
	std::int64_t mWritten = 0;
	double filteredError = 0.0;
	double integral = 0.0;
	double mCorrection = 0.0;
	double mLastError = 0.0;
	std::int64_t mSnaps = 0;
	std::vector<float> scratch;
};

class FrameQueue {
public:
	void append(const std::vector<float>& stereo);
	[[nodiscard]] std::int64_t total();
	std::size_t read(float* out, std::size_t frames);

private:
	std::mutex mutex;
	std::vector<float> data;
	std::size_t readPos = 0;
	std::int64_t mTotal = 0;
};

class PeakLimiter {
public:
	void configure(int rate);
	void process(const float* stereo, std::size_t frames, std::vector<float>& out);
	void flush(std::vector<float>& out);

	static constexpr float THRESHOLD = 0.944F;

private:
	void pushFrame(float left, float right, std::vector<float>& out);

	std::size_t window = 1;
	double releaseCoefficient = 0.0;
	std::size_t primed = 0;

	std::deque<std::pair<std::uint64_t, float>> minimum;
	std::uint64_t index = 0;
	float released = 1.0F;

	std::vector<float> averageRing;
	std::size_t averagePos = 0;
	double averageSum = 0.0;

	std::vector<float> delayRing;
	std::size_t delayPos = 0;
};

class AudioMixer {
public:
	explicit AudioMixer(int rate);

	void addSource(FrameQueue* queue) { this->sources.push_back(queue); }
	void mix(bool drain, std::vector<std::int16_t>& out);

	[[nodiscard]] std::int64_t mixed() const { return this->mMixed; }

private:
	std::vector<FrameQueue*> sources;
	std::int64_t mMixed = 0;
	PeakLimiter limiter;
	std::vector<float> sum;
	std::vector<float> scratch;
	std::vector<float> limited;
};

} // namespace qs::windows::recorder
