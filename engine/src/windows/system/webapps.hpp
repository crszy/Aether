#pragma once

#include <memory>

#include <qnetworkaccessmanager.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtmetamacros.h>
#include <qvariant.h>

namespace qs::windows::sys {

struct WebAppCreateJob;

class WebApps: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged);
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged);
	Q_PROPERTY(QString error READ error NOTIFY errorChanged);

public:
	explicit WebApps(QObject* parent = nullptr);

	[[nodiscard]] QVariantList items() const { return this->mItems; }
	[[nodiscard]] bool busy() const { return this->mPending > 0; }
	[[nodiscard]] QString error() const { return this->mError; }

	Q_INVOKABLE void refresh();
	Q_INVOKABLE void create(const QStringList& urls);
	Q_INVOKABLE void remove(const QString& id);
	Q_INVOKABLE void open(const QString& id);

signals:
	void itemsChanged();
	void busyChanged();
	void errorChanged();

private:
	void startJob(const QString& rawUrl);
	void fetchPage(const std::shared_ptr<WebAppCreateJob>& job);
	void fetchManifest(const std::shared_ptr<WebAppCreateJob>& job, const QString& manifestUrl);
	void startIconFetch(const std::shared_ptr<WebAppCreateJob>& job);
	void tryNextPageIcon(const std::shared_ptr<WebAppCreateJob>& job);
	void tryNextFallbackIcon(const std::shared_ptr<WebAppCreateJob>& job);
	void finalizeJob(const std::shared_ptr<WebAppCreateJob>& job, const QByteArray& iconBytes);
	void jobFinished(const QString& rawUrl, bool ok, const QString& message);

	QNetworkAccessManager mNam;
	QVariantList mItems;
	QString mError;
	int mPending = 0;
	QStringList mFailures;
};

} // namespace qs::windows::sys
