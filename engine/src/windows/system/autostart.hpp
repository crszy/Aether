#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtmetamacros.h>
#include <qvariant.h>

namespace qs::windows::sys {

class Autostart: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged);
	Q_PROPERTY(bool selfStartupEnabled READ selfStartupEnabled NOTIFY selfStartupChanged);
	Q_PROPERTY(QString selfStartupMethod READ selfStartupMethod NOTIFY selfStartupChanged);
	Q_PROPERTY(bool selfStartupFellBack READ selfStartupFellBack NOTIFY selfStartupChanged);
	Q_PROPERTY(QString selfStartupError READ selfStartupError NOTIFY selfStartupChanged);

public:
	explicit Autostart(QObject* parent = nullptr): QObject(parent) { this->refresh(); }

	[[nodiscard]] QVariantList entries() const { return this->mEntries; }
	[[nodiscard]] bool selfStartupEnabled() const;
	[[nodiscard]] QString selfStartupMethod() const;
	[[nodiscard]] bool selfStartupFellBack() const { return this->mSelfFellBack; }
	[[nodiscard]] QString selfStartupError() const { return this->mSelfError; }

	Q_INVOKABLE void refresh();
	Q_INVOKABLE void setEnabled(const QString& id, bool enabled);
	Q_INVOKABLE bool
	addFromPath(const QString& filePath, const QString& name, const QStringList& arguments);
	Q_INVOKABLE bool removeEntry(const QString& id);
	Q_INVOKABLE bool setSelfStartup(bool enabled, const QString& method);

signals:
	void entriesChanged();
	void selfStartupChanged();

private:
	void loadSelf() const;

	QVariantList mEntries;
	mutable bool mSelfLoaded = false;
	mutable bool mSelfEnabled = false;
	mutable QString mSelfMethod;
	bool mSelfFellBack = false;
	QString mSelfError;
};

} // namespace qs::windows::sys
