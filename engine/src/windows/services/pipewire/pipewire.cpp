#include "pipewire.hpp"

#include <memory>

#include <qobject.h>

#include "pw_backend.hpp"
#include "pw_link.hpp"
#include "pw_node.hpp"

namespace qs::windows::services::pipewire {

namespace {
Pipewire* sInstance = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
} // namespace

Pipewire::Pipewire(QObject* parent): QObject(parent) {
	if (sInstance == nullptr) sInstance = this;

	this->backend = std::make_unique<PwBackend>(this);
	this->backend->start();
}

Pipewire::~Pipewire() {
	this->backend.reset();
	if (sInstance == this) sInstance = nullptr;
}

Pipewire* Pipewire::instance() { return sInstance; }

void Pipewire::setPreferredDefaultAudioSink(PwNode* node) {
	this->backend->setPreferredDefault(node, true);
}

void Pipewire::setPreferredDefaultAudioSource(PwNode* node) {
	this->backend->setPreferredDefault(node, false);
}

void Pipewire::backendAddNode(PwNode* node) { this->mNodes.insertObject(node); }

void Pipewire::backendRemoveNode(PwNode* node) {
	if (this->mDefaultSink == node) this->backendSetDefaultSink(nullptr);
	if (this->mDefaultSource == node) this->backendSetDefaultSource(nullptr);

	this->mNodes.removeObject(node);
	node->deleteLater();
}

void Pipewire::backendAddLinkGroup(PwLinkGroup* group) { this->mLinkGroups.insertObject(group); }

void Pipewire::backendRemoveLinkGroup(PwLinkGroup* group) {
	this->mLinkGroups.removeObject(group);
	group->deleteLater();
}

void Pipewire::backendSetDefaultSink(PwNode* node) {
	if (node == this->mDefaultSink) return;
	this->mDefaultSink = node;
	emit this->defaultAudioSinkChanged();
}

void Pipewire::backendSetDefaultSource(PwNode* node) {
	if (node == this->mDefaultSource) return;
	this->mDefaultSource = node;
	emit this->defaultAudioSourceChanged();
}

void Pipewire::backendSetReady(bool ready) {
	if (ready == this->mReady) return;
	this->mReady = ready;
	emit this->readyChanged();
}

} // namespace qs::windows::services::pipewire
