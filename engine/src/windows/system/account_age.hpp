#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>

namespace qs::windows::sys {

class AccountAge: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	// clang-format off
	Q_PROPERTY(bool ready READ ready NOTIFY changed);
	Q_PROPERTY(QString accountKind READ accountKind NOTIFY changed);
	Q_PROPERTY(QString ageGroup READ ageGroup NOTIFY changed);
	Q_PROPERTY(bool spicyAllowed READ spicyAllowed NOTIFY changed);
	Q_PROPERTY(QString spicyRestriction READ spicyRestriction NOTIFY changed);
	Q_PROPERTY(bool canConfirmAge READ canConfirmAge NOTIFY changed);
	// clang-format on

public:
	explicit AccountAge(QObject* parent = nullptr);

	[[nodiscard]] bool ready() const { return this->mReady; }
	[[nodiscard]] QString accountKind() const { return this->mKind; }
	[[nodiscard]] QString ageGroup() const { return this->mAge; }
	[[nodiscard]] bool spicyAllowed() const;
	[[nodiscard]] QString spicyRestriction() const;
	[[nodiscard]] bool canConfirmAge() const;

	Q_INVOKABLE void refresh();

signals:
	void changed();

private:
	void start();
	void apply(const QString& kind, const QString& age);

	bool mReady = false;
	bool mRunning = false;
	QString mKind = QStringLiteral("unknown");
	QString mAge = QStringLiteral("unknown");
};

} // namespace qs::windows::sys
