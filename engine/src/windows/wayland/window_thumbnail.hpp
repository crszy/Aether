#pragma once

#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qquickitem.h>
#include <qsize.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qt_windows.h>

#include <dwmapi.h>

namespace qs::wayland::screencopy {

class WindowThumbnail: public QQuickItem {
	Q_OBJECT;
	QML_ELEMENT;
	// clang-format off
	Q_PROPERTY(QObject* captureSource READ captureSource WRITE setCaptureSource NOTIFY captureSourceChanged);
	Q_PROPERTY(QSizeF constraintSize READ constraintSize WRITE setConstraintSize NOTIFY constraintSizeChanged);
	Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY sourceSizeChanged);
	Q_PROPERTY(bool hasContent READ hasContent NOTIFY hasContentChanged);
	// clang-format on

public:
	explicit WindowThumbnail(QQuickItem* parent = nullptr);
	~WindowThumbnail() override;
	Q_DISABLE_COPY_MOVE(WindowThumbnail);

	[[nodiscard]] QObject* captureSource() const { return this->mCaptureSource; }
	void setCaptureSource(QObject* source);
	[[nodiscard]] QSizeF constraintSize() const { return this->mConstraintSize; }
	void setConstraintSize(QSizeF size);
	[[nodiscard]] QSize sourceSize() const { return this->mSourceSize; }
	[[nodiscard]] bool hasContent() const { return this->mThumbnail != nullptr; }

signals:
	void captureSourceChanged();
	void constraintSizeChanged();
	void sourceSizeChanged();
	void hasContentChanged();

protected:
	void componentComplete() override;
	void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
	[[nodiscard]] HWND sourceWindow() const;
	void refresh();
	void unregister();
	void sync();
	void setSourceSize(QSize size);
	void updateImplicitSize();

	QPointer<QObject> mCaptureSource;
	QSizeF mConstraintSize;
	QSize mSourceSize;
	HTHUMBNAIL mThumbnail = nullptr;
	HWND mDestination = nullptr;
	HWND mSource = nullptr;
	QMetaObject::Connection mFrameConnection;
	DWM_THUMBNAIL_PROPERTIES mApplied {};
	bool mCompleted = false;
};

} // namespace qs::wayland::screencopy
