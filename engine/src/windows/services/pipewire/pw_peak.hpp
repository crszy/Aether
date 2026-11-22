#pragma once

#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtimer.h>
#include <qvector.h>

#include "pw_node.hpp"
#include "pw_types.hpp"

namespace qs::windows::services::pipewire {

class PwNodePeakMonitor: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(qs::windows::services::pipewire::PwNode* node READ node WRITE setNode NOTIFY nodeChanged);
	Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged);
	Q_PROPERTY(QVector<float> peaks READ peaks NOTIFY peaksChanged);
	Q_PROPERTY(float peak READ peak NOTIFY peakChanged);
	Q_PROPERTY(QVector<qs::windows::services::pipewire::PwAudioChannel::Enum> channels READ channels NOTIFY channelsChanged);
	// clang-format on
	QML_ELEMENT;

public:
	explicit PwNodePeakMonitor(QObject* parent = nullptr);
	Q_DISABLE_COPY_MOVE(PwNodePeakMonitor);

	[[nodiscard]] PwNode* node() const { return this->mNode; }
	void setNode(PwNode* node);

	[[nodiscard]] bool isEnabled() const { return this->mEnabled; }
	void setEnabled(bool enabled);

	[[nodiscard]] QVector<float> peaks() const { return this->mPeaks; }
	[[nodiscard]] float peak() const { return this->mPeak; }
	[[nodiscard]] QVector<PwAudioChannel::Enum> channels() const { return this->mChannels; }

signals:
	void nodeChanged();
	void enabledChanged();
	void peaksChanged();
	void peakChanged();
	void channelsChanged();

private:
	void updateTimerState();
	void poll();
	void clear();

	QPointer<PwNode> mNode;
	bool mEnabled = true;
	QVector<float> mPeaks;
	float mPeak = 0.0F;
	QVector<PwAudioChannel::Enum> mChannels;
	QTimer mTimer;
};

} // namespace qs::windows::services::pipewire
