#include "screen_recorder.hpp"

#include <qdatetime.h>
#include <qdir.h>
#include <qguiapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qrect.h>
#include <qscreen.h>
#include <qstandardpaths.h>
#include <qstring.h>
#include <qurl.h>

#include "../recorder/audio_capture.hpp"
#include "../recorder/recording.hpp"
#include "../util.hpp"

namespace qs::windows::sys {

namespace {
Q_LOGGING_CATEGORY(logScreenRecorder, "quickshell.windows.recorder", QtWarningMsg);

constexpr int MIN_SIZE = 16;

using recorder::RecorderController;

} // namespace

ScreenRecorder::ScreenRecorder(QObject* parent): QObject(parent) {
	using C = RecorderController;
	auto* controller = C::instance();
	QObject::connect(controller, &C::recordingChanged, this, &ScreenRecorder::recordingChanged);
	QObject::connect(controller, &C::started, this, &ScreenRecorder::started);
	QObject::connect(controller, &C::finished, this, &ScreenRecorder::finished);
	QObject::connect(controller, &C::failed, this, &ScreenRecorder::failed);
	QObject::connect(controller, &C::soundUnavailable, this, &ScreenRecorder::soundUnavailable);
	QObject::connect(
	    controller,
	    &C::microphoneUnavailable,
	    this,
	    &ScreenRecorder::microphoneUnavailable
	);
}

bool ScreenRecorder::recording() const { return RecorderController::instance()->recording(); }
QString ScreenRecorder::path() const { return RecorderController::instance()->path(); }
bool ScreenRecorder::available() const { return RecorderController::instance()->available(); }

QString ScreenRecorder::unavailableReason() const {
	return RecorderController::instance()->unavailableReason();
}

bool ScreenRecorder::start(
    int x,
    int y,
    int width,
    int height,
    bool sound,
    const QString& saveDir,
    bool microphone
) {
	auto fail = [this](const QString& reason) {
		qCWarning(logScreenRecorder) << "Cannot start recording:" << reason;
		emit this->failed(reason);
		return false;
	};

	auto region = QRect(x, y, width & ~1, height & ~1);
	if (region.width() < MIN_SIZE || region.height() < MIN_SIZE) {
		return fail("the region is too small");
	}

	auto dir = saveDir;
	if (dir.startsWith("file:")) dir = QUrl(dir).toLocalFile();
	if (dir.isEmpty()) dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
	if (dir.isEmpty()) dir = QDir::home().filePath("Videos");

	if (!QDir().mkpath(dir)) return fail("cannot create " + QDir::toNativeSeparators(dir));

	auto stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH.mm.ss");
	auto name = QString("recording_%1.mp4").arg(stamp);

	recorder::RecordingRequest request;
	request.region = region;
	request.systemAudio = sound;
	request.microphone = microphone;
	request.path = QDir(dir).absoluteFilePath(name);

	QString error;
	if (!RecorderController::instance()->start(request, &error)) return fail(error);
	return true;
}

bool ScreenRecorder::startScreen(
    const QString& screenName,
    bool sound,
    const QString& saveDir,
    bool microphone
) {
	for (auto* screen: QGuiApplication::screens()) {
		if (screen->name() != screenName) continue;

		auto rects = monitorRects(monitorForScreen(screen));
		if (!rects.valid) break;

		const auto& r = rects.monitor;
		return this->start(r.x(), r.y(), r.width(), r.height(), sound, saveDir, microphone);
	}

	auto reason = QString("no screen named %1").arg(screenName);
	qCWarning(logScreenRecorder) << "Cannot start recording:" << reason;
	emit this->failed(reason);
	return false;
}

void ScreenRecorder::stop() { RecorderController::instance()->stop(); }

QString ScreenRecorder::microphoneBlockedReason() {
	return recorder::AudioCapture::microphoneBlockedReason();
}

} // namespace qs::windows::sys
