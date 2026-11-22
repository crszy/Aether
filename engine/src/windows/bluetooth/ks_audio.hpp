#pragma once

#include <qt_windows.h>
#include <qtypes.h>

namespace qs::bluetooth::ks {

struct OneShotResult {
	int endpoints = 0;
	int filters = 0;
	int sent = 0;
	HRESULT lastError = S_OK;
};

OneShotResult sendOneShot(const GUID& containerId, bool reconnect);

} // namespace qs::bluetooth::ks
