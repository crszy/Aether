#include "window_thumbnail.hpp"
#include <cmath>
#include <cstring>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qquickitem.h>
#include <qquickwindow.h>
#include <qrect.h>

#include "../../core/logcat.hpp"
#include "../window_tracker.hpp"
#include "toplevel.hpp"

namespace qs::wayland::screencopy {

namespace {
QS_LOGGING_CATEGORY(logThumbnail, "quickshell.windows.thumbnail", QtWarningMsg);

LONG rounded(qreal value) { return static_cast<LONG>(std::lround(value)); }
} // namespace

WindowThumbnail::WindowThumbnail(QQuickItem* parent): QQuickItem(parent) {}

WindowThumbnail::~WindowThumbnail() { this->unregister(); }

void WindowThumbnail::setCaptureSource(QObject* source) {
	if (source == this->mCaptureSource) return;
	if (this->mCaptureSource) QObject::disconnect(this->mCaptureSource, nullptr, this, nullptr);

	this->mCaptureSource = source;
	if (source != nullptr) {
		QObject::connect(source, &QObject::destroyed, this, &WindowThumbnail::refresh);
	}

	emit this->captureSourceChanged();
	this->refresh();
}

void WindowThumbnail::setConstraintSize(QSizeF size) {
	if (size == this->mConstraintSize) return;
	this->mConstraintSize = size;
	emit this->constraintSizeChanged();
	this->updateImplicitSize();
}

void WindowThumbnail::componentComplete() {
	QQuickItem::componentComplete();
	this->mCompleted = true;
	this->refresh();
}

void WindowThumbnail::itemChange(ItemChange change, const ItemChangeData& data) {
	QQuickItem::itemChange(change, data);
	if (change == ItemSceneChange) this->refresh();
	else if (change == ItemVisibleHasChanged || change == ItemOpacityHasChanged) this->sync();
}

HWND WindowThumbnail::sourceWindow() const {
	auto* toplevel = qobject_cast<toplevel::Toplevel*>(this->mCaptureSource);
	if (toplevel == nullptr || toplevel->window() == nullptr) return nullptr;
	auto* hwnd = toplevel->window()->hwnd();
	return IsWindow(hwnd) ? hwnd : nullptr;
}

void WindowThumbnail::refresh() {
	auto* window = this->mCompleted ? this->window() : nullptr;
	auto* destination = window != nullptr ? reinterpret_cast<HWND>(window->winId()) : nullptr; // NOLINT
	auto* source = this->sourceWindow();

	if (this->mThumbnail != nullptr && destination == this->mDestination && source == this->mSource) {
		this->sync();
		return;
	}

	auto had = this->mThumbnail != nullptr;
	this->unregister();

	if (destination != nullptr && source != nullptr) {
		HTHUMBNAIL thumbnail = nullptr;
		auto result = DwmRegisterThumbnail(destination, source, &thumbnail);
		if (FAILED(result)) {
			qCWarning(logThumbnail) << "Couldn't register a thumbnail of" << source << "result" << Qt::hex << result;
		} else {
			this->mThumbnail = thumbnail;
			this->mDestination = destination;
			this->mSource = source;
			this->mFrameConnection =
			    QObject::connect(window, &QQuickWindow::afterAnimating, this, &WindowThumbnail::sync);
		}
	}

	if (had != (this->mThumbnail != nullptr)) emit this->hasContentChanged();
	if (this->mThumbnail == nullptr) {
		this->setSourceSize(QSize());
		return;
	}

	this->sync();
	window->update();
}

void WindowThumbnail::unregister() {
	if (this->mFrameConnection) QObject::disconnect(this->mFrameConnection);
	if (this->mThumbnail != nullptr) DwmUnregisterThumbnail(this->mThumbnail);
	this->mThumbnail = nullptr;
	this->mDestination = nullptr;
	this->mSource = nullptr;
	this->mApplied = {};
}

void WindowThumbnail::sync() {
	if (this->mThumbnail == nullptr) return;
	auto* window = this->window();
	if (window == nullptr) return;

	SIZE source {};
	if (FAILED(DwmQueryThumbnailSourceSize(this->mThumbnail, &source))) {
		this->unregister();
		emit this->hasContentChanged();
		this->setSourceSize(QSize());
		return;
	}
	this->setSourceSize(QSize(static_cast<int>(source.cx), static_cast<int>(source.cy)));

	qreal opacity = 1;
	for (const QQuickItem* item = this; item != nullptr; item = item->parentItem()) {
		opacity *= item->opacity();
	}

	auto rect = this->mapRectToScene(QRectF(0, 0, this->width(), this->height()));
	auto shown = rect.intersected(QRectF(0, 0, window->width(), window->height()));
	auto visible = this->isVisible() && window->isVisible() && !shown.isEmpty() && opacity > 0
	            && source.cx > 0 && source.cy > 0;

	DWM_THUMBNAIL_PROPERTIES props {};
	props.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_OPACITY
	              | DWM_TNP_SOURCECLIENTAREAONLY;
	props.fVisible = visible ? TRUE : FALSE;
	props.fSourceClientAreaOnly = FALSE;
	props.opacity = static_cast<BYTE>(std::lround(qBound(0.0, opacity, 1.0) * 255));

	if (visible) {
		auto dpr = window->devicePixelRatio();
		props.rcDestination = {
		    rounded(shown.left() * dpr),
		    rounded(shown.top() * dpr),
		    rounded(shown.right() * dpr),
		    rounded(shown.bottom() * dpr),
		};

		auto sx = static_cast<qreal>(source.cx) / rect.width();
		auto sy = static_cast<qreal>(source.cy) / rect.height();
		props.rcSource = {
		    rounded((shown.left() - rect.left()) * sx),
		    rounded((shown.top() - rect.top()) * sy),
		    rounded((shown.right() - rect.left()) * sx),
		    rounded((shown.bottom() - rect.top()) * sy),
		};
	}

	if (std::memcmp(&props, &this->mApplied, sizeof(props)) == 0) return;
	if (FAILED(DwmUpdateThumbnailProperties(this->mThumbnail, &props))) {
		this->unregister();
		emit this->hasContentChanged();
		return;
	}
	this->mApplied = props;
}

void WindowThumbnail::setSourceSize(QSize size) {
	if (size == this->mSourceSize) return;
	this->mSourceSize = size;
	emit this->sourceSizeChanged();
	this->updateImplicitSize();
}

void WindowThumbnail::updateImplicitSize() {
	auto size = this->mSourceSize.toSizeF();
	auto constraint = this->mConstraintSize;
	if (!size.isEmpty() && constraint.width() > 0 && constraint.height() > 0) {
		size.scale(constraint, Qt::KeepAspectRatio);
	}
	this->setImplicitSize(size.width(), size.height());
}

} // namespace qs::wayland::screencopy
