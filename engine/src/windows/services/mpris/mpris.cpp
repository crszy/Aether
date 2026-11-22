#include "mpris.hpp"

#include <memory>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qtypes.h>

#include "gsmtc_backend.hpp"
#include "player.hpp"

namespace qs::windows::services::mpris {

namespace {
Q_LOGGING_CATEGORY(logMpris, "quickshell.windows.mpris", QtWarningMsg);
}

Mpris::Mpris(QObject* parent): QObject(parent) {
	this->mBackend = std::make_unique<GsmtcBackend>(this);
}

Mpris::~Mpris() {
	this->mBackend.reset();
}

MprisPlayer* Mpris::findPlayer(quint64 sessionId) const {
	for (auto* object: this->mPlayers.valueList()) {
		if (object->sessionId() == sessionId) return object;
	}

	return nullptr;
}

void Mpris::backendAddPlayer(
    quint64 sessionId,
    const QString& identity,
    const QString& desktopEntry,
    const QString& dbusName,
    const MprisPlayer::PlaybackSnapshot& playback,
    const MprisPlayer::TimelineSnapshot& timeline,
    const MprisPlayer::MediaSnapshot& media
) {
	if (this->findPlayer(sessionId) != nullptr) {
		qCWarning(logMpris) << "Ignoring duplicate GSMTC session id" << sessionId;
		return;
	}

	auto* player = new MprisPlayer(sessionId, this->mBackend.get(), this);
	player->applyIdentity(identity, desktopEntry, dbusName);
	player->applyPlaybackInfo(playback);
	player->applyTimeline(timeline);
	player->applyMediaProperties(media);

	this->mPlayers.insertObject(player);
	qCDebug(logMpris) << "Added MprisPlayer for GSMTC session" << sessionId << identity;
}

void Mpris::backendRemovePlayer(quint64 sessionId) {
	auto* player = this->findPlayer(sessionId);
	if (player == nullptr) {
		qCWarning(logMpris) << "Ignoring removal of untracked GSMTC session id" << sessionId;
		return;
	}

	this->mPlayers.removeObject(player);
	player->deleteLater();
	qCDebug(logMpris) << "Removed MprisPlayer for GSMTC session" << sessionId;
}

void Mpris::backendUpdatePlaybackInfo(quint64 sessionId, const MprisPlayer::PlaybackSnapshot& snapshot) {
	if (auto* player = this->findPlayer(sessionId)) player->applyPlaybackInfo(snapshot);
}

void Mpris::backendUpdateTimeline(quint64 sessionId, const MprisPlayer::TimelineSnapshot& snapshot) {
	if (auto* player = this->findPlayer(sessionId)) player->applyTimeline(snapshot);
}

void Mpris::backendUpdateMediaProperties(quint64 sessionId, const MprisPlayer::MediaSnapshot& snapshot) {
	if (auto* player = this->findPlayer(sessionId)) player->applyMediaProperties(snapshot);
}

} // namespace qs::windows::services::mpris
