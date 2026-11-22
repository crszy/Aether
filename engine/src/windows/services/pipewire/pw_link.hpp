#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qqmllist.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvector.h>

#include "pw_node.hpp"
#include "pw_types.hpp"

namespace qs::windows::services::pipewire {

class PwLink: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(quint32 id READ id CONSTANT);
	Q_PROPERTY(qs::windows::services::pipewire::PwNode* target READ target CONSTANT);
	Q_PROPERTY(qs::windows::services::pipewire::PwNode* source READ source CONSTANT);
	Q_PROPERTY(qs::windows::services::pipewire::PwLinkState::Enum state READ state NOTIFY stateChanged);
	// clang-format on
	QML_NAMED_ELEMENT(PwLink);
	QML_UNCREATABLE("PwLinks cannot be created directly");

public:
	explicit PwLink(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] quint32 id() const { return this->mId; }
	[[nodiscard]] PwNode* target() const { return this->mTarget; }
	[[nodiscard]] PwNode* source() const { return this->mSource; }
	[[nodiscard]] PwLinkState::Enum state() const { return PwLinkState::Active; }

signals:
	void stateChanged();

private:
	quint32 mId = 0;
	PwNode* mTarget = nullptr;
	PwNode* mSource = nullptr;
};

class PwLinkGroup: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(qs::windows::services::pipewire::PwNode* target READ target CONSTANT);
	Q_PROPERTY(qs::windows::services::pipewire::PwNode* source READ source CONSTANT);
	Q_PROPERTY(qs::windows::services::pipewire::PwLinkState::Enum state READ state NOTIFY stateChanged);
	// clang-format on
	QML_NAMED_ELEMENT(PwLinkGroup);
	QML_UNCREATABLE("PwLinkGroups cannot be created directly");

public:
	explicit PwLinkGroup(PwNode* source, PwNode* target, QObject* parent = nullptr)
	    : QObject(parent)
	    , mSource(source)
	    , mTarget(target) {}

	[[nodiscard]] PwNode* target() const { return this->mTarget; }
	[[nodiscard]] PwNode* source() const { return this->mSource; }
	[[nodiscard]] PwLinkState::Enum state() const { return PwLinkState::Active; }

signals:
	void stateChanged();

private:
	PwNode* mSource;
	PwNode* mTarget;
};

class PwNodeLinkTracker: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(qs::windows::services::pipewire::PwNode* node READ node WRITE setNode NOTIFY nodeChanged);
	Q_PROPERTY(QQmlListProperty<qs::windows::services::pipewire::PwLinkGroup> linkGroups READ linkGroups NOTIFY linkGroupsChanged);
	// clang-format on
	QML_ELEMENT;

public:
	explicit PwNodeLinkTracker(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] PwNode* node() const { return this->mNode; }
	void setNode(PwNode* node);

	[[nodiscard]] QQmlListProperty<PwLinkGroup> linkGroups();

	void refresh();

signals:
	void nodeChanged();
	void linkGroupsChanged();

private:
	static qsizetype linkGroupsCount(QQmlListProperty<PwLinkGroup>* property);
	static PwLinkGroup* linkGroupAt(QQmlListProperty<PwLinkGroup>* property, qsizetype index);

	PwNode* mNode = nullptr;
	QVector<PwLinkGroup*> mLinkGroups;
};

} // namespace qs::windows::services::pipewire
