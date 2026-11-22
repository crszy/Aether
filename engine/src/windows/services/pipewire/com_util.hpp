#pragma once

#include <utility>

#include <qmetaobject.h>
#include <qmutex.h>
#include <qnamespace.h>

namespace qs::windows::services::pipewire {

template <typename T> class ComCallbackTarget {
public:
	explicit ComCallbackTarget(T* target): mTarget(target) {}
	virtual ~ComCallbackTarget() = default;

	void detach() {
		QMutexLocker locker(&this->mMutex);
		this->mTarget = nullptr;
	}

protected:
	template <typename Fn>
	bool post(Fn&& fn) {
		QMutexLocker locker(&this->mMutex);
		auto* target = this->mTarget;
		if (target == nullptr) return false;

		QMetaObject::invokeMethod(
		    target,
		    [target, fn = std::forward<Fn>(fn)]() { fn(target); },
		    Qt::QueuedConnection
		);
		return true;
	}

private:
	QMutex mMutex;
	T* mTarget;
};

} // namespace qs::windows::services::pipewire
