#include "keyboard_hook.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <windows.h>

#include "input_mask.hpp"
#include "util.hpp"

namespace qs::windows::hotkeys {

namespace {

Q_LOGGING_CATEGORY(logKeyboardHook, "quickshell.windows.hotkeys", QtInfoMsg);

std::atomic<std::shared_ptr<const HookSnapshot>> gSnapshot; // NOLINT
std::atomic<HWND> gTarget = nullptr;                         // NOLINT
std::atomic<UINT> gMessageBase = 0;                          // NOLINT
std::atomic<DWORD> gThreadId = 0;                            // NOLINT
std::atomic<bool> gStopRequested = false;                    // NOLINT
std::atomic<bool> gFailed = false;                           // NOLINT
std::atomic<bool> gForegroundBlocked = false; // NOLINT
std::atomic<bool> gSuperChord = false;        // NOLINT

std::thread* gThread = nullptr; // NOLINT

struct HeldKey {
	uint32_t id = 0;
	uint32_t serial = 0;
	DWORD lastTime = 0;
	bool active = false;
};

std::array<HeldKey, 256> gHeld {}; // NOLINT
uint8_t gTapCandidate = 0;         // NOLINT
DWORD gTapStart = 0;               // NOLINT
uint8_t gHoldActive = 0;           // NOLINT

constexpr DWORD STALE_REPEAT_MS = 1500;

bool isDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

uint8_t heldModifiers() {
	uint8_t mods = 0;
	if (isDown(VK_CONTROL)) mods |= ModCtrl;
	if (isDown(VK_MENU)) mods |= ModAlt;
	if (isDown(VK_SHIFT)) mods |= ModShift;
	if (isDown(VK_LWIN) || isDown(VK_RWIN)) mods |= ModSuper;
	return mods;
}

int otherSide(DWORD vk) {
	switch (vk) {
	case VK_LWIN: return VK_RWIN;
	case VK_RWIN: return VK_LWIN;
	case VK_LCONTROL: return VK_RCONTROL;
	case VK_RCONTROL: return VK_LCONTROL;
	case VK_LMENU: return VK_RMENU;
	case VK_RMENU: return VK_LMENU;
	case VK_LSHIFT: return VK_RSHIFT;
	case VK_RSHIFT: return VK_LSHIFT;
	default: return 0;
	}
}

bool isExtendedKey(uint8_t vk) {
	switch (vk) {
	case VK_INSERT:
	case VK_DELETE:
	case VK_HOME:
	case VK_END:
	case VK_PRIOR:
	case VK_NEXT:
	case VK_LEFT:
	case VK_RIGHT:
	case VK_UP:
	case VK_DOWN:
	case VK_LWIN:
	case VK_RWIN:
	case VK_APPS:
	case VK_RCONTROL:
	case VK_RMENU:
	case VK_DIVIDE:
	case VK_SNAPSHOT: return true;
	default: return vk >= VK_BROWSER_BACK && vk <= VK_LAUNCH_APP2;
	}
}

INPUT keyInput(uint8_t vk, bool up, WORD scan = 0, bool extended = false) {
	INPUT input {};
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = vk;
	input.ki.wScan = scan;
	input.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
	input.ki.dwExtraInfo = KeyboardHook::INJECTION_MARKER;
	return input;
}

INPUT chordInput(uint8_t vk, bool up) {
	auto scan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
	return keyInput(vk, up, scan, isExtendedKey(vk));
}

void injectMask() {
	std::array<INPUT, 2> inputs {
	    keyInput(KeyboardHook::MASK_KEY, false),
	    keyInput(KeyboardHook::MASK_KEY, true),
	};
	SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void injectMaskedRelease(const KBDLLHOOKSTRUCT* original) {
	std::array<INPUT, 3> inputs {
	    keyInput(KeyboardHook::MASK_KEY, false),
	    keyInput(KeyboardHook::MASK_KEY, true),
	    keyInput(
	        static_cast<uint8_t>(original->vkCode),
	        true,
	        static_cast<WORD>(original->scanCode),
	        (original->flags & LLKHF_EXTENDED) != 0
	    ),
	};
	SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void post(HookEvent event, uint32_t id, uint32_t serial) {
	auto* target = gTarget.load();
	if (target == nullptr) return;
	PostMessageW(target, gMessageBase.load() + event, id, static_cast<LPARAM>(serial));
}

void postAll(
    const HookSnapshot& snapshot,
    HookTrigger::Kind kind,
    uint8_t lone,
    HookEvent event
) {
	for (const auto& trigger: snapshot.triggers) {
		if (trigger.kind == kind && trigger.combo.lone == lone) post(event, trigger.id, snapshot.serial);
	}
}

bool hasTrigger(const HookSnapshot& snapshot, HookTrigger::Kind kind, uint8_t lone) {
	for (const auto& trigger: snapshot.triggers) {
		if (trigger.kind == kind && trigger.combo.lone == lone) return true;
	}
	return false;
}

DWORD integrityLevel(HANDLE token) {
	DWORD size = 0;
	GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
	if (size == 0) return 0;

	auto buffer = std::make_unique<uint8_t[]>(size);
	if (!GetTokenInformation(token, TokenIntegrityLevel, buffer.get(), size, &size)) return 0;

	auto* label = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.get());
	auto count = *GetSidSubAuthorityCount(label->Label.Sid);
	return count == 0 ? 0 : *GetSidSubAuthority(label->Label.Sid, count - 1);
}

DWORD ownIntegrityLevel() {
	static const DWORD level = []() {
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return DWORD(0);
		auto level = integrityLevel(token);
		CloseHandle(token);
		return level;
	}();

	return level;
}

bool isHiddenFromUs(HWND hwnd) {
	if (hwnd == nullptr) return false;

	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if (pid == 0 || pid == GetCurrentProcessId()) return false;

	auto* process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (process == nullptr) return true;

	auto hidden = true;
	HANDLE token = nullptr;
	if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
		auto level = integrityLevel(token);
		hidden = level == 0 || level > ownIntegrityLevel();
		CloseHandle(token);
	}

	CloseHandle(process);
	return hidden;
}

void CALLBACK foregroundChanged(
    HWINEVENTHOOK /*hook*/,
    DWORD /*event*/,
    HWND hwnd,
    LONG idObject,
    LONG /*idChild*/,
    DWORD /*thread*/,
    DWORD /*time*/
) {
	if (idObject != OBJID_WINDOW) return;
	gForegroundBlocked.store(isHiddenFromUs(hwnd));
}

LRESULT onModifier(const HookSnapshot& snapshot, const KBDLLHOOKSTRUCT* info, uint8_t bit, bool up) {
	auto vk = static_cast<int>(info->vkCode);
	auto blocked = gForegroundBlocked.load();

	if (!up) {
		if (isDown(vk)) return 0;
		if (bit == ModSuper && !isDown(otherSide(info->vkCode))) gSuperChord.store(false);

		if (gTapCandidate != 0) {
			gTapCandidate = 0;
		} else if (!blocked && heldModifiers() == 0 && hasTrigger(snapshot, HookTrigger::Tap, bit)) {
			gTapCandidate = bit;
			gTapStart = info->time;
		}

		if (!isDown(otherSide(info->vkCode))) {
			gHoldActive |= bit;
			postAll(snapshot, HookTrigger::Hold, bit, HookPressed);
		}

		return 0;
	}

	if ((gHoldActive & bit) != 0 && !isDown(otherSide(info->vkCode))) {
		gHoldActive &= ~bit;
		postAll(snapshot, HookTrigger::Hold, bit, HookReleased);
	}

	if (gTapCandidate != bit) return 0;
	gTapCandidate = 0;
	if (blocked) return 0;
	if (bit == ModSuper && gSuperChord.load()) return 0;

	auto masked = bit == ModSuper || bit == ModAlt;
	if (masked) injectMaskedRelease(info);

	if (info->time - gTapStart <= snapshot.tapTimeoutMs) {
		postAll(snapshot, HookTrigger::Tap, bit, HookTapped);
	}

	return masked ? 1 : 0;
}

LRESULT onKey(const HookSnapshot& snapshot, const KBDLLHOOKSTRUCT* info, bool up) {
	auto vk = static_cast<uint8_t>(info->vkCode);
	auto& held = gHeld.at(vk);

	if (up) {
		if (!held.active) return 0;
		held.active = false;
		post(HookReleased, held.id, held.serial);
		return 1;
	}

	gTapCandidate = 0;

	if (held.active) {
		if (info->time - held.lastTime <= STALE_REPEAT_MS) {
			held.lastTime = info->time;
			post(HookRepeated, held.id, held.serial);
			return 1;
		}

		held.active = false;
	}

	if (gForegroundBlocked.load()) return 0;

	auto mods = heldModifiers();

	if (vk == VK_DELETE && (mods & ModCtrl) != 0 && (mods & ModAlt) != 0) return 0;

	for (const auto& trigger: snapshot.triggers) {
		if (trigger.kind != HookTrigger::Combo) continue;
		if (trigger.combo.vk != vk || trigger.combo.mods != mods) continue;

		held = HeldKey {.id = trigger.id, .serial = snapshot.serial, .lastTime = info->time, .active = true};
		post(HookPressed, trigger.id, snapshot.serial);

		if ((mods & (ModSuper | ModAlt)) != 0) injectMask();
		return 1;
	}

	return 0;
}

LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam) {
	if (code != HC_ACTION) return CallNextHookEx(nullptr, code, wParam, lParam);

	auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam); // NOLINT(performance-no-int-to-ptr)
	qs::windows::noteHookDelay(true, info->time);

	if ((info->flags & LLKHF_INJECTED) != 0 && info->dwExtraInfo == KeyboardHook::INJECTION_MARKER) {
		return CallNextHookEx(nullptr, code, wParam, lParam);
	}

	auto snapshot = gSnapshot.load(std::memory_order_acquire);
	if (snapshot == nullptr || info->vkCode > 0xFF) {
		return CallNextHookEx(nullptr, code, wParam, lParam);
	}

	auto up = (info->flags & LLKHF_UP) != 0;
	auto bit = modifierBit(info->vkCode);

	auto swallow = bit != 0 ? onModifier(*snapshot, info, bit, up) : onKey(*snapshot, info, up);
	if (swallow != 0) return 1;

	return CallNextHookEx(nullptr, code, wParam, lParam);
}

void hookThreadMain() {
	MSG msg {};
	PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
	gThreadId.store(GetCurrentThreadId());
	if (gStopRequested.load()) return;

	prioritizeInputThread(THREAD_PRIORITY_TIME_CRITICAL);

	auto* hook = SetWindowsHookExW(WH_KEYBOARD_LL, &hookProc, GetModuleHandleW(nullptr), 0);

	if (hook == nullptr) {
		auto error = GetLastError();
		gFailed.store(true);
		qCWarning(logKeyboardHook) << "Failed to install the keyboard hook (error" << error
		                           << "), binds with the Windows key are unavailable.";
		return;
	}

	auto* foregroundHook = SetWinEventHook(
	    EVENT_SYSTEM_FOREGROUND,
	    EVENT_SYSTEM_FOREGROUND,
	    nullptr,
	    &foregroundChanged,
	    0,
	    0,
	    WINEVENT_OUTOFCONTEXT
	);

	gForegroundBlocked.store(isHiddenFromUs(GetForegroundWindow()));

	while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	if (foregroundHook != nullptr) UnhookWinEvent(foregroundHook);
	UnhookWindowsHookEx(hook);

	gHeld.fill({});
	gTapCandidate = 0;
	gHoldActive = 0;
}

} // namespace

uint8_t modifierBit(DWORD vk) {
	switch (vk) {
	case VK_LWIN:
	case VK_RWIN: return ModSuper;
	case VK_CONTROL:
	case VK_LCONTROL:
	case VK_RCONTROL: return ModCtrl;
	case VK_MENU:
	case VK_LMENU:
	case VK_RMENU: return ModAlt;
	case VK_SHIFT:
	case VK_LSHIFT:
	case VK_RSHIFT: return ModShift;
	default: return 0;
	}
}

bool KeyboardHook::start(HWND target, UINT messageBase) {
	gTarget.store(target);
	gMessageBase.store(messageBase);

	if (gThread != nullptr && !gFailed.load()) return true;

	if (gThread != nullptr) {
		gThread->join();
		delete gThread;
	}

	gThreadId.store(0);
	gStopRequested.store(false);
	gFailed.store(false);
	gThread = new std::thread(&hookThreadMain);
	return true;
}

void KeyboardHook::stop() {
	if (gThread == nullptr) return;

	gStopRequested.store(true);
	auto threadId = gThreadId.load();
	if (threadId != 0) PostThreadMessageW(threadId, WM_QUIT, 0, 0);

	gThread->join();
	delete gThread;
	gThread = nullptr;
	gTarget.store(nullptr);
}

bool KeyboardHook::isRunning() { return gThread != nullptr && !gFailed.load(); }
bool KeyboardHook::foregroundBlocked() { return gForegroundBlocked.load(); }

void KeyboardHook::setSnapshot(std::shared_ptr<const HookSnapshot> snapshot) {
	gSnapshot.store(std::move(snapshot), std::memory_order_release);
}

void KeyboardHook::noteSuperChord() { gSuperChord.store(true); }

void KeyboardHook::maskAlt() { injectMask(); }

void KeyboardHook::maskSuper() {
	if (!gSuperChord.exchange(true)) injectMask();
}

bool KeyboardHook::sendKey(uint8_t vk) {
	auto scan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
	auto extended = isExtendedKey(vk);

	std::array<INPUT, 2> inputs {
	    keyInput(vk, false, scan, extended),
	    keyInput(vk, true, scan, extended),
	};

	return SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) == inputs.size();
}

bool KeyboardHook::sendChord(uint8_t mods, uint8_t vk, int times) {
	struct Group {
		uint8_t bit;
		uint8_t left;
		uint8_t right;
	};

	static constexpr std::array<Group, 4> groups {{
	    {.bit = ModCtrl, .left = VK_LCONTROL, .right = VK_RCONTROL},
	    {.bit = ModAlt, .left = VK_LMENU, .right = VK_RMENU},
	    {.bit = ModShift, .left = VK_LSHIFT, .right = VK_RSHIFT},
	    {.bit = ModSuper, .left = VK_LWIN, .right = VK_RWIN},
	}};

	std::vector<INPUT> lift;
	std::vector<INPUT> press;
	std::vector<INPUT> restore;
	auto masked = false;

	for (const auto& group: groups) {
		if ((mods & group.bit) != 0) {
			if (!isDown(group.left) && !isDown(group.right)) {
				press.push_back(chordInput(group.left, false));
			}

			continue;
		}

		for (auto key: {group.left, group.right}) {
			if (!isDown(key)) continue;
			lift.push_back(chordInput(key, true));
			restore.push_back(chordInput(key, false));
			masked = masked || group.bit == ModSuper || group.bit == ModAlt;
		}
	}

	std::vector<INPUT> inputs;
	auto mask = [&inputs]() {
		inputs.push_back(keyInput(KeyboardHook::MASK_KEY, false));
		inputs.push_back(keyInput(KeyboardHook::MASK_KEY, true));
	};

	if (masked) mask();
	inputs.insert(inputs.end(), lift.begin(), lift.end());
	inputs.insert(inputs.end(), press.begin(), press.end());

	for (auto i = 0; i < times; i++) {
		inputs.push_back(chordInput(vk, false));
		inputs.push_back(chordInput(vk, true));
	}

	for (auto input = press.rbegin(); input != press.rend(); ++input) {
		input->ki.dwFlags |= KEYEVENTF_KEYUP;
		inputs.push_back(*input);
	}

	inputs.insert(inputs.end(), restore.begin(), restore.end());
	if (masked) mask();

	return SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) == inputs.size();
}

} // namespace qs::windows::hotkeys
