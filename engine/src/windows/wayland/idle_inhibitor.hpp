#pragma once

#include <qobject.h>
#include <qpointer.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <windows.h>

namespace qs::windows::wayland {

class IdleInhibitor: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(bool enabled READ default WRITE default NOTIFY enabledChanged BINDABLE bindableEnabled);
	Q_PROPERTY(QObject* window READ window WRITE setWindow NOTIFY windowChanged);
	// clang-format on
	QML_ELEMENT;

public:
	explicit IdleInhibitor(QObject* parent = nullptr);
	~IdleInhibitor() override;
	Q_DISABLE_COPY_MOVE(IdleInhibitor);

	[[nodiscard]] QObject* window() const { return this->mWindow; }
	void setWindow(QObject* window);

	[[nodiscard]] QBindable<bool> bindableEnabled() { return &this->bEnabled; }

signals:
	void enabledChanged();
	void windowChanged();

private:
	void update();

	QPointer<QObject> mWindow;
	HANDLE request = nullptr;
	bool active = false;

	Q_OBJECT_BINDABLE_PROPERTY(IdleInhibitor, bool, bEnabled, &IdleInhibitor::enabledChanged);
};

} // namespace qs::windows::wayland
