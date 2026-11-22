#pragma once

#include <vector>

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qt_windows.h>
#include <qtimer.h>
#include <qtmetamacros.h>

namespace qs::windows::sys {

class KeyboardLayoutWatcher: public QObject {
	Q_OBJECT;

public:
	static KeyboardLayoutWatcher* instance();
	Q_DISABLE_COPY_MOVE(KeyboardLayoutWatcher);

	[[nodiscard]] HKL current() const { return this->mCurrent; }
	[[nodiscard]] const std::vector<HKL>& layouts() const { return this->mLayouts; }

	void check();

signals:
	void currentChanged();
	void layoutsChanged();

private:
	explicit KeyboardLayoutWatcher(QObject* parent);

	HKL mCurrent = nullptr;
	std::vector<HKL> mLayouts;
	QTimer pollTimer;
};

class Keyboard: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	// clang-format off
	Q_PROPERTY(QString currentLayoutCode READ default NOTIFY currentLayoutChanged BINDABLE bindableCurrentLayoutCode);
	Q_PROPERTY(QString currentLayoutName READ default NOTIFY currentLayoutChanged BINDABLE bindableCurrentLayoutName);
	Q_PROPERTY(QStringList layoutCodes READ default NOTIFY layoutsChanged BINDABLE bindableLayoutCodes);
	Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged);
	// clang-format on

public:
	explicit Keyboard(QObject* parent = nullptr);
	~Keyboard() override;
	Q_DISABLE_COPY_MOVE(Keyboard);

	[[nodiscard]] bool active() const { return this->mActive; }
	void setActive(bool active);

	[[nodiscard]] QBindable<QString> bindableCurrentLayoutCode() const {
		return &this->bCurrentLayoutCode;
	}
	[[nodiscard]] QBindable<QString> bindableCurrentLayoutName() const {
		return &this->bCurrentLayoutName;
	}
	[[nodiscard]] QBindable<QStringList> bindableLayoutCodes() const {
		return &this->bLayoutCodes;
	}

	Q_INVOKABLE void activateLayout(const QString& code);

	void refresh();

signals:
	void currentLayoutChanged();
	void layoutsChanged();
	void activeChanged();

private:
	void initDeferred();
	void refreshLayoutList();

	bool mActive = true;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(Keyboard, QString, bCurrentLayoutCode, &Keyboard::currentLayoutChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Keyboard, QString, bCurrentLayoutName, &Keyboard::currentLayoutChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Keyboard, QStringList, bLayoutCodes, &Keyboard::layoutsChanged);
	// clang-format on
};

} // namespace qs::windows::sys
