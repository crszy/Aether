#include "gsmtc_worker.hpp"

#include <qcoreapplication.h>
#include <qdatetime.h>
#include <qhash.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qpair.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtimezone.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include "art_cache.hpp"
#include "mpris.hpp"

using namespace winrt::Windows::Media::Control;
using winrt::Windows::Media::MediaPlaybackAutoRepeatMode;

namespace qs::windows::services::mpris {

namespace {
constexpr int EMPTY_REREAD_MS = 400;
constexpr int MAX_EMPTY_REREADS = 8;
constexpr int ART_RETRY_MS = 350;
constexpr int MAX_ART_RETRIES = 6;

bool isBlankTrack(const QString& trackKey) {
	return QString(trackKey).remove(QChar(0x1f)).trimmed().isEmpty();
}

Q_LOGGING_CATEGORY(logMprisWorker, "quickshell.windows.mpris.worker", QtWarningMsg);

constexpr qint64 WINDOWS_TO_UNIX_EPOCH_TICKS = 116444736000000000LL;

QString toQString(const winrt::hstring& value) {
	return QString::fromWCharArray(value.c_str(), static_cast<qsizetype>(value.size()));
}

QDateTime toQDateTime(const winrt::Windows::Foundation::DateTime& dt) {
	auto windowsTicks = dt.time_since_epoch().count();
	auto unixMs = (windowsTicks - WINDOWS_TO_UNIX_EPOCH_TICKS) / 10'000;
	return QDateTime::fromMSecsSinceEpoch(unixMs, QTimeZone::UTC);
}

QPair<QString, QString> identityForAumid(const QString& aumid) {
	static const QHash<QString, QPair<QString, QString>> known = {
	    {"Spotify.exe",                                           {"Spotify", "spotify"}                },
	    {"Spotify",                                               {"Spotify", "spotify"}                },
	    {"MSEdge",                                                {"Microsoft Edge", "msedge"}          },
	    {"Microsoft.MicrosoftEdge_8wekyb3d8bbwe!MicrosoftEdge",   {"Microsoft Edge", "msedge"}          },
	    {"MicrosoftEdge.exe",                                     {"Microsoft Edge", "msedge"}          },
	    {"msedge.exe",                                             {"Microsoft Edge", "msedge"}          },
	    {"308046B0AF4A39CB",                                      {"Firefox", "firefox"}                },
	    {"firefox.exe",                                            {"Firefox", "firefox"}                },
	    {"chrome.exe",                                             {"Google Chrome", "google-chrome"}    },
	    {"Google.Chrome",                                          {"Google Chrome", "google-chrome"}    },
	    {"Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic", {"Media Player", "mediaplayer"}       },
	    {"Microsoft.Media.Player_8wekyb3d8bbwe!App",              {"Media Player", "mediaplayer"}       },
	    {"WMPlayerApp.exe",                                        {"Windows Media Player", "wmplayer"}  },
	};

	auto found = known.constFind(aumid);
	if (found != known.constEnd()) return found.value();

	auto identity = aumid;
	if (identity.endsWith(".exe", Qt::CaseInsensitive)) identity.chop(4);
	return {identity, aumid};
}

MprisPlaybackState::Enum mapPlaybackStatus(GlobalSystemMediaTransportControlsSessionPlaybackStatus status) {
	switch (status) {
	case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
		return MprisPlaybackState::Playing;
	case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
		return MprisPlaybackState::Paused;
	default: return MprisPlaybackState::Stopped;
	}
}
} // namespace

GsmtcWorker::GsmtcWorker(Mpris* frontend): mFrontend(frontend) {}

GsmtcWorker::~GsmtcWorker() = default;

void GsmtcWorker::start() {
	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
		this->mApartmentInitialized = true;
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMprisWorker) << "init_apartment(multi_threaded) failed:" << Qt::hex
		                          << static_cast<uint32_t>(e.code().value);
		return;
	}

	try {
		this->mManager =
		    GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMprisWorker) << "GSMTC RequestAsync failed:" << Qt::hex
		                          << static_cast<uint32_t>(e.code().value);
		return;
	}

	this->mSessionsChangedToken = this->mManager.SessionsChanged([this](auto&&, auto&&) {
		QMetaObject::invokeMethod(this, [this] { this->resyncSessions(); }, Qt::QueuedConnection);
	});

	this->mCurrentSessionChangedToken = this->mManager.CurrentSessionChanged([this](auto&&, auto&&) {
		QMetaObject::invokeMethod(this, [this] { this->resyncSessions(); }, Qt::QueuedConnection);
	});

	this->resyncSessions();
}

void GsmtcWorker::shutdown() {
	if (this->mManager) {
		this->mManager.SessionsChanged(this->mSessionsChangedToken);
		this->mManager.CurrentSessionChanged(this->mCurrentSessionChangedToken);

		for (auto& [id, entry]: this->mEntries) {
			entry->session.MediaPropertiesChanged(entry->mediaToken);
			entry->session.PlaybackInfoChanged(entry->playbackToken);
			entry->session.TimelinePropertiesChanged(entry->timelineToken);
		}
		this->mEntries.clear();
		this->mManager = nullptr;
	}

	if (this->mApartmentInitialized) {
		winrt::uninit_apartment();
		this->mApartmentInitialized = false;
	}
}

bool GsmtcWorker::sessionStillTracked(
    const GlobalSystemMediaTransportControlsSession& session
) const {
	for (const auto& [id, entry]: this->mEntries) {
		if (entry->session == session) return true;
	}
	return false;
}

void GsmtcWorker::resyncSessions() {
	if (!this->mManager) return;

	winrt::Windows::Foundation::Collections::IVectorView<GlobalSystemMediaTransportControlsSession>
	    sessions {nullptr};
	try {
		sessions = this->mManager.GetSessions();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMprisWorker) << "GetSessions failed:" << Qt::hex
		                          << static_cast<uint32_t>(e.code().value);
		return;
	}

	QVector<quint64> toRemove;
	for (const auto& [id, entry]: this->mEntries) {
		auto stillLive = false;
		for (const auto& s: sessions) {
			if (s == entry->session) {
				stillLive = true;
				break;
			}
		}
		if (!stillLive) toRemove.push_back(id);
	}
	for (auto id: toRemove) this->removeSession(id);

	for (const auto& s: sessions) {
		if (!this->sessionStillTracked(s)) this->addSession(s);
	}
}

void GsmtcWorker::addSession(const GlobalSystemMediaTransportControlsSession& session) {
	auto id = this->mNextId++;
	auto entryOwner = std::make_unique<SessionEntry>();
	auto* entry = entryOwner.get();
	entry->session = session;
	this->mEntries.emplace(id, std::move(entryOwner));

	entry->mediaToken = session.MediaPropertiesChanged([this, id](auto&&, auto&&) {
		QMetaObject::invokeMethod(this, [this, id] { this->refreshMediaProperties(id); }, Qt::QueuedConnection);
	});

	entry->playbackToken = session.PlaybackInfoChanged([this, id](auto&&, auto&&) {
		QMetaObject::invokeMethod(
		    this,
		    [this, id] {
			    this->refreshPlaybackInfo(id);
			    this->refreshTimeline(id);
		    },
		    Qt::QueuedConnection
		);
	});

	entry->timelineToken = session.TimelinePropertiesChanged([this, id](auto&&, auto&&) {
		QMetaObject::invokeMethod(this, [this, id] { this->refreshTimeline(id); }, Qt::QueuedConnection);
	});

	auto aumid = toQString(session.SourceAppUserModelId());
	auto mappedIdentity = identityForAumid(aumid);
	auto identity = mappedIdentity.first;
	auto desktopEntry = mappedIdentity.second;
	auto dbusName = QStringLiteral("gsmtc:") + aumid + QStringLiteral("#") + QString::number(id);

	auto playback = this->buildPlaybackSnapshot(session).value_or(MprisPlayer::PlaybackSnapshot {
	    .state = MprisPlaybackState::Stopped,
	    .canPlay = false,
	    .canPause = false,
	    .canSeek = false,
	    .canGoNext = false,
	    .canGoPrevious = false,
	    .canControl = false,
	    .shuffle = false,
	    .shuffleSupported = false,
	    .loopState = MprisLoopState::None,
	    .loopSupported = false,
	    .rate = 1.0,
	});
	auto timeline = this->buildTimelineSnapshot(session).value_or(MprisPlayer::TimelineSnapshot {
	    .basePositionTicks = 0,
	    .baseTimestamp = {},
	    .lengthTicks = 0,
	    .lengthSupported = false,
	    .positionSupported = false,
	});

	MprisPlayer::MediaSnapshot media {0, {}, {}, {}, {}, {}};
	try {
		auto props = session.TryGetMediaPropertiesAsync().get();
		if (!props) winrt::throw_hresult(winrt::hresult(static_cast<int32_t>(0x80004003)));
		auto title = toQString(props.Title());
		auto artist = toQString(props.Artist());
		auto album = toQString(props.AlbumTitle());
		auto albumArtist = toQString(props.AlbumArtist());
		auto trackKey = title + QChar(0x1f) + artist + QChar(0x1f) + album;

		entry->trackKey = trackKey;
		entry->uniqueId = 1;
		this->updateArt(*entry, id, props);

		media = {entry->uniqueId, title, artist, album, albumArtist, entry->cachedArtUrl};
		qCDebug(logMprisWorker) << "New session" << id << "title" << title << "artist" << artist
		                        << "art" << !entry->cachedArtUrl.isEmpty();
		if (title.isEmpty()) this->scheduleReread(id);
	} catch (const winrt::hresult_error& e) {
		qCDebug(logMprisWorker) << "TryGetMediaPropertiesAsync failed for new session:" << Qt::hex
		                        << static_cast<uint32_t>(e.code().value);
		this->scheduleReread(id);
	}

	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [frontend = this->mFrontend, id, identity, desktopEntry, dbusName, playback, timeline, media] {
		    if (!frontend) return;
		    frontend->backendAddPlayer(id, identity, desktopEntry, dbusName, playback, timeline, media);
	    },
	    Qt::QueuedConnection
	);
}

void GsmtcWorker::removeSession(quint64 sessionId) {
	auto it = this->mEntries.find(sessionId);
	if (it == this->mEntries.end()) return;

	auto& entry = *it->second;
	entry.session.MediaPropertiesChanged(entry.mediaToken);
	entry.session.PlaybackInfoChanged(entry.playbackToken);
	entry.session.TimelinePropertiesChanged(entry.timelineToken);
	this->mEntries.erase(it);

	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [frontend = this->mFrontend, sessionId] {
		    if (frontend) frontend->backendRemovePlayer(sessionId);
	    },
	    Qt::QueuedConnection
	);
}

std::optional<MprisPlayer::PlaybackSnapshot> GsmtcWorker::buildPlaybackSnapshot(
    const GlobalSystemMediaTransportControlsSession& session
) const try {
	auto info = session.GetPlaybackInfo();
	if (!info) return std::nullopt;
	auto controls = info.Controls();
	if (!controls) return std::nullopt;

	auto shuffleSupported = false;
	auto shuffle = false;
	if (auto ref = info.IsShuffleActive()) {
		shuffleSupported = true;
		shuffle = ref.Value();
	}

	auto loopSupported = false;
	auto loopState = MprisLoopState::None;
	if (auto ref = info.AutoRepeatMode()) {
		loopSupported = true;
		switch (ref.Value()) {
		case MediaPlaybackAutoRepeatMode::Track: loopState = MprisLoopState::Track; break;
		case MediaPlaybackAutoRepeatMode::List: loopState = MprisLoopState::Playlist; break;
		default: loopState = MprisLoopState::None; break;
		}
	}

	qreal rate = 1.0;
	if (auto ref = info.PlaybackRate()) rate = ref.Value();

	auto canControl = controls.IsPlayEnabled() || controls.IsPauseEnabled() || controls.IsNextEnabled()
	                || controls.IsPreviousEnabled() || controls.IsPlaybackPositionEnabled()
	                || controls.IsShuffleEnabled() || controls.IsRepeatEnabled();

	return MprisPlayer::PlaybackSnapshot {
	    .state = mapPlaybackStatus(info.PlaybackStatus()),
	    .canPlay = controls.IsPlayEnabled(),
	    .canPause = controls.IsPauseEnabled(),
	    .canSeek = controls.IsPlaybackPositionEnabled(),
	    .canGoNext = controls.IsNextEnabled(),
	    .canGoPrevious = controls.IsPreviousEnabled(),
	    .canControl = canControl,
	    .shuffle = shuffle,
	    .shuffleSupported = shuffleSupported,
	    .loopState = loopState,
	    .loopSupported = loopSupported,
	    .rate = rate,
	};
} catch (const winrt::hresult_error& e) {
	qCDebug(logMprisWorker) << "Reading playback info failed:" << Qt::hex << static_cast<uint32_t>(e.code().value);
	return std::nullopt;
}

std::optional<MprisPlayer::TimelineSnapshot> GsmtcWorker::buildTimelineSnapshot(
    const GlobalSystemMediaTransportControlsSession& session
) const try {
	auto timeline = session.GetTimelineProperties();
	if (!timeline) return std::nullopt;

	auto startTicks = timeline.StartTime().count();
	auto endTicks = timeline.EndTime().count();
	auto positionTicks = timeline.Position().count();
	auto lengthSupported = endTicks > startTicks;

	return MprisPlayer::TimelineSnapshot {
	    .basePositionTicks = positionTicks,
	    .baseTimestamp = toQDateTime(timeline.LastUpdatedTime()),
	    .lengthTicks = lengthSupported ? (endTicks - startTicks) : 0,
	    .lengthSupported = lengthSupported,
	    .positionSupported = true,
	};
} catch (const winrt::hresult_error& e) {
	qCDebug(logMprisWorker) << "Reading the timeline failed:" << Qt::hex << static_cast<uint32_t>(e.code().value);
	return std::nullopt;
}

void GsmtcWorker::refreshPlaybackInfo(quint64 sessionId) {
	auto it = this->mEntries.find(sessionId);
	if (it == this->mEntries.end()) return;

	auto snapshot = this->buildPlaybackSnapshot(it->second->session);
	if (!snapshot) return;
	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [frontend = this->mFrontend, sessionId, snapshot = *snapshot] {
		    if (frontend) frontend->backendUpdatePlaybackInfo(sessionId, snapshot);
	    },
	    Qt::QueuedConnection
	);
}

void GsmtcWorker::refreshTimeline(quint64 sessionId) {
	auto it = this->mEntries.find(sessionId);
	if (it == this->mEntries.end()) return;

	auto snapshot = this->buildTimelineSnapshot(it->second->session);
	if (!snapshot) return;
	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [frontend = this->mFrontend, sessionId, snapshot = *snapshot] {
		    if (frontend) frontend->backendUpdateTimeline(sessionId, snapshot);
	    },
	    Qt::QueuedConnection
	);
}

void GsmtcWorker::scheduleReread(quint64 sessionId) {
	auto it = this->mEntries.find(sessionId);
	if (it == this->mEntries.end()) return;
	if (++it->second->emptyReads > MAX_EMPTY_REREADS) return;

	QTimer::singleShot(EMPTY_REREAD_MS, this, [this, sessionId] { this->refreshMediaProperties(sessionId); });
}

void GsmtcWorker::updateArt(
    SessionEntry& entry,
    quint64 sessionId,
    const winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties&
        props
) {
	auto thumb = props.Thumbnail();
	if (thumb == nullptr) return;

	auto art = cacheThumbnail(thumb);
	if (art.url.isEmpty()) return;

	if (isBlankTrack(entry.trackKey)) {
		entry.cachedArtUrl = art.url;
		return;
	}

	if (!entry.staleArtHash.isEmpty() && art.hash == entry.staleArtHash
	    && entry.artRetries < MAX_ART_RETRIES)
	{
		entry.artRetries++;
		QTimer::singleShot(ART_RETRY_MS, this, [this, sessionId] {
			this->refreshMediaProperties(sessionId);
		});
		return;
	}

	entry.staleArtHash.clear();
	entry.artHash = art.hash;
	entry.cachedArtUrl = art.url;
}

void GsmtcWorker::refreshMediaProperties(quint64 sessionId) {
	auto it = this->mEntries.find(sessionId);
	if (it == this->mEntries.end()) return;
	auto& entry = *it->second;

	try {
		auto props = entry.session.TryGetMediaPropertiesAsync().get();
		if (!props) winrt::throw_hresult(winrt::hresult(static_cast<int32_t>(0x80004003)));
		auto title = toQString(props.Title());
		auto artist = toQString(props.Artist());
		auto album = toQString(props.AlbumTitle());
		auto albumArtist = toQString(props.AlbumArtist());
		auto trackKey = title + QChar(0x1f) + artist + QChar(0x1f) + album;

		if (trackKey != entry.trackKey) {
			if (!isBlankTrack(entry.trackKey)) entry.staleArtHash = entry.artHash;
			entry.trackKey = trackKey;
			entry.uniqueId++;
			entry.cachedArtUrl.clear();
			entry.artRetries = 0;
			entry.emptyReads = 0;
		}

		this->updateArt(entry, sessionId, props);

		qCDebug(logMprisWorker) << "Session" << sessionId << "title" << title << "artist" << artist
		                        << "art" << !entry.cachedArtUrl.isEmpty() << "reads" << entry.emptyReads;
		if (title.isEmpty()) this->scheduleReread(sessionId);
		else entry.emptyReads = 0;

		MprisPlayer::MediaSnapshot snapshot {
		    entry.uniqueId,
		    title,
		    artist,
		    album,
		    albumArtist,
		    entry.cachedArtUrl,
		};

		QMetaObject::invokeMethod(
		    QCoreApplication::instance(),
		    [frontend = this->mFrontend, sessionId, snapshot] {
			    if (frontend) frontend->backendUpdateMediaProperties(sessionId, snapshot);
		    },
		    Qt::QueuedConnection
		);
	} catch (const winrt::hresult_error& e) {
		qCDebug(logMprisWorker) << "TryGetMediaPropertiesAsync failed:" << Qt::hex
		                        << static_cast<uint32_t>(e.code().value);
	}
}

void GsmtcWorker::cmdPlay(quint64 sessionId) {
	auto it = this->mEntries.find(sessionId);
	if (it == this->mEntries.end()) return;
	try {
		it->second->session.TryPlayAsync().get();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMprisWorker) << "TryPlayAsync failed:" << Qt::hex << static_cast<uint32_t>(e.code().value);
	}
}

void GsmtcWorker::cmdPause(quint64 sessionId) {
	auto it = this->mEntries.find(sessionId);
	if (it == this->mEntries.end()) return;
	try {
		it->second->session.TryPauseAsync().get();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMprisWorker) << "TryPauseAsync failed:" << Qt::hex << static_cast<uint32_t>(e.code().value);
	}
}

void GsmtcWorker::cmdTogglePlayPause(quint64 sessionId) {
	auto it = this->mEntries.find(sessionId);
	if (it == this->mEntries.end()) return;
	try {
		it->second->session.TryTogglePlayPauseAsync().get();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMprisWorker) << "TryTogglePlayPauseAsync failed:" << Qt::hex
		                          << static_cast<uint32_t>(e.code().value);
	}
}

void GsmtcWorker::cmdNext(quint64 sessionId) {
	auto it = this->mEntries.find(sessionId);
	if (it == this->mEntries.end()) return;
	try {
		it->second->session.TrySkipNextAsync().get();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMprisWorker) << "TrySkipNextAsync failed:" << Qt::hex
		                          << static_cast<uint32_t>(e.code().value);
	}
}

void GsmtcWorker::cmdPrevious(quint64 sessionId) {
	auto it = this->mEntries.find(sessionId);
	if (it == this->mEntries.end()) return;
	try {
		it->second->session.TrySkipPreviousAsync().get();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMprisWorker) << "TrySkipPreviousAsync failed:" << Qt::hex
		                          << static_cast<uint32_t>(e.code().value);
	}
}

void GsmtcWorker::cmdSetPosition(quint64 sessionId, qint64 ticks) {
	auto it = this->mEntries.find(sessionId);
	if (it == this->mEntries.end()) return;
	try {
		it->second->session.TryChangePlaybackPositionAsync(ticks).get();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMprisWorker) << "TryChangePlaybackPositionAsync failed:" << Qt::hex
		                          << static_cast<uint32_t>(e.code().value);
	}
}

void GsmtcWorker::cmdSetShuffle(quint64 sessionId, bool shuffle) {
	auto it = this->mEntries.find(sessionId);
	if (it == this->mEntries.end()) return;
	try {
		it->second->session.TryChangeShuffleActiveAsync(shuffle).get();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMprisWorker) << "TryChangeShuffleActiveAsync failed:" << Qt::hex
		                          << static_cast<uint32_t>(e.code().value);
	}
}

void GsmtcWorker::cmdSetLoopState(quint64 sessionId, MprisLoopState::Enum state) {
	auto it = this->mEntries.find(sessionId);
	if (it == this->mEntries.end()) return;

	auto mode = MediaPlaybackAutoRepeatMode::None;
	switch (state) {
	case MprisLoopState::Track: mode = MediaPlaybackAutoRepeatMode::Track; break;
	case MprisLoopState::Playlist: mode = MediaPlaybackAutoRepeatMode::List; break;
	default: mode = MediaPlaybackAutoRepeatMode::None; break;
	}

	try {
		it->second->session.TryChangeAutoRepeatModeAsync(mode).get();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMprisWorker) << "TryChangeAutoRepeatModeAsync failed:" << Qt::hex
		                          << static_cast<uint32_t>(e.code().value);
	}
}

} // namespace qs::windows::services::mpris
