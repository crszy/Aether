#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#include <qlist.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qt_windows.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../visualizer/spectrum.hpp"

namespace qs::windows::sys {

class AudioVisualizer: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	// clang-format off
	Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged);
	Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged);
	Q_PROPERTY(int bars READ bars WRITE setBars NOTIFY barsChanged);
	Q_PROPERTY(int framerate READ framerate WRITE setFramerate NOTIFY framerateChanged);
	Q_PROPERTY(qreal lowerCutoff READ lowerCutoff WRITE setLowerCutoff NOTIFY lowerCutoffChanged);
	Q_PROPERTY(qreal upperCutoff READ upperCutoff WRITE setUpperCutoff NOTIFY upperCutoffChanged);
	Q_PROPERTY(int noiseReduction READ noiseReduction WRITE setNoiseReduction NOTIFY noiseReductionChanged);
	Q_PROPERTY(int maxValue READ maxValue WRITE setMaxValue NOTIFY maxValueChanged);
	Q_PROPERTY(QList<qreal> values READ values NOTIFY valuesChanged);
	// clang-format on

public:
	explicit AudioVisualizer(QObject* parent = nullptr);
	~AudioVisualizer() override;
	Q_DISABLE_COPY_MOVE(AudioVisualizer);

	[[nodiscard]] bool running() const { return this->mRunning; }
	void setRunning(bool running);

	[[nodiscard]] bool capturing() const { return this->mCapturing; }

	[[nodiscard]] int bars() const { return this->mBars; }
	void setBars(int bars);

	[[nodiscard]] int framerate() const { return this->mFramerate; }
	void setFramerate(int framerate);

	[[nodiscard]] qreal lowerCutoff() const { return this->mLowerCutoff; }
	void setLowerCutoff(qreal cutoff);

	[[nodiscard]] qreal upperCutoff() const { return this->mUpperCutoff; }
	void setUpperCutoff(qreal cutoff);

	[[nodiscard]] int noiseReduction() const { return this->mNoiseReduction; }
	void setNoiseReduction(int noiseReduction);

	[[nodiscard]] int maxValue() const { return this->mMaxValue; }
	void setMaxValue(int maxValue);

	[[nodiscard]] QList<qreal> values() const { return this->mValues; }

signals:
	void runningChanged();
	void capturingChanged();
	void barsChanged();
	void framerateChanged();
	void lowerCutoffChanged();
	void upperCutoffChanged();
	void noiseReductionChanged();
	void maxValueChanged();
	void valuesChanged();

private:
	struct Settings {
		visualizer::SpectrumSettings spectrum;
		int maxValue = 1000;
	};

	void start();
	void stop();
	void joinWorker();
	void storeSettings();
	[[nodiscard]] Settings loadSettings();
	void run(quint64 generation);
	void publish(quint64 generation, const QList<qreal>& values);
	void applyFrame();
	void reportCapturing(quint64 generation, bool capturing);
	void setCapturing(bool capturing);

	bool mRunning = false;
	bool mCapturing = false;
	int mBars = 50;
	int mFramerate = 60;
	qreal mLowerCutoff = 50.0;
	qreal mUpperCutoff = 10000.0;
	int mNoiseReduction = 20;
	int mMaxValue = 1000;
	QList<qreal> mValues;

	std::thread worker;
	HANDLE stopEvent = nullptr;
	quint64 generation = 0;

	std::mutex mutex;
	Settings settings;
	std::atomic<quint64> settingsSerial {0};
	QList<qreal> pendingValues;
	quint64 pendingGeneration = 0;
	bool pendingFresh = false;
	std::atomic<bool> framePosted {false};
};

} // namespace qs::windows::sys
