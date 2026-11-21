#include "qml.hpp"

#include <qobject.h>

#include "host.hpp"
#include "item.hpp"

namespace qs::windows::services::systray {

SystemTray::SystemTray(QObject* parent): QObject(parent) {
	auto* host = TrayHost::instance();

	QObject::connect(host, &TrayHost::itemAdded, this, [this](SystemTrayItem* item) {
		this->mItems.insertObject(item);
	});

	QObject::connect(host, &TrayHost::itemRemoved, this, [this](SystemTrayItem* item) {
		this->mItems.removeObject(item);
	});

	for (auto* item: host->items()) {
		this->mItems.insertObject(item);
	}
}

} // namespace qs::windows::services::systray
