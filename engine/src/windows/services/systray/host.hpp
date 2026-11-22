#pragma once

#include <vector>

#include <qt_windows.h>

#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <quuid.h>

#include "hook.hpp"
#include "item.hpp"

namespace qs::windows::services::systray {

class TrayHost: public QObject {
	Q_OBJECT;

public:
	static TrayHost* instance();

	[[nodiscard]] const QList<SystemTrayItem*>& items() const { return this->mItems; }

signals:
	void itemAdded(SystemTrayItem* item);
	void itemRemoved(SystemTrayItem* item);

private:
	explicit TrayHost();

	void apply(const TrayIconMessage& message);
	void add(const TrayIconMessage& message);
	void remove(SystemTrayItem* item);
	void pruneDead();
	void startSnapshot();
	void scheduleSnapshot(int delayMs);
	void onSnapshotDone(bool ok);
	void recoverCallbacks();
	void onToolbarData(const std::vector<ExplorerIconData>& icons);
	void requestTitle(const QString& exePath);
	void readTitles();
	void onTitles(const QHash<QString, QString>& found);
	[[nodiscard]] SystemTrayItem* find(HWND hwnd, UINT uid, const QUuid& guid) const;
	[[nodiscard]] QString uniqueId(const QString& base, UINT uid) const;
	[[nodiscard]] bool deletedRecently(const TrayIconMessage& message);

	struct Deletion {
		HWND hwnd;
		UINT uid;
		QUuid guid;
		ULONGLONG at;
	};

	QList<SystemTrayItem*> mItems;
	QList<Deletion> deletions;
	QTimer pruneTimer;

	TrayIconSink sink;
	QTimer snapshotTimer;
	bool snapshotRunning = false;
	bool snapshotAgain = false;
	ULONGLONG snapshotStartedAt = 0;
	ULONGLONG snapshotDoneAt = 0;
	int snapshotReported = 0;

	QTimer pollTimer;
	QTimer recoverTimer;
	bool toolbarReading = false;
	bool recoverAgain = false;

	QHash<QString, QString> titles;
	QList<QString> titleQueue;
	bool titlesReading = false;
};

} // namespace qs::windows::services::systray
