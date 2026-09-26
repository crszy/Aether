// Aether - the macro engine.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ==================================== MACRO ENGINE (from Z:\MacroMaker) =========================
// The Python/CustomTkinter macro maker rebuilt natively. Same schema as ProfileStudio: a macro has a
// trigger key, a MODE, and a list of steps; the engine registers triggers with the shell's existing
// low-level keyboard hook rather than a second global hook of its own.
//
//   press  - one pass per press
//   hold   - repeats every interval_ms while the trigger is held
//   toggle - press starts it, press again stops it
//
// F8 is the master arm/disarm, exactly as the original. DISARMED IS THE DEFAULT and nothing is
// registered until the user arms it: a shell that silently synthesises input would be a menace.
//
// Input goes through SendInput with KEYEVENTF_SCANCODE, which is what the `keyboard` module did and
// what games actually read. This is plain user-space SendInput - no driver, no injection into
// another process, nothing that hides itself.
enum MacroMode { MM_PRESS=0, MM_HOLD, MM_TOGGLE };
enum MacroStepKind {
    MS_KEY_DOWN=0, MS_KEY_UP, MS_KEY_TAP,
    MS_MOUSE_DOWN, MS_MOUSE_UP, MS_MOUSE_CLICK,
    MS_WHEEL, MS_SLEEP, MS_WAIT_RELEASE
};
static const char* MSTEP_NAME[9]={
    "Key down","Key up","Key tap","Mouse down","Mouse up","Mouse click","Wheel","Sleep","Wait for release" };
struct MacroStep{ int kind=MS_KEY_TAP; int code=0; int arg=0; };   // code = vk or button, arg = ms / wheel delta
struct Macro{
    std::string name, desc;
    int  trigger=0;              // virtual-key code
    int  mode=MM_PRESS;
    bool suppress=false;         // swallow the trigger key so the game never sees it
    int  interval=50;            // ms between passes in hold/toggle
    bool enabled=false;
    std::vector<MacroStep> steps;
};
static std::vector<Macro> g_macros;
static bool  g_macroArmed=false;                 // F8 master switch, NEVER persisted as on
static std::atomic<int> g_macroRunning{-1};      // index currently executing, -1 = none
static std::atomic<bool> g_macroStop{false};
static std::vector<char> g_macroHeld;            // per-macro: is its trigger down right now
static std::vector<char> g_macroToggled;         // per-macro: toggle state
static std::string g_macroLog;                   // last thing the engine did, shown in Settings
// --macrotest only. The hook ignores INJECTED keys so a macro that types its own trigger cannot
// retrigger itself forever; that also means no script can drive a trigger, so the engine is
// untestable without this door. It is never set outside the flag.
static bool g_macroAllowInjected=false;
static std::atomic<int> g_macroTrigSeen{0};   // --macrotest counters
static std::atomic<int> g_macroSteps{0};
static std::atomic<int> g_macroSent{0};       // SendInput calls Windows ACCEPTED (return value 1)

static void MacroLoadPresets();      // fwd - the body needs helpers defined further down
static void MacroStopAll();          // fwd
static std::string MacroKeyName(int vk);   // fwd

// Caelestia's own vertical-bar layout, from the upstream sources (copies in reference\):
//   BarWrapper.qml   contentWidth = Tokens.sizes.bar.innerWidth + padding*2,
//                    padding      = max(Tokens.padding.small, Config.border.thickness) = max(8,10)
//   tokens.hpp       innerWidth 40      borderconfig.hpp  thickness 10   ->  a 60px strip
//   barconfig.hpp    entries = logo, workspaces, spacer, activeWindow, spacer, tray, clock,
//                              statusIcons, power ; statusIcons defaults network+bluetooth+battery
static void SaveConfig();
