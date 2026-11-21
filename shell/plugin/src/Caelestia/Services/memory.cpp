#include "memory.hpp"

#include <qfile.h>
#include <qregularexpression.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace caelestia::shell::services {

using Qt::StringLiterals::operator""_s;

Memory::Memory(QObject* parent)
    : TickingService(parent) {}

qreal Memory::used() const {
    return m_used;
}

qreal Memory::total() const {
    return m_total;
}

qreal Memory::percentage() const {
    return m_total > 0.0 ? m_used / m_total : 0.0;
}

void Memory::tick() {
#ifdef _WIN32
    {
        MEMORYSTATUSEX m{}; m.dwLength = sizeof m;
        if (!GlobalMemoryStatusEx(&m)) return;
        const quint64 totalKib = m.ullTotalPhys / 1024, usedKib = (m.ullTotalPhys - m.ullAvailPhys) / 1024;
        if (totalKib == m_lastTotal && usedKib == m_lastUsed) return;
        m_lastTotal = totalKib; m_lastUsed = usedKib;
        m_total = static_cast<qreal>(totalKib); m_used = static_cast<qreal>(usedKib);
        emit changed();
        return;
    }
#endif
    QFile f(u"/proc/meminfo"_s);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    const QByteArray data = f.readAll();
    f.close();

    static const QRegularExpression k_reTotal(u"MemTotal: *(\\d+)"_s);
    static const QRegularExpression k_reAvail(u"MemAvailable: *(\\d+)"_s);
    const QString text = QString::fromLatin1(data);

    const auto totalMatch = k_reTotal.match(text);
    const auto availMatch = k_reAvail.match(text);
    if (!totalMatch.hasMatch() || !availMatch.hasMatch()) {
        return;
    }

    const quint64 totalKib = totalMatch.captured(1).toULongLong();
    const quint64 availKib = availMatch.captured(1).toULongLong();
    if (totalKib == 0) {
        return;
    }
    const quint64 usedKib = totalKib > availKib ? totalKib - availKib : 0;

    if (totalKib == m_lastTotal && usedKib == m_lastUsed) {
        return;
    }
    m_lastTotal = totalKib;
    m_lastUsed = usedKib;
    m_total = static_cast<qreal>(totalKib);
    m_used = static_cast<qreal>(usedKib);
    emit changed();
}

} // namespace caelestia::shell::services
