#include "cpu.hpp"

#include <qfile.h>
#include <qregularexpression.h>

#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

#include "sensorslib.hpp"

namespace caelestia::shell::services {

using Qt::StringLiterals::operator""_s;

Cpu::Cpu(QObject* parent)
    : TickingService(parent) {
    readNameOnce();
}

QString Cpu::name() const {
    return m_name;
}

qreal Cpu::percentage() const {
    return m_percentage;
}

qreal Cpu::temperature() const {
    return m_temperature;
}

void Cpu::tick() {
    if (!m_nameLoaded) {
        readNameOnce();
    }
    refreshPercentage();
    refreshTemperature();
}

void Cpu::readNameOnce() {
#ifdef _WIN32
    {
        HKEY k;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, LR"(HARDWARE\DESCRIPTION\System\CentralProcessor\0)", 0, KEY_READ, &k) == ERROR_SUCCESS) {
            wchar_t buf[256]; DWORD sz = sizeof buf, type = 0;
            if (RegQueryValueExW(k, L"ProcessorNameString", nullptr, &type, reinterpret_cast<BYTE*>(buf), &sz) == ERROR_SUCCESS) {
                m_name = cleanName(QString::fromWCharArray(buf));
                m_nameLoaded = true;
                emit nameChanged();
            }
            RegCloseKey(k);
        }
        return;
    }
#endif
    QFile f(u"/proc/cpuinfo"_s);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    const QByteArray data = f.readAll();
    f.close();

    static const QRegularExpression k_re(u"model name\\s*:\\s*(.+)"_s);
    const auto match = k_re.match(QString::fromLatin1(data));
    if (!match.hasMatch()) {
        return;
    }

    const QString cleaned = cleanName(match.captured(1));
    m_nameLoaded = true;
    if (cleaned == m_name) {
        return;
    }
    m_name = cleaned;
    emit nameChanged();
}

void Cpu::refreshPercentage() {
#ifdef _WIN32
    {
        FILETIME i, k, u;
        if (!GetSystemTimes(&i, &k, &u)) return;
        const auto q = [](FILETIME f) { return (static_cast<quint64>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
        const quint64 idle = q(i), total = q(k) + q(u);   // kernel time includes idle
        const quint64 totalDiff = total > m_lastTotal ? total - m_lastTotal : 0;
        const quint64 idleDiff = idle > m_lastIdle ? idle - m_lastIdle : 0;
        const qreal newPerc = totalDiff > 0 ? 1.0 - static_cast<qreal>(idleDiff) / static_cast<qreal>(totalDiff) : 0.0;
        m_lastTotal = total; m_lastIdle = idle;
        if (std::abs(newPerc - m_percentage) > 0.0001) { m_percentage = newPerc; emit percentageChanged(); }
        return;
    }
#endif
    QFile f(u"/proc/stat"_s);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    const QByteArray data = f.readAll();
    f.close();

    static const QRegularExpression k_re(
        u"^cpu\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)"_s);
    const auto match = k_re.match(QString::fromLatin1(data));
    if (!match.hasMatch()) {
        return;
    }

    quint64 total = 0;
    quint64 idle = 0;
    for (int i = 1; i <= 7; ++i) {
        const quint64 v = match.captured(i).toULongLong();
        total += v;
        if (i == 4 || i == 5) {
            idle += v;
        }
    }

    const quint64 totalDiff = total > m_lastTotal ? total - m_lastTotal : 0;
    const quint64 idleDiff = idle > m_lastIdle ? idle - m_lastIdle : 0;
    const qreal newPerc = totalDiff > 0 ? 1.0 - static_cast<qreal>(idleDiff) / static_cast<qreal>(totalDiff) : 0.0;

    m_lastTotal = total;
    m_lastIdle = idle;

    if (std::abs(newPerc - m_percentage) > 0.0001) {
        m_percentage = newPerc;
        emit percentageChanged();
    }
}

void Cpu::refreshTemperature() {
    const auto t = sensorslib::cpuPackageTemp();
    const qreal newTemp = t.value_or(0.0);
    if (std::abs(newTemp - m_temperature) > 0.05) {
        m_temperature = newTemp;
        emit temperatureChanged();
    }
}

QString Cpu::cleanName(QString s) {
    static const QRegularExpression k_noise(
        u"\\(R\\)|\\(TM\\)|CPU|\\d+(?:th|nd|rd|st) Gen |Core |Processor"_s, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression k_spaces(u"\\s+"_s);

    s.replace(k_noise, {});
    s.replace(k_spaces, u" "_s);
    return s.trimmed();
}

} // namespace caelestia::shell::services
