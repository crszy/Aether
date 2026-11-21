#pragma once

#include <qt_windows.h>

#include <mmdeviceapi.h>
#include <mmreg.h>
#include <unknwn.h>

namespace qs::windows::services::pipewire {

struct IPolicyConfig: public IUnknown {
	virtual HRESULT STDMETHODCALLTYPE
	GetMixFormat(PCWSTR endpointId, WAVEFORMATEX** format) = 0;

	virtual HRESULT STDMETHODCALLTYPE
	GetDeviceFormat(PCWSTR endpointId, INT isDefault, WAVEFORMATEX** format) = 0;

	virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR endpointId) = 0;

	virtual HRESULT STDMETHODCALLTYPE
	SetDeviceFormat(PCWSTR endpointId, WAVEFORMATEX* endpointFormat, WAVEFORMATEX* mixFormat) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(
	    PCWSTR endpointId,
	    INT isDefault,
	    PINT64 defaultPeriod,
	    PINT64 minimumPeriod
	) = 0;

	virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR endpointId, PINT64 period) = 0;

	virtual HRESULT STDMETHODCALLTYPE
	GetShareMode(PCWSTR endpointId, struct DeviceShareMode* mode) = 0;

	virtual HRESULT STDMETHODCALLTYPE
	SetShareMode(PCWSTR endpointId, struct DeviceShareMode* mode) = 0;

	virtual HRESULT STDMETHODCALLTYPE
	GetPropertyValue(PCWSTR endpointId, const PROPERTYKEY& key, PROPVARIANT* value) = 0;

	virtual HRESULT STDMETHODCALLTYPE
	SetPropertyValue(PCWSTR endpointId, const PROPERTYKEY& key, PROPVARIANT* value) = 0;

	virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR endpointId, ERole role) = 0;

	virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR endpointId, INT visible) = 0;
};

extern const CLSID CLSID_PolicyConfigClient;
extern const IID IID_IPolicyConfig;

} // namespace qs::windows::services::pipewire
