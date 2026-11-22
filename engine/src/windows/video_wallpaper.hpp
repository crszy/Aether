#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include <qhash.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qquickitem.h>
#include <qsize.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qthread.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "capture.hpp"

namespace qs::windows::video {

class VideoWorker;

struct FrameSlot {
	std::mutex mutex;
	std::shared_ptr<capture::SharedFrame> frame;
	quint64 serial = 0;
};

class VideoSource: public QObject {
	Q_OBJECT;

public:
	explicit VideoSource(QString path);
	~VideoSource() override;
	Q_DISABLE_COPY_MOVE(VideoSource);

	static std::shared_ptr<VideoSource> acquire(const QString& path);

	[[nodiscard]] const QString& path() const { return this->mPath; }
	[[nodiscard]] bool ready() const { return this->mReady; }
	[[nodiscard]] const QString& errorString() const { return this->mError; }
	[[nodiscard]] std::shared_ptr<capture::SharedFrame> latestFrame(quint64* serial) const;

	void setWanted(const void* viewer, bool wanted);

signals:
	void frameReady();
	void stateChanged();

private:
	void onReady();
	void onFailed(const QString& error);
	void updatePlaying();

	QString mPath;
	std::unique_ptr<QThread> thread;
	VideoWorker* worker = nullptr;
	std::shared_ptr<FrameSlot> slot;
	QHash<const void*, bool> viewers;
	bool mReady = false;
	bool mPlaying = false;
	QString mError;
};

class VideoWallpaper: public QQuickItem {
	Q_OBJECT;
	QML_ELEMENT;
	Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged);
	Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged);
	Q_PROPERTY(bool ready READ ready NOTIFY stateChanged);
	Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged);

public:
	explicit VideoWallpaper(QQuickItem* parent = nullptr);
	~VideoWallpaper() override;
	Q_DISABLE_COPY_MOVE(VideoWallpaper);

	[[nodiscard]] QString source() const { return this->mSource; }
	void setSource(const QString& source);

	[[nodiscard]] bool playing() const { return this->mPlaying; }
	void setPlaying(bool playing);

	[[nodiscard]] bool ready() const { return this->mVideo && this->mVideo->ready(); }
	[[nodiscard]] QString errorString() const {
		return this->mVideo ? this->mVideo->errorString() : QString();
	}

signals:
	void sourceChanged();
	void playingChanged();
	void stateChanged();

protected:
	QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
	void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
	void attach();
	void detach();
	void updateWanted();

	QString mSource;
	bool mPlaying = true;
	std::shared_ptr<VideoSource> mVideo;
};

} // namespace qs::windows::video
