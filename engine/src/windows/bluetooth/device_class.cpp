#include "device_class.hpp"

#include <qstring.h>
#include <qtypes.h>

namespace qs::bluetooth {

QString iconForClassOfDevice(quint32 cod) {
	const auto major = (cod >> 8) & 0x1f;
	const auto minor = (cod >> 2) & 0x3f;

	switch (major) {
	case 0x01: return QStringLiteral("computer");
	case 0x02:
		return minor == 0x04 ? QStringLiteral("modem") : QStringLiteral("phone");
	case 0x03: return QStringLiteral("network-wireless");
	case 0x04:
		switch (minor) {
		case 0x01:
		case 0x02:
			return QStringLiteral("audio-headset");
		case 0x06: return QStringLiteral("audio-headphones");
		case 0x04: return QStringLiteral("audio-input-microphone");
		case 0x0b:
		case 0x0c:
		case 0x0d:
			return QStringLiteral("camera-video");
		case 0x0e:
		case 0x0f:
			return QStringLiteral("video-display");
		case 0x10: return QStringLiteral("camera-web");
		case 0x12: return QStringLiteral("input-gaming");
		default: return QStringLiteral("audio-card");
		}
	case 0x05:
		switch (minor & 0x30) {
		case 0x10: return QStringLiteral("input-keyboard");
		case 0x20: return QStringLiteral("input-mouse");
		case 0x30: return QStringLiteral("input-keyboard");
		default: break;
		}
		switch (minor & 0x0f) {
		case 0x01:
		case 0x02:
			return QStringLiteral("input-gaming");
		case 0x05:
		case 0x07:
			return QStringLiteral("input-tablet");
		default: return {};
		}
	case 0x06:
		if (minor & 0x20) return QStringLiteral("printer");
		if (minor & 0x10) return QStringLiteral("scanner");
		if (minor & 0x08) return QStringLiteral("camera-photo");
		if (minor & 0x04) return QStringLiteral("video-display");
		return {};
	case 0x08: return QStringLiteral("input-gaming");
	default: return {};
	}
}

QString iconForAppearance(quint16 appearance) {
	const auto category = appearance >> 6;
	const auto subcategory = appearance & 0x3f;

	switch (category) {
	case 0x001: return QStringLiteral("phone");
	case 0x002: return QStringLiteral("computer");
	case 0x005: return QStringLiteral("video-display");
	case 0x00a: return QStringLiteral("multimedia-player");
	case 0x00b: return QStringLiteral("scanner");
	case 0x00f:
		switch (subcategory) {
		case 0x01: return QStringLiteral("input-keyboard");
		case 0x02: return QStringLiteral("input-mouse");
		case 0x03:
		case 0x04:
			return QStringLiteral("input-gaming");
		case 0x05:
		case 0x07:
			return QStringLiteral("input-tablet");
		case 0x08: return QStringLiteral("scanner");
		case 0x09: return QStringLiteral("input-touchpad");
		default: return {};
		}
	case 0x021: return QStringLiteral("audio-speakers");
	case 0x022: return QStringLiteral("audio-input-microphone");
	case 0x025:
		return subcategory == 0x03 ? QStringLiteral("audio-headphones") : QStringLiteral("audio-headset");
	case 0x029: return QStringLiteral("audio-headphones");
	case 0x02a: return QStringLiteral("input-gaming");
	default: return {};
	}
}

} // namespace qs::bluetooth
