#pragma once

#include <qcontainerfwd.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>
#include <qvector.h>

#include "pw_types.hpp"

struct IAudioEndpointVolume;
struct ISimpleAudioVolume;
struct IAudioMeterInformation;
struct IMMDevice;

namespace qs::windows::services::pipewire {

class PwNode;

class PwNodeAudio: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged);
	Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged);
	Q_PROPERTY(QVector<qs::windows::services::pipewire::PwAudioChannel::Enum> channels READ channels NOTIFY channelsChanged);
	Q_PROPERTY(QVector<float> volumes READ volumes WRITE setVolumes NOTIFY volumesChanged);
	// clang-format on
	QML_NAMED_ELEMENT(PwNodeAudio);
	QML_UNCREATABLE("PwNodeAudio cannot be created directly");

public:
	explicit PwNodeAudio(QObject* parent = nullptr): QObject(parent) {}
	~PwNodeAudio() override;
	Q_DISABLE_COPY_MOVE(PwNodeAudio);

	void bindEndpoint(IAudioEndpointVolume* endpointVolume);
	void bindSession(ISimpleAudioVolume* sessionVolume);

	[[nodiscard]] bool isMuted() const;
	void setMuted(bool muted);

	[[nodiscard]] float volume() const;
	void setVolume(float volume);

	[[nodiscard]] QVector<PwAudioChannel::Enum> channels() const;

	[[nodiscard]] QVector<float> volumes() const;
	void setVolumes(const QVector<float>& volumes);

	void refreshFromDevice();

signals:
	void mutedChanged();
	void volumeChanged();
	void channelsChanged();
	void volumesChanged();

private:
	void readState();
	[[nodiscard]] float readVolume() const;

	IAudioEndpointVolume* mEndpointVolume = nullptr;
	ISimpleAudioVolume* mSessionVolume = nullptr;
	void* mEndpointCallback = nullptr;
	bool mMuted = false;
	float mVolume = 0.0F;
	QVector<float> mVolumes;
};

class PwNode: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(quint32 id READ id CONSTANT);
	Q_PROPERTY(QString name READ name CONSTANT);
	Q_PROPERTY(QString description READ description CONSTANT);
	Q_PROPERTY(QString nickname READ nickname CONSTANT);
	Q_PROPERTY(bool isSink READ isSink CONSTANT);
	Q_PROPERTY(bool isStream READ isStream CONSTANT);
	Q_PROPERTY(qs::windows::services::pipewire::PwNodeType::Flags type READ type CONSTANT);
	Q_PROPERTY(QVariantMap properties READ properties NOTIFY propertiesChanged);
	Q_PROPERTY(qs::windows::services::pipewire::PwNodeAudio* audio READ audio CONSTANT);
	Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged);
	// clang-format on
	QML_NAMED_ELEMENT(PwNode);
	QML_UNCREATABLE("PwNodes cannot be created directly");

public:
	explicit PwNode(QObject* parent = nullptr);
	~PwNode() override;
	Q_DISABLE_COPY_MOVE(PwNode);

	[[nodiscard]] quint32 id() const { return this->mId; }
	void setId(quint32 id) { this->mId = id; }

	[[nodiscard]] QString name() const { return this->mName; }
	void setName(const QString& name) { this->mName = name; }

	[[nodiscard]] QString description() const { return this->mDescription; }
	void setDescription(const QString& description) { this->mDescription = description; }

	[[nodiscard]] QString nickname() const { return this->mNickname; }
	void setNickname(const QString& nickname) { this->mNickname = nickname; }

	[[nodiscard]] bool isSink() const { return this->mType.testFlag(PwNodeType::Sink); }
	[[nodiscard]] bool isStream() const { return this->mType.testFlag(PwNodeType::Stream); }

	[[nodiscard]] PwNodeType::Flags type() const { return this->mType; }
	void setType(PwNodeType::Flags type) { this->mType = type; }

	[[nodiscard]] QVariantMap properties() const { return this->mProperties; }
	void setProperties(const QVariantMap& properties);

	[[nodiscard]] PwNodeAudio* audio() const { return this->mAudio; }

	[[nodiscard]] bool isReady() const { return this->mReady; }
	void setReady(bool ready);

	[[nodiscard]] const QString& backendKey() const { return this->mBackendKey; }
	void setBackendKey(const QString& key) { this->mBackendKey = key; }

	void setMeterDevice(IMMDevice* device);
	[[nodiscard]] IAudioMeterInformation* meterInformation();

signals:
	void propertiesChanged();
	void readyChanged();

private:
	quint32 mId = 0;
	QString mName;
	QString mDescription;
	QString mNickname;
	PwNodeType::Flags mType = PwNodeType::Untracked;
	QVariantMap mProperties;
	PwNodeAudio* mAudio = nullptr;
	bool mReady = false;
	QString mBackendKey;
	IMMDevice* mMeterDevice = nullptr;
	IAudioMeterInformation* mMeter = nullptr;
};

} // namespace qs::windows::services::pipewire
