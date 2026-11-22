#include "brightness.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

#include <qt_windows.h>

#include <winioctl.h>

#include <lowlevelmonitorconfigurationapi.h>
#include <ntddvdeo.h>
#include <physicalmonitorenumerationapi.h>
#include <oleauto.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <qcoreapplication.h>
#include <qguiapplication.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qmutex.h>
#include <qpointer.h>
#include <qrunnable.h>
#include <qscreen.h>
#include <qstringlist.h>
#include <qthreadpool.h>
#include <qtimer.h>

#include "../../core/logcat.hpp"
#include "../startup.hpp"
#include "../util.hpp"
#include "gamma.hpp"

using Microsoft::WRL::ComPtr;

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logBrightness, "quickshell.windows.brightness", QtInfoMsg);

constexpr int POOL_STOP_TIMEOUT_MS = 2000;
constexpr long WMI_WAIT_MS = 5000;
constexpr long WMI_PROBE_WAIT_MS = 15000;
constexpr int SETTLE_MS = 1200;
constexpr int EVENT_ATTEMPTS = 5;
constexpr DWORD EVENT_RETRY_MS = 5000;
constexpr qreal SAME_VALUE = 0.005;
constexpr BYTE VCP_BRIGHTNESS = 0x10;
constexpr BYTE NO_SYSTEM_BATTERY = 128;
constexpr DWORD MAX_LCD_LEVELS = 256;

QString hexCode(quint32 code) {
	return QStringLiteral("0x%1").arg(code, 8, 16, QLatin1Char('0'));
}

QString hrText(HRESULT hr) {
	struct Name {
		HRESULT hr;
		const char* text;
	};

	static const Name names[] = {
	    {static_cast<HRESULT>(WBEM_E_FAILED), "failed"},
	    {static_cast<HRESULT>(WBEM_E_NOT_FOUND), "not found"},
	    {static_cast<HRESULT>(WBEM_E_ACCESS_DENIED), "access denied"},
	    {static_cast<HRESULT>(WBEM_E_PROVIDER_FAILURE), "provider failure"},
	    {static_cast<HRESULT>(WBEM_E_TYPE_MISMATCH), "type mismatch"},
	    {static_cast<HRESULT>(WBEM_E_INVALID_PARAMETER), "invalid parameter"},
	    {static_cast<HRESULT>(WBEM_E_NOT_SUPPORTED), "not supported"},
	    {static_cast<HRESULT>(WBEM_E_INVALID_CLASS), "invalid class"},
	    {static_cast<HRESULT>(WBEM_E_INVALID_METHOD), "invalid method"},
	    {static_cast<HRESULT>(WBEM_E_INVALID_METHOD_PARAMETERS), "invalid method parameters"},
	    {static_cast<HRESULT>(WBEM_E_TRANSPORT_FAILURE), "transport failure"},
	    {HRESULT_FROM_WIN32(ERROR_TIMEOUT), "timed out"},
	    {RPC_E_DISCONNECTED, "disconnected"},
	    {E_ACCESSDENIED, "access denied"},
	};

	auto text = hexCode(static_cast<quint32>(hr));
	for (const auto& name: names) {
		if (name.hr == hr) return QStringLiteral("%1 (%2)").arg(text, QLatin1StringView(name.text));
	}

	return text;
}

QString win32Text(DWORD error) {
	if (error < 0x10000) return QStringLiteral("error %1").arg(error);
	return QStringLiteral("error %1").arg(hexCode(error));
}

QString percentText(qreal value) {
	return QStringLiteral("%1%").arg(std::lround(value * 100.0));
}

struct PhysicalMonitors {
	explicit PhysicalMonitors(HMONITOR hMonitor) {
		if (hMonitor == nullptr) {
			this->error = ERROR_INVALID_HANDLE;
			return;
		}

		DWORD count = 0;
		if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &count)) {
			this->error = GetLastError();
			return;
		}

		if (count == 0) {
			this->error = ERROR_NOT_FOUND;
			return;
		}

		this->monitors.resize(count);
		if (!GetPhysicalMonitorsFromHMONITOR(hMonitor, count, this->monitors.data())) {
			this->error = GetLastError();
			this->monitors.clear();
		}
	}

	~PhysicalMonitors() {
		if (!this->monitors.empty()) {
			DestroyPhysicalMonitors(static_cast<DWORD>(this->monitors.size()), this->monitors.data());
		}
	}

	PhysicalMonitors(const PhysicalMonitors&) = delete;
	PhysicalMonitors(PhysicalMonitors&&) = delete;
	PhysicalMonitors& operator=(const PhysicalMonitors&) = delete;
	PhysicalMonitors& operator=(PhysicalMonitors&&) = delete;

	std::vector<PHYSICAL_MONITOR> monitors;
	DWORD error = ERROR_SUCCESS;
};

bool ddcQuery(HMONITOR hMonitor, qreal& outBrightness, DWORD& outError) {
	PhysicalMonitors physical(hMonitor);
	if (physical.monitors.empty()) {
		outError = physical.error;
		return false;
	}

	MC_VCP_CODE_TYPE vcpType {};
	DWORD current = 0;
	DWORD maxValue = 0;
	if (!GetVCPFeatureAndVCPFeatureReply(
	        physical.monitors[0].hPhysicalMonitor,
	        VCP_BRIGHTNESS,
	        &vcpType,
	        &current,
	        &maxValue
	    ))
	{
		outError = GetLastError();
		return false;
	}

	if (maxValue == 0) {
		outError = ERROR_INVALID_DATA;
		return false;
	}

	outBrightness = std::clamp(qreal(current) / qreal(maxValue), 0.0, 1.0);
	return true;
}

bool ddcSet(HMONITOR hMonitor, qreal value, DWORD& outError) {
	PhysicalMonitors physical(hMonitor);
	if (physical.monitors.empty()) {
		outError = physical.error;
		return false;
	}

	auto ok = false;
	for (const auto& monitor: physical.monitors) {
		MC_VCP_CODE_TYPE vcpType {};
		DWORD current = 0;
		DWORD maxValue = 0;
		if (!GetVCPFeatureAndVCPFeatureReply(
		        monitor.hPhysicalMonitor,
		        VCP_BRIGHTNESS,
		        &vcpType,
		        &current,
		        &maxValue
		    )
		    || maxValue == 0)
		{
			maxValue = 100;
		}

		auto target = static_cast<DWORD>(std::lround(std::clamp(value, 0.0, 1.0) * qreal(maxValue)));
		if (SetVCPFeature(monitor.hPhysicalMonitor, VCP_BRIGHTNESS, target)) ok = true;
		else outError = GetLastError();
	}

	return ok;
}

enum class PanelKind : quint8 {
	Unknown,
	Internal,
	External,
};

const char* panelKindName(PanelKind kind) {
	switch (kind) {
	case PanelKind::Internal: return "built-in";
	case PanelKind::External: return "external";
	default: return "unknown output";
	}
}

bool isInternalOutput(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology) {
	switch (technology) {
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL:
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED:
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED:
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS: return true;
	default: return false;
	}
}

const char* technologyName(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology) {
	switch (technology) {
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15: return "VGA";
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI: return "DVI";
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI: return "HDMI";
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS: return "LVDS";
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL: return "DisplayPort";
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED: return "eDP";
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_USB_TUNNEL: return "USB-C DisplayPort";
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EXTERNAL: return "UDI";
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED: return "embedded UDI";
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST: return "Miracast";
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED: return "indirect";
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL: return "virtual";
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL: return "internal";
	default: return "other";
	}
}

QString instanceIdFromDevicePath(const QString& devicePath) {
	auto id = devicePath;
	if (id.startsWith(QStringLiteral("\\\\?\\"))) id.remove(0, 4);

	auto guid = id.lastIndexOf(QStringLiteral("#{"));
	if (guid >= 0) id.truncate(guid);

	id.replace(QLatin1Char('#'), QLatin1Char('\\'));
	return id.toUpper();
}

bool instanceMatches(const QString& wmiInstance, const QString& instanceId) {
	if (instanceId.isEmpty()) return false;

	auto name = wmiInstance.toUpper();
	return name == instanceId || name.startsWith(instanceId + QLatin1Char('_'));
}

struct MonitorTarget {
	QString instanceId;
	QString friendlyName;
	DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
};

struct MonitorDescription {
	PanelKind kind = PanelKind::Unknown;
	std::vector<MonitorTarget> targets;
};

MonitorDescription describeMonitor(HMONITOR hMonitor) {
	MonitorDescription description;
	if (hMonitor == nullptr) return description;

	MONITORINFOEXW info {};
	info.cbSize = sizeof(info);
	if (!GetMonitorInfoW(hMonitor, &info)) return description;

	std::vector<DISPLAYCONFIG_PATH_INFO> paths;
	std::vector<DISPLAYCONFIG_MODE_INFO> modes;
	LONG result = ERROR_INSUFFICIENT_BUFFER;

	for (auto attempt = 0; attempt < 3 && result == ERROR_INSUFFICIENT_BUFFER; attempt++) {
		UINT32 pathCount = 0;
		UINT32 modeCount = 0;
		result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
		if (result != ERROR_SUCCESS) return description;

		paths.resize(pathCount);
		modes.resize(modeCount);
		result = QueryDisplayConfig(
		    QDC_ONLY_ACTIVE_PATHS,
		    &pathCount,
		    paths.data(),
		    &modeCount,
		    modes.data(),
		    nullptr
		);
		paths.resize(pathCount);
	}

	if (result != ERROR_SUCCESS) return description;

	auto matched = false;
	for (const auto& path: paths) {
		DISPLAYCONFIG_SOURCE_DEVICE_NAME source {};
		source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
		source.header.size = sizeof(source);
		source.header.adapterId = path.sourceInfo.adapterId;
		source.header.id = path.sourceInfo.id;
		if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
		if (std::wcscmp(source.viewGdiDeviceName, info.szDevice) != 0) continue;

		matched = true;

		MonitorTarget target;
		target.technology = path.targetInfo.outputTechnology;

		DISPLAYCONFIG_TARGET_DEVICE_NAME name {};
		name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
		name.header.size = sizeof(name);
		name.header.adapterId = path.targetInfo.adapterId;
		name.header.id = path.targetInfo.id;
		if (DisplayConfigGetDeviceInfo(&name.header) == ERROR_SUCCESS) {
			target.instanceId = instanceIdFromDevicePath(QString::fromWCharArray(name.monitorDevicePath));
			target.friendlyName = QString::fromWCharArray(name.monitorFriendlyDeviceName);
		}

		if (isInternalOutput(target.technology)) description.kind = PanelKind::Internal;
		description.targets.push_back(std::move(target));
	}

	if (matched && description.kind != PanelKind::Internal) description.kind = PanelKind::External;
	return description;
}

bool hasSystemBattery() {
	SYSTEM_POWER_STATUS status {};
	if (!GetSystemPowerStatus(&status)) return true;
	return status.BatteryFlag != NO_SYSTEM_BATTERY;
}

struct MtaScope {
	MtaScope(): hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}

	MtaScope(const MtaScope&) = delete;
	MtaScope(MtaScope&&) = delete;
	MtaScope& operator=(const MtaScope&) = delete;
	MtaScope& operator=(MtaScope&&) = delete;

	~MtaScope() {
		if (SUCCEEDED(this->hr)) CoUninitialize();
	}

	HRESULT hr;
};

struct Bstr {
	explicit Bstr(const wchar_t* text): value(SysAllocString(text)) {}
	explicit Bstr(const QString& text)
	    : value(SysAllocStringLen(
	          reinterpret_cast<const OLECHAR*>(text.utf16()), // NOLINT
	          static_cast<UINT>(text.size())
	      )) {}

	Bstr(const Bstr&) = delete;
	Bstr(Bstr&&) = delete;
	Bstr& operator=(const Bstr&) = delete;
	Bstr& operator=(Bstr&&) = delete;

	~Bstr() { SysFreeString(this->value); }

	BSTR value;
};

struct Variant {
	Variant() { VariantInit(&this->value); }

	Variant(const Variant&) = delete;
	Variant(Variant&&) = delete;
	Variant& operator=(const Variant&) = delete;
	Variant& operator=(Variant&&) = delete;

	~Variant() { VariantClear(&this->value); }

	VARIANT value;
};

void secure(IUnknown* proxy) {
	if (proxy == nullptr) return;

	CoSetProxyBlanket(
	    proxy,
	    RPC_C_AUTHN_WINNT,
	    RPC_C_AUTHZ_NONE,
	    nullptr,
	    RPC_C_AUTHN_LEVEL_CALL,
	    RPC_C_IMP_LEVEL_IMPERSONATE,
	    nullptr,
	    EOAC_NONE
	);
}

HRESULT connectWmi(IWbemServices** out) {
	*out = nullptr;

	ComPtr<IWbemLocator> locator;
	auto hr = CoCreateInstance(
	    CLSID_WbemLocator,
	    nullptr,
	    CLSCTX_INPROC_SERVER,
	    IID_IWbemLocator,
	    reinterpret_cast<LPVOID*>(locator.GetAddressOf()) // NOLINT
	);
	if (FAILED(hr)) return hr;
	if (!locator) return E_FAIL;

	Bstr ns(L"ROOT\\WMI");
	hr = locator->ConnectServer(ns.value, nullptr, nullptr, nullptr, 0, nullptr, nullptr, out);
	if (FAILED(hr)) return hr;
	if (*out == nullptr) return E_FAIL;

	secure(*out);
	return S_OK;
}

bool wmiConnectionLost(HRESULT hr) {
	return hr == RPC_E_DISCONNECTED || hr == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE)
	    || hr == HRESULT_FROM_WIN32(RPC_S_CALL_FAILED) || hr == WBEM_E_TRANSPORT_FAILURE
	    || hr == WBEM_E_SHUTTING_DOWN;
}

struct WmiCache {
	QMutex mutex;
	IWbemServices* services = nullptr;
	bool mtaHeld = false;
	bool mtaAttempted = false;
};

WmiCache& wmiCache() {
	static auto* cache = new WmiCache();
	return *cache;
}

template <typename Fn>
HRESULT withWmi(Fn&& fn) {
	MtaScope com;

	auto runOnce = [&fn]() -> HRESULT {
		ComPtr<IWbemServices> services;
		auto hr = connectWmi(services.GetAddressOf());
		if (FAILED(hr)) return hr;
		return fn(services.Get());
	};

	if (com.hr == RPC_E_CHANGED_MODE) return runOnce();
	if (FAILED(com.hr)) return com.hr;

	auto& cache = wmiCache();
	QMutexLocker locker(&cache.mutex);

	if (!cache.mtaAttempted) {
		cache.mtaAttempted = true;
		CO_MTA_USAGE_COOKIE cookie = nullptr;
		cache.mtaHeld = SUCCEEDED(CoIncrementMTAUsage(&cookie));
	}

	if (!cache.mtaHeld) return runOnce();

	if (cache.services == nullptr) {
		auto hr = connectWmi(&cache.services);
		if (FAILED(hr)) return hr;
	}

	auto hr = fn(cache.services);
	if (wmiConnectionLost(hr)) {
		qCDebug(logBrightness) << "WMI connection lost, reconnecting";
		cache.services->Release();
		cache.services = nullptr;
		auto reconnect = connectWmi(&cache.services);
		if (FAILED(reconnect)) return reconnect;
		hr = fn(cache.services);
	}

	return hr;
}

HRESULT execQuery(IWbemServices* services, const QString& wql, ComPtr<IEnumWbemClassObject>& out) {
	Bstr language(L"WQL");
	Bstr query(wql);
	auto hr = services->ExecQuery(
	    language.value,
	    query.value,
	    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
	    nullptr,
	    out.ReleaseAndGetAddressOf()
	);
	if (FAILED(hr)) return hr;
	if (!out) return E_FAIL;

	secure(out.Get());
	return S_OK;
}

HRESULT nextObject(
    IEnumWbemClassObject* enumerator,
    ComPtr<IWbemClassObject>& out,
    long timeout = WMI_WAIT_MS
) {
	ULONG returned = 0;
	auto hr = enumerator->Next(timeout, 1, out.ReleaseAndGetAddressOf(), &returned);
	if (hr == WBEM_S_TIMEDOUT) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
	if (FAILED(hr)) return hr;
	return returned == 1 && out ? S_OK : S_FALSE;
}

QString stringProperty(IWbemClassObject* object, const wchar_t* name) {
	Variant value;
	if (FAILED(object->Get(name, 0, &value.value, nullptr, nullptr))) return {};
	if (value.value.vt != VT_BSTR || value.value.bstrVal == nullptr) return {};

	return QString::fromWCharArray(
	    value.value.bstrVal,
	    static_cast<qsizetype>(SysStringLen(value.value.bstrVal))
	);
}

int intProperty(IWbemClassObject* object, const wchar_t* name, int fallback) {
	Variant value;
	if (FAILED(object->Get(name, 0, &value.value, nullptr, nullptr))) return fallback;

	switch (value.value.vt) {
	case VT_UI1: return value.value.bVal;
	case VT_I2: return value.value.iVal;
	case VT_UI2: return value.value.uiVal;
	case VT_I4: return static_cast<int>(value.value.lVal);
	case VT_UI4: return static_cast<int>(value.value.ulVal);
	case VT_INT: return value.value.intVal;
	case VT_UINT: return static_cast<int>(value.value.uintVal);
	default: return fallback;
	}
}

bool boolProperty(IWbemClassObject* object, const wchar_t* name, bool fallback) {
	Variant value;
	if (FAILED(object->Get(name, 0, &value.value, nullptr, nullptr))) return fallback;
	if (value.value.vt != VT_BOOL) return fallback;
	return value.value.boolVal != VARIANT_FALSE;
}

QString wqlString(const QString& text) {
	auto escaped = text;
	escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
	escaped.replace(QStringLiteral("'"), QStringLiteral("\\'"));
	return escaped;
}

QString methodsPathFor(const QString& instanceName) {
	auto escaped = instanceName;
	escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
	escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
	return QStringLiteral("WmiMonitorBrightnessMethods.InstanceName=\"%1\"").arg(escaped);
}

struct WmiPanel {
	QString instanceName;
	QString methodsPath;
	bool active = true;
	int current = -1;
	int levels = 0;
};

HRESULT listWmiPanels(IWbemServices* services, std::vector<WmiPanel>& out) {
	out.clear();

	ComPtr<IEnumWbemClassObject> enumerator;
	auto hr = execQuery(services, QStringLiteral("SELECT * FROM WmiMonitorBrightness"), enumerator);
	if (FAILED(hr)) return hr;

	ComPtr<IWbemClassObject> object;
	while ((hr = nextObject(enumerator.Get(), object, WMI_PROBE_WAIT_MS)) == S_OK) {
		WmiPanel panel;
		panel.instanceName = stringProperty(object.Get(), L"InstanceName");
		panel.active = boolProperty(object.Get(), L"Active", true);
		panel.current = intProperty(object.Get(), L"CurrentBrightness", -1);
		panel.levels = intProperty(object.Get(), L"Levels", 0);
		if (!panel.instanceName.isEmpty()) out.push_back(std::move(panel));
	}

	if (FAILED(hr)) return hr;
	if (out.empty()) return S_OK;

	if (FAILED(execQuery(services, QStringLiteral("SELECT * FROM WmiMonitorBrightnessMethods"), enumerator)))
	{
		return S_OK;
	}

	while (nextObject(enumerator.Get(), object, WMI_PROBE_WAIT_MS) == S_OK) {
		auto name = stringProperty(object.Get(), L"InstanceName");
		auto path = stringProperty(object.Get(), L"__RELPATH");
		if (name.isEmpty() || path.isEmpty()) continue;

		for (auto& panel: out) {
			if (panel.instanceName.compare(name, Qt::CaseInsensitive) == 0) panel.methodsPath = path;
		}
	}

	return S_OK;
}

HRESULT readWmiPanel(IWbemServices* services, const QString& instanceName, int& outPercent) {
	ComPtr<IEnumWbemClassObject> enumerator;
	auto hr = execQuery(
	    services,
	    QStringLiteral("SELECT * FROM WmiMonitorBrightness WHERE InstanceName = '%1'")
	        .arg(wqlString(instanceName)),
	    enumerator
	);
	if (FAILED(hr)) return hr;

	ComPtr<IWbemClassObject> object;
	hr = nextObject(enumerator.Get(), object);
	if (FAILED(hr)) return hr;
	if (hr != S_OK) return WBEM_E_NOT_FOUND;

	outPercent = intProperty(object.Get(), L"CurrentBrightness", -1);
	return outPercent >= 0 ? S_OK : WBEM_E_TYPE_MISMATCH;
}

HRESULT writeWmiPanel(IWbemServices* services, const QString& methodsPath, int percent) {
	Bstr className(L"WmiMonitorBrightnessMethods");
	Bstr methodName(L"WmiSetBrightness");

	ComPtr<IWbemClassObject> classObject;
	auto hr = services->GetObject(className.value, 0, nullptr, classObject.GetAddressOf(), nullptr);
	if (FAILED(hr)) return hr;
	if (!classObject) return E_FAIL;

	ComPtr<IWbemClassObject> signature;
	hr = classObject->GetMethod(methodName.value, 0, signature.GetAddressOf(), nullptr);
	if (FAILED(hr)) return hr;
	if (!signature) return WBEM_E_INVALID_METHOD;

	ComPtr<IWbemClassObject> params;
	hr = signature->SpawnInstance(0, params.GetAddressOf());
	if (FAILED(hr)) return hr;
	if (!params) return E_FAIL;

	Variant timeout;
	timeout.value.vt = VT_I4;
	timeout.value.lVal = 0;
	hr = params->Put(L"Timeout", 0, &timeout.value, 0);
	if (FAILED(hr)) return hr;

	Variant level;
	level.value.vt = VT_UI1;
	level.value.bVal = static_cast<BYTE>(std::clamp(percent, 0, 100));
	hr = params->Put(L"Brightness", 0, &level.value, 0);
	if (FAILED(hr)) return hr;

	Bstr path(methodsPath);
	ComPtr<IWbemCallResult> call;
	hr = services->ExecMethod(
	    path.value,
	    methodName.value,
	    WBEM_FLAG_RETURN_IMMEDIATELY,
	    nullptr,
	    params.Get(),
	    nullptr,
	    call.GetAddressOf()
	);
	if (FAILED(hr)) return hr;
	if (!call) return S_OK;

	secure(call.Get());
	long status = WBEM_S_NO_ERROR;
	hr = call->GetCallStatus(WMI_WAIT_MS, &status);
	if (hr == WBEM_S_TIMEDOUT) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
	if (FAILED(hr)) return hr;
	return status;
}

struct LcdPanel {
	bool usable = false;
	DWORD error = ERROR_SUCCESS;
	int current = -1;
	std::vector<UCHAR> levels;
};

struct DeviceHandle {
	DeviceHandle()
	    : handle(CreateFileW(
	          L"\\\\.\\LCD",
	          GENERIC_READ | GENERIC_WRITE,
	          FILE_SHARE_READ | FILE_SHARE_WRITE,
	          nullptr,
	          OPEN_EXISTING,
	          0,
	          nullptr
	      )) {
		if (this->handle == INVALID_HANDLE_VALUE) this->error = GetLastError();
	}

	DeviceHandle(const DeviceHandle&) = delete;
	DeviceHandle(DeviceHandle&&) = delete;
	DeviceHandle& operator=(const DeviceHandle&) = delete;
	DeviceHandle& operator=(DeviceHandle&&) = delete;

	~DeviceHandle() {
		if (this->handle != INVALID_HANDLE_VALUE) CloseHandle(this->handle);
	}

	[[nodiscard]] bool valid() const { return this->handle != INVALID_HANDLE_VALUE; }

	HANDLE handle;
	DWORD error = ERROR_SUCCESS;
};

LcdPanel queryLcd(const DeviceHandle& device) {
	LcdPanel panel;
	if (!device.valid()) {
		panel.error = device.error;
		return panel;
	}

	UCHAR levels[MAX_LCD_LEVELS] {}; // NOLINT
	DWORD bytes = 0;
	if (DeviceIoControl(
	        device.handle,
	        IOCTL_VIDEO_QUERY_SUPPORTED_BRIGHTNESS,
	        nullptr,
	        0,
	        levels,
	        sizeof(levels),
	        &bytes,
	        nullptr
	    ))
	{
		panel.levels.assign(levels, levels + std::min<DWORD>(bytes, MAX_LCD_LEVELS)); // NOLINT
	}

	DISPLAY_BRIGHTNESS state {};
	bytes = 0;
	if (!DeviceIoControl(
	        device.handle,
	        IOCTL_VIDEO_QUERY_DISPLAY_BRIGHTNESS,
	        nullptr,
	        0,
	        &state,
	        sizeof(state),
	        &bytes,
	        nullptr
	    ))
	{
		panel.error = GetLastError();
		return panel;
	}

	panel.usable = true;
	panel.current = (state.ucDisplayPolicy & DISPLAYPOLICY_DC) != 0 ? state.ucDCBrightness
	                                                                 : state.ucACBrightness;
	return panel;
}

LcdPanel queryLcd() {
	DeviceHandle device;
	return queryLcd(device);
}

bool lcdSet(int percent, int& outApplied, DWORD& outError) {
	DeviceHandle device;
	auto panel = queryLcd(device);
	if (!panel.usable) {
		outError = panel.error;
		return false;
	}

	auto level = std::clamp(percent, 0, 100);
	if (!panel.levels.empty()) {
		auto best = static_cast<int>(panel.levels.front());
		for (auto candidate: panel.levels) {
			if (std::abs(static_cast<int>(candidate) - level) < std::abs(best - level)) best = candidate;
		}
		level = best;
	}

	DISPLAY_BRIGHTNESS state {};
	state.ucDisplayPolicy = static_cast<UCHAR>(DISPLAYPOLICY_BOTH);
	state.ucACBrightness = static_cast<UCHAR>(level);
	state.ucDCBrightness = static_cast<UCHAR>(level);
	DWORD bytes = 0;
	if (!DeviceIoControl(
	        device.handle,
	        IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS,
	        &state,
	        sizeof(state),
	        nullptr,
	        0,
	        &bytes,
	        nullptr
	    ))
	{
		outError = GetLastError();
		return false;
	}

	auto after = queryLcd(device);
	if (after.usable && std::abs(after.current - level) > 1) {
		outError = ERROR_INVALID_DATA;
		return false;
	}

	outApplied = level;
	return true;
}

struct Probe {
	MonitorDescription monitor;
	bool wmiAsked = false;
	HRESULT wmiResult = S_OK;
	std::vector<WmiPanel> wmi;
	bool lcdAsked = false;
	LcdPanel lcd;
	bool ddcAsked = false;
	bool ddcOk = false;
	qreal ddcBrightness = 1.0;
	DWORD ddcError = ERROR_SUCCESS;
};

const WmiPanel* matchingPanel(const Probe& probe) {
	for (const auto& target: probe.monitor.targets) {
		for (const auto& panel: probe.wmi) {
			if (instanceMatches(panel.instanceName, target.instanceId)) return &panel;
		}
	}

	return nullptr;
}

Probe runProbe(HMONITOR hMonitor, bool readDdc) {
	Probe probe;
	probe.monitor = describeMonitor(hMonitor);
	auto kind = probe.monitor.kind;

	if (kind != PanelKind::External || hasSystemBattery()) {
		probe.wmiAsked = true;
		probe.wmiResult = withWmi([&probe](IWbemServices* services) {
			return listWmiPanels(services, probe.wmi);
		});
	}

	auto matched = matchingPanel(probe) != nullptr;

	if (!matched && kind != PanelKind::External) {
		probe.lcdAsked = true;
		probe.lcd = queryLcd();
	}

	if (!matched && readDdc && kind != PanelKind::Internal) {
		probe.ddcAsked = true;
		probe.ddcOk = ddcQuery(hMonitor, probe.ddcBrightness, probe.ddcError);
	}

	return probe;
}

struct Decision {
	BrightnessRoute route;
	bool available = false;
	qreal brightness = 1.0;
	QString reason;
};

Decision decide(const Probe& probe, const QSet<QString>& claimedWmi, bool lcdClaimed) {
	Decision decision;
	decision.route.external = probe.monitor.kind == PanelKind::External;

	auto useWmi = [&decision](const WmiPanel& panel, const QString& reason) {
		decision.route.method = BrightnessMethod::Wmi;
		decision.route.wmiInstance = panel.instanceName;
		decision.route.wmiMethodsPath =
		    panel.methodsPath.isEmpty() ? methodsPathFor(panel.instanceName) : panel.methodsPath;
		decision.available = panel.current >= 0;
		decision.brightness = panel.current >= 0 ? std::clamp(panel.current / 100.0, 0.0, 1.0) : 1.0;
		decision.reason = reason;
	};

	const auto* matched = matchingPanel(probe);
	if (matched != nullptr && !claimedWmi.contains(matched->instanceName.toUpper())) {
		useWmi(*matched, QStringLiteral("its WMI brightness instance matches the monitor's device path"));
		return decision;
	}

	if (probe.monitor.kind != PanelKind::External) {
		const WmiPanel* active = nullptr;
		const WmiPanel* any = nullptr;
		auto activeCount = 0;
		auto anyCount = 0;
		for (const auto& panel: probe.wmi) {
			if (claimedWmi.contains(panel.instanceName.toUpper())) continue;
			any = &panel;
			anyCount++;
			if (panel.active) {
				active = &panel;
				activeCount++;
			}
		}

		if (activeCount == 1) {
			useWmi(
			    *active,
			    QStringLiteral("no WMI instance matches its device path, so it takes the only unclaimed active one")
			);
			return decision;
		}

		if (activeCount == 0 && anyCount == 1) {
			useWmi(
			    *any,
			    QStringLiteral("no WMI instance matches its device path, so it takes the only unclaimed one (WMI calls it inactive)")
			);
			return decision;
		}

		if (probe.lcd.usable && !lcdClaimed) {
			decision.route.method = BrightnessMethod::Lcd;
			decision.route.lcdTried = true;
			decision.available = probe.lcd.current >= 0;
			decision.brightness =
			    probe.lcd.current >= 0 ? std::clamp(probe.lcd.current / 100.0, 0.0, 1.0) : 1.0;
			decision.reason = activeCount > 1
			                    ? QStringLiteral("several WMI instances could be it and none matches its device path; \\\\.\\LCD answers")
			                    : QStringLiteral("WMI has no brightness instance for it; \\\\.\\LCD answers");
			return decision;
		}
	}

	if (probe.monitor.kind == PanelKind::Internal) {
		decision.route.method = BrightnessMethod::Software;
		decision.route.lcdTried = true;
		decision.reason = QStringLiteral("built-in panel, but neither WMI nor \\\\.\\LCD controls its backlight (DDC/CI isn't tried on built-in panels)");
		return decision;
	}

	decision.route.method = BrightnessMethod::Ddc;
	if (probe.ddcAsked && probe.ddcOk) {
		decision.available = true;
		decision.brightness = probe.ddcBrightness;
		decision.reason = QStringLiteral("it answers DDC/CI");
	} else if (probe.ddcAsked) {
		decision.reason = QStringLiteral("DDC/CI didn't answer (%1); it's tried again on the first change, then software dimming")
		                      .arg(win32Text(probe.ddcError));
	} else {
		decision.reason = QStringLiteral("not a built-in panel; DDC/CI is tried on the first change, then software dimming if the monitor refuses");
	}

	return decision;
}

const char* methodKey(BrightnessMethod method) {
	switch (method) {
	case BrightnessMethod::Wmi: return "wmi";
	case BrightnessMethod::Lcd: return "lcd";
	case BrightnessMethod::Ddc: return "ddc";
	case BrightnessMethod::Software: return "software";
	default: return "";
	}
}

const char* methodLabel(BrightnessMethod method) {
	switch (method) {
	case BrightnessMethod::Wmi: return "WMI";
	case BrightnessMethod::Lcd: return "\\\\.\\LCD";
	case BrightnessMethod::Ddc: return "DDC/CI";
	case BrightnessMethod::Software: return "software dimming";
	default: return "nothing";
	}
}

const char* methodDescription(BrightnessMethod method) {
	switch (method) {
	case BrightnessMethod::Wmi: return "the backlight through WMI";
	case BrightnessMethod::Lcd: return "the backlight through \\\\.\\LCD";
	case BrightnessMethod::Ddc: return "DDC/CI";
	case BrightnessMethod::Software: return "software dimming (gamma ramp, the backlight stays as it is)";
	default: return "nothing";
	}
}

QString describeProbe(const QString& screenName, const Probe& probe, const Decision& decision) {
	QStringList parts;

	QStringList targets;
	for (const auto& target: probe.monitor.targets) {
		targets << QStringLiteral("%1 over %2 [%3]")
		               .arg(
		                   target.friendlyName.isEmpty() ? QStringLiteral("unnamed") : target.friendlyName,
		                   QLatin1StringView(technologyName(target.technology)),
		                   target.instanceId.isEmpty() ? QStringLiteral("no device path") : target.instanceId
		               );
	}

	parts << QStringLiteral("%1, %2")
	             .arg(
	                 QLatin1StringView(panelKindName(probe.monitor.kind)),
	                 targets.isEmpty() ? QStringLiteral("no active display path") : targets.join(QStringLiteral(", "))
	             );

	if (!probe.wmiAsked) {
		parts << QStringLiteral("WMI not asked (external output on a machine without a battery)");
	} else if (FAILED(probe.wmiResult)) {
		parts << QStringLiteral("WMI brightness query failed with %1").arg(hrText(probe.wmiResult));
	} else if (probe.wmi.empty()) {
		parts << QStringLiteral("WMI has no brightness instances");
	} else {
		QStringList panels;
		for (const auto& panel: probe.wmi) {
			panels << QStringLiteral("%1 (%2, %3%, %4 levels)")
			              .arg(
			                  panel.instanceName,
			                  panel.active ? QStringLiteral("active") : QStringLiteral("inactive"),
			                  QString::number(panel.current),
			                  QString::number(panel.levels)
			              );
		}
		parts << QStringLiteral("WMI brightness instances: %1").arg(panels.join(QStringLiteral(", ")));
	}

	if (probe.lcdAsked) {
		parts << (probe.lcd.usable ? QStringLiteral("\\\\.\\LCD at %1% (%2 levels)")
		                                 .arg(QString::number(probe.lcd.current), QString::number(probe.lcd.levels.size()))
		                           : QStringLiteral("no \\\\.\\LCD (%1)").arg(win32Text(probe.lcd.error)));
	}

	if (probe.ddcAsked) {
		parts << (probe.ddcOk ? QStringLiteral("DDC/CI at %1").arg(percentText(probe.ddcBrightness))
		                      : QStringLiteral("DDC/CI read failed (%1)").arg(win32Text(probe.ddcError)));
	}

	return QStringLiteral("%1: %2 -> %3, because %4")
	    .arg(
	        screenName,
	        parts.join(QStringLiteral("; ")),
	        QLatin1StringView(methodDescription(decision.route.method)),
	        decision.reason
	    );
}

QScreen* screenNamed(const QString& screenName) {
	for (auto* screen: QGuiApplication::screens()) {
		if (screen->name() == screenName) return screen;
	}

	return nullptr;
}

struct EventWatch {
	std::atomic<bool> started = false;
};

EventWatch& eventWatch() {
	static auto* watch = new EventWatch();
	return *watch;
}

QPointer<Brightness>& activeBrightness() {
	static auto* instance = new QPointer<Brightness>();
	return *instance;
}

void watchBrightnessEvents(const std::function<void(const QString&, int)>& deliver) {
	auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(com)) {
		qCWarning(logBrightness).noquote()
		    << "Can't watch brightness changes: CoInitializeEx failed with" << hrText(com);
		return;
	}

	auto failures = 0;
	auto announced = false;

	while (failures < EVENT_ATTEMPTS) {
		ComPtr<IWbemServices> services;
		auto hr = connectWmi(services.GetAddressOf());

		ComPtr<IEnumWbemClassObject> events;
		if (SUCCEEDED(hr)) {
			Bstr language(L"WQL");
			Bstr query(L"SELECT * FROM WmiMonitorBrightnessEvent");
			hr = services->ExecNotificationQuery(
			    language.value,
			    query.value,
			    WBEM_FLAG_RETURN_IMMEDIATELY | WBEM_FLAG_FORWARD_ONLY,
			    nullptr,
			    events.GetAddressOf()
			);
			if (SUCCEEDED(hr) && !events) hr = E_FAIL;
		}

		if (SUCCEEDED(hr)) {
			secure(events.Get());
			if (!announced) {
				announced = true;
				qCInfo(logBrightness) << "Watching WmiMonitorBrightnessEvent for brightness changes made "
				                         "outside ii (Fn keys, Settings, adaptive brightness)";
			}

			while (true) {
				ComPtr<IWbemClassObject> event;
				ULONG returned = 0;
				hr = events->Next(WBEM_INFINITE, 1, event.GetAddressOf(), &returned);
				if (FAILED(hr) || returned == 0 || !event) break;

				failures = 0;
				auto instanceName = stringProperty(event.Get(), L"InstanceName");
				auto level = intProperty(event.Get(), L"Brightness", -1);
				if (!instanceName.isEmpty() && level >= 0) deliver(instanceName, level);
			}
		}

		failures++;
		qCWarning(logBrightness).noquote()
		    << "Brightness change events stopped or couldn't start:" << hrText(hr);
		if (failures < EVENT_ATTEMPTS) Sleep(EVENT_RETRY_MS * static_cast<DWORD>(failures));
	}

	qCWarning(logBrightness) << "Gave up on brightness change events; changes made outside ii show "
	                            "up the next time a sidebar opens";
	CoUninitialize();
}

} // namespace

Brightness::Brightness(QObject* parent): QObject(parent), pool(new QThreadPool()) {
	this->pool->setMaxThreadCount(1);
	activeBrightness() = this;
}

Brightness::~Brightness() {
	this->pool->clear();
	if (!this->pool->waitForDone(POOL_STOP_TIMEOUT_MS)) {
		qCWarning(logBrightness) << "A brightness call didn't finish in time";
		return;
	}

	delete this->pool;
}

void Brightness::query(const QString& screenName) { this->startProbe(screenName, true); }

void Brightness::probe(const QString& screenName) { this->startProbe(screenName, false); }

void Brightness::startProbe(const QString& screenName, bool readDdc) {
	if (!startup::settled()) {
		startup::afterFirstFrame(this, [this, screenName, readDdc]() {
			this->startProbe(screenName, readDdc);
		});
		return;
	}

	auto* target = screenNamed(screenName);
	auto hMonitor = target != nullptr ? qs::windows::monitorForScreen(target) : nullptr;

	auto guard = QPointer<Brightness>(this);
	auto* task = QRunnable::create([guard, screenName, hMonitor, readDdc]() {
		auto probe = runProbe(hMonitor, readDdc);

		QMetaObject::invokeMethod(
		    QCoreApplication::instance(),
		    [guard, screenName, probe]() {
			    auto* self = guard.data();
			    if (self == nullptr) return;

			    auto previous = self->routes.take(screenName);

			    QSet<QString> present;
			    for (auto* screen: QGuiApplication::screens()) present.insert(screen->name());
			    self->routes.removeIf([&present](QHash<QString, BrightnessRoute>::iterator entry) {
				    return !present.contains(entry.key());
			    });

			    QSet<QString> claimedWmi;
			    auto lcdClaimed = false;
			    for (const auto& route: std::as_const(self->routes)) {
				    if (route.method == BrightnessMethod::Wmi) claimedWmi.insert(route.wmiInstance.toUpper());
				    if (route.method == BrightnessMethod::Lcd) lcdClaimed = true;
			    }

			    auto decision = decide(probe, claimedWmi, lcdClaimed);
			    if (previous.method == BrightnessMethod::Software
			        && decision.route.method != BrightnessMethod::Software)
			    {
				    decision.route.method = BrightnessMethod::Software;
				    decision.available = false;
				    decision.brightness = 1.0;
				    decision.reason = QStringLiteral("it already fell back to software dimming this session");
			    }

			    qCInfo(logBrightness).noquote() << describeProbe(screenName, probe, decision);

			    auto* gamma = GammaController::instance();
			    if (previous.method == BrightnessMethod::Software
			        && decision.route.method == BrightnessMethod::Software
			        && self->lastValue.contains(screenName))
			    {
				    decision.available = true;
				    decision.brightness = self->lastValue.value(screenName);
			    } else if (gamma->dims(screenName)) {
				    qCInfo(logBrightness).noquote()
				        << screenName
				        << "drops the software dimming left from before, so the screen matches the slider";
				    gamma->setBrightness(screenName, 1.0);
			    }

			    self->routes.insert(screenName, decision.route);
			    if (decision.available) self->lastValue.insert(screenName, decision.brightness);
			    else self->lastValue.remove(screenName);

			    if (decision.route.method == BrightnessMethod::Wmi) self->ensureEventWatch();

			    emit self->routeChanged(screenName, QLatin1StringView(methodKey(decision.route.method)));
			    emit self->queried(
			        screenName,
			        decision.available,
			        decision.route.method == BrightnessMethod::Ddc
			            || decision.route.method == BrightnessMethod::Software,
			        decision.brightness
			    );
		    },
		    Qt::QueuedConnection
		);
	});

	this->pool->start(task);
}

void Brightness::refresh(const QString& screenName) {
	auto method = this->routes.value(screenName).method;
	if (method != BrightnessMethod::Wmi && method != BrightnessMethod::Lcd) return;
	if (this->busy(screenName)) return;

	this->startReadBack(screenName);
}

QString Brightness::methodFor(const QString& screenName) const {
	return QLatin1StringView(methodKey(this->routes.value(screenName).method));
}

void Brightness::setBrightness(const QString& screenName, bool isDdc, qreal value) {
	auto clamped = std::clamp(value, 0.0, 1.0);

	auto& route = this->routes[screenName];
	if (route.method == BrightnessMethod::Unresolved) {
		route.method = BrightnessMethod::Ddc;
		qCInfo(logBrightness).noquote() << screenName << "was never probed; trying DDC/CI"
		                                << (isDdc ? "as asked" : "anyway");
	}

	this->lastValue.insert(screenName, clamped);
	this->generation[screenName]++;
	if (auto* timer = this->settleTimers.value(screenName)) timer->stop();

	if (route.method == BrightnessMethod::Software) {
		GammaController::instance()->setBrightness(screenName, clamped);
		emit this->brightnessSetFinished(screenName, true);
		return;
	}

	if (this->inFlight.contains(screenName)) {
		this->pending.insert(screenName, clamped);
		return;
	}

	this->startSet(screenName, clamped);
}

void Brightness::startSet(const QString& screenName, qreal value) {
	auto route = this->routes.value(screenName);
	if (route.method == BrightnessMethod::Software) {
		GammaController::instance()->setBrightness(screenName, value);
		emit this->brightnessSetFinished(screenName, true);
		return;
	}

	auto* target = screenNamed(screenName);
	auto hMonitor = target != nullptr ? qs::windows::monitorForScreen(target) : nullptr;

	this->inFlight.insert(screenName);

	auto guard = QPointer<Brightness>(this);
	auto* task = QRunnable::create([guard, screenName, route, hMonitor, value]() {
		auto ok = false;
		QString error;
		auto percent = static_cast<int>(std::lround(value * 100.0));

		switch (route.method) {
		case BrightnessMethod::Wmi: {
			auto hr = withWmi([&route, percent](IWbemServices* services) {
				return writeWmiPanel(services, route.wmiMethodsPath, percent);
			});
			ok = SUCCEEDED(hr);
			if (!ok) error = hrText(hr);
			break;
		}
		case BrightnessMethod::Lcd: {
			DWORD code = ERROR_SUCCESS;
			auto applied = 0;
			ok = lcdSet(percent, applied, code);
			if (!ok) error = win32Text(code);
			break;
		}
		case BrightnessMethod::Ddc: {
			DWORD code = ERROR_SUCCESS;
			ok = ddcSet(hMonitor, value, code);
			if (!ok) error = win32Text(code);
			break;
		}
		default: error = QStringLiteral("no brightness method"); break;
		}

		QMetaObject::invokeMethod(
		    QCoreApplication::instance(),
		    [guard, screenName, method = route.method, value, ok, error]() {
			    auto* self = guard.data();
			    if (self == nullptr) return;
			    self->finishSet(screenName, method, value, ok, error);
		    },
		    Qt::QueuedConnection
		);
	});

	this->pool->start(task);
}

void Brightness::finishSet(
    const QString& screenName,
    BrightnessMethod method,
    qreal value,
    bool ok,
    const QString& error
) {
	this->inFlight.remove(screenName);
	auto latest = this->pending.contains(screenName) ? this->pending.take(screenName) : value;

	if (ok) {
		if (latest != value) {
			this->startSet(screenName, latest);
			return;
		}

		this->settle(screenName);
		emit this->brightnessSetFinished(screenName, true);
		return;
	}

	qCWarning(logBrightness).noquote() << QStringLiteral("%1: setting %2 with %3 failed: %4")
	                                          .arg(
	                                              screenName,
	                                              percentText(value),
	                                              QLatin1StringView(methodLabel(method)),
	                                              error
	                                          );

	auto found = this->routes.find(screenName);
	if (found == this->routes.end()) return;

	auto& route = *found;
	if (route.method != method) {
		this->startSet(screenName, latest);
		return;
	}

	auto lcdFree = true;
	for (auto iter = this->routes.cbegin(); iter != this->routes.cend(); ++iter) {
		if (iter.key() != screenName && iter->method == BrightnessMethod::Lcd) lcdFree = false;
	}

	if (method == BrightnessMethod::Wmi && !route.lcdTried && lcdFree) {
		route.lcdTried = true;
		route.method = BrightnessMethod::Lcd;
		qCInfo(logBrightness).noquote() << screenName << "tries \\\\.\\LCD instead of WMI";
		emit this->routeChanged(screenName, QLatin1StringView(methodKey(route.method)));
		this->startSet(screenName, latest);
		return;
	}

	this->useSoftware(
	    screenName,
	    latest,
	    QStringLiteral("%1 refused (%2)").arg(QLatin1StringView(methodLabel(method)), error)
	);
	emit this->brightnessSetFinished(screenName, true);
}

void Brightness::useSoftware(const QString& screenName, qreal value, const QString& reason) {
	auto& route = this->routes[screenName];
	route.method = BrightnessMethod::Software;

	qCWarning(logBrightness).noquote()
	    << QStringLiteral("%1: %2; dimming it in software through the gamma ramp from now on, the "
	                      "backlight stays as it is")
	           .arg(screenName, reason);

	emit this->routeChanged(screenName, QLatin1StringView(methodKey(route.method)));
	GammaController::instance()->setBrightness(screenName, value);
}

void Brightness::settle(const QString& screenName) {
	auto method = this->routes.value(screenName).method;
	if (method != BrightnessMethod::Wmi && method != BrightnessMethod::Lcd) return;

	auto* timer = this->settleTimers.value(screenName);
	if (timer == nullptr) {
		timer = new QTimer(this);
		timer->setSingleShot(true);
		timer->setInterval(SETTLE_MS);
		QObject::connect(timer, &QTimer::timeout, this, [this, screenName]() {
			this->startReadBack(screenName);
		});
		this->settleTimers.insert(screenName, timer);
	}

	timer->start();
}

bool Brightness::busy(const QString& screenName) const {
	if (this->inFlight.contains(screenName) || this->pending.contains(screenName)) return true;

	auto* timer = this->settleTimers.value(screenName);
	return timer != nullptr && timer->isActive();
}

void Brightness::startReadBack(const QString& screenName) {
	auto route = this->routes.value(screenName);
	if (route.method != BrightnessMethod::Wmi && route.method != BrightnessMethod::Lcd) return;
	if (this->inFlight.contains(screenName) || this->pending.contains(screenName)) return;

	auto expected = this->generation.value(screenName);
	auto guard = QPointer<Brightness>(this);
	auto* task = QRunnable::create([guard, screenName, route, expected]() {
		auto percent = -1;
		QString error;

		if (route.method == BrightnessMethod::Wmi) {
			auto hr = withWmi([&route, &percent](IWbemServices* services) {
				return readWmiPanel(services, route.wmiInstance, percent);
			});
			if (FAILED(hr)) error = hrText(hr);
		} else {
			auto panel = queryLcd();
			if (panel.usable) percent = panel.current;
			else error = win32Text(panel.error);
		}

		QMetaObject::invokeMethod(
		    QCoreApplication::instance(),
		    [guard, screenName, method = route.method, expected, percent, error]() {
			    auto* self = guard.data();
			    if (self == nullptr) return;

			    if (percent < 0) {
				    qCWarning(logBrightness).noquote()
				        << QStringLiteral("%1: reading the brightness back with %2 failed: %3")
				               .arg(screenName, QLatin1StringView(methodLabel(method)), error);
				    return;
			    }

			    if (self->generation.value(screenName) != expected) return;
			    if (self->routes.value(screenName).method != method) return;
			    if (self->inFlight.contains(screenName) || self->pending.contains(screenName)) return;

			    auto value = std::clamp(percent / 100.0, 0.0, 1.0);
			    auto previous = self->lastValue.value(screenName, -1.0);
			    self->lastValue.insert(screenName, value);
			    if (previous >= 0.0 && std::abs(previous - value) <= SAME_VALUE) return;

			    qCDebug(logBrightness).noquote() << screenName << "reads back at" << percentText(value);
			    emit self->systemBrightnessChanged(screenName, value);
		    },
		    Qt::QueuedConnection
		);
	});

	this->pool->start(task);
}

void Brightness::onSystemBrightness(const QString& instanceName, int percent) {
	QString screenName;
	for (auto iter = this->routes.cbegin(); iter != this->routes.cend(); ++iter) {
		if (iter->method == BrightnessMethod::Wmi
		    && iter->wmiInstance.compare(instanceName, Qt::CaseInsensitive) == 0)
		{
			screenName = iter.key();
			break;
		}
	}

	if (screenName.isEmpty() || this->busy(screenName)) return;

	auto value = std::clamp(percent / 100.0, 0.0, 1.0);
	auto previous = this->lastValue.value(screenName, -1.0);
	if (previous >= 0.0 && std::abs(previous - value) <= SAME_VALUE) return;

	this->lastValue.insert(screenName, value);
	qCDebug(logBrightness).noquote() << screenName << "changed outside ii to" << percentText(value);
	emit this->systemBrightnessChanged(screenName, value);
}

void Brightness::ensureEventWatch() {
	auto& watch = eventWatch();
	if (watch.started.exchange(true)) return;

	std::thread([]() {
		watchBrightnessEvents([](const QString& instanceName, int percent) {
			auto* app = QCoreApplication::instance();
			if (app == nullptr) return;

			QMetaObject::invokeMethod(
			    app,
			    [instanceName, percent]() {
				    auto* self = activeBrightness().data();
				    if (self != nullptr) self->onSystemBrightness(instanceName, percent);
			    },
			    Qt::QueuedConnection
			);
		});
	}).detach();
}

} // namespace qs::windows::sys
