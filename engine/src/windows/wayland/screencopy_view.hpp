#pragma once

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qquickitem.h>
#include <qquickwindow.h>
#include <qsgnode.h>
#include <qsize.h>
#include <qtmetamacros.h>

#include "../capture.hpp"
#include "toplevel.hpp"

namespace qs::wayland::screencopy {

class ScreencopyView: public QQuickItem {
	Q_OBJECT;
	QML_ELEMENT;
	// clang-format off
	Q_PROPERTY(QObject* captureSource READ captureSource WRITE setCaptureSource NOTIFY captureSourceChanged);
	Q_PROPERTY(bool paintCursor READ paintCursors WRITE setPaintCursors NOTIFY paintCursorsChanged);
	Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged);
	Q_PROPERTY(bool hasContent READ default NOTIFY hasContentChanged BINDABLE bindableHasContent);
	Q_PROPERTY(QSize sourceSize READ default NOTIFY sourceSizeChanged BINDABLE bindableSourceSize);
	Q_PROPERTY(QSizeF constraintSize READ default WRITE default NOTIFY constraintSizeChanged BINDABLE bindableConstraintSize);
	// clang-format on

public:
	explicit ScreencopyView(QQuickItem* parent = nullptr);
	~ScreencopyView() override;
	Q_DISABLE_COPY_MOVE(ScreencopyView);

	void componentComplete() override;

	Q_INVOKABLE void captureFrame();

	[[nodiscard]] QObject* captureSource() const { return this->mCaptureSource; }
	void setCaptureSource(QObject* captureSource);

	[[nodiscard]] bool paintCursors() const { return this->mPaintCursors; }
	void setPaintCursors(bool paintCursors);

	[[nodiscard]] bool live() const { return this->mLive; }
	void setLive(bool live);

	[[nodiscard]] QBindable<bool> bindableHasContent() { return &this->bHasContent; }
	[[nodiscard]] QBindable<QSize> bindableSourceSize() { return &this->bSourceSize; }
	[[nodiscard]] QBindable<QSizeF> bindableConstraintSize() { return &this->bConstraintSize; }

	[[nodiscard]] qs::windows::capture::CaptureHandle* handle() const { return this->mHandle; }
	[[nodiscard]] bool hasFrame() const { return this->mHasFrame; }

signals:
	void stopped();

	void captureSourceChanged();
	void paintCursorsChanged();
	void liveChanged();
	void hasContentChanged();
	void sourceSizeChanged();
	void constraintSizeChanged();

protected:
	QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
	void itemChange(ItemChange change, const ItemChangeData& value) override;

private slots:
	void onCaptureSourceDestroyed();
	void onFrameReady();
	void onCaptureStopped(bool error);

private:
	void createContext();
	void destroyContext(bool update = true);
	void syncSession();
	void updateImplicitSize();
	void watchWindow(QQuickWindow* window);

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(ScreencopyView, bool, bHasContent, &ScreencopyView::hasContentChanged);
	Q_OBJECT_BINDABLE_PROPERTY(ScreencopyView, QSize, bSourceSize, &ScreencopyView::sourceSizeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(ScreencopyView, QSizeF, bConstraintSize, &ScreencopyView::constraintSizeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(ScreencopyView, QSizeF, bImplicitSize, &ScreencopyView::updateImplicitSize);
	// clang-format on

	QObject* mCaptureSource = nullptr;
	toplevel::Toplevel* toplevel = nullptr;
	bool mPaintCursors = false;
	bool mLive = false;
	bool completed = false;
	bool wantFrame = false;
	bool mHasFrame = false;
	qs::windows::capture::CaptureHandle* mHandle = nullptr;
	QQuickWindow* watchedWindow = nullptr;
};

} // namespace qs::wayland::screencopy
