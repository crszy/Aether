#include "sensorslib.hpp"

// Windows build of Caelestia's temperature reader (lm_sensors does not exist on Windows).
//  - GPU: the graphics kernel's own adapter perf data (D3DKMT, the same source Task Manager uses), averaged
//    over every adapter that reports a temperature.
//  - CPU: Windows exposes no CPU temperature without a ring-0 helper. If Aether's LibreHardwareMonitor sidecar
//    is running it writes %LOCALAPPDATA%\Aether\sensors.json; a file older than 15 s is ignored so a stopped
//    sidecar shows "no reading", never a frozen number.
#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>

#include <qdatetime.h>
#include <qfile.h>
#include <qjsondocument.h>
#include <qjsonobject.h>

#include <mutex>
#include <vector>

namespace caelestia::shell::services::sensorslib {

namespace {

std::mutex g_mutex;
qint64 g_gpuAt = 0;
std::optional<double> g_gpu;
qint64 g_cpuAt = 0;
std::optional<double> g_cpu;

std::optional<double> readGpu() {
    D3DKMT_ENUMADAPTERS2 en{};
    if (D3DKMTEnumAdapters2(&en) < 0 || en.NumAdapters == 0) return std::nullopt;
    std::vector<D3DKMT_ADAPTERINFO> infos(en.NumAdapters);
    en.pAdapters = infos.data();
    if (D3DKMTEnumAdapters2(&en) < 0) return std::nullopt;
    double sum = 0;
    int n = 0;
    for (ULONG i = 0; i < en.NumAdapters; ++i) {
        D3DKMT_ADAPTER_PERFDATA perf{};
        D3DKMT_QUERYADAPTERINFO q{};
        q.hAdapter = infos[i].hAdapter;
        q.Type = KMTQAITYPE_ADAPTERPERFDATA;
        q.pPrivateDriverData = &perf;
        q.PrivateDriverDataSize = sizeof perf;
        if (D3DKMTQueryAdapterInfo(&q) >= 0 && perf.Temperature != 0) {   // deci-Celsius
            sum += perf.Temperature / 10.0;
            ++n;
        }
        D3DKMT_CLOSEADAPTER close{};
        close.hAdapter = infos[i].hAdapter;
        D3DKMTCloseAdapter(&close);
    }
    if (n == 0) return std::nullopt;
    return sum / n;
}

std::optional<double> readCpu() {
    const auto base = qEnvironmentVariable("LOCALAPPDATA");
    if (base.isEmpty()) return std::nullopt;
    QFile f(base + QStringLiteral("/Aether/sensors.json"));
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const auto obj = QJsonDocument::fromJson(f.readAll()).object();
    if (!obj.value(QStringLiteral("ok")).toBool()) return std::nullopt;
    const auto ts = static_cast<qint64>(obj.value(QStringLiteral("ts")).toDouble());
    if (ts == 0 || QDateTime::currentSecsSinceEpoch() - ts > 15) return std::nullopt;
    const auto v = obj.value(QStringLiteral("cpu"));
    if (!v.isDouble()) return std::nullopt;
    const double c = v.toDouble();
    if (c <= 0 || c >= 150) return std::nullopt;
    return c;
}

} // namespace

void ensureInit() {}

std::optional<double> cpuPackageTemp() {
    const std::scoped_lock lock(g_mutex);
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (now - g_cpuAt > 2000) { g_cpu = readCpu(); g_cpuAt = now; }
    return g_cpu;
}

std::optional<double> gpuPciAverageTemp() {
    const std::scoped_lock lock(g_mutex);
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (now - g_gpuAt > 2000) { g_gpu = readGpu(); g_gpuAt = now; }
    return g_gpu;
}

} // namespace caelestia::shell::services::sensorslib
