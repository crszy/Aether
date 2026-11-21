#pragma once

#include <qdatetime.h>
#include <qlist.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

namespace qs::windows::services::mpris {

class MprisPlaybackState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Stopped = 0,
		Playing = 1,
		Paused = 2,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::services::mpris::MprisPlaybackState::Enum status);
};

class MprisLoopState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		None = 0,
		Track = 1,
		Playlist = 2,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::services::mpris::MprisLoopState::Enum status);
};

class GsmtcBackend;

class MprisPlayer: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(bool canControl READ canControl NOTIFY canControlChanged);
	Q_PROPERTY(bool canPlay READ canPlay NOTIFY canPlayChanged);
	Q_PROPERTY(bool canPause READ canPause NOTIFY canPauseChanged);
	Q_PROPERTY(bool canTogglePlaying READ canTogglePlaying NOTIFY canTogglePlayingChanged);
	Q_PROPERTY(bool canSeek READ canSeek NOTIFY canSeekChanged);
	Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY canGoNextChanged);
	Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY canGoPreviousChanged);
	Q_PROPERTY(bool canQuit READ canQuit CONSTANT);
	Q_PROPERTY(bool canRaise READ canRaise CONSTANT);
	Q_PROPERTY(bool canSetFullscreen READ canSetFullscreen CONSTANT);
	Q_PROPERTY(QString identity READ identity NOTIFY identityChanged);
	Q_PROPERTY(QString desktopEntry READ desktopEntry NOTIFY desktopEntryChanged);
	Q_PROPERTY(QString dbusName READ dbusName CONSTANT);
	Q_PROPERTY(qreal position READ position WRITE setPosition NOTIFY positionChanged);
	Q_PROPERTY(bool positionSupported READ positionSupported NOTIFY positionSupportedChanged);
	Q_PROPERTY(qreal length READ length NOTIFY lengthChanged);
	Q_PROPERTY(bool lengthSupported READ lengthSupported NOTIFY lengthSupportedChanged);
	Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged);
	Q_PROPERTY(bool volumeSupported READ volumeSupported CONSTANT);
	Q_PROPERTY(QVariantMap metadata READ metadata NOTIFY metadataChanged);
	Q_PROPERTY(quint32 uniqueId READ uniqueId NOTIFY uniqueIdChanged);
	Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY trackTitleChanged);
	Q_PROPERTY(QString trackArtist READ trackArtist NOTIFY trackArtistChanged);
	Q_PROPERTY(QString trackArtists READ trackArtist NOTIFY trackArtistChanged);
	Q_PROPERTY(QString trackAlbum READ trackAlbum NOTIFY trackAlbumChanged);
	Q_PROPERTY(QString trackAlbumArtist READ trackAlbumArtist NOTIFY trackAlbumArtistChanged);
	Q_PROPERTY(QString trackArtUrl READ trackArtUrl NOTIFY trackArtUrlChanged);
	Q_PROPERTY(qs::windows::services::mpris::MprisPlaybackState::Enum playbackState READ playbackState WRITE setPlaybackState NOTIFY playbackStateChanged);
	Q_PROPERTY(bool isPlaying READ isPlaying WRITE setPlaying NOTIFY isPlayingChanged);
	Q_PROPERTY(qs::windows::services::mpris::MprisLoopState::Enum loopState READ loopState WRITE setLoopState NOTIFY loopStateChanged);
	Q_PROPERTY(bool loopSupported READ loopSupported NOTIFY loopSupportedChanged);
	Q_PROPERTY(qreal rate READ rate NOTIFY rateChanged);
	Q_PROPERTY(qreal minRate READ minRate CONSTANT);
	Q_PROPERTY(qreal maxRate READ maxRate CONSTANT);
	Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged);
	Q_PROPERTY(bool shuffleSupported READ shuffleSupported NOTIFY shuffleSupportedChanged);
	Q_PROPERTY(bool fullscreen READ fullscreen CONSTANT);
	Q_PROPERTY(QList<QString> supportedUriSchemes READ supportedUriSchemes CONSTANT);
	Q_PROPERTY(QList<QString> supportedMimeTypes READ supportedMimeTypes CONSTANT);
	// clang-format on
	QML_NAMED_ELEMENT(MprisPlayer);
	QML_UNCREATABLE("MprisPlayers can only be acquired from Mpris");

public:
	explicit MprisPlayer(quint64 sessionId, GsmtcBackend* backend, QObject* parent = nullptr);
	Q_DISABLE_COPY_MOVE(MprisPlayer);

	[[nodiscard]] quint64 sessionId() const { return this->mSessionId; }

	Q_INVOKABLE void raise();
	Q_INVOKABLE void quit();
	Q_INVOKABLE void openUri(const QString& uri);
	Q_INVOKABLE void next();
	Q_INVOKABLE void previous();
	Q_INVOKABLE void seek(qreal offset);
	Q_INVOKABLE void play();
	Q_INVOKABLE void pause();
	Q_INVOKABLE void stop();
	Q_INVOKABLE void togglePlaying();

	[[nodiscard]] bool canControl() const { return this->mCanControl; }
	[[nodiscard]] bool canPlay() const { return this->mCanControl && this->mCanPlay; }
	[[nodiscard]] bool canPause() const { return this->mCanControl && this->mCanPause; }
	[[nodiscard]] bool canTogglePlaying() const {
		return this->mIsPlaying ? this->canPause() : this->canPlay();
	}
	[[nodiscard]] bool canSeek() const { return this->mCanControl && this->mCanSeek; }
	[[nodiscard]] bool canGoNext() const { return this->mCanControl && this->mCanGoNext; }
	[[nodiscard]] bool canGoPrevious() const { return this->mCanControl && this->mCanGoPrevious; }
	[[nodiscard]] static bool canQuit() { return false; }
	[[nodiscard]] static bool canRaise() { return false; }
	[[nodiscard]] static bool canSetFullscreen() { return false; }

	[[nodiscard]] QString identity() const { return this->mIdentity; }
	[[nodiscard]] QString desktopEntry() const { return this->mDesktopEntry; }
	[[nodiscard]] QString dbusName() const { return this->mDbusName; }

	[[nodiscard]] qreal position() const;
	void setPosition(qreal position);
	[[nodiscard]] bool positionSupported() const { return this->mPositionSupported; }

	[[nodiscard]] qreal length() const;
	[[nodiscard]] bool lengthSupported() const { return this->mLengthSupported; }

	[[nodiscard]] static qreal volume() { return 1.0; }
	static void setVolume(qreal) {}
	[[nodiscard]] static bool volumeSupported() { return false; }

	[[nodiscard]] QVariantMap metadata() const { return this->mMetadata; }
	[[nodiscard]] quint32 uniqueId() const { return this->mUniqueId; }
	[[nodiscard]] QString trackTitle() const { return this->mTrackTitle; }
	[[nodiscard]] QString trackArtist() const { return this->mTrackArtist; }
	[[nodiscard]] QString trackAlbum() const { return this->mTrackAlbum; }
	[[nodiscard]] QString trackAlbumArtist() const { return this->mTrackAlbumArtist; }
	[[nodiscard]] QString trackArtUrl() const { return this->mTrackArtUrl; }

	[[nodiscard]] MprisPlaybackState::Enum playbackState() const { return this->mPlaybackState; }
	void setPlaybackState(MprisPlaybackState::Enum state);

	[[nodiscard]] bool isPlaying() const { return this->mIsPlaying; }
	void setPlaying(bool playing);

	[[nodiscard]] MprisLoopState::Enum loopState() const { return this->mLoopState; }
	void setLoopState(MprisLoopState::Enum state);
	[[nodiscard]] bool loopSupported() const { return this->mLoopSupported; }

	[[nodiscard]] qreal rate() const { return this->mRate; }
	[[nodiscard]] static qreal minRate() { return 1.0; }
	[[nodiscard]] static qreal maxRate() { return 1.0; }

	[[nodiscard]] bool shuffle() const { return this->mShuffle; }
	void setShuffle(bool shuffle);
	[[nodiscard]] bool shuffleSupported() const { return this->mShuffleSupported; }

	[[nodiscard]] static bool fullscreen() { return false; }
	[[nodiscard]] static QList<QString> supportedUriSchemes() { return {}; }
	[[nodiscard]] static QList<QString> supportedMimeTypes() { return {}; }

	void applyIdentity(const QString& identity, const QString& desktopEntry, const QString& dbusName);

	struct MediaSnapshot {
		quint32 uniqueId;
		QString title;
		QString artist;
		QString album;
		QString albumArtist;
		QString artUrl;
	};
	void applyMediaProperties(const MediaSnapshot& snapshot);

	struct PlaybackSnapshot {
		MprisPlaybackState::Enum state;
		bool canPlay;
		bool canPause;
		bool canSeek;
		bool canGoNext;
		bool canGoPrevious;
		bool canControl;
		bool shuffle;
		bool shuffleSupported;
		MprisLoopState::Enum loopState;
		bool loopSupported;
		qreal rate;
	};
	void applyPlaybackInfo(const PlaybackSnapshot& snapshot);

	struct TimelineSnapshot {
		qint64 basePositionTicks;
		QDateTime baseTimestamp;
		qint64 lengthTicks;
		bool lengthSupported;
		bool positionSupported;
	};
	void applyTimeline(const TimelineSnapshot& snapshot);

signals:
	void trackChanged();
	void postTrackChanged();

	void canControlChanged();
	void canPlayChanged();
	void canPauseChanged();
	void canTogglePlayingChanged();
	void canSeekChanged();
	void canGoNextChanged();
	void canGoPreviousChanged();
	void identityChanged();
	void desktopEntryChanged();
	void positionChanged();
	void positionSupportedChanged();
	void lengthChanged();
	void lengthSupportedChanged();
	void volumeChanged();
	void metadataChanged();
	void uniqueIdChanged();
	void trackTitleChanged();
	void trackArtistChanged();
	void trackAlbumChanged();
	void trackAlbumArtistChanged();
	void trackArtUrlChanged();
	void playbackStateChanged();
	void isPlayingChanged();
	void loopStateChanged();
	void loopSupportedChanged();
	void rateChanged();
	void shuffleChanged();
	void shuffleSupportedChanged();

private:
	quint64 mSessionId;
	GsmtcBackend* mBackend;

	bool mCanControl = false;
	bool mCanPlay = false;
	bool mCanPause = false;
	bool mCanSeek = false;
	bool mCanGoNext = false;
	bool mCanGoPrevious = false;

	QString mIdentity;
	QString mDesktopEntry;
	QString mDbusName;

	qint64 mBasePositionTicks = 0;
	QDateTime mBaseTimestamp;
	qreal mRate = 1.0;
	bool mPositionSupported = false;
	qint64 mLengthTicks = 0;
	bool mLengthSupported = false;

	QVariantMap mMetadata;
	quint32 mUniqueId = 0;
	QString mTrackTitle;
	QString mTrackArtist;
	QString mTrackAlbum;
	QString mTrackAlbumArtist;
	QString mTrackArtUrl;

	MprisPlaybackState::Enum mPlaybackState = MprisPlaybackState::Stopped;
	bool mIsPlaying = false;
	MprisLoopState::Enum mLoopState = MprisLoopState::None;
	bool mLoopSupported = false;
	bool mShuffle = false;
	bool mShuffleSupported = false;
};

} // namespace qs::windows::services::mpris
