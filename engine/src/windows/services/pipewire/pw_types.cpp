#include "pw_types.hpp"

#include <qstring.h>

namespace qs::windows::services::pipewire {

QString PwNodeType::toString(qs::windows::services::pipewire::PwNodeType::Flags type) {
	switch (static_cast<quint8>(type.toInt())) {
	case AudioSink: return "AudioSink";
	case AudioSource: return "AudioSource";
	case AudioDuplex: return "AudioDuplex";
	case AudioOutStream: return "AudioOutStream";
	case AudioInStream: return "AudioInStream";
	case VideoSource: return "VideoSource";
	case VideoSink: return "VideoSink";
	case Audio: return "Audio";
	case Video: return "Video";
	case Stream: return "Stream";
	case Source: return "Source";
	case Sink: return "Sink";
	default: return "Untracked";
	}
}

QString PwAudioChannel::toString(qs::windows::services::pipewire::PwAudioChannel::Enum value) {
	return "channel" + QString::number(static_cast<int>(value));
}

QString PwLinkState::toString(qs::windows::services::pipewire::PwLinkState::Enum value) {
	switch (value) {
	case Unlinked: return "Unlinked";
	case Init: return "Init";
	case Negotiating: return "Negotiating";
	case Allocating: return "Allocating";
	case Paused: return "Paused";
	case Active: return "Active";
	default: return "Error";
	}
}

} // namespace qs::windows::services::pipewire
