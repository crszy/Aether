#pragma once

#include <qt_windows.h>

#include <qcontainerfwd.h>
#include <qfilesystemwatcher.h>
#include <qlist.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlengine.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

#include "keyboard_hook.hpp"

namespace qs::windows::hotkeys {

[[nodiscard]] bool parseKeys(const QString& text, KeyCombo& combo, QString& error);
[[nodiscard]] uint8_t keyNameToVk(const QString& name);

struct Bind {
	enum class Action : quint8 {
		Global,
		Dispatch,
		Exec,
		Ipc,
		Key,
		Reload,
		Native,
	};

	enum class Mechanism : quint8 {
		None,
		Hotkey,
		Hook,
		Native,
		Unavailable,
	};

	QString keys;
	KeyCombo combo;
	Action action = Action::Native;
	QString appid;
	QString name;
	QString argument;
	QString function;
	QStringList args;
	uint8_t sendVk = 0;
	bool onRelease = false;
	bool repeat = false;
	QString description;
	Mechanism mechanism = Mechanism::None;
	QString submap;   // Hyprland submap this bind belongs to ("" = the normal binds)
};

class HotkeyManager: public QObject {
	Q_OBJECT;

public:
	~HotkeyManager() override;
	Q_DISABLE_COPY_MOVE(HotkeyManager);

	static HotkeyManager* instance();

	void setShellDir(const QString& shellDir);
	void reload();

	void registerShortcut(QObject* shortcut);
	void unregisterShortcut(QObject* shortcut);

	void triggerGlobal(const QString& name);

	// Hyprland submaps: only the binds of the active submap work ("" = the normal binds)
	void setSubmap(const QString& name);
	[[nodiscard]] QString submap() const { return this->mSubmap; }

	[[nodiscard]] QString configPath() const { return this->mConfigPath; }
	[[nodiscard]] QVariantList bindsInfo() const;
	[[nodiscard]] bool hookActive() const;

signals:
	void bindsChanged();

private:
	explicit HotkeyManager(QObject* parent);

	enum class Phase : quint8 {
		Press,
		Repeat,
		Release,
		Tap,
	};

	struct Trigger {
		HookTrigger::Kind kind = HookTrigger::Combo;
		KeyCombo combo;
		Bind::Mechanism mechanism = Bind::Mechanism::None;
		QList<qsizetype> binds;
		bool repeat = false;
		bool down = false;
	};

	void loadFile();
	void parse(const QByteArray& data, const QString& path);
	void unregisterAll();
	void registerAll();
	qsizetype addToTrigger(qsizetype index);
	void resetTrigger(qsizetype index);
	void registerTriggers();
	void relayout();
	void mergeDefaults();
	void updateWatches();
	void scheduleReload();

	void onHotkey(int id);
	void onHookEvent(HookEvent event, quint32 triggerId, quint32 serial);
	void pollReleases();
	void fire(qsizetype trigger, Phase phase);
	void fireBind(const Bind& bind, Phase phase);
	void emitGlobal(const QString& appid, const QString& name, bool pressed);
	void releaseAllGlobals();

	void runDispatch(const Bind& bind);
	void runExec(const Bind& bind) const;
	void runIpc(const Bind& bind);
	void runReload();

	[[nodiscard]] QString userFilePath() const;
	[[nodiscard]] QString defaultFilePath() const;
	[[nodiscard]] QString readUserFile() const;

	static LRESULT CALLBACK messageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	HWND messageWindow = nullptr;
	QString shellDir;
	QString userFile;
	QString mConfigPath;
	QByteArray loadedData;
	bool loaded = false;
	bool parsed = false;

	QList<Bind> binds;
	QList<Trigger> triggers;
	quint32 serial = 0;
	bool hookRunning = false;

	QList<QPointer<QObject>> shortcuts;
	QList<QPair<QString, QString>> pressedGlobals;

	QFileSystemWatcher watcher;
	QTimer reloadTimer;
	QTimer releaseTimer;
	HKL lastLayout = nullptr;
	QString mSubmap;
};

class Hotkeys: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString configPath READ configPath NOTIFY bindsChanged);
	Q_PROPERTY(QVariantList binds READ binds NOTIFY bindsChanged);
	Q_PROPERTY(bool hookActive READ hookActive NOTIFY bindsChanged);
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Hotkeys(QObject* parent);

	static Hotkeys* create(QQmlEngine* engine, QJSEngine* jsEngine);

	[[nodiscard]] QString configPath() const;
	[[nodiscard]] QVariantList binds() const;
	[[nodiscard]] bool hookActive() const;

	Q_INVOKABLE void registerShortcut(QObject* shortcut);
	Q_INVOKABLE void unregisterShortcut(QObject* shortcut);
	Q_INVOKABLE void reload();

signals:
	void bindsChanged();
};

} // namespace qs::windows::hotkeys
