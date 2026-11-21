#include "pw_link.hpp"

#include <qobject.h>
#include <qqmllist.h>
#include <qtypes.h>

#include "pipewire.hpp"

namespace qs::windows::services::pipewire {

void PwNodeLinkTracker::setNode(PwNode* node) {
	if (node == this->mNode) return;

	if (this->mNode != nullptr) {
		QObject::disconnect(this->mNode, nullptr, this, nullptr);
	}

	this->mNode = node;

	if (node != nullptr) {
		QObject::connect(node, &QObject::destroyed, this, [this]() {
			this->mNode = nullptr;
			this->refresh();
		});
	}

	this->refresh();
	emit this->nodeChanged();
}

void PwNodeLinkTracker::refresh() {
	QVector<PwLinkGroup*> groups;

	if (this->mNode != nullptr) {
		auto* pw = Pipewire::instance();
		if (pw != nullptr) {
			for (auto* group: pw->linkGroupList()) {
				if (group->source() == this->mNode || group->target() == this->mNode) {
					groups.append(group);
				}
			}
		}
	}

	if (groups != this->mLinkGroups) {
		this->mLinkGroups = groups;
		emit this->linkGroupsChanged();
	}
}

QQmlListProperty<PwLinkGroup> PwNodeLinkTracker::linkGroups() {
	return QQmlListProperty<PwLinkGroup>(
	    this,
	    this,
	    &PwNodeLinkTracker::linkGroupsCount,
	    &PwNodeLinkTracker::linkGroupAt
	);
}

qsizetype PwNodeLinkTracker::linkGroupsCount(QQmlListProperty<PwLinkGroup>* property) {
	return static_cast<PwNodeLinkTracker*>(property->data)->mLinkGroups.length();
}

PwLinkGroup* PwNodeLinkTracker::linkGroupAt(QQmlListProperty<PwLinkGroup>* property, qsizetype index) {
	return static_cast<PwNodeLinkTracker*>(property->data)->mLinkGroups.at(index);
}

} // namespace qs::windows::services::pipewire
