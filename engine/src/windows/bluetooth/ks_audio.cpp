#include "ks_audio.hpp"

#include <functional>
#include <string>
#include <vector>

#include <combaseapi.h>
#include <devicetopology.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <propsys.h>
#include <qt_windows.h>
#include <winrt/base.h>

namespace qs::bluetooth::ks {

namespace {

constexpr GUID BT_AUDIO_PROPSET = {0x7fa06c40, 0xb8f6, 0x4c7e, {0x85, 0x56, 0xe8, 0xc3, 0x3a, 0x12, 0xe5, 0x4d}};

constexpr PROPERTYKEY CONTAINER_ID_KEY = {
    {0x8c7ed206, 0x3f8a, 0x4827, {0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c}},
    2,
};

HRESULT forEachEndpoint(
    IMMDeviceEnumerator* enumerator,
    const GUID& containerId,
    const std::function<void(IMMDevice*)>& visit
) {
	winrt::com_ptr<IMMDeviceCollection> endpoints;
	auto hr = enumerator->EnumAudioEndpoints(eAll, DEVICE_STATEMASK_ALL, endpoints.put());
	if (FAILED(hr)) return hr;

	UINT count = 0;
	hr = endpoints->GetCount(&count);
	if (FAILED(hr)) return hr;

	for (UINT i = 0; i < count; i++) {
		winrt::com_ptr<IMMDevice> endpoint;
		if (FAILED(endpoints->Item(i, endpoint.put()))) continue;

		winrt::com_ptr<IPropertyStore> props;
		if (FAILED(endpoint->OpenPropertyStore(STGM_READ, props.put()))) continue;

		PROPVARIANT value;
		PropVariantInit(&value);
		auto matches = SUCCEEDED(props->GetValue(CONTAINER_ID_KEY, &value)) && value.vt == VT_CLSID
		            && value.puuid != nullptr && IsEqualGUID(*value.puuid, containerId);
		PropVariantClear(&value);

		if (matches) visit(endpoint.get());
	}

	return S_OK;
}

winrt::com_ptr<IMMDeviceEnumerator> createEnumerator(HRESULT* hr) {
	winrt::com_ptr<IMMDeviceEnumerator> enumerator;
	*hr = CoCreateInstance(
	    __uuidof(MMDeviceEnumerator),
	    nullptr,
	    CLSCTX_INPROC_SERVER,
	    __uuidof(IMMDeviceEnumerator),
	    enumerator.put_void()
	);
	return enumerator;
}

std::wstring filterIdForEndpoint(IMMDevice* endpoint, HRESULT* hr) {
	winrt::com_ptr<IDeviceTopology> topology;
	*hr = endpoint->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr, topology.put_void());
	if (FAILED(*hr)) return {};

	winrt::com_ptr<IConnector> connector;
	*hr = topology->GetConnector(0, connector.put());
	if (FAILED(*hr)) return {};

	LPWSTR filterId = nullptr;
	*hr = connector->GetDeviceIdConnectedTo(&filterId);
	if (FAILED(*hr) || filterId == nullptr) return {};

	std::wstring result(filterId);
	CoTaskMemFree(filterId);
	return result;
}

} // namespace

OneShotResult sendOneShot(const GUID& containerId, bool reconnect) {
	OneShotResult result;

	auto hr = S_OK;
	auto enumerator = createEnumerator(&hr);
	if (!enumerator) {
		result.lastError = hr;
		return result;
	}

	std::vector<std::wstring> filters;
	hr = forEachEndpoint(enumerator.get(), containerId, [&](IMMDevice* endpoint) {
		result.endpoints++;

		auto endpointHr = S_OK;
		auto filterId = filterIdForEndpoint(endpoint, &endpointHr);
		if (filterId.empty()) {
			result.lastError = endpointHr;
			return;
		}

		for (const auto& seen: filters) {
			if (seen == filterId) return;
		}
		filters.push_back(filterId);
	});

	if (FAILED(hr)) {
		result.lastError = hr;
		return result;
	}

	result.filters = static_cast<int>(filters.size());

	for (const auto& filterId: filters) {
		winrt::com_ptr<IMMDevice> filter;
		hr = enumerator->GetDevice(filterId.c_str(), filter.put());
		if (FAILED(hr)) {
			result.lastError = hr;
			continue;
		}

		winrt::com_ptr<IKsControl> control;
		hr = filter->Activate(__uuidof(IKsControl), CLSCTX_ALL, nullptr, control.put_void());
		if (FAILED(hr)) {
			result.lastError = hr;
			continue;
		}

		KSPROPERTY property {};
		property.Set = BT_AUDIO_PROPSET;
		property.Id = reconnect ? KSPROPERTY_ONESHOT_RECONNECT : KSPROPERTY_ONESHOT_DISCONNECT;
		property.Flags = KSPROPERTY_TYPE_GET;

		ULONG returned = 0;
		hr = control->KsProperty(&property, sizeof(property), nullptr, 0, &returned);
		if (SUCCEEDED(hr)) result.sent++;
		else result.lastError = hr;
	}

	return result;
}

} // namespace qs::bluetooth::ks
