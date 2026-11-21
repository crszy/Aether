#pragma once

#include <memory>

#include <qobject.h>
#include <qtclasshelpermacros.h>
#include <qthread.h>
#include <qtypes.h>

#include "player.hpp"

namespace qs::windows::services::mpris {

class Mpris;
class GsmtcWorker;

class GsmtcBackend: public QObject {
	Q_OBJECT;

public:
	explicit GsmtcBackend(Mpris* frontend);
	~GsmtcBackend() override;
	Q_DISABLE_COPY_MOVE(GsmtcBackend);

	void play(quint64 sessionId);
	void pause(quint64 sessionId);
	void togglePlayPause(quint64 sessionId);
	void next(quint64 sessionId);
	void previous(quint64 sessionId);
	void setPosition(quint64 sessionId, qint64 ticks);
	void setShuffle(quint64 sessionId, bool shuffle);
	void setLoopState(quint64 sessionId, MprisLoopState::Enum state);

private:
	std::unique_ptr<QThread> mThread;
	GsmtcWorker* mWorker = nullptr;
};

} // namespace qs::windows::services::mpris
