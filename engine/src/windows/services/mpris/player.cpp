#include "player.hpp"

#include <qdatetime.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnumeric.h>
#include <qobject.h>
#include <qtypes.h>

#include "gsmtc_backend.hpp"

namespace qs::windows::services::mpris {

namespace {
Q_LOGGING_CATEGORY(logMprisPlayer, "quickshell.windows.mpris.player", QtWarningMsg);

constexpr qint64 TICKS_PER_SECOND = 10'000'000;
} // namespace

QString MprisPlaybackState::toString(MprisPlaybackState::Enum status) {
	switch (status) {
	case MprisPlaybackState::Stopped: return "Stopped";
	case MprisPlaybackState::Playing: return "Playing";
	case MprisPlaybackState::Paused: return "Paused";
	default: return "Unknown Status";
	}
}

QString MprisLoopState::toString(MprisLoopState::Enum status) {
	switch (status) {
	case MprisLoopState::None: return "None";
	case MprisLoopState::Track: return "Track";
	case MprisLoopState::Playlist: return "Playlist";
	default: return "Unknown Status";
	}
}

MprisPlayer::MprisPlayer(quint64 sessionId, GsmtcBackend* backend, QObject* parent)
    : QObject(parent)
    , mSessionId(sessionId)
    , mBackend(backend) {}

void MprisPlayer::raise() { qCInfo(logMprisPlayer) << "raise() has no GSMTC equivalent, ignoring"; }
void MprisPlayer::quit() { qCInfo(logMprisPlayer) << "quit() has no GSMTC equivalent, ignoring"; }

void MprisPlayer::openUri(const QString& uri) {
	qCInfo(logMprisPlayer) << "openUri() has no GSMTC equivalent, ignoring request for" << uri;
}

void MprisPlayer::next() {
	if (!this->canGoNext()) {
		qCWarning(logMprisPlayer) << "Cannot call next() on" << this << "because canGoNext is false.";
		return;
	}

	this->mBackend->next(this->mSessionId);
}

void MprisPlayer::previous() {
	if (!this->canGoPrevious()) {
		qCWarning(logMprisPlayer) << "Cannot call previous() on" << this
		                          << "because canGoPrevious is false.";
		return;
	}

	this->mBackend->previous(this->mSessionId);
}

void MprisPlayer::seek(qreal offset) {
	if (!this->canSeek()) {
		qCWarning(logMprisPlayer) << "Cannot call seek() on" << this << "because canSeek is false.";
		return;
	}

	auto targetTicks =
	    static_cast<qint64>(this->position() * static_cast<qreal>(TICKS_PER_SECOND)) +
	    static_cast<qint64>(offset * static_cast<qreal>(TICKS_PER_SECOND));
	if (targetTicks < 0) targetTicks = 0;
	this->mBackend->setPosition(this->mSessionId, targetTicks);
}

void MprisPlayer::play() { this->setPlaybackState(MprisPlaybackState::Playing); }
void MprisPlayer::pause() { this->setPlaybackState(MprisPlaybackState::Paused); }
void MprisPlayer::stop() { this->setPlaybackState(MprisPlaybackState::Stopped); }

void MprisPlayer::togglePlaying() {
	if (!this->canTogglePlaying()) {
		qCWarning(logMprisPlayer) << "Cannot call togglePlaying() on" << this
		                          << "because canTogglePlaying is false.";
		return;
	}

	this->mBackend->togglePlayPause(this->mSessionId);
}

qreal MprisPlayer::position() const {
	if (!this->mPositionSupported) return 0;
	if (this->mPlaybackState == MprisPlaybackState::Stopped) return 0;

	auto ticks = this->mBasePositionTicks;

	if (this->mPlaybackState == MprisPlaybackState::Playing && this->mBaseTimestamp.isValid()) {
		auto elapsedMs = this->mBaseTimestamp.msecsTo(QDateTime::currentDateTimeUtc());
		ticks += static_cast<qint64>(
		    static_cast<qreal>(elapsedMs) * (static_cast<qreal>(TICKS_PER_SECOND) / 1000.0) *
		    this->mRate
		);
	}

	if (this->mLengthSupported && ticks > this->mLengthTicks) ticks = this->mLengthTicks;
	if (ticks < 0) ticks = 0;

	return static_cast<qreal>(ticks) / static_cast<qreal>(TICKS_PER_SECOND);
}

void MprisPlayer::setPosition(qreal position) {
	if (!this->mPositionSupported) {
		qCWarning(logMprisPlayer) << "Cannot set position of" << this
		                          << "because position is not supported.";
		return;
	}

	if (!this->canSeek()) {
		qCWarning(logMprisPlayer) << "Cannot set position of" << this << "because canSeek is false.";
		return;
	}

	auto targetTicks = static_cast<qint64>(position * static_cast<qreal>(TICKS_PER_SECOND));
	if (targetTicks < 0) targetTicks = 0;
	this->mBackend->setPosition(this->mSessionId, targetTicks);
}

qreal MprisPlayer::length() const {
	if (!this->mLengthSupported) return this->position();
	return static_cast<qreal>(this->mLengthTicks) / static_cast<qreal>(TICKS_PER_SECOND);
}

void MprisPlayer::setPlaybackState(MprisPlaybackState::Enum state) {
	if (state == this->mPlaybackState) return;

	switch (state) {
	case MprisPlaybackState::Playing:
		if (!this->canPlay()) {
			qCWarning(logMprisPlayer) << "Cannot set playbackState of" << this
			                          << "to Playing because canPlay is false.";
			return;
		}
		this->mBackend->play(this->mSessionId);
		break;
	case MprisPlaybackState::Paused:
		if (!this->canPause()) {
			qCWarning(logMprisPlayer) << "Cannot set playbackState of" << this
			                          << "to Paused because canPause is false.";
			return;
		}
		this->mBackend->pause(this->mSessionId);
		break;
	case MprisPlaybackState::Stopped:
		qCWarning(logMprisPlayer) << "Cannot set playbackState of" << this
		                          << "to Stopped: GSMTC has no stop command.";
		return;
	default:
		qCWarning(logMprisPlayer) << "Cannot set playbackState of" << this << "to unknown value"
		                          << state;
		return;
	}
}

void MprisPlayer::setPlaying(bool playing) {
	if (playing == this->mIsPlaying) return;
	this->togglePlaying();
}

void MprisPlayer::setLoopState(MprisLoopState::Enum state) {
	if (!this->canControl()) {
		qCWarning(logMprisPlayer) << "Cannot set loopState of" << this << "because canControl is false.";
		return;
	}

	if (!this->mLoopSupported) {
		qCWarning(logMprisPlayer) << "Cannot set loopState of" << this
		                          << "because loop state is not supported.";
		return;
	}

	if (state == this->mLoopState) return;
	this->mBackend->setLoopState(this->mSessionId, state);
}

void MprisPlayer::setShuffle(bool shuffle) {
	if (!this->mShuffleSupported) {
		qCWarning(logMprisPlayer) << "Cannot set shuffle for" << this
		                          << "because shuffle is not supported.";
		return;
	}

	if (!this->canControl()) {
		qCWarning(logMprisPlayer) << "Cannot set shuffle state of" << this
		                          << "because canControl is false.";
		return;
	}

	if (shuffle == this->mShuffle) return;
	this->mBackend->setShuffle(this->mSessionId, shuffle);
}

void MprisPlayer::applyIdentity(
    const QString& identity,
    const QString& desktopEntry,
    const QString& dbusName
) {
	if (identity != this->mIdentity) {
		this->mIdentity = identity;
		emit this->identityChanged();
	}

	if (desktopEntry != this->mDesktopEntry) {
		this->mDesktopEntry = desktopEntry;
		emit this->desktopEntryChanged();
	}

	this->mDbusName = dbusName;
}

void MprisPlayer::applyMediaProperties(const MprisPlayer::MediaSnapshot& snapshot) {
	const auto trackChanged = snapshot.uniqueId != this->mUniqueId;
	if (trackChanged) emit this->trackChanged();

	if (snapshot.title != this->mTrackTitle) {
		this->mTrackTitle = snapshot.title;
		emit this->trackTitleChanged();
	}

	if (snapshot.artist != this->mTrackArtist) {
		this->mTrackArtist = snapshot.artist;
		emit this->trackArtistChanged();
	}

	if (snapshot.album != this->mTrackAlbum) {
		this->mTrackAlbum = snapshot.album;
		emit this->trackAlbumChanged();
	}

	if (snapshot.albumArtist != this->mTrackAlbumArtist) {
		this->mTrackAlbumArtist = snapshot.albumArtist;
		emit this->trackAlbumArtistChanged();
	}

	if (snapshot.artUrl != this->mTrackArtUrl) {
		this->mTrackArtUrl = snapshot.artUrl;
		emit this->trackArtUrlChanged();
	}

	this->mUniqueId = snapshot.uniqueId;

	this->mMetadata = QVariantMap {
	    {"xesam:title",      this->mTrackTitle      },
	    {"xesam:artist",     this->mTrackArtist     },
	    {"xesam:album",      this->mTrackAlbum      },
	    {"xesam:albumArtist", this->mTrackAlbumArtist},
	    {"mpris:artUrl",     this->mTrackArtUrl     },
	};
	emit this->metadataChanged();
	emit this->uniqueIdChanged();

	if (trackChanged) emit this->postTrackChanged();
}

void MprisPlayer::applyPlaybackInfo(const MprisPlayer::PlaybackSnapshot& snapshot) {
	const auto wasPlaying = this->mIsPlaying;
	const auto wasTogglePlaying = this->canTogglePlaying();

	if (snapshot.canControl != this->mCanControl) {
		this->mCanControl = snapshot.canControl;
		emit this->canControlChanged();
	}

	if (snapshot.canPlay != this->mCanPlay) {
		this->mCanPlay = snapshot.canPlay;
		emit this->canPlayChanged();
	}

	if (snapshot.canPause != this->mCanPause) {
		this->mCanPause = snapshot.canPause;
		emit this->canPauseChanged();
	}

	if (snapshot.canSeek != this->mCanSeek) {
		this->mCanSeek = snapshot.canSeek;
		emit this->canSeekChanged();
	}

	if (snapshot.canGoNext != this->mCanGoNext) {
		this->mCanGoNext = snapshot.canGoNext;
		emit this->canGoNextChanged();
	}

	if (snapshot.canGoPrevious != this->mCanGoPrevious) {
		this->mCanGoPrevious = snapshot.canGoPrevious;
		emit this->canGoPreviousChanged();
	}

	if (snapshot.state != this->mPlaybackState) {
		this->mPlaybackState = snapshot.state;
		emit this->playbackStateChanged();
	}

	this->mIsPlaying = snapshot.state == MprisPlaybackState::Playing;
	if (this->mIsPlaying != wasPlaying) emit this->isPlayingChanged();

	if (snapshot.loopState != this->mLoopState) {
		this->mLoopState = snapshot.loopState;
		emit this->loopStateChanged();
	}

	if (snapshot.loopSupported != this->mLoopSupported) {
		this->mLoopSupported = snapshot.loopSupported;
		emit this->loopSupportedChanged();
	}

	if (snapshot.shuffle != this->mShuffle) {
		this->mShuffle = snapshot.shuffle;
		emit this->shuffleChanged();
	}

	if (snapshot.shuffleSupported != this->mShuffleSupported) {
		this->mShuffleSupported = snapshot.shuffleSupported;
		emit this->shuffleSupportedChanged();
	}

	if (!qFuzzyCompare(snapshot.rate, this->mRate)) {
		this->mRate = snapshot.rate;
		emit this->rateChanged();
	}

	if (this->canTogglePlaying() != wasTogglePlaying) emit this->canTogglePlayingChanged();
}

void MprisPlayer::applyTimeline(const MprisPlayer::TimelineSnapshot& snapshot) {
	this->mBasePositionTicks = snapshot.basePositionTicks;
	this->mBaseTimestamp = snapshot.baseTimestamp;

	if (snapshot.lengthTicks != this->mLengthTicks) {
		this->mLengthTicks = snapshot.lengthTicks;
		emit this->lengthChanged();
	}

	if (snapshot.lengthSupported != this->mLengthSupported) {
		this->mLengthSupported = snapshot.lengthSupported;
		emit this->lengthSupportedChanged();
	}

	if (snapshot.positionSupported != this->mPositionSupported) {
		this->mPositionSupported = snapshot.positionSupported;
		emit this->positionSupportedChanged();
	}

	emit this->positionChanged();
}

} // namespace qs::windows::services::mpris
