#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::windows::services::pipewire {

class PwNodeType: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Flag : quint8 {
		Untracked = 0b0,
		Audio = 0b1,
		Video = 0b10,
		Stream = 0b100,
		Source = 0b1000,
		Sink = 0b10000,
		AudioSink = Audio | Sink,
		AudioSource = Audio | Source,
		AudioDuplex = Audio | Sink | Source,
		AudioOutStream = Audio | Sink | Stream,
		AudioInStream = Audio | Source | Stream,
		VideoSource = Video | Source,
		VideoSink = Video | Sink,
	};
	Q_ENUM(Flag);
	Q_DECLARE_FLAGS(Flags, Flag);

	Q_INVOKABLE static QString
	toString(qs::windows::services::pipewire::PwNodeType::Flags type);
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PwNodeType::Flags)

class PwAudioChannel: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum {
		Unknown = 0,
		NA = 1,
		Mono = 2,
		FrontCenter = 3,
		FrontLeft = 4,
		FrontRight = 5,
		FrontLeftCenter = 6,
		FrontRightCenter = 7,
		FrontLeftWide = 8,
		FrontRightWide = 9,
		FrontCenterHigh = 10,
		FrontLeftHigh = 11,
		FrontRightHigh = 12,
		LowFrequencyEffects = 13,
		LowFrequencyEffects2 = 14,
		LowFrequencyEffectsLeft = 15,
		LowFrequencyEffectsRight = 16,
		SideLeft = 17,
		SideRight = 18,
		RearCenter = 19,
		RearLeft = 20,
		RearRight = 21,
		RearLeftCenter = 22,
		RearRightCenter = 23,
		TopCenter = 24,
		TopFrontCenter = 25,
		TopFrontLeft = 26,
		TopFrontRight = 27,
		TopFrontLeftCenter = 28,
		TopFrontRightCenter = 29,
		TopSideLeft = 30,
		TopSideRight = 31,
		TopRearCenter = 32,
		TopRearLeft = 33,
		TopRearRight = 34,
		BottomCenter = 35,
		BottomLeftCenter = 36,
		BottomRightCenter = 37,
		AuxRangeStart = 38,
		AuxRangeEnd = 39,
		CustomRangeStart = 40,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::services::pipewire::PwAudioChannel::Enum value);
};

class PwLinkState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum {
		Error = 0,
		Unlinked = 1,
		Init = 2,
		Negotiating = 3,
		Allocating = 4,
		Paused = 5,
		Active = 6,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::services::pipewire::PwLinkState::Enum value);
};

} // namespace qs::windows::services::pipewire
