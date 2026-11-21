#include "panel_effects.hpp"
#include <algorithm>
#include <memory>
#include <utility>

#include <qbytearray.h>
#include <qcoreapplication.h>
#include <qfile.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlcomponent.h>
#include <qqmlengine.h>
#include <qqmlfile.h>
#include <qquickitem.h>
#include <qquickwindow.h>
#include <qrect.h>
#include <qsize.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtypes.h>
#include <qurl.h>
#include <qvariant.h>
#include <qvectornd.h>
#include <rhi/qshader.h>

#include "panel_animation.hpp"

namespace qs::windows {

namespace {
Q_LOGGING_CATEGORY(logEffects, "quickshell.windows.panel.effects", QtWarningMsg);

constexpr int UNLOAD_MS = 120000;
constexpr int SHADER_ERROR = 2;

const auto EFFECT_QML = QByteArray(R"(
import QtQuick

ShaderEffect {
	id: effect
	property Item sourceItem: null
	property bool mipmapped: false
	property size itemSize: Qt.size(width, height)
	property vector2d blurStep: Qt.vector2d(0, 0)
	property real blurLod: 0
	property real dissolve: 1
	property real noiseScale: 0.045
	property rect revealRect: Qt.rect(0, 0, 0, 0)
	property real revealRadius: -1
	property Item source: layer
	anchors.fill: parent

	ShaderEffectSource {
		id: layer
		sourceItem: effect.sourceItem
		hideSource: true
		mipmap: effect.mipmapped
		visible: false
	}
}
)");

} // namespace

PanelEffectKeepAlive::PanelEffectKeepAlive(int intervalMs, QObject* parent): QObject(parent) {
	this->timer.setSingleShot(true);
	this->timer.setInterval(intervalMs);
	QObject::connect(&this->timer, &QTimer::timeout, this, &PanelEffectKeepAlive::onTimeout);
}

void PanelEffectKeepAlive::acquire() {
	this->mUsers++;
	this->timer.stop();
}

void PanelEffectKeepAlive::release() {
	if (this->mUsers > 0) this->mUsers--;
	if (this->mUsers == 0) this->timer.start();
}

void PanelEffectKeepAlive::arm() {
	if (this->mUsers == 0) this->timer.start();
}

void PanelEffectKeepAlive::onTimeout() {
	if (this->mUsers == 0) emit this->expired();
}

PanelEffectLayer::PanelEffectLayer(PanelEffects* owner, QQuickItem* item)
    : effects(owner)
    , mItem(item) {
	if (owner != nullptr) owner->keepAlive.acquire();
	if (item == nullptr) return;

	const auto* meta = item->metaObject();
	auto signal = meta->method(meta->indexOfSignal("statusChanged()"));
	auto slot = this->metaObject()->method(this->metaObject()->indexOfSlot("onStatusChanged()"));
	if (signal.isValid() && slot.isValid()) QObject::connect(item, signal, this, slot);
}

PanelEffectLayer::~PanelEffectLayer() {
	delete this->mItem.data();
	if (this->effects != nullptr) this->effects->layerDone();
}

void PanelEffectLayer::onStatusChanged() {
	auto* item = this->mItem.data();
	if (item == nullptr || !PanelEffects::shaderBroken(item)) return;

	if (this->effects != nullptr) {
		this->effects->fail(QString("%1 did not load: %2")
		                        .arg(item->property("fragmentShader").toString())
		                        .arg(item->property("log").toString().trimmed()));
	}

	item->setProperty("sourceItem", QVariant::fromValue<QQuickItem*>(nullptr));
	item->setVisible(false);
	item->deleteLater();
}

void PanelEffectLayer::apply(const PanelEffectFrame& frame, const QSizeF& size, qreal dpr) {
	auto* item = this->mItem.data();
	if (item == nullptr) return;

	auto sampling = panelBlurSampling(frame.blurRadius, size, dpr);
	item->setProperty("blurStep", QVector2D(sampling.step));
	item->setProperty("blurLod", sampling.lod);
	item->setProperty("dissolve", frame.dissolve);
	item->setProperty("revealRect", frame.revealRect);
	item->setProperty("revealRadius", frame.revealRadius);
}

PanelEffects::PanelEffects(QUrl shaderUrl, int unloadMs, QObject* parent)
    : QObject(parent)
    , shader(std::move(shaderUrl))
    , keepAlive(unloadMs) {
	QObject::connect(&this->keepAlive, &PanelEffectKeepAlive::expired, this, &PanelEffects::unload);
}

PanelEffects::~PanelEffects() {
	for (auto& entry: this->components) {
		QObject::disconnect(entry.guard);
		if (entry.engine != nullptr) delete entry.component.data();
	}
}

PanelEffects* PanelEffects::instance() {
	static QPointer<PanelEffects> effects;

	if (effects == nullptr) {
		effects = new PanelEffects(defaultShader(), defaultUnloadMs(), QCoreApplication::instance());
	}

	return effects;
}

QUrl PanelEffects::defaultShader() {
	return QUrl("qrc:/Quickshell/Windows/shaders/panel_effect.frag.qsb");
}

int PanelEffects::defaultUnloadMs() { return UNLOAD_MS; }

std::unique_ptr<PanelEffectLayer> PanelEffects::createLayer(
    QQmlEngine* engine,
    QObject* engineGuard,
    QQuickItem* root,
    QQuickItem* source,
    bool mipmap
) {
	if (this->mFailed || engine == nullptr || root == nullptr || source == nullptr) return nullptr;
	if (!this->checkShader()) return nullptr;

	auto* component = this->componentFor(engine, engineGuard);
	if (component == nullptr) return nullptr;

	auto* object = component->beginCreate(engine->rootContext());
	auto* item = qobject_cast<QQuickItem*>(object);

	if (item != nullptr) {
		item->setParent(root);
		item->setParentItem(root);
		item->setProperty("sourceItem", QVariant::fromValue(source));
		item->setProperty("mipmapped", mipmap);
		item->setProperty("fragmentShader", this->shader);
	}

	component->completeCreate();

	if (item == nullptr) {
		delete object;
		this->fail("the effect component did not create an item");
		return nullptr;
	}

	if (shaderBroken(item)) {
		auto log = item->property("log").toString().trimmed();
		delete item;
		this->fail(QString("%1 did not load: %2").arg(this->shader.toString(), log));
		return nullptr;
	}

	qCDebug(logEffects) << "Effect layer on" << root << "with shader status"
	                    << item->property("status").toInt();
	this->trackHost(root->window());
	return std::make_unique<PanelEffectLayer>(this, item);
}

bool PanelEffects::shaderBroken(const QQuickItem* item) {
	return item->property("status").toInt() == SHADER_ERROR;
}

bool PanelEffects::loaded() const {
	return std::ranges::any_of(this->components, [](const Loaded& entry) {
		return entry.component != nullptr;
	});
}

void PanelEffects::unload() {
	if (this->keepAlive.users() > 0) return;

	auto dropped = std::exchange(this->components, {});

	for (auto& entry: dropped) {
		QObject::disconnect(entry.guard);
		if (entry.engine == nullptr) continue;

		delete entry.component.data();
		entry.engine->trimComponentCache();
	}

	auto released = this->releaseHosts();
	if (dropped.empty() && released == 0) return;

	qCInfo(logEffects) << "Unloaded the expressive panel effects after" << this->keepAlive.interval()
	                   << "ms without use and released the cached graphics of" << released
	                   << "shown windows";
	emit this->unloaded();
}

qsizetype PanelEffects::hostWindows() const {
	return std::ranges::count_if(this->hosts, [](const QPointer<QQuickWindow>& host) {
		return host != nullptr;
	});
}

void PanelEffects::trackHost(QQuickWindow* window) {
	std::erase_if(this->hosts, [](const QPointer<QQuickWindow>& host) { return host == nullptr; });
	if (window == nullptr) return;

	auto known = std::ranges::any_of(this->hosts, [window](const QPointer<QQuickWindow>& host) {
		return host == window;
	});

	if (!known) this->hosts.emplace_back(window);
}

int PanelEffects::releaseHosts() {
	auto released = 0;

	for (const auto& host: std::exchange(this->hosts, {})) {
		if (host == nullptr || !host->isVisible()) continue;

		host->releaseResources();
		released++;
	}

	return released;
}

QQmlComponent* PanelEffects::componentFor(QQmlEngine* engine, QObject* engineGuard) {
	for (const auto& entry: this->components) {
		if (entry.engine == engine && entry.component != nullptr) return entry.component;
	}

	auto component = std::make_unique<QQmlComponent>(engine);
	component->setData(EFFECT_QML, QUrl());

	if (!component->isReady()) {
		this->fail(component->errorString().trimmed());
		return nullptr;
	}

	auto entry = Loaded {.engine = engine, .component = component.get()};

	if (engineGuard != nullptr) {
		entry.guard = QObject::connect(engineGuard, &QObject::destroyed, this, [this, engine]() {
			this->drop(engine);
		});
	}

	this->components.push_back(entry);
	this->keepAlive.arm();
	qCInfo(logEffects) << "Loaded the expressive panel effects";

	return component.release();
}

bool PanelEffects::checkShader() {
	if (this->shaderChecked) return !this->mFailed;
	this->shaderChecked = true;

	auto file = QFile(QQmlFile::urlToLocalFileOrQrc(this->shader));
	if (!file.open(QFile::ReadOnly)) {
		this->fail(QString("%1 can't be read").arg(this->shader.toString()));
		return false;
	}

	if (!QShader::fromSerialized(file.readAll()).isValid()) {
		this->fail(QString("%1 is not a valid shader").arg(this->shader.toString()));
		return false;
	}

	return true;
}

void PanelEffects::drop(const QQmlEngine* engine) {
	auto it = std::ranges::find_if(this->components, [engine](const Loaded& entry) {
		return entry.engine == engine;
	});

	if (it == this->components.end()) return;

	QObject::disconnect(it->guard);
	if (it->engine != nullptr) delete it->component.data();
	this->components.erase(it);
}

void PanelEffects::fail(const QString& reason) {
	if (this->mFailed) return;
	this->mFailed = true;

	qCWarning(logEffects).noquote()
	    << "Expressive panel effects are unavailable, panels use the standard animations:" << reason;
}

void PanelEffects::layerDone() { this->keepAlive.release(); }

} // namespace qs::windows
