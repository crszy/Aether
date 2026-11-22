#pragma once

#include <functional>
#include <vector>

#include <qt_windows.h>

#include <qobject.h>
#include <qqmlintegration.h>
#include <qrect.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qvariant.h>

namespace qs::windows::sys {

struct DisplayTopology {
	std::vector<DISPLAYCONFIG_PATH_INFO> paths;
	std::vector<DISPLAYCONFIG_MODE_INFO> modes;
};

class Displays: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	// clang-format off
	Q_PROPERTY(QVariantList displays READ displays NOTIFY displaysChanged);
	Q_PROPERTY(QString primaryName READ primaryName NOTIFY displaysChanged);
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged);
	Q_PROPERTY(bool pendingConfirmation READ pendingConfirmation NOTIFY pendingConfirmationChanged);
	Q_PROPERTY(int confirmSecondsLeft READ confirmSecondsLeft NOTIFY confirmSecondsLeftChanged);
	Q_PROPERTY(int confirmTimeout READ confirmTimeout CONSTANT);
	Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged);
	// clang-format on

public:
	explicit Displays(QObject* parent = nullptr);
	~Displays() override;
	Q_DISABLE_COPY_MOVE(Displays);

	[[nodiscard]] QVariantList displays();
	[[nodiscard]] QString primaryName();
	[[nodiscard]] bool busy() const { return this->mBusy; }
	[[nodiscard]] bool pendingConfirmation() const { return this->mPending; }
	[[nodiscard]] int confirmSecondsLeft() const { return this->mSecondsLeft; }
	[[nodiscard]] static int confirmTimeout();
	[[nodiscard]] QString lastError() const { return this->mLastError; }

	Q_INVOKABLE void refresh();

	Q_INVOKABLE bool setMode(const QString& id, int width, int height, int refreshRate);
	Q_INVOKABLE bool setRotation(const QString& id, int rotation);
	Q_INVOKABLE QVariantMap previewPosition(const QString& id, int x, int y);
	Q_INVOKABLE bool setPosition(const QString& id, int x, int y);
	Q_INVOKABLE bool setPrimary(const QString& id);
	Q_INVOKABLE bool setEnabled(const QString& id, bool enabled);

	Q_INVOKABLE bool setHdrEnabled(const QString& id, bool enabled);
	Q_INVOKABLE bool setSdrBrightness(const QString& id, int nits);
	Q_INVOKABLE bool setScale(const QString& id, int percent);

	Q_INVOKABLE void keepChanges();
	Q_INVOKABLE void revertChanges();

signals:
	void displaysChanged();
	void busyChanged();
	void pendingConfirmationChanged();
	void confirmSecondsLeftChanged();
	void lastErrorChanged();
	void changeFailed(const QString& message);

private:
	struct Entry {
		QString id;
		QString sourceKey;
		LUID adapterId {};
		UINT32 targetId = 0;
		UINT32 sourceId = 0;
		bool active = false;
		bool primary = false;
		bool builtIn = false;
		int number = 0;
		QString name;
		QString connector;
		QString gdiName;
		QRect rect;
		int modeWidth = 0;
		int modeHeight = 0;
		int refreshRate = 0;
		double refreshExact = 0;
		int rotation = 0;
		int preferredWidth = 0;
		int preferredHeight = 0;
		QVariantList modes;
		bool hdrSupported = false;
		bool hdrEnabled = false;
		bool hdrLimited = false;
		bool hdrModern = false;
		int sdrWhiteNits = 0;
		bool scaleSupported = false;
		int scale = 0;
		int scaleRecommended = 0;
		QVariantList scales;
	};

	using Job = std::function<QString()>;

	void load(bool notify);
	void scheduleRefresh(int delay);
	[[nodiscard]] const Entry* find(const QString& id) const;
	bool runJob(Job job, bool confirm);
	void onJobFinished(const QString& error, bool confirm, bool tookSnapshot);
	void armConfirmation();
	void clearConfirmation();
	void setBusy(bool busy);
	void fail(const QString& message);
	bool applyLayoutChange(DisplayTopology topology);

	bool mLoaded = false;
	bool mBusy = false;
	bool mPending = false;
	bool mSdrSetFailed = false;
	int mSecondsLeft = 0;
	QString mLastError;
	QString mPrimaryName;
	std::vector<Entry> mEntries;
	QVariantList mDisplays;
	DisplayTopology mPrevious;
	QTimer refreshTimer;
	QTimer countdownTimer;
};

} // namespace qs::windows::sys
