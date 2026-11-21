#pragma once

#include <memory>

#include <qobject.h>
#include <qrect.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qthread.h>
#include <qtmetamacros.h>

namespace qs::windows::recorder {

class RecordingJob;

struct RecordingRequest {
	QRect region;
	bool systemAudio = false;
	bool microphone = false;
	bool cursor = true;
	int fps = 30;
	QString path;
};

class RecorderController: public QObject {
	Q_OBJECT;

public:
	static RecorderController* instance();

	[[nodiscard]] bool recording() const { return this->job != nullptr; }
	[[nodiscard]] QString path() const;

	[[nodiscard]] bool available();
	[[nodiscard]] QString unavailableReason();

	bool start(const RecordingRequest& request, QString* error);
	void stop();

signals:
	void recordingChanged();
	void started(const QString& path);
	void finished(const QString& path);
	void failed(const QString& reason);
	void soundUnavailable(const QString& reason);
	void microphoneUnavailable(const QString& reason, bool blocked);

private:
	explicit RecorderController();
	~RecorderController() override = default;
	Q_DISABLE_COPY_MOVE(RecorderController);

	friend class RecordingJob;
	void onJobStarted(RecordingJob* job);
	void onJobSoundUnavailable(RecordingJob* job, const QString& reason);
	void onJobMicrophoneUnavailable(RecordingJob* job, const QString& reason, bool blocked);
	void onJobDone(RecordingJob* job, bool ok, const QString& reason);
	void shutdown();

	std::shared_ptr<RecordingJob> job;
	QThread* thread = nullptr;
	bool checked = false;
	QString mUnavailableReason;
};

} // namespace qs::windows::recorder
