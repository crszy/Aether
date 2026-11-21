#pragma once

#include <memory>
#include <vector>

#include <qmetaobject.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlcomponent.h>
#include <qqmlengine.h>
#include <qquickitem.h>
#include <qquickwindow.h>
#include <qsize.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qurl.h>

#include "panel_animation.hpp"

namespace qs::windows {

class PanelEffectKeepAlive: public QObject {
	Q_OBJECT;

public:
	explicit PanelEffectKeepAlive(int intervalMs, QObject* parent = nullptr);

	void acquire();
	void release();
	void arm();
	void setInterval(int intervalMs) { this->timer.setInterval(intervalMs); }

	[[nodiscard]] int users() const { return this->mUsers; }
	[[nodiscard]] bool armed() const { return this->timer.isActive(); }
	[[nodiscard]] int interval() const { return this->timer.interval(); }

signals:
	void expired();

private:
	void onTimeout();

	QTimer timer;
	int mUsers = 0;
};

class PanelEffects;

class PanelEffectLayer: public QObject {
	Q_OBJECT;

public:
	PanelEffectLayer(PanelEffects* owner, QQuickItem* item);
	~PanelEffectLayer() override;
	Q_DISABLE_COPY_MOVE(PanelEffectLayer);

	void apply(const PanelEffectFrame& frame, const QSizeF& size, qreal dpr);
	[[nodiscard]] QQuickItem* item() const { return this->mItem; }

private slots:
	void onStatusChanged();

private:
	QPointer<PanelEffects> effects;
	QPointer<QQuickItem> mItem;
};

class PanelEffects: public QObject {
	Q_OBJECT;

public:
	explicit PanelEffects(QUrl shaderUrl, int unloadMs, QObject* parent = nullptr);
	~PanelEffects() override;
	Q_DISABLE_COPY_MOVE(PanelEffects);

	static PanelEffects* instance();
	[[nodiscard]] static QUrl defaultShader();
	[[nodiscard]] static int defaultUnloadMs();

	[[nodiscard]] std::unique_ptr<PanelEffectLayer> createLayer(
	    QQmlEngine* engine,
	    QObject* engineGuard,
	    QQuickItem* root,
	    QQuickItem* source,
	    bool mipmap
	);

	[[nodiscard]] bool loaded() const;
	[[nodiscard]] bool failed() const { return this->mFailed; }
	[[nodiscard]] int liveLayers() const { return this->keepAlive.users(); }
	[[nodiscard]] bool unloadPending() const { return this->keepAlive.armed(); }
	[[nodiscard]] qsizetype hostWindows() const;
	void setUnloadInterval(int intervalMs) { this->keepAlive.setInterval(intervalMs); }

	void unload();

signals:
	void unloaded();

private:
	struct Loaded {
		QPointer<QQmlEngine> engine;
		QPointer<QQmlComponent> component;
		QMetaObject::Connection guard {};
	};

	[[nodiscard]] static bool shaderBroken(const QQuickItem* item);
	[[nodiscard]] QQmlComponent* componentFor(QQmlEngine* engine, QObject* engineGuard);
	[[nodiscard]] bool checkShader();
	void drop(const QQmlEngine* engine);
	void fail(const QString& reason);
	void layerDone();
	void trackHost(QQuickWindow* window);
	int releaseHosts();

	QUrl shader;
	PanelEffectKeepAlive keepAlive;
	std::vector<Loaded> components;
	std::vector<QPointer<QQuickWindow>> hosts;
	bool mFailed = false;
	bool shaderChecked = false;

	friend class PanelEffectLayer;
};

} // namespace qs::windows
