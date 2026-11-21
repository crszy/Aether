#include "autostart.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include <qbytearray.h>
#include <qcoreapplication.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qloggingcategory.h>
#include <qregularexpression.h>

#include <qt_windows.h>

#include <objbase.h>
#include <oleauto.h>
#include <sddl.h>
#include <taskschd.h>
#include <wrl/client.h>

#include "../../core/instanceinfo.hpp"
#include "autostart_config.hpp"
#include "registry_util.hpp"
#include "shell_link.hpp"

using Microsoft::WRL::ComPtr;

namespace qs::windows::sys {

namespace {

Q_LOGGING_CATEGORY(logAutostart, "quickshell.windows.autostart", QtWarningMsg);

const wchar_t* const RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* const APPROVED_RUN_KEY =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
const wchar_t* const APPROVED_FOLDER_KEY =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder";
const auto SELF_RUN_VALUE = QStringLiteral(QS_AUTOSTART_NAME);
const auto TASK_PREFIX = QStringLiteral(QS_AUTOSTART_NAME);
const auto TASK_DESCRIPTION = QStringLiteral(QS_AUTOSTART_DESCRIPTION);

QString shellArgs() {
	auto args = QStringLiteral(QS_AUTOSTART_ARGS);
	if (!args.isEmpty()) return args;
	return QStringLiteral("-n -p \"") + QDir::toNativeSeparators(InstanceInfo::CURRENT.configPath)
	     + QStringLiteral("\"");
}

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

class Bstr {
public:
	explicit Bstr(const QString& text)
	    : mValue(SysAllocStringLen(
	          reinterpret_cast<const OLECHAR*>(text.utf16()),
	          static_cast<UINT>(text.size())
	      )) {}

	Bstr(const Bstr&) = delete;
	Bstr(Bstr&&) = delete;
	Bstr& operator=(const Bstr&) = delete;
	Bstr& operator=(Bstr&&) = delete;

	~Bstr() { SysFreeString(this->mValue); }

	[[nodiscard]] BSTR get() const { return this->mValue; }
	[[nodiscard]] bool ok() const { return this->mValue != nullptr; }

private:
	BSTR mValue;
};

QString hrText(HRESULT hr) {
	wchar_t* buffer = nullptr;
	auto length = FormatMessageW(
	    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	    nullptr,
	    static_cast<DWORD>(hr),
	    0,
	    reinterpret_cast<LPWSTR>(&buffer),
	    0,
	    nullptr
	);
	auto text = length > 0
	              ? QString::fromWCharArray(buffer, static_cast<qsizetype>(length)).trimmed()
	              : QString();
	if (buffer) LocalFree(buffer);

	auto code = QStringLiteral("0x%1").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
	return text.isEmpty() ? code : text + QStringLiteral(" (") + code + u')';
}

bool taskMissing(HRESULT hr) {
	return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
	    || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}

QString formatUserId(const QString& domain, const QString& name, const QString& sid) {
	auto d = domain.trimmed();
	auto n = name.trimmed();
	if (n.isEmpty()) return sid.trimmed();
	if (d.isEmpty()) return n;
	return d + u'\\' + n;
}

QString xmlEscape(const QString& text) {
	auto out = QString();
	out.reserve(text.size());
	for (auto c: text) {
		switch (c.unicode()) {
		case u'&': out += QStringLiteral("&amp;"); break;
		case u'<': out += QStringLiteral("&lt;"); break;
		case u'>': out += QStringLiteral("&gt;"); break;
		case u'"': out += QStringLiteral("&quot;"); break;
		case u'\'': out += QStringLiteral("&apos;"); break;
		default: out += c; break;
		}
	}
	return out;
}

QString taskCommand(const QString& exe) {
	auto hasSpace = std::any_of(exe.cbegin(), exe.cend(), [](QChar c) { return c.isSpace(); });
	if (hasSpace && !exe.startsWith(u'"')) return QStringLiteral("\"") + exe + u'"';
	return exe;
}

QString taskXml(const QString& user, const QString& exe, const QString& workDir) {
	auto escapedUser = xmlEscape(user);
	auto x = QString();
	x += QStringLiteral(
	    "<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
	);
	x += QStringLiteral("<RegistrationInfo><Description>");
	x += xmlEscape(TASK_DESCRIPTION);
	x += QStringLiteral("</Description></RegistrationInfo>");
	x += QStringLiteral("<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>");
	x += escapedUser;
	x += QStringLiteral("</UserId></LogonTrigger></Triggers>");
	x += QStringLiteral("<Principals><Principal id=\"Author\"><UserId>");
	x += escapedUser;
	x += QStringLiteral(
	    "</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel>"
	    "</Principal></Principals>"
	);
	x += QStringLiteral("<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>");
	x += QStringLiteral("<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>");
	x += QStringLiteral("<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>");
	x += QStringLiteral(
	    "<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>4</Priority><Enabled>true</Enabled>"
	    "</Settings>"
	);
	x += QStringLiteral("<Actions Context=\"Author\"><Exec><Command>");
	x += xmlEscape(taskCommand(exe));
	x += QStringLiteral("</Command><Arguments>");
	x += xmlEscape(shellArgs());
	x += QStringLiteral("</Arguments><WorkingDirectory>");
	x += xmlEscape(workDir);
	x += QStringLiteral("</WorkingDirectory></Exec></Actions></Task>");
	return x;
}

struct Account {
	QString sid;
	QString userId;
};

std::optional<Account> currentAccount() {
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return std::nullopt;

	DWORD length = 0;
	GetTokenInformation(token, TokenUser, nullptr, 0, &length);
	auto words = (length + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t) + 1;
	auto buffer = std::vector<std::uint64_t>(words);
	auto got = GetTokenInformation(
	    token,
	    TokenUser,
	    buffer.data(),
	    static_cast<DWORD>(buffer.size() * sizeof(std::uint64_t)),
	    &length
	);
	CloseHandle(token);
	if (!got) return std::nullopt;

	auto* sid = reinterpret_cast<const TOKEN_USER*>(buffer.data())->User.Sid;

	LPWSTR sidText = nullptr;
	if (!ConvertSidToStringSidW(sid, &sidText)) return std::nullopt;
	auto sidString = QString::fromWCharArray(sidText);
	LocalFree(sidText);
	if (sidString.isEmpty()) return std::nullopt;

	DWORD nameLength = 0;
	DWORD domainLength = 0;
	auto use = SID_NAME_USE();
	LookupAccountSidW(nullptr, sid, nullptr, &nameLength, nullptr, &domainLength, &use);
	auto name = std::vector<wchar_t>(std::max<DWORD>(nameLength, 1));
	auto domain = std::vector<wchar_t>(std::max<DWORD>(domainLength, 1));
	auto nameString = QString();
	auto domainString = QString();
	if (LookupAccountSidW(nullptr, sid, name.data(), &nameLength, domain.data(), &domainLength, &use))
	{
		nameString = QString::fromWCharArray(name.data(), static_cast<qsizetype>(nameLength));
		domainString = QString::fromWCharArray(domain.data(), static_cast<qsizetype>(domainLength));
	}

	return Account {sidString, formatUserId(domainString, nameString, sidString)};
}

QString taskName(const QString& sid) { return TASK_PREFIX + u'-' + sid.trimmed(); }

QString selfExe() {
	auto dir = QCoreApplication::applicationDirPath();
	auto qsw = dir + QStringLiteral("/qsw.exe");
	return QDir::toNativeSeparators(
	    QFileInfo::exists(qsw) ? qsw : QCoreApplication::applicationFilePath()
	);
}

QString selfWorkDir() { return QDir::toNativeSeparators(QCoreApplication::applicationDirPath()); }

QString runCommand() {
	return QStringLiteral("\"") + selfExe() + QStringLiteral("\" ") + shellArgs();
}

HRESULT openTaskRoot(ComPtr<ITaskFolder>& root) {
	ComPtr<ITaskService> service;
	auto hr = CoCreateInstance(
	    __uuidof(TaskScheduler),
	    nullptr,
	    CLSCTX_INPROC_SERVER,
	    IID_PPV_ARGS(&service)
	);
	if (FAILED(hr)) return hr;

	VARIANT empty;
	VariantInit(&empty);
	hr = service->Connect(empty, empty, empty, empty);
	if (FAILED(hr)) return hr;

	auto path = Bstr(QStringLiteral("\\"));
	if (!path.ok()) return E_OUTOFMEMORY;
	return service->GetFolder(path.get(), root.GetAddressOf());
}

std::optional<bool> selfTaskEnabled() {
	auto account = currentAccount();
	if (!account) return std::nullopt;

	auto com = ComApartment(COINIT_APARTMENTTHREADED);
	if (!comOk(com.hr)) return std::nullopt;

	ComPtr<ITaskFolder> root;
	auto hr = openTaskRoot(root);
	if (FAILED(hr)) {
		qCWarning(logAutostart) << "Couldn't open the Task Scheduler:" << hrText(hr);
		return std::nullopt;
	}

	auto name = Bstr(taskName(account->sid));
	if (!name.ok()) return std::nullopt;

	ComPtr<IRegisteredTask> task;
	hr = root->GetTask(name.get(), task.GetAddressOf());
	if (FAILED(hr)) {
		if (!taskMissing(hr)) {
			qCWarning(logAutostart) << "Couldn't read the scheduled task:" << hrText(hr);
		}
		return std::nullopt;
	}

	auto enabled = VARIANT_BOOL(VARIANT_FALSE);
	hr = task->get_Enabled(&enabled);
	if (FAILED(hr)) {
		qCWarning(logAutostart) << "Couldn't read whether the scheduled task is on:" << hrText(hr);
		return true;
	}
	return enabled != VARIANT_FALSE;
}

bool registerSelfTask(QString& error) {
	auto account = currentAccount();
	if (!account) {
		error = QStringLiteral("couldn't read this account's security identifier");
		return false;
	}

	auto com = ComApartment(COINIT_APARTMENTTHREADED);
	if (!comOk(com.hr)) {
		error = hrText(com.hr);
		return false;
	}

	ComPtr<ITaskFolder> root;
	auto hr = openTaskRoot(root);
	if (FAILED(hr)) {
		error = hrText(hr);
		return false;
	}

	auto name = Bstr(taskName(account->sid));
	auto xml = Bstr(taskXml(account->userId, selfExe(), selfWorkDir()));
	if (!name.ok() || !xml.ok()) {
		error = hrText(E_OUTOFMEMORY);
		return false;
	}

	VARIANT empty;
	VariantInit(&empty);
	ComPtr<IRegisteredTask> task;
	hr = root->RegisterTask(
	    name.get(),
	    xml.get(),
	    TASK_CREATE_OR_UPDATE,
	    empty,
	    empty,
	    TASK_LOGON_INTERACTIVE_TOKEN,
	    empty,
	    task.GetAddressOf()
	);
	if (FAILED(hr)) {
		error = hrText(hr);
		return false;
	}
	return true;
}

bool deleteSelfTask(QString& error) {
	auto account = currentAccount();
	if (!account) return true;

	auto com = ComApartment(COINIT_APARTMENTTHREADED);
	if (!comOk(com.hr)) {
		error = hrText(com.hr);
		return false;
	}

	ComPtr<ITaskFolder> root;
	auto hr = openTaskRoot(root);
	if (FAILED(hr)) {
		error = hrText(hr);
		return false;
	}

	auto name = Bstr(taskName(account->sid));
	if (!name.ok()) {
		error = hrText(E_OUTOFMEMORY);
		return false;
	}

	hr = root->DeleteTask(name.get(), 0);
	if (FAILED(hr) && !taskMissing(hr)) {
		error = hrText(hr);
		return false;
	}
	return true;
}

QString userStartupFolder() {
	auto appData = qEnvironmentVariable("AppData");
	if (appData.isEmpty()) return QString();
	return appData + QStringLiteral("/Microsoft/Windows/Start Menu/Programs/Startup");
}

QString machineStartupFolder() {
	auto programData = qEnvironmentVariable("ProgramData");
	if (programData.isEmpty()) return QString();
	return programData + QStringLiteral("/Microsoft/Windows/Start Menu/Programs/Startup");
}

bool isApprovedEnabled(const wchar_t* approvedSubkey, const QString& name) {
	auto wname = name.toStdWString();
	auto data = readRegistryBinary(HKEY_CURRENT_USER, approvedSubkey, wname.c_str());
	if (data.isEmpty()) return true;
	return static_cast<unsigned char>(data.at(0)) != 0x03;
}

bool setApproved(const wchar_t* approvedSubkey, const QString& name, bool enabled) {
	auto wname = name.toStdWString();
	if (enabled) return deleteRegistryValue(HKEY_CURRENT_USER, approvedSubkey, wname.c_str());

	auto buffer = QByteArray(12, '\0');
	buffer[0] = 0x03;
	FILETIME fileTime;
	GetSystemTimeAsFileTime(&fileTime);
	std::memcpy(buffer.data() + 4, &fileTime, sizeof(fileTime));
	return writeRegistryBinary(HKEY_CURRENT_USER, approvedSubkey, wname.c_str(), buffer);
}

void appendRegistryEntries(QVariantList& items, HKEY root, const QString& scope) {
	const auto isUser = root == HKEY_CURRENT_USER;

	auto appendFromView = [&](REGSAM extraFlags) {
		for (const auto& value: enumerateRegistryStrings(root, RUN_KEY, extraFlags)) {
			auto alreadyListed = std::any_of(items.cbegin(), items.cend(), [&](const QVariant& v) {
				auto map = v.toMap();
				return map.value(QStringLiteral("source")) == QStringLiteral("registry")
				    && map.value(QStringLiteral("scope")) == scope
				    && map.value(QStringLiteral("name")) == value.name;
			});
			if (alreadyListed) continue;

			if (isUser && value.name.compare(SELF_RUN_VALUE, Qt::CaseInsensitive) == 0) continue;

			auto item = QVariantMap();
			item[QStringLiteral("id")] = (isUser ? QStringLiteral("reg:user:") : QStringLiteral("reg:machine:"))
			                            + value.name;
			item[QStringLiteral("name")] = value.name;
			item[QStringLiteral("command")] = value.stringValue;
			item[QStringLiteral("scope")] = scope;
			item[QStringLiteral("source")] = QStringLiteral("registry");
			item[QStringLiteral("enabled")] =
			    isUser ? isApprovedEnabled(APPROVED_RUN_KEY, value.name) : true;
			item[QStringLiteral("removable")] = isUser;
			items.append(item);
		}
	};

	appendFromView(0);
	if (!isUser) appendFromView(KEY_WOW64_32KEY);
}

void appendFolderEntries(QVariantList& items, const QString& folder, const QString& scope) {
	const auto isUser = scope == QStringLiteral("user");
	auto dir = QDir(folder);
	if (!dir.exists()) return;

	const auto files = dir.entryInfoList({QStringLiteral("*.lnk")}, QDir::Files);
	for (const auto& fileInfo: files) {
		auto link = readShellLink(fileInfo.absoluteFilePath());
		auto fileName = fileInfo.fileName();

		auto command = link.valid
		    ? (link.arguments.isEmpty() ? link.targetPath : link.targetPath + u' ' + link.arguments)
		    : fileInfo.absoluteFilePath();

		auto item = QVariantMap();
		item[QStringLiteral("id")] =
		    (isUser ? QStringLiteral("folder:user:") : QStringLiteral("folder:machine:")) + fileName;
		item[QStringLiteral("name")] = fileInfo.completeBaseName();
		item[QStringLiteral("command")] = command;
		item[QStringLiteral("scope")] = scope;
		item[QStringLiteral("source")] = QStringLiteral("folder");
		item[QStringLiteral("enabled")] = isUser ? isApprovedEnabled(APPROVED_FOLDER_KEY, fileName) : true;
		item[QStringLiteral("removable")] = isUser;
		items.append(item);
	}
}

} // namespace

void Autostart::refresh() {
	auto items = QVariantList();

	appendRegistryEntries(items, HKEY_CURRENT_USER, QStringLiteral("user"));
	appendRegistryEntries(items, HKEY_LOCAL_MACHINE, QStringLiteral("machine"));
	appendFolderEntries(items, userStartupFolder(), QStringLiteral("user"));
	appendFolderEntries(items, machineStartupFolder(), QStringLiteral("machine"));

	std::sort(items.begin(), items.end(), [](const QVariant& a, const QVariant& b) {
		auto mapA = a.toMap();
		auto mapB = b.toMap();
		if (mapA.value(QStringLiteral("scope")) != mapB.value(QStringLiteral("scope"))) {
			return mapA.value(QStringLiteral("scope")).toString() == QStringLiteral("user");
		}
		return mapA.value(QStringLiteral("name")).toString().toLower()
		     < mapB.value(QStringLiteral("name")).toString().toLower();
	});

	this->mEntries = items;
	emit this->entriesChanged();

	if (this->mSelfLoaded) {
		auto wasEnabled = this->mSelfEnabled;
		auto wasMethod = this->mSelfMethod;
		this->loadSelf();
		if (wasEnabled != this->mSelfEnabled || wasMethod != this->mSelfMethod) {
			emit this->selfStartupChanged();
		}
	}
}

void Autostart::loadSelf() const {
	auto task = selfTaskEnabled();
	auto wname = SELF_RUN_VALUE.toStdWString();
	auto run = readRegistryString(HKEY_CURRENT_USER, RUN_KEY, wname.c_str());

	if (task.has_value()) {
		this->mSelfMethod = QStringLiteral("task");
		this->mSelfEnabled = *task;
	} else if (!run.isEmpty()) {
		this->mSelfMethod = QStringLiteral("run");
		this->mSelfEnabled = isApprovedEnabled(APPROVED_RUN_KEY, SELF_RUN_VALUE);
	} else {
		this->mSelfMethod.clear();
		this->mSelfEnabled = false;
	}
	this->mSelfLoaded = true;
}

bool Autostart::selfStartupEnabled() const {
	if (!this->mSelfLoaded) this->loadSelf();
	return this->mSelfEnabled;
}

QString Autostart::selfStartupMethod() const {
	if (!this->mSelfLoaded) this->loadSelf();
	return this->mSelfMethod;
}

bool Autostart::setSelfStartup(bool enabled, const QString& method) {
	auto wname = SELF_RUN_VALUE.toStdWString();
	auto removeRun = [&]() {
		auto ok = deleteRegistryValue(HKEY_CURRENT_USER, RUN_KEY, wname.c_str())
		       || readRegistryString(HKEY_CURRENT_USER, RUN_KEY, wname.c_str()).isEmpty();
		deleteRegistryValue(HKEY_CURRENT_USER, APPROVED_RUN_KEY, wname.c_str());
		return ok;
	};
	auto writeRun = [&]() {
		if (!writeRegistryString(HKEY_CURRENT_USER, RUN_KEY, wname.c_str(), runCommand())) {
			return false;
		}
		deleteRegistryValue(HKEY_CURRENT_USER, APPROVED_RUN_KEY, wname.c_str());
		return true;
	};

	auto ok = true;
	auto error = QString();
	this->mSelfFellBack = false;

	if (!enabled) {
		ok = deleteSelfTask(error);
		if (!removeRun()) {
			ok = false;
			if (error.isEmpty()) error = hrText(HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
		}
	} else if (method == QStringLiteral("run")) {
		ok = writeRun();
		if (ok) {
			QString taskError;
			if (!deleteSelfTask(taskError)) {
				qCWarning(logAutostart) << "Couldn't delete the old scheduled task:" << taskError;
			}
		} else {
			error = hrText(HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
		}
	} else if (registerSelfTask(error)) {
		removeRun();
	} else {
		ok = false;
		this->mSelfFellBack = writeRun();
		qCWarning(logAutostart) << "Couldn't create the scheduled task, using the Run key instead:"
		                        << error;
	}

	if (!ok && !this->mSelfFellBack) {
		qCWarning(logAutostart) << "Couldn't change how ii starts:" << error;
	}
	this->mSelfError = ok ? QString() : error;
	this->loadSelf();
	emit this->selfStartupChanged();
	return ok;
}

void Autostart::setEnabled(const QString& id, bool enabled) {
	if (id.startsWith(QStringLiteral("reg:user:"))) {
		setApproved(APPROVED_RUN_KEY, id.mid(9), enabled);
	} else if (id.startsWith(QStringLiteral("folder:user:"))) {
		setApproved(APPROVED_FOLDER_KEY, id.mid(12), enabled);
	} else {
		return;
	}

	this->refresh();
}

bool Autostart::removeEntry(const QString& id) {
	auto ok = false;

	if (id.startsWith(QStringLiteral("reg:user:"))) {
		auto name = id.mid(9);
		if (name.compare(SELF_RUN_VALUE, Qt::CaseInsensitive) == 0) return false;

		auto wname = name.toStdWString();
		ok = deleteRegistryValue(HKEY_CURRENT_USER, RUN_KEY, wname.c_str());
		deleteRegistryValue(HKEY_CURRENT_USER, APPROVED_RUN_KEY, wname.c_str());
	} else if (id.startsWith(QStringLiteral("folder:user:"))) {
		auto fileName = id.mid(12);
		if (fileName.contains(u'/') || fileName.contains(u'\\')
		    || fileName.contains(QStringLiteral("..")))
		{
			return false;
		}

		ok = QFile::remove(userStartupFolder() + u'/' + fileName);
		deleteRegistryValue(HKEY_CURRENT_USER, APPROVED_FOLDER_KEY, fileName.toStdWString().c_str());
	} else {
		return false;
	}

	this->refresh();
	return ok;
}

bool Autostart::addFromPath(
    const QString& filePath,
    const QString& name,
    const QStringList& arguments
) {
	if (filePath.isEmpty()) return false;

	auto folder = userStartupFolder();
	if (folder.isEmpty() || !QDir().mkpath(folder)) return false;

	auto safeName = name.isEmpty() ? QFileInfo(filePath).completeBaseName() : name;
	safeName.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|]+)")), QStringLiteral("_"));
	safeName = safeName.trimmed();
	if (safeName.isEmpty()) safeName = QStringLiteral("App");

	auto lnkPath = folder + u'/' + safeName + QStringLiteral(".lnk");
	auto counter = 2;
	while (QFile::exists(lnkPath)) {
		lnkPath = QStringLiteral("%1/%2 (%3).lnk").arg(folder, safeName, QString::number(counter));
		counter++;
	}

	auto data = ShellLinkData();
	data.targetPath = filePath;
	data.arguments = arguments.join(u' ');
	data.description = safeName;
	if (!filePath.startsWith(QStringLiteral("shell:"), Qt::CaseInsensitive)) {
		data.workingDirectory = QFileInfo(filePath).absolutePath();
	}

	auto ok = writeShellLink(lnkPath, data);
	if (ok) this->refresh();
	return ok;
}

} // namespace qs::windows::sys
