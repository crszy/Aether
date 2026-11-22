#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace qs::windows::visualizer {

class RealFft {
public:
	void reset(size_t size);
	void transform(const std::vector<float>& input, std::vector<std::complex<float>>& output);
	[[nodiscard]] size_t size() const { return this->mSize; }

private:
	size_t mSize = 0;
	std::vector<size_t> reversed;
	std::vector<std::complex<float>> halfTwiddles;
	std::vector<std::complex<float>> splitTwiddles;
	std::vector<std::complex<float>> work;
};

struct SpectrumSettings {
	int bars = 50;
	int framerate = 60;
	double lowerCutoff = 50.0;
	double upperCutoff = 10000.0;
	double noiseReduction = 0.2;

	bool operator==(const SpectrumSettings&) const = default;
};

class Spectrum {
public:
	Spectrum(const SpectrumSettings& settings, int sampleRate);

	void setSettings(const SpectrumSettings& settings);
	void setSampleRate(int sampleRate);
	[[nodiscard]] int sampleRate() const { return this->rate; }

	void push(const float* samples, size_t count);
	void pushSilence(size_t count);

	bool update(std::vector<double>& out);
	[[nodiscard]] bool idle() const;

private:
	struct Band {
		bool treble = false;
		size_t lo = 0;
		size_t hi = 0;
		double scale = 0.0;
	};

	struct Analysis {
		RealFft fft;
		std::vector<float> window;
		std::vector<float> frame;
		std::vector<std::complex<float>> bins;
		double binHz = 0.0;
		double powerScale = 0.0;
		bool used = false;
	};

	void rebuild();
	void resetBars();
	void mapBands(size_t first, size_t last, const std::vector<double>& edges, bool treble);
	void analyze(Analysis& analysis);

	SpectrumSettings settings;
	int rate = 0;
	double gravity = 0.0;

	std::vector<float> ring;
	size_t ringPos = 0;
	size_t quietSamples = 0;
	bool heardAudio = false;

	Analysis bass;
	Analysis treble;
	std::vector<Band> bands;

	std::vector<double> peak;
	std::vector<double> fall;
	std::vector<double> previous;
	std::vector<double> memory;
	double sensitivity = 1.0;
	bool sensitivityInit = true;
};

} // namespace qs::windows::visualizer
