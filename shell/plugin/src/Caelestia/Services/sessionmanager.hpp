#pragma once
// Windows build: no logind / D-Bus. Same QML API as upstream; actions use Windows APIs.
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qstringlist.h>
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_SERVICES_API


namespace caelestia::shell::services {

class CAE_SERVICES_API SessionManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit SessionManager(QObject* parent = nullptr);

    Q_INVOKABLE void logout();
    Q_INVOKABLE void suspend();
    Q_INVOKABLE void suspendThenHibernate();
    Q_INVOKABLE void hibernate();
    Q_INVOKABLE void poweroff();
    Q_INVOKABLE void reboot();
    Q_INVOKABLE bool exec(const QStringList& command);

signals:
    void aboutToSleep();
    void resumed();
    void lockRequested();
    void unlockRequested();
};

} // namespace caelestia::shell::services
