#include "shell_link.hpp"

#include <qdir.h>
#include <qfileinfo.h>
#include <qloggingcategory.h>
#include <qstring.h>

#include <qt_windows.h>

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace qs::windows::sys {

namespace {
Q_LOGGING_CATEGORY(logShellLink, "quickshell.windows.shelllink", QtWarningMsg);

struct ComApartment {
	explicit ComApartment(DWORD coinit): hr(CoInitializeEx(nullptr, coinit)) {}

	ComApartment(const ComApartment&) = delete;
	ComApartment(ComApartment&&) = delete;
	ComApartment& operator=(const ComApartment&) = delete;
	ComApartment& operator=(ComApartment&&) = delete;

	~ComApartment() {
		if (SUCCEEDED(this->hr)) CoUninitialize();
	}

	HRESULT hr;
};

bool comOk(HRESULT hr) { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }

} // namespace

bool writeShellLink(const QString& lnkPath, const ShellLinkData& data) {
	auto com = ComApartment(COINIT_APARTMENTTHREADED);
	if (!comOk(com.hr)) {
		qCWarning(logShellLink) << "CoInitializeEx failed:" << Qt::hex << com.hr;
		return false;
	}

	ComPtr<IShellLinkW> link;
	auto hr =
	    CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
	if (FAILED(hr)) {
		qCWarning(logShellLink) << "CoCreateInstance(ShellLink) failed:" << Qt::hex << hr;
		return false;
	}

	auto target = QDir::toNativeSeparators(data.targetPath).toStdWString();
	link->SetPath(target.c_str());

	if (!data.arguments.isEmpty()) {
		auto args = data.arguments.toStdWString();
		link->SetArguments(args.c_str());
	}

	if (!data.description.isEmpty()) {
		auto desc = data.description.left(256).toStdWString();
		link->SetDescription(desc.c_str());
	}

	if (!data.workingDirectory.isEmpty()) {
		auto dir = QDir::toNativeSeparators(data.workingDirectory).toStdWString();
		link->SetWorkingDirectory(dir.c_str());
	}

	if (!data.iconPath.isEmpty()) {
		auto icon = QDir::toNativeSeparators(data.iconPath).toStdWString();
		link->SetIconLocation(icon.c_str(), data.iconIndex);
	}

	link->SetShowCmd(SW_SHOWNORMAL);

	ComPtr<IPersistFile> persistFile;
	hr = link.As(&persistFile);
	if (FAILED(hr)) {
		qCWarning(logShellLink) << "QueryInterface(IPersistFile) failed:" << Qt::hex << hr;
		return false;
	}

	auto dir = QDir(QFileInfo(lnkPath).absolutePath());
	if (!dir.exists() && !dir.mkpath(".")) {
		qCWarning(logShellLink) << "could not create" << dir.absolutePath();
		return false;
	}

	auto wpath = QDir::toNativeSeparators(lnkPath).toStdWString();
	hr = persistFile->Save(wpath.c_str(), TRUE);
	if (FAILED(hr)) {
		qCWarning(logShellLink) << "IPersistFile::Save failed for" << lnkPath << ":" << Qt::hex << hr;
		return false;
	}

	return true;
}

ShellLinkData readShellLink(const QString& lnkPath) {
	ShellLinkData data;

	auto com = ComApartment(COINIT_APARTMENTTHREADED);
	if (!comOk(com.hr)) return data;

	ComPtr<IShellLinkW> link;
	auto hr =
	    CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
	if (FAILED(hr)) return data;

	ComPtr<IPersistFile> persistFile;
	hr = link.As(&persistFile);
	if (FAILED(hr)) return data;

	auto wpath = QDir::toNativeSeparators(lnkPath).toStdWString();
	hr = persistFile->Load(wpath.c_str(), STGM_READ);
	if (FAILED(hr)) return data;

	wchar_t pathBuf[MAX_PATH] = {};
	auto fd = WIN32_FIND_DATAW {};
	if (SUCCEEDED(link->GetPath(pathBuf, MAX_PATH, &fd, SLGP_RAWPATH))) {
		data.targetPath = QString::fromWCharArray(pathBuf);
	}

	wchar_t argsBuf[INFOTIPSIZE] = {};
	if (SUCCEEDED(link->GetArguments(argsBuf, INFOTIPSIZE))) {
		data.arguments = QString::fromWCharArray(argsBuf);
	}

	wchar_t descBuf[INFOTIPSIZE] = {};
	if (SUCCEEDED(link->GetDescription(descBuf, INFOTIPSIZE))) {
		data.description = QString::fromWCharArray(descBuf);
	}

	wchar_t dirBuf[MAX_PATH] = {};
	if (SUCCEEDED(link->GetWorkingDirectory(dirBuf, MAX_PATH))) {
		data.workingDirectory = QString::fromWCharArray(dirBuf);
	}

	wchar_t iconBuf[MAX_PATH] = {};
	auto iconIndex = 0;
	if (SUCCEEDED(link->GetIconLocation(iconBuf, MAX_PATH, &iconIndex))) {
		data.iconPath = QString::fromWCharArray(iconBuf);
		data.iconIndex = iconIndex;
	}

	data.valid = true;
	return data;
}

} // namespace qs::windows::sys
