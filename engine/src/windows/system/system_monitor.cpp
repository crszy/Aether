#include "system_monitor.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <qt_windows.h>

#include <dxcore.h>
#include <dxgi1_2.h>
#if defined(_M_X64) || defined(_M_IX86)
#include <intrin.h>
#endif
#include <pdh.h>
#include <pdhmsg.h>
#include <powrprof.h>
#include <wbemidl.h>
#include <winioctl.h>
#include <winternl.h>
#include <d3dkmthk.h>

#include <qdir.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qnumeric.h>
#include <qproperty.h>
#include <qstring.h>
#include <qvariant.h>

#include <winrt/base.h>

#include "../../core/logcat.hpp"

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logSystemMonitor, "quickshell.windows.systemmonitor", QtWarningMsg);

constexpr auto PRIME_DELAY = std::chrono::milliseconds(250);
constexpr auto DISK_REFRESH = std::chrono::seconds(5);
constexpr int MIN_INTERVAL_MS = 250;
constexpr DWORD MAX_PHYSICAL_DRIVES = 32;

struct ProcessorPowerInformation {
	ULONG Number;
	ULONG MaxMhz;
	ULONG CurrentMhz;
	ULONG MhzLimit;
	ULONG MaxIdleState;
	ULONG CurrentIdleState;
};

QString regString(HKEY root, const wchar_t* key, const wchar_t* value) {
	DWORD size = 0;
	auto status = RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, nullptr, &size);
	if (status != ERROR_SUCCESS || size == 0) return QString();

	std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1);
	size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
	status = RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buffer.data(), &size);
	if (status != ERROR_SUCCESS) return QString();

	return QString::fromWCharArray(buffer.data()).trimmed();
}

std::optional<DWORD> regDword(HKEY root, const wchar_t* key, const wchar_t* value) {
	DWORD data = 0;
	DWORD size = sizeof(data);
	auto status = RegGetValueW(root, key, value, RRF_RT_REG_DWORD, nullptr, &data, &size);
	if (status != ERROR_SUCCESS) return std::nullopt;
	return data;
}

QString wideField(const wchar_t* text) { return QString::fromWCharArray(text).trimmed(); }

QString asciiField(const BYTE* base, DWORD offset, DWORD limit) {
	if (offset == 0 || offset >= limit) return QString();
	const auto* start = reinterpret_cast<const char*>(base + offset);
	auto length = strnlen(start, limit - offset);
	return QString::fromLatin1(start, static_cast<qsizetype>(length)).simplified();
}

QString luidText(const LUID& luid) {
	return QString::asprintf(
	    "0x%08lX_0x%08lX",
	    static_cast<unsigned long>(luid.HighPart),
	    static_cast<unsigned long>(luid.LowPart)
	);
}

QString vendorName(UINT vendorId) {
	switch (vendorId) {
	case 0x10DE: return QStringLiteral("NVIDIA");
	case 0x1002:
	case 0x1022: return QStringLiteral("AMD");
	case 0x8086: return QStringLiteral("Intel");
	default: return QString();
	}
}

QString driverVersionText(const LARGE_INTEGER& version, UINT vendorId) {
	auto high = static_cast<quint32>(version.HighPart);
	auto low = static_cast<quint32>(version.LowPart);
	auto part1 = high >> 16;
	auto part2 = high & 0xFFFF;
	auto part3 = low >> 16;
	auto part4 = low & 0xFFFF;

	if (vendorId == 0x10DE) {
		auto digits = QString::number(part3) + QString::number(part4).rightJustified(4, u'0');
		if (digits.size() >= 5) {
			auto tail = digits.right(5);
			return tail.left(3) + u'.' + tail.right(2);
		}
	}

	return QStringLiteral("%1.%2.%3.%4").arg(part1).arg(part2).arg(part3).arg(part4);
}

QString busTypeName(STORAGE_BUS_TYPE type) {
	switch (type) {
	case BusTypeScsi: return QStringLiteral("SCSI");
	case BusTypeAtapi: return QStringLiteral("ATAPI");
	case BusTypeAta: return QStringLiteral("ATA");
	case BusType1394: return QStringLiteral("1394");
	case BusTypeSsa: return QStringLiteral("SSA");
	case BusTypeFibre: return QStringLiteral("Fibre Channel");
	case BusTypeUsb: return QStringLiteral("USB");
	case BusTypeRAID: return QStringLiteral("RAID");
	case BusTypeiScsi: return QStringLiteral("iSCSI");
	case BusTypeSas: return QStringLiteral("SAS");
	case BusTypeSata: return QStringLiteral("SATA");
	case BusTypeSd: return QStringLiteral("SD");
	case BusTypeMmc: return QStringLiteral("MMC");
	case BusTypeVirtual: return QStringLiteral("Virtual");
	case BusTypeFileBackedVirtual: return QStringLiteral("File-backed virtual");
	case BusTypeSpaces: return QStringLiteral("Storage Spaces");
	case BusTypeNvme: return QStringLiteral("NVMe");
	case BusTypeSCM: return QStringLiteral("SCM");
	case BusTypeUfs: return QStringLiteral("UFS");
	default: return QString();
	}
}

qreal mhzFromHz(ULONGLONG hz) {
	if (hz == 0) return qQNaN();
	return static_cast<qreal>(hz) / 1e6;
}

template <typename T>
bool kmtQuery(D3DKMT_HANDLE adapter, KMTQUERYADAPTERINFOTYPE type, T& data) {
	D3DKMT_QUERYADAPTERINFO query {};
	query.hAdapter = adapter;
	query.Type = type;
	query.pPrivateDriverData = &data;
	query.PrivateDriverDataSize = sizeof(T);
	return D3DKMTQueryAdapterInfo(&query) >= 0;
}

template <typename Prop>
void setReal(Prop& property, qreal value) {
	auto old = property.value();
	if (old == value || (std::isnan(old) && std::isnan(value))) return;
	property.setValue(value);
}

template <typename Fn>
void forEachInstance(PDH_HCOUNTER counter, DWORD format, Fn&& fn) {
	DWORD size = 0;
	DWORD count = 0;
	auto status = PdhGetFormattedCounterArrayW(counter, format, &size, &count, nullptr);
	if (status != static_cast<PDH_STATUS>(PDH_MORE_DATA) || size == 0) return;

	std::vector<BYTE> buffer(size);
	auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
	status = PdhGetFormattedCounterArrayW(counter, format, &size, &count, items);
	if (status != ERROR_SUCCESS) return;

	for (DWORD i = 0; i != count; ++i) {
		const auto& value = items[i].FmtValue;
		if (value.CStatus != PDH_CSTATUS_VALID_DATA && value.CStatus != PDH_CSTATUS_NEW_DATA) continue;
		fn(QString::fromWCharArray(items[i].szName), value);
	}
}

QString instanceField(const QString& name, QLatin1StringView tag, qsizetype length = -1) {
	auto index = name.indexOf(tag, 0, Qt::CaseInsensitive);
	if (index < 0) return QString();
	return name.mid(index + tag.size(), length).toLower();
}

struct NvmlPciInfo {
	char busIdLegacy[16];
	unsigned int domain;
	unsigned int bus;
	unsigned int device;
	unsigned int pciDeviceId;
	unsigned int pciSubSystemId;
	char busId[32];
};

struct NvmlDeviceOpaque;
using NvmlDevice = NvmlDeviceOpaque*;

constexpr int NVML_SUCCESS = 0;
constexpr int NVML_TEMPERATURE_GPU = 0;
constexpr int NVML_CLOCK_GRAPHICS = 0;
constexpr int NVML_CLOCK_MEM = 2;

class NvmlApi {
public:
	NvmlApi() = default;
	~NvmlApi() { this->unload(); }
	Q_DISABLE_COPY_MOVE(NvmlApi);

	bool load() {
		if (this->initialized) return true;

		this->module = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (this->module == nullptr) return false;

		auto resolve = [this](auto& fn, const char* name) {
			fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(
			    reinterpret_cast<void*>(GetProcAddress(this->module, name))
			);
			return fn != nullptr;
		};

		auto ok = resolve(this->init, "nvmlInit_v2") && resolve(this->shutdown, "nvmlShutdown")
		       && resolve(this->getCount, "nvmlDeviceGetCount_v2")
		       && resolve(this->getHandle, "nvmlDeviceGetHandleByIndex_v2")
		       && resolve(this->getPciInfo, "nvmlDeviceGetPciInfo_v3")
		       && resolve(this->getTemperature, "nvmlDeviceGetTemperature")
		       && resolve(this->getPowerUsage, "nvmlDeviceGetPowerUsage")
		       && resolve(this->getPowerLimit, "nvmlDeviceGetEnforcedPowerLimit")
		       && resolve(this->getClock, "nvmlDeviceGetClockInfo");

		if (!ok || this->init() != NVML_SUCCESS) {
			FreeLibrary(this->module);
			this->module = nullptr;
			return false;
		}

		this->initialized = true;
		return true;
	}

	void unload() {
		if (this->initialized) this->shutdown();
		this->initialized = false;
		if (this->module != nullptr) FreeLibrary(this->module);
		this->module = nullptr;
	}

	[[nodiscard]] bool loaded() const { return this->initialized; }

	std::vector<std::pair<NvmlDevice, NvmlPciInfo>> devices() {
		std::vector<std::pair<NvmlDevice, NvmlPciInfo>> result;
		unsigned int count = 0;
		if (this->getCount(&count) != NVML_SUCCESS) return result;

		for (unsigned int i = 0; i != count; ++i) {
			NvmlDevice device = nullptr;
			if (this->getHandle(i, &device) != NVML_SUCCESS) continue;
			NvmlPciInfo pci {};
			if (this->getPciInfo(device, &pci) != NVML_SUCCESS) continue;
			result.emplace_back(device, pci);
		}

		return result;
	}

	qreal temperature(NvmlDevice device) const {
		unsigned int value = 0;
		if (this->getTemperature(device, NVML_TEMPERATURE_GPU, &value) != NVML_SUCCESS) {
			return qQNaN();
		}
		return value;
	}

	qreal powerWatts(NvmlDevice device) const {
		unsigned int milliwatts = 0;
		if (this->getPowerUsage(device, &milliwatts) != NVML_SUCCESS) return qQNaN();
		return milliwatts / 1000.0;
	}

	qreal powerLimitWatts(NvmlDevice device) const {
		unsigned int milliwatts = 0;
		if (this->getPowerLimit(device, &milliwatts) != NVML_SUCCESS || milliwatts == 0) {
			return qQNaN();
		}
		return milliwatts / 1000.0;
	}

	qreal clockMhz(NvmlDevice device, int type) const {
		unsigned int value = 0;
		if (this->getClock(device, type, &value) != NVML_SUCCESS || value == 0) return qQNaN();
		return value;
	}

private:
	HMODULE module = nullptr;
	bool initialized = false;
	int (*init)() = nullptr;
	int (*shutdown)() = nullptr;
	int (*getCount)(unsigned int*) = nullptr;
	int (*getHandle)(unsigned int, NvmlDevice*) = nullptr;
	int (*getPciInfo)(NvmlDevice, NvmlPciInfo*) = nullptr;
	int (*getTemperature)(NvmlDevice, int, unsigned int*) = nullptr;
	int (*getPowerUsage)(NvmlDevice, unsigned int*) = nullptr;
	int (*getPowerLimit)(NvmlDevice, unsigned int*) = nullptr;
	int (*getClock)(NvmlDevice, int, unsigned int*) = nullptr;
};

struct GpuState {
	QString name;
	QString vendor;
	QString driverVersion;
	QString luid;
	bool integrated = false;
	qint64 vramTotal = 0;
	UINT vendorId = 0;
	LUID adapterLuid {};
	D3DKMT_HANDLE kmt = 0;
	bool haveAddress = false;
	D3DKMT_ADAPTERADDRESS address {};
	bool haveFanCaps = false;
	ULONG maxFanRpm = 0;
	NvmlDevice nvml = nullptr;
};

void readHost(SystemMonitorStaticInfo& info) {
	DWORD size = 0;
	GetComputerNameExW(ComputerNamePhysicalDnsHostname, nullptr, &size);
	if (size == 0) return;

	std::vector<wchar_t> buffer(size + 1);
	size = static_cast<DWORD>(buffer.size());
	if (GetComputerNameExW(ComputerNamePhysicalDnsHostname, buffer.data(), &size)) {
		info.hostName = QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(size));
	}
}

void readOs(SystemMonitorStaticInfo& info) {
	const auto* key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
	auto product = regString(HKEY_LOCAL_MACHINE, key, L"ProductName");
	auto build = regString(HKEY_LOCAL_MACHINE, key, L"CurrentBuild");
	if (build.isEmpty()) build = regString(HKEY_LOCAL_MACHINE, key, L"CurrentBuildNumber");

	if (build.toInt() >= 22000 && product.startsWith(QLatin1StringView("Windows 10"))) {
		product.replace(0, 10, QStringLiteral("Windows 11"));
	}

	info.osName = product;
	info.osVersion = regString(HKEY_LOCAL_MACHINE, key, L"DisplayVersion");
	if (info.osVersion.isEmpty()) info.osVersion = regString(HKEY_LOCAL_MACHINE, key, L"ReleaseId");

	auto ubr = regDword(HKEY_LOCAL_MACHINE, key, L"UBR");
	info.osBuild = ubr && !build.isEmpty() ? build + u'.' + QString::number(*ubr) : build;
}

QString wmiString(IWbemClassObject* object, const wchar_t* name) {
	VARIANT value;
	VariantInit(&value);
	QString result;
	if (SUCCEEDED(object->Get(name, 0, &value, nullptr, nullptr)) && value.vt == VT_BSTR
	    && value.bstrVal != nullptr)
	{
		result = QString::fromWCharArray(value.bstrVal).trimmed();
	}
	VariantClear(&value);
	return result;
}

void readBoardWmi(SystemMonitorStaticInfo& info) {
	winrt::com_ptr<IWbemLocator> locator;
	auto hr = CoCreateInstance(
	    CLSID_WbemLocator,
	    nullptr,
	    CLSCTX_INPROC_SERVER,
	    IID_IWbemLocator,
	    locator.put_void()
	);
	if (FAILED(hr)) return;

	auto* ns = SysAllocString(L"ROOT\\CIMV2");
	winrt::com_ptr<IWbemServices> services;
	hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, services.put());
	SysFreeString(ns);
	if (FAILED(hr)) return;

	CoSetProxyBlanket(
	    services.get(),
	    RPC_C_AUTHN_WINNT,
	    RPC_C_AUTHZ_NONE,
	    nullptr,
	    RPC_C_AUTHN_LEVEL_CALL,
	    RPC_C_IMP_LEVEL_IMPERSONATE,
	    nullptr,
	    EOAC_NONE
	);

	auto* language = SysAllocString(L"WQL");
	auto* query = SysAllocString(L"SELECT Manufacturer, Product FROM Win32_BaseBoard");
	winrt::com_ptr<IEnumWbemClassObject> enumerator;
	hr = services->ExecQuery(
	    language,
	    query,
	    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
	    nullptr,
	    enumerator.put()
	);
	SysFreeString(language);
	SysFreeString(query);
	if (FAILED(hr)) return;

	winrt::com_ptr<IWbemClassObject> object;
	ULONG returned = 0;
	if (enumerator->Next(5000, 1, object.put(), &returned) != WBEM_S_NO_ERROR || returned == 0) return;

	if (info.boardVendor.isEmpty()) info.boardVendor = wmiString(object.get(), L"Manufacturer");
	if (info.boardModel.isEmpty()) info.boardModel = wmiString(object.get(), L"Product");
}

void readBoard(SystemMonitorStaticInfo& info) {
	const auto* key = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
	info.boardVendor = regString(HKEY_LOCAL_MACHINE, key, L"BaseBoardManufacturer");
	info.boardModel = regString(HKEY_LOCAL_MACHINE, key, L"BaseBoardProduct");
	if (info.boardVendor.isEmpty() || info.boardModel.isEmpty()) readBoardWmi(info);
}

void readCpuid(SystemMonitorStaticInfo& info) {
#if defined(_M_X64) || defined(_M_IX86)
	auto previous = SetThreadAffinityMask(GetCurrentThread(), 1);

	int regs[4] {};
	__cpuidex(regs, 0, 0);
	auto maxLeaf = regs[0];
	char vendor[13] {};
	std::memcpy(vendor, &regs[1], 4);
	std::memcpy(vendor + 4, &regs[3], 4);
	std::memcpy(vendor + 8, &regs[2], 4);

	if (std::strcmp(vendor, "GenuineIntel") == 0 && maxLeaf >= 0x16) {
		__cpuidex(regs, 0x16, 0);
		auto base = regs[0] & 0xFFFF;
		auto max = regs[1] & 0xFFFF;
		if (max != 0) info.cpuMaxMhz = max;
		if (std::isnan(info.cpuBaseMhz) && base != 0) info.cpuBaseMhz = base;
	}

	if (previous != 0) SetThreadAffinityMask(GetCurrentThread(), previous);
#else
	Q_UNUSED(info);
#endif
}

void readCpu(SystemMonitorStaticInfo& info) {
	const auto* key = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
	info.cpuName = regString(HKEY_LOCAL_MACHINE, key, L"ProcessorNameString").simplified();

	DWORD length = 0;
	GetLogicalProcessorInformationEx(RelationAll, nullptr, &length);
	if (length != 0) {
		std::vector<BYTE> buffer(length);
		if (GetLogicalProcessorInformationEx(
		        RelationAll,
		        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
		        &length
		    ))
		{
			for (DWORD offset = 0; offset < length;) {
				const auto* item =
				    reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
				if (item->Size == 0) break;

				if (item->Relationship == RelationProcessorCore) {
					info.cpuCores++;
					for (WORD g = 0; g != item->Processor.GroupCount; ++g) {
						info.cpuThreads += std::popcount(item->Processor.GroupMask[g].Mask);
					}
				} else if (item->Relationship == RelationCache && item->Cache.Level == 3) {
					info.cpuL3Bytes += item->Cache.CacheSize;
				}

				offset += item->Size;
			}
		}
	}

	if (info.cpuThreads == 0) {
		info.cpuThreads = static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
	}

	auto processors = std::max<DWORD>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS), 1);
	std::vector<ProcessorPowerInformation> power(processors);
	auto status = CallNtPowerInformation(
	    ProcessorInformation,
	    nullptr,
	    0,
	    power.data(),
	    static_cast<ULONG>(power.size() * sizeof(ProcessorPowerInformation))
	);
	if (status >= 0 && power[0].MaxMhz != 0) info.cpuBaseMhz = power[0].MaxMhz;

	if (std::isnan(info.cpuBaseMhz)) {
		auto mhz = regDword(HKEY_LOCAL_MACHINE, key, L"~MHz");
		if (mhz && *mhz != 0) info.cpuBaseMhz = *mhz;
	}

	readCpuid(info);
}

struct StorageQueryResult {
	std::vector<BYTE> data;
	bool ok = false;
};

StorageQueryResult storageQuery(HANDLE device, STORAGE_PROPERTY_ID property, DWORD bufferSize) {
	StorageQueryResult result;
	result.data.resize(bufferSize);

	STORAGE_PROPERTY_QUERY query {};
	query.PropertyId = property;
	query.QueryType = PropertyStandardQuery;

	DWORD returned = 0;
	result.ok = DeviceIoControl(
	                device,
	                IOCTL_STORAGE_QUERY_PROPERTY,
	                &query,
	                sizeof(query),
	                result.data.data(),
	                bufferSize,
	                &returned,
	                nullptr
	            )
	         && returned != 0;
	if (result.ok) result.data.resize(returned);
	return result;
}

HANDLE openDevice(const wchar_t* path) {
	return CreateFileW(
	    path,
	    0,
	    FILE_SHARE_READ | FILE_SHARE_WRITE,
	    nullptr,
	    OPEN_EXISTING,
	    0,
	    nullptr
	);
}

std::map<DWORD, QVariantList> collectVolumes() {
	std::map<DWORD, QVariantList> result;

	wchar_t drives[512] {};
	auto length = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives) - 1), drives);
	if (length == 0 || length >= std::size(drives)) return result;

	for (const wchar_t* root = drives; *root != L'\0'; root += wcslen(root) + 1) {
		auto type = GetDriveTypeW(root);
		if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;
		if (wcslen(root) < 2 || root[1] != L':') continue;

		wchar_t volumePath[] = L"\\\\.\\X:";
		volumePath[4] = root[0];
		auto* volume = openDevice(volumePath);
		if (volume == INVALID_HANDLE_VALUE) continue;

		std::vector<BYTE> extentBuffer(sizeof(VOLUME_DISK_EXTENTS) + 31 * sizeof(DISK_EXTENT));
		DWORD returned = 0;
		auto ok = DeviceIoControl(
		    volume,
		    IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
		    nullptr,
		    0,
		    extentBuffer.data(),
		    static_cast<DWORD>(extentBuffer.size()),
		    &returned,
		    nullptr
		);
		CloseHandle(volume);

		const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(extentBuffer.data());
		if (!ok || extents->NumberOfDiskExtents == 0) continue;

		wchar_t label[MAX_PATH + 1] {};
		wchar_t fs[MAX_PATH + 1] {};
		if (!GetVolumeInformationW(
		        root,
		        label,
		        MAX_PATH + 1,
		        nullptr,
		        nullptr,
		        nullptr,
		        fs,
		        MAX_PATH + 1
		    ))
		{
			continue;
		}

		ULARGE_INTEGER total {};
		ULARGE_INTEGER totalFree {};
		if (!GetDiskFreeSpaceExW(root, nullptr, &total, &totalFree)) continue;

		QVariantMap entry;
		entry.insert(QStringLiteral("mount"), QString::fromWCharArray(root));
		entry.insert(QStringLiteral("label"), wideField(label));
		entry.insert(QStringLiteral("fs"), wideField(fs));
		entry.insert(
		    QStringLiteral("usedBytes"),
		    static_cast<qint64>(total.QuadPart - std::min(totalFree.QuadPart, total.QuadPart))
		);
		entry.insert(QStringLiteral("totalBytes"), static_cast<qint64>(total.QuadPart));

		result[extents->Extents[0].DiskNumber].append(entry);
	}

	return result;
}

QVariantMap collectDisk(DWORD number, HANDLE device, QVariantList volumes) {
	QString model;
	QString bus;
	QString kind;
	bool removable = false;
	auto busType = BusTypeUnknown;

	auto descriptor = storageQuery(device, StorageDeviceProperty, 1024);
	if (descriptor.ok && descriptor.data.size() >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
		const auto* desc = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(descriptor.data.data());
		auto limit = static_cast<DWORD>(descriptor.data.size());
		auto vendor = asciiField(descriptor.data.data(), desc->VendorIdOffset, limit);
		auto product = asciiField(descriptor.data.data(), desc->ProductIdOffset, limit);

		model = product;
		if (!vendor.isEmpty() && vendor.compare(QLatin1StringView("ATA"), Qt::CaseInsensitive) != 0
		    && vendor.compare(QLatin1StringView("NVMe"), Qt::CaseInsensitive) != 0
		    && !product.contains(vendor, Qt::CaseInsensitive))
		{
			model = product.isEmpty() ? vendor : vendor + u' ' + product;
		}

		busType = desc->BusType;
		bus = busTypeName(busType);
		removable = desc->RemovableMedia != FALSE;
	}

	std::optional<bool> seekPenalty;
	auto penalty = storageQuery(device, StorageDeviceSeekPenaltyProperty, sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR));
	if (penalty.ok && penalty.data.size() >= sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR)) {
		seekPenalty =
		    reinterpret_cast<const DEVICE_SEEK_PENALTY_DESCRIPTOR*>(penalty.data.data())->IncursSeekPenalty
		    != FALSE;
	}

	if (busType == BusTypeNvme) kind = QStringLiteral("NVMe SSD");
	else if (busType == BusTypeUsb) kind = QStringLiteral("USB");
	else if (busType == BusTypeSd || busType == BusTypeMmc || removable) kind = QStringLiteral("Removable");
	else if (seekPenalty) kind = *seekPenalty ? QStringLiteral("HDD") : QStringLiteral("SSD");

	qint64 size = 0;
	std::vector<BYTE> geometry(sizeof(DISK_GEOMETRY_EX) + 256);
	DWORD returned = 0;
	if (DeviceIoControl(
	        device,
	        IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
	        nullptr,
	        0,
	        geometry.data(),
	        static_cast<DWORD>(geometry.size()),
	        &returned,
	        nullptr
	    ))
	{
		size = reinterpret_cast<const DISK_GEOMETRY_EX*>(geometry.data())->DiskSize.QuadPart;
	}

	qreal temperature = qQNaN();
	auto thermal = storageQuery(
	    device,
	    StorageDeviceTemperatureProperty,
	    sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR) + 7 * sizeof(STORAGE_TEMPERATURE_INFO)
	);
	if (thermal.ok && thermal.data.size() >= sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR)) {
		const auto* data =
		    reinterpret_cast<const STORAGE_TEMPERATURE_DATA_DESCRIPTOR*>(thermal.data.data());
		if (data->InfoCount > 0) {
			auto value = data->TemperatureInfo[0].Temperature;
			if (value != static_cast<SHORT>(STORAGE_TEMPERATURE_VALUE_NOT_REPORTED) && value > 0
			    && value < 150)
			{
				temperature = value;
			}
		}
	}

	std::ranges::sort(volumes, [](const QVariant& a, const QVariant& b) {
		return a.toMap().value(QStringLiteral("mount")).toString()
		     < b.toMap().value(QStringLiteral("mount")).toString();
	});

	QVariantMap disk;
	disk.insert(QStringLiteral("number"), static_cast<int>(number));
	disk.insert(QStringLiteral("model"), model);
	disk.insert(QStringLiteral("kind"), kind);
	disk.insert(QStringLiteral("bus"), bus);
	disk.insert(QStringLiteral("sizeBytes"), size);
	disk.insert(QStringLiteral("temperature"), temperature);
	disk.insert(QStringLiteral("volumes"), volumes);
	return disk;
}

QVariantList collectDisks() {
	auto volumes = collectVolumes();

	std::vector<DWORD> numbers;
	for (DWORD n = 0; n != MAX_PHYSICAL_DRIVES; ++n) numbers.push_back(n);
	for (const auto& [n, list]: volumes) {
		if (n >= MAX_PHYSICAL_DRIVES) numbers.push_back(n);
	}

	QVariantList disks;
	for (auto number: numbers) {
		auto path = std::wstring(L"\\\\.\\PhysicalDrive") + std::to_wstring(number);
		auto* device = openDevice(path.c_str());
		if (device == INVALID_HANDLE_VALUE) continue;

		auto it = volumes.find(number);
		auto disk = collectDisk(number, device, it == volumes.end() ? QVariantList() : it->second);
		CloseHandle(device);

		if (disk.value(QStringLiteral("sizeBytes")).toLongLong() == 0
		    && disk.value(QStringLiteral("volumes")).toList().isEmpty())
		{
			continue;
		}

		disks.append(disk);
	}

	return disks;
}

} // namespace

class SystemMonitorWorker {
public:
	SystemMonitorWorker(SystemMonitor* owner, int intervalMs)
	    : owner(owner)
	    , intervalMs(intervalMs)
	    , thread([this] { this->run(); }) {}

	~SystemMonitorWorker() {
		{
			auto lock = std::scoped_lock(this->mutex);
			this->stop = true;
		}
		this->cv.notify_all();
		if (this->thread.joinable()) this->thread.join();
	}

	Q_DISABLE_COPY_MOVE(SystemMonitorWorker);

	void setActive(bool active) {
		{
			auto lock = std::scoped_lock(this->mutex);
			this->active = active;
		}
		this->cv.notify_all();
	}

	void setIntervalMs(int intervalMs) {
		auto lock = std::scoped_lock(this->mutex);
		this->intervalMs = intervalMs;
	}

private:
	void run();
	void collectStatic();
	void collectGpus();
	void matchNvml();
	void releaseGpus();
	void openLive();
	void closeLive();
	void prime();
	void sample(bool refreshDisks);
	QVariantList sampleGpus(bool havePdh);
	std::optional<qreal> sampleCpuUsage();

	SystemMonitor* owner;

	std::mutex mutex;
	std::condition_variable cv;
	bool stop = false;
	bool active = false;
	int intervalMs;

	bool staticDone = false;
	qreal cpuBaseMhz = qQNaN();
	std::vector<GpuState> gpus;
	NvmlApi nvml;

	PDH_HQUERY query = nullptr;
	PDH_HCOUNTER cpuPerformance = nullptr;
	PDH_HCOUNTER pageFileUsage = nullptr;
	PDH_HCOUNTER gpuEngine = nullptr;
	PDH_HCOUNTER gpuDedicated = nullptr;
	PDH_HCOUNTER gpuShared = nullptr;

	bool havePrevCpuTimes = false;
	ULONGLONG prevIdle = 0;
	ULONGLONG prevKernel = 0;
	ULONGLONG prevUser = 0;

	std::chrono::steady_clock::time_point lastDisks;

	std::thread thread;
};

void SystemMonitorWorker::run() {
	SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, nullptr);
	auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(com)) qCWarning(logSystemMonitor) << "CoInitializeEx failed:" << Qt::hex << com;

	auto lock = std::unique_lock(this->mutex);
	while (true) {
		this->cv.wait(lock, [this] { return this->stop || this->active; });
		if (this->stop) break;

		lock.unlock();
		if (!this->staticDone) {
			this->collectStatic();
			this->staticDone = true;
		}
		this->openLive();
		this->prime();
		lock.lock();

		auto deadline = std::chrono::steady_clock::now() + PRIME_DELAY;
		auto refreshDisks = true;
		while (true) {
			if (this->cv.wait_until(lock, deadline, [this] { return this->stop || !this->active; })) {
				break;
			}

			auto started = std::chrono::steady_clock::now();
			lock.unlock();
			this->sample(refreshDisks);
			refreshDisks = false;
			lock.lock();
			deadline = started + std::chrono::milliseconds(this->intervalMs);
		}

		lock.unlock();
		this->closeLive();
		lock.lock();
	}
	lock.unlock();

	this->closeLive();
	this->releaseGpus();
	this->nvml.unload();
	if (SUCCEEDED(com)) CoUninitialize();
}

void SystemMonitorWorker::collectStatic() {
	SystemMonitorStaticInfo info;
	readHost(info);
	readOs(info);
	readBoard(info);
	readCpu(info);
	this->cpuBaseMhz = info.cpuBaseMhz;

	this->collectGpus();

	auto* target = this->owner;
	QMetaObject::invokeMethod(
	    target,
	    [target, info = std::move(info)]() { target->applyStatic(info); },
	    Qt::QueuedConnection
	);
}

void SystemMonitorWorker::collectGpus() {
	winrt::com_ptr<IDXGIFactory1> factory;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.put_void()))) return;

	using DXCoreCreateFn = HRESULT(WINAPI*)(REFIID, void**);
	auto* dxcoreModule = LoadLibraryExW(L"dxcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	{
		winrt::com_ptr<IDXCoreAdapterFactory> dxcore;
		if (dxcoreModule != nullptr) {
			auto create = reinterpret_cast<DXCoreCreateFn>(
			    reinterpret_cast<void*>(GetProcAddress(dxcoreModule, "DXCoreCreateAdapterFactory"))
			);
			if (create != nullptr) create(__uuidof(IDXCoreAdapterFactory), dxcore.put_void());
		}

		for (UINT i = 0;; ++i) {
			winrt::com_ptr<IDXGIAdapter1> adapter;
			if (factory->EnumAdapters1(i, adapter.put()) == DXGI_ERROR_NOT_FOUND) break;

			DXGI_ADAPTER_DESC1 desc {};
			if (FAILED(adapter->GetDesc1(&desc))) continue;
			if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || desc.VendorId == 0x1414) continue;

			auto duplicate = std::ranges::any_of(this->gpus, [&](const GpuState& gpu) {
				return gpu.adapterLuid.LowPart == desc.AdapterLuid.LowPart
				    && gpu.adapterLuid.HighPart == desc.AdapterLuid.HighPart;
			});
			if (duplicate) continue;

			GpuState gpu;
			gpu.name = wideField(desc.Description);
			gpu.vendorId = desc.VendorId;
			gpu.vendor = vendorName(desc.VendorId);
			gpu.adapterLuid = desc.AdapterLuid;
			gpu.luid = luidText(desc.AdapterLuid);
			gpu.vramTotal = static_cast<qint64>(desc.DedicatedVideoMemory);

			LARGE_INTEGER umd {};
			if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
				gpu.driverVersion = driverVersionText(umd, desc.VendorId);
			}

			std::optional<bool> integrated;
			if (dxcore) {
				winrt::com_ptr<IDXCoreAdapter> core;
				if (SUCCEEDED(dxcore->GetAdapterByLuid(
				        desc.AdapterLuid,
				        __uuidof(IDXCoreAdapter),
				        core.put_void()
				    ))
				    && core->IsPropertySupported(DXCoreAdapterProperty::IsIntegrated))
				{
					bool value = false;
					if (SUCCEEDED(core->GetProperty(DXCoreAdapterProperty::IsIntegrated, sizeof(value), &value))) {
						integrated = value;
					}
				}
			}
			if (!integrated) {
				integrated = (desc.VendorId == 0x8086 || desc.VendorId == 0x1002)
				          && desc.DedicatedVideoMemory < 1024ull * 1024 * 1024;
			}
			gpu.integrated = *integrated;

			D3DKMT_OPENADAPTERFROMLUID open {};
			open.AdapterLuid = desc.AdapterLuid;
			if (D3DKMTOpenAdapterFromLuid(&open) >= 0) {
				gpu.kmt = open.hAdapter;

				D3DKMT_ADAPTERADDRESS address {};
				if (kmtQuery(gpu.kmt, KMTQAITYPE_ADAPTERADDRESS, address)) {
					gpu.address = address;
					gpu.haveAddress = true;
				}

				D3DKMT_ADAPTER_PERFDATACAPS caps {};
				if (kmtQuery(gpu.kmt, KMTQAITYPE_ADAPTERPERFDATA_CAPS, caps)) {
					gpu.haveFanCaps = true;
					gpu.maxFanRpm = caps.MaxFanRPM;
				}
			}

			this->gpus.push_back(std::move(gpu));
		}
	}
	if (dxcoreModule != nullptr) FreeLibrary(dxcoreModule);

	this->matchNvml();
}

void SystemMonitorWorker::matchNvml() {
	auto nvidiaCount = std::ranges::count_if(this->gpus, [](const GpuState& gpu) {
		return gpu.vendorId == 0x10DE;
	});
	if (nvidiaCount == 0 || !this->nvml.load()) return;

	auto devices = this->nvml.devices();
	auto matched = false;
	for (auto& gpu: this->gpus) {
		if (gpu.vendorId != 0x10DE) continue;

		for (const auto& [device, pci]: devices) {
			auto byAddress = gpu.haveAddress && pci.bus == gpu.address.BusNumber
			              && pci.device == gpu.address.DeviceNumber;
			auto onlyOne = !gpu.haveAddress && nvidiaCount == 1 && devices.size() == 1;
			if (byAddress || onlyOne) {
				gpu.nvml = device;
				matched = true;
				break;
			}
		}
	}

	if (!matched) this->nvml.unload();
}

void SystemMonitorWorker::releaseGpus() {
	for (auto& gpu: this->gpus) {
		if (gpu.kmt != 0) {
			D3DKMT_CLOSEADAPTER close {};
			close.hAdapter = gpu.kmt;
			D3DKMTCloseAdapter(&close);
			gpu.kmt = 0;
		}
		gpu.nvml = nullptr;
	}
	this->gpus.clear();
}

void SystemMonitorWorker::openLive() {
	if (this->query != nullptr) return;
	if (PdhOpenQueryW(nullptr, 0, &this->query) != ERROR_SUCCESS) {
		this->query = nullptr;
		return;
	}

	auto add = [this](const wchar_t* path) -> PDH_HCOUNTER {
		PDH_HCOUNTER counter = nullptr;
		auto status = PdhAddEnglishCounterW(this->query, path, 0, &counter);
		if (status != ERROR_SUCCESS) {
			qCDebug(logSystemMonitor) << "PDH counter unavailable:" << QString::fromWCharArray(path)
			                          << Qt::hex << status;
			return nullptr;
		}
		return counter;
	};

	this->cpuPerformance = add(L"\\Processor Information(_Total)\\% Processor Performance");
	this->pageFileUsage = add(L"\\Paging File(_Total)\\% Usage");
	if (!this->gpus.empty()) {
		this->gpuEngine = add(L"\\GPU Engine(*)\\Utilization Percentage");
		this->gpuDedicated = add(L"\\GPU Adapter Memory(*)\\Dedicated Usage");
		this->gpuShared = add(L"\\GPU Adapter Memory(*)\\Shared Usage");
	}
}

void SystemMonitorWorker::closeLive() {
	if (this->query != nullptr) PdhCloseQuery(this->query);
	this->query = nullptr;
	this->cpuPerformance = nullptr;
	this->pageFileUsage = nullptr;
	this->gpuEngine = nullptr;
	this->gpuDedicated = nullptr;
	this->gpuShared = nullptr;
	this->havePrevCpuTimes = false;
}

void SystemMonitorWorker::prime() {
	this->sampleCpuUsage();
	if (this->query != nullptr) PdhCollectQueryData(this->query);
}

std::optional<qreal> SystemMonitorWorker::sampleCpuUsage() {
	FILETIME idleTime {};
	FILETIME kernelTime {};
	FILETIME userTime {};
	if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) return std::nullopt;

	auto toU64 = [](const FILETIME& time) {
		return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
	};
	auto idle = toU64(idleTime);
	auto kernel = toU64(kernelTime);
	auto user = toU64(userTime);

	std::optional<qreal> usage;
	if (this->havePrevCpuTimes) {
		auto idleDelta = idle - this->prevIdle;
		auto totalDelta = (kernel - this->prevKernel) + (user - this->prevUser);
		if (totalDelta > 0) {
			auto busy = totalDelta > idleDelta ? totalDelta - idleDelta : 0;
			usage = std::clamp(static_cast<qreal>(busy) / static_cast<qreal>(totalDelta), 0.0, 1.0);
		}
	}

	this->prevIdle = idle;
	this->prevKernel = kernel;
	this->prevUser = user;
	this->havePrevCpuTimes = true;
	return usage;
}

void SystemMonitorWorker::sample(bool refreshDisks) {
	SystemMonitorSample result;
	result.uptimeSeconds = static_cast<qint64>(GetTickCount64() / 1000);

	if (auto usage = this->sampleCpuUsage()) result.cpuUsage = *usage;

	auto collected = this->query != nullptr && PdhCollectQueryData(this->query) == ERROR_SUCCESS;

	if (collected && this->cpuPerformance != nullptr && !std::isnan(this->cpuBaseMhz)) {
		PDH_FMT_COUNTERVALUE value {};
		if (PdhGetFormattedCounterValue(
		        this->cpuPerformance,
		        PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
		        nullptr,
		        &value
		    ) == ERROR_SUCCESS
		    && (value.CStatus == PDH_CSTATUS_VALID_DATA || value.CStatus == PDH_CSTATUS_NEW_DATA)
		    && value.doubleValue > 0)
		{
			result.cpuMhz = this->cpuBaseMhz * value.doubleValue / 100.0;
		}
	}

	MEMORYSTATUSEX memory {};
	memory.dwLength = sizeof(memory);
	if (GlobalMemoryStatusEx(&memory)) {
		result.haveMemory = true;
		result.memoryTotal = static_cast<qint64>(memory.ullTotalPhys);
		result.memoryUsed =
		    static_cast<qint64>(memory.ullTotalPhys - std::min(memory.ullAvailPhys, memory.ullTotalPhys));

		auto swapTotal = memory.ullTotalPageFile > memory.ullTotalPhys
		                   ? memory.ullTotalPageFile - memory.ullTotalPhys
		                   : 0;
		result.swapTotal = static_cast<qint64>(swapTotal);

		if (collected && this->pageFileUsage != nullptr && swapTotal != 0) {
			PDH_FMT_COUNTERVALUE value {};
			if (PdhGetFormattedCounterValue(this->pageFileUsage, PDH_FMT_DOUBLE, nullptr, &value)
			        == ERROR_SUCCESS
			    && (value.CStatus == PDH_CSTATUS_VALID_DATA || value.CStatus == PDH_CSTATUS_NEW_DATA))
			{
				auto fraction = std::clamp(value.doubleValue / 100.0, 0.0, 1.0);
				result.swapUsed = static_cast<qint64>(std::llround(fraction * static_cast<double>(swapTotal)));
			}
		}
	}

	result.gpus = this->sampleGpus(collected);

	auto now = std::chrono::steady_clock::now();
	if (refreshDisks || now - this->lastDisks >= DISK_REFRESH) {
		result.haveDisks = true;
		result.disks = collectDisks();
		this->lastDisks = now;
	}

	auto* target = this->owner;
	QMetaObject::invokeMethod(
	    target,
	    [target, result = std::move(result)]() { target->applySample(result); },
	    Qt::QueuedConnection
	);
}

QVariantList SystemMonitorWorker::sampleGpus(bool havePdh) {
	std::map<QString, std::map<QString, double>> engineByLuid;
	if (havePdh && this->gpuEngine != nullptr) {
		forEachInstance(
		    this->gpuEngine,
		    PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
		    [&](const QString& name, const PDH_FMT_COUNTERVALUE& value) {
			    auto luid = instanceField(name, QLatin1StringView("luid_"), 21);
			    auto engine = instanceField(name, QLatin1StringView("engtype_"));
			    if (luid.isEmpty()) return;
			    engineByLuid[luid][engine] += value.doubleValue;
		    }
		);
	}

	auto memoryByLuid = [&](PDH_HCOUNTER counter) {
		std::map<QString, qint64> result;
		if (!havePdh || counter == nullptr) return result;
		forEachInstance(counter, PDH_FMT_LARGE, [&](const QString& name, const PDH_FMT_COUNTERVALUE& value) {
			auto luid = instanceField(name, QLatin1StringView("luid_"), 21);
			if (!luid.isEmpty()) result[luid] += value.largeValue;
		});
		return result;
	};
	auto dedicated = memoryByLuid(this->gpuDedicated);
	auto shared = memoryByLuid(this->gpuShared);

	QVariantList list;
	for (const auto& gpu: this->gpus) {
		auto key = gpu.luid.toLower();

		qreal usage = qQNaN();
		if (havePdh && this->gpuEngine != nullptr) {
			usage = 0;
			auto it = engineByLuid.find(key);
			if (it != engineByLuid.end()) {
				for (const auto& [engine, sum]: it->second) usage = std::max(usage, sum / 100.0);
			}
			usage = std::clamp(usage, 0.0, 1.0);
		}

		auto memoryValue = [&](const std::map<QString, qint64>& map, PDH_HCOUNTER counter) -> QVariant {
			if (!havePdh || counter == nullptr) return qQNaN();
			auto it = map.find(key);
			return it == map.end() ? qint64(0) : it->second;
		};

		qreal temperature = qQNaN();
		qreal powerPercent = qQNaN();
		qreal powerWatts = qQNaN();
		qreal clockMhz = qQNaN();
		qreal memoryClockMhz = qQNaN();
		qreal fanRpm = qQNaN();

		if (gpu.kmt != 0) {
			D3DKMT_ADAPTER_PERFDATA perf {};
			if (kmtQuery(gpu.kmt, KMTQAITYPE_ADAPTERPERFDATA, perf)) {
				if (perf.Temperature != 0) temperature = perf.Temperature / 10.0;
				if (perf.Power != 0) powerPercent = std::clamp(perf.Power / 1000.0, 0.0, 1.0);
				memoryClockMhz = mhzFromHz(perf.MemoryFrequency);
				if (gpu.haveFanCaps ? gpu.maxFanRpm != 0 : perf.FanRPM != 0) fanRpm = perf.FanRPM;
			}

			D3DKMT_NODE_PERFDATA node {};
			if (kmtQuery(gpu.kmt, KMTQAITYPE_NODEPERFDATA, node)) clockMhz = mhzFromHz(node.Frequency);
		}

		if (gpu.nvml != nullptr) {
			if (std::isnan(temperature)) temperature = this->nvml.temperature(gpu.nvml);
			powerWatts = this->nvml.powerWatts(gpu.nvml);
			if (std::isnan(powerPercent) && !std::isnan(powerWatts)) {
				auto limit = this->nvml.powerLimitWatts(gpu.nvml);
				if (!std::isnan(limit)) powerPercent = std::clamp(powerWatts / limit, 0.0, 1.0);
			}
			if (std::isnan(clockMhz)) clockMhz = this->nvml.clockMhz(gpu.nvml, NVML_CLOCK_GRAPHICS);
			if (std::isnan(memoryClockMhz)) {
				memoryClockMhz = this->nvml.clockMhz(gpu.nvml, NVML_CLOCK_MEM);
			}
		}

		QVariantMap entry;
		entry.insert(QStringLiteral("name"), gpu.name);
		entry.insert(QStringLiteral("vendor"), gpu.vendor);
		entry.insert(QStringLiteral("integrated"), gpu.integrated);
		entry.insert(QStringLiteral("driverVersion"), gpu.driverVersion);
		entry.insert(QStringLiteral("luid"), gpu.luid);
		entry.insert(QStringLiteral("vramTotal"), gpu.vramTotal);
		entry.insert(QStringLiteral("vramUsed"), memoryValue(dedicated, this->gpuDedicated));
		entry.insert(QStringLiteral("sharedUsed"), memoryValue(shared, this->gpuShared));
		entry.insert(QStringLiteral("usage"), usage);
		entry.insert(QStringLiteral("temperature"), temperature);
		entry.insert(QStringLiteral("powerWatts"), powerWatts);
		entry.insert(QStringLiteral("powerPercent"), powerPercent);
		entry.insert(QStringLiteral("clockMhz"), clockMhz);
		entry.insert(QStringLiteral("memoryClockMhz"), memoryClockMhz);
		entry.insert(QStringLiteral("fanRpm"), fanRpm);
		list.append(entry);
	}

	return list;
}

SystemMonitor::SystemMonitor(QObject* parent): QObject(parent) {}

SystemMonitor::~SystemMonitor() { this->worker.reset(); }

void SystemMonitor::setActive(bool active) {
	if (this->mActive == active) return;
	this->mActive = active;

	if (active && !this->worker) {
		this->worker = std::make_unique<SystemMonitorWorker>(this, this->mIntervalMs);
	}
	if (this->worker) this->worker->setActive(active);

	emit this->activeChanged();
}

void SystemMonitor::setIntervalMs(int intervalMs) {
	intervalMs = std::max(intervalMs, MIN_INTERVAL_MS);
	if (this->mIntervalMs == intervalMs) return;
	this->mIntervalMs = intervalMs;
	if (this->worker) this->worker->setIntervalMs(intervalMs);
	emit this->intervalMsChanged();
}

QVariantMap SystemMonitor::volumeUsage(const QString& path) const {
	if (path.isEmpty()) return {};

	auto native = QDir::toNativeSeparators(path);
	if (!native.endsWith(u'\\')) native += u'\\';

	DWORD previousMode = 0;
	SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &previousMode);
	ULARGE_INTEGER total {};
	ULARGE_INTEGER totalFree {};
	auto ok = GetDiskFreeSpaceExW(
	    reinterpret_cast<const wchar_t*>(native.utf16()),
	    nullptr,
	    &total,
	    &totalFree
	);
	SetThreadErrorMode(previousMode, nullptr);
	if (!ok) return {};

	QVariantMap result;
	result.insert(
	    QStringLiteral("usedBytes"),
	    static_cast<qint64>(total.QuadPart - std::min(totalFree.QuadPart, total.QuadPart))
	);
	result.insert(QStringLiteral("totalBytes"), static_cast<qint64>(total.QuadPart));
	return result;
}

void SystemMonitor::applyStatic(const SystemMonitorStaticInfo& info) {
	{
		const QScopedPropertyUpdateGroup group;
		this->bHostName = info.hostName;
		this->bOsName = info.osName;
		this->bOsVersion = info.osVersion;
		this->bOsBuild = info.osBuild;
		this->bBoardVendor = info.boardVendor;
		this->bBoardModel = info.boardModel;
		this->bCpuName = info.cpuName;
		this->bCpuCores = info.cpuCores;
		this->bCpuThreads = info.cpuThreads;
		setReal(this->bCpuBaseMhz, info.cpuBaseMhz);
		setReal(this->bCpuMaxMhz, info.cpuMaxMhz);
		this->bCpuL3Bytes = info.cpuL3Bytes;
		this->bReady = true;
	}
}

void SystemMonitor::applySample(const SystemMonitorSample& sample) {
	{
		const QScopedPropertyUpdateGroup group;
		this->bUptimeSeconds = sample.uptimeSeconds;
		if (!std::isnan(sample.cpuUsage)) this->bCpuUsage = sample.cpuUsage;
		setReal(this->bCpuMhz, sample.cpuMhz);
		setReal(this->bCpuTemperature, sample.cpuTemperature);
		if (sample.haveMemory) {
			this->bMemoryTotal = sample.memoryTotal;
			this->bMemoryUsed = sample.memoryUsed;
			this->bSwapTotal = sample.swapTotal;
			this->bSwapUsed = sample.swapUsed;
		}
		this->bGpus = sample.gpus;
		if (sample.haveDisks) this->bDisks = sample.disks;
	}
	emit this->updated();
}

} // namespace qs::windows::sys
