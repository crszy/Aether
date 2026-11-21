#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>

namespace qs::windows::sys {

class ScreenRecorder: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged);
	Q_PROPERTY(QString path READ path NOTIFY recordingChanged);
	Q_PROPERTY(bool available READ available CONSTANT);
	Q_PROPERTY(QString unavailableReason READ unavailableReason CONSTANT);

public:
	explicit ScreenRecorder(QObject* parent = nullptr);

	Q_INVOKABLE bool start(
	    int x,
	    int y,
	    int width,
	    int height,
	    bool sound = false,
	    const QString& saveDir = QString(),
	    bool microphone = false
	);
	Q_INVOKABLE bool startScreen(
	    const QString& screenName,
	    bool sound = false,
	    const QString& saveDir = QString(),
	    bool microphone = false
	);
	Q_INVOKABLE void stop();
	Q_INVOKABLE static QString microphoneBlockedReason();

	[[nodiscard]] bool recording() const;
	[[nodiscard]] QString path() const;
	[[nodiscard]] bool available() const;
	[[nodiscard]] QString unavailableReason() const;

signals:
	void recordingChanged();
	void started(const QString& path);
	void finished(const QString& path);
	void failed(const QString& reason);
	void soundUnavailable(const QString& reason);
	void microphoneUnavailable(const QString& reason, bool blocked);
};

} // namespace qs::windows::sys
