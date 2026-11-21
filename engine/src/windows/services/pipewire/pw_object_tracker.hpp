#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>

namespace qs::windows::services::pipewire {

class PwObjectTracker: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QList<QObject*> objects READ objects WRITE setObjects NOTIFY objectsChanged);
	QML_ELEMENT;

public:
	explicit PwObjectTracker(QObject* parent = nullptr): QObject(parent) {}
	~PwObjectTracker() override;
	Q_DISABLE_COPY_MOVE(PwObjectTracker);

	[[nodiscard]] QList<QObject*> objects() const { return this->mObjects; }
	void setObjects(const QList<QObject*>& objects);

signals:
	void objectsChanged();

private slots:
	void onObjectDestroyed(QObject* object);

private:
	QList<QObject*> mObjects;
};

} // namespace qs::windows::services::pipewire
