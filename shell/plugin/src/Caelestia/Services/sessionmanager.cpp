#include "sessionmanager.hpp"

#include <qhash.h>
#include <qloggingcategory.h>
#include <qprocess.h>

#include <windows.h>
#include <powrprof.h>

namespace {
Q_LOGGING_CATEGORY(lcSessionManager, "caelestia.services.sessionmanager", QtInfoMsg)
}

namespace caelestia::shell::services {

using Qt::StringLiterals::operator""_s;

SessionManager::SessionManager(QObject* parent)
    : QObject(parent) {}

bool SessionManager::exec(const QStringList& command) {
    if (command.isEmpty())
        return false;
    static const QHash<QString, void (SessionManager::*)()> k_cmds = {
        { u"logout"_s, &SessionManager::logout },
        { u"suspend"_s, &SessionManager::suspend },
        { u"suspendthenhibernate"_s, &SessionManager::suspendThenHibernate },
        { u"hibernate"_s, &SessionManager::hibernate },
        { u"poweroff"_s, &SessionManager::poweroff },
        { u"reboot"_s, &SessionManager::reboot },
    };
    auto cmd = command.first();
    if ((cmd == u"systemctl"_s || cmd == u"loginctl"_s) && command.size() == 2)
        cmd = command.at(1);
    if (cmd == u"loginctl"_s && command.size() == 3 && command.at(1) == u"terminate-user"_s && command.at(2).isEmpty())
        cmd = u"logout"_s;
    cmd = cmd.remove(u'-').remove(u'_').toLower();
    if (const auto m = k_cmds.value(cmd, nullptr)) {
        (this->*m)();
        return true;
    }
    return false;
}

// Windows: the real session actions (only ever run when you press the button in Caelestia's power menu)
void SessionManager::logout() {
    qCInfo(lcSessionManager) << "logout";
    ExitWindowsEx(EWX_LOGOFF, SHTDN_REASON_FLAG_PLANNED);
}
void SessionManager::suspend() {
    qCInfo(lcSessionManager) << "suspend";
    SetSuspendState(FALSE, FALSE, FALSE);
}
void SessionManager::suspendThenHibernate() { suspend(); }
void SessionManager::hibernate() {
    qCInfo(lcSessionManager) << "hibernate";
    SetSuspendState(TRUE, FALSE, FALSE);
}
void SessionManager::poweroff() {
    qCInfo(lcSessionManager) << "poweroff";
    QProcess::startDetached(u"shutdown.exe"_s, { u"/s"_s, u"/t"_s, u"0"_s });
}
void SessionManager::reboot() {
    qCInfo(lcSessionManager) << "reboot";
    QProcess::startDetached(u"shutdown.exe"_s, { u"/r"_s, u"/t"_s, u"0"_s });
}

} // namespace caelestia::shell::services
