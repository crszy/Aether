#pragma once

#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

#include "../../core/region.hpp"

namespace qs::wayland::background_effect {

/// Windows stand-in for the Wayland blur-behind request: it keeps the region a
/// config sets (so bindings work); drawing blur is up to the window's own colours.
class BackgroundEffect: public QObject {
	Q_OBJECT;
	Q_PROPERTY(PendingRegion* blurRegion READ blurRegion WRITE setBlurRegion NOTIFY blurRegionChanged);
	QML_ELEMENT;
	QML_UNCREATABLE("BackgroundEffect can only be used as an attached object.");
	QML_ATTACHED(BackgroundEffect);

public:
	explicit BackgroundEffect(QObject* parent): QObject(parent) {}

	[[nodiscard]] PendingRegion* blurRegion() const { return this->mBlurRegion; }
	void setBlurRegion(PendingRegion* region) {
		if (region == this->mBlurRegion) return;
		this->mBlurRegion = region;
		emit this->blurRegionChanged();
	}

	static BackgroundEffect* qmlAttachedProperties(QObject* object) { return new BackgroundEffect(object); }

signals:
	void blurRegionChanged();

private:
	QPointer<PendingRegion> mBlurRegion;
};

} // namespace qs::wayland::background_effect
