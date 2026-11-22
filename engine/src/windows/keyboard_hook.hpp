#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <windows.h>

namespace qs::windows::hotkeys {

enum Modifier : uint8_t {
	ModCtrl = 1 << 0,
	ModAlt = 1 << 1,
	ModShift = 1 << 2,
	ModSuper = 1 << 3,
};

struct KeyCombo {
	uint8_t mods = 0;
	uint8_t vk = 0;
	uint8_t lone = 0;

	[[nodiscard]] bool operator==(const KeyCombo& other) const = default;
};

struct HookTrigger {
	enum Kind : uint8_t {
		Combo,
		Tap,
		Hold,
	};

	Kind kind = Combo;
	KeyCombo combo;
	uint32_t id = 0;
};

struct HookSnapshot {
	std::vector<HookTrigger> triggers;
	uint32_t serial = 0;
	uint32_t tapTimeoutMs = 1000;
};

enum HookEvent : UINT {
	HookPressed = 0,
	HookRepeated = 1,
	HookReleased = 2,
	HookTapped = 3,
	HookEventCount = 4,
};

class KeyboardHook {
public:
	static bool start(HWND target, UINT messageBase);
	static void stop();
	[[nodiscard]] static bool isRunning();
	[[nodiscard]] static bool foregroundBlocked();

	static void setSnapshot(std::shared_ptr<const HookSnapshot> snapshot);

	static bool sendKey(uint8_t vk);
	static bool sendChord(uint8_t mods, uint8_t vk, int times = 1);
	static void noteSuperChord();
	static void maskSuper();
	static void maskAlt();   // sends the mask key so the coming Alt release does not open a menu bar

	static constexpr ULONG_PTR INJECTION_MARKER = 0x5153484B;
	static constexpr uint8_t MASK_KEY = 0xE8;
};

[[nodiscard]] uint8_t modifierBit(DWORD vk);

} // namespace qs::windows::hotkeys
