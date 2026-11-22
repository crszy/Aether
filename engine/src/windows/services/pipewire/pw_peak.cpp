#include "pw_peak.hpp"

#include <qobject.h>
#include <qtypes.h>

#include <endpointvolume.h>

namespace qs::windows::services::pipewire {

namespace {
constexpr auto PollIntervalMs = 33;
}

PwNodePeakMonitor::PwNodePeakMonitor(QObject* parent): QObject(parent) {
	this->mTimer.setInterval(PollIntervalMs);
	QObject::connect(&this->mTimer, &QTimer::timeout, this, &PwNodePeakMonitor::poll);
}

void PwNodePeakMonitor::setNode(PwNode* node) {
	if (node == this->mNode) return;

	if (this->mNode != nullptr) {
		QObject::disconnect(this->mNode, nullptr, this, nullptr);
	}

	this->mNode = node;

	if (node != nullptr) {
		QObject::connect(node, &QObject::destroyed, this, [this]() {
			this->mNode = nullptr;
			this->clear();
			this->updateTimerState();
		});
	}

	this->clear();
	this->updateTimerState();
	emit this->nodeChanged();
}

void PwNodePeakMonitor::setEnabled(bool enabled) {
	if (enabled == this->mEnabled) return;
	this->mEnabled = enabled;
	if (!enabled) this->clear();
	this->updateTimerState();
	emit this->enabledChanged();
}

void PwNodePeakMonitor::updateTimerState() {
	auto shouldRun = this->mEnabled && this->mNode != nullptr;
	if (shouldRun && !this->mTimer.isActive()) {
		this->mTimer.start();
	} else if (!shouldRun && this->mTimer.isActive()) {
		this->mTimer.stop();
	}
}

void PwNodePeakMonitor::clear() {
	if (!this->mPeaks.isEmpty()) {
		this->mPeaks.clear();
		emit this->peaksChanged();
	}

	if (this->mPeak != 0.0F) {
		this->mPeak = 0.0F;
		emit this->peakChanged();
	}

	if (!this->mChannels.isEmpty()) {
		this->mChannels.clear();
		emit this->channelsChanged();
	}
}

void PwNodePeakMonitor::poll() {
	if (this->mNode == nullptr) return;

	auto* meter = this->mNode->meterInformation();
	if (meter == nullptr) {
		this->clear();
		return;
	}

	float peak = 0.0F;
	if (FAILED(meter->GetPeakValue(&peak))) {
		this->clear();
		return;
	}

	UINT channelCount = 0;
	meter->GetMeteringChannelCount(&channelCount);

	QVector<float> peaks(static_cast<qsizetype>(channelCount), 0.0F);
	if (channelCount > 0) {
		meter->GetChannelsPeakValues(channelCount, peaks.data());
	}

	if (peaks != this->mPeaks) {
		this->mPeaks = peaks;
		emit this->peaksChanged();
	}

	if (peak != this->mPeak) {
		this->mPeak = peak;
		emit this->peakChanged();
	}

	if (this->mChannels.length() != peaks.length()) {
		this->mChannels = QVector<PwAudioChannel::Enum>(peaks.length(), PwAudioChannel::Unknown);
		emit this->channelsChanged();
	}
}

} // namespace qs::windows::services::pipewire
