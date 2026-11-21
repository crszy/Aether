#include "pw_object_tracker.hpp"

#include <qobject.h>

namespace qs::windows::services::pipewire {

PwObjectTracker::~PwObjectTracker() {
	for (auto* object: this->mObjects) {
		if (object != nullptr) QObject::disconnect(object, nullptr, this, nullptr);
	}
}

void PwObjectTracker::setObjects(const QList<QObject*>& objects) {
	for (auto* object: this->mObjects) {
		if (object != nullptr) QObject::disconnect(object, nullptr, this, nullptr);
	}

	this->mObjects = objects;

	for (auto* object: this->mObjects) {
		if (object != nullptr) {
			QObject::connect(object, &QObject::destroyed, this, &PwObjectTracker::onObjectDestroyed);
		}
	}

	emit this->objectsChanged();
}

void PwObjectTracker::onObjectDestroyed(QObject* object) {
	if (this->mObjects.removeOne(object)) {
		emit this->objectsChanged();
	}
}

} // namespace qs::windows::services::pipewire
