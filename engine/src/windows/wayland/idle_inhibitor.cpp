#include "idle_inhibitor.hpp"

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>

namespace qs::windows::wayland {

namespace {
Q_LOGGING_CATEGORY(logIdleInhibitor, "quickshell.windows.idleinhibitor", QtWarningMsg);
}

IdleInhibitor::IdleInhibitor(QObject* parent): QObject(parent) {
	QObject::connect(this, &IdleInhibitor::enabledChanged, this, &IdleInhibitor::update);
}

IdleInhibitor::~IdleInhibitor() {
	if (this->request != nullptr) {
		if (this->active) {
			PowerClearRequest(this->request, PowerRequestDisplayRequired);
			PowerClearRequest(this->request, PowerRequestSystemRequired);
		}

		CloseHandle(this->request);
	}
}

void IdleInhibitor::setWindow(QObject* window) {
	if (window == this->mWindow) return;
	this->mWindow = window;
	emit this->windowChanged();
}

void IdleInhibitor::update() {
	auto enabled = this->bEnabled.value();
	if (enabled == this->active) return;

	if (this->request == nullptr) {
		auto reason = REASON_CONTEXT {};
		reason.Version = POWER_REQUEST_CONTEXT_VERSION;
		reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
		reason.Reason.SimpleReasonString = const_cast<LPWSTR>(L"Quickshell idle inhibitor"); // NOLINT
		this->request = PowerCreateRequest(&reason);

		if (this->request == INVALID_HANDLE_VALUE) {
			this->request = nullptr;
			qCWarning(logIdleInhibitor) << "PowerCreateRequest failed:" << GetLastError();
			return;
		}
	}

	if (enabled) {
		PowerSetRequest(this->request, PowerRequestDisplayRequired);
		PowerSetRequest(this->request, PowerRequestSystemRequired);
	} else {
		PowerClearRequest(this->request, PowerRequestDisplayRequired);
		PowerClearRequest(this->request, PowerRequestSystemRequired);
	}

	this->active = enabled;
}

} // namespace qs::windows::wayland
