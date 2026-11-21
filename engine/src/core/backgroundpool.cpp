#include "backgroundpool.hpp"

#include <qthread.h>
#include <qthreadpool.h>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

QThreadPool* BackgroundThreadPool::instance() {
	static auto* pool = []() {
		auto* pool = new QThreadPool(); // NOLINT
		pool->setMaxThreadCount(2);
#ifdef Q_OS_WIN
		pool->setThreadPriority(QThread::LowPriority);
#endif
		return pool;
	}();

	return pool;
}

#ifdef Q_OS_WIN
namespace {

bool setEfficiencyMode(bool enabled) {
	THREAD_POWER_THROTTLING_STATE state {};
	state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
	state.ControlMask = enabled ? THREAD_POWER_THROTTLING_EXECUTION_SPEED : 0;
	state.StateMask = enabled ? THREAD_POWER_THROTTLING_EXECUTION_SPEED : 0;
	return SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state))
	    != FALSE;
}

} // namespace
#endif

BackgroundWorkScope::BackgroundWorkScope(bool lowIoPriority) {
#ifdef Q_OS_WIN
	if (QThread::isMainThread()) return;

	this->mEfficiencyMode = setEfficiencyMode(true);

	if (lowIoPriority) {
		this->mBackgroundMode =
		    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN) != FALSE;
	}
#else
	Q_UNUSED(lowIoPriority);
#endif
}

BackgroundWorkScope::~BackgroundWorkScope() {
#ifdef Q_OS_WIN
	if (this->mBackgroundMode) SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
	if (this->mEfficiencyMode) setEfficiencyMode(false);
#endif
}
