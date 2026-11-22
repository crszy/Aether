#pragma once

#include <memory>

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qvector.h>

#include "../../../core/model.hpp"
#include "pw_link.hpp"
#include "pw_node.hpp"

namespace qs::windows::services::pipewire {

class PwBackend;

class Pipewire: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(UntypedObjectModel* nodes READ nodes CONSTANT);
	Q_PROPERTY(UntypedObjectModel* links READ links CONSTANT);
	Q_PROPERTY(UntypedObjectModel* linkGroups READ linkGroups CONSTANT);
	Q_PROPERTY(qs::windows::services::pipewire::PwNode* defaultAudioSink READ defaultAudioSink NOTIFY defaultAudioSinkChanged);
	Q_PROPERTY(qs::windows::services::pipewire::PwNode* defaultAudioSource READ defaultAudioSource NOTIFY defaultAudioSourceChanged);
	Q_PROPERTY(qs::windows::services::pipewire::PwNode* preferredDefaultAudioSink READ defaultAudioSink WRITE setPreferredDefaultAudioSink NOTIFY defaultAudioSinkChanged);
	Q_PROPERTY(qs::windows::services::pipewire::PwNode* preferredDefaultAudioSource READ defaultAudioSource WRITE setPreferredDefaultAudioSource NOTIFY defaultAudioSourceChanged);
	Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged);
	// clang-format on
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Pipewire(QObject* parent = nullptr);
	~Pipewire() override;
	Q_DISABLE_COPY_MOVE(Pipewire);

	[[nodiscard]] UntypedObjectModel* nodes() { return &this->mNodes; }
	[[nodiscard]] UntypedObjectModel* links() { return &this->mLinks; }
	[[nodiscard]] UntypedObjectModel* linkGroups() { return &this->mLinkGroups; }

	[[nodiscard]] PwNode* defaultAudioSink() const { return this->mDefaultSink; }
	[[nodiscard]] PwNode* defaultAudioSource() const { return this->mDefaultSource; }

	void setPreferredDefaultAudioSink(PwNode* node);
	void setPreferredDefaultAudioSource(PwNode* node);

	[[nodiscard]] bool isReady() const { return this->mReady; }

	[[nodiscard]] const QList<PwLinkGroup*>& linkGroupList() const { return this->mLinkGroups.valueList(); }

	static Pipewire* instance();

	void backendAddNode(PwNode* node);
	void backendRemoveNode(PwNode* node);
	void backendAddLinkGroup(PwLinkGroup* group);
	void backendRemoveLinkGroup(PwLinkGroup* group);
	void backendSetDefaultSink(PwNode* node);
	void backendSetDefaultSource(PwNode* node);
	void backendSetReady(bool ready);

signals:
	void defaultAudioSinkChanged();
	void defaultAudioSourceChanged();
	void readyChanged();

private:
	ObjectModel<PwNode> mNodes {this};
	ObjectModel<PwLink> mLinks {this};
	ObjectModel<PwLinkGroup> mLinkGroups {this};

	PwNode* mDefaultSink = nullptr;
	PwNode* mDefaultSource = nullptr;
	bool mReady = false;

	std::unique_ptr<PwBackend> backend;
};

} // namespace qs::windows::services::pipewire
