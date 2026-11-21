#pragma once

#include <memory>

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qvariant.h>

namespace qs::windows::sys {

struct FileIndexData;

class FileIndex: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	Q_PROPERTY(bool indexing READ indexing NOTIFY indexingChanged);
	Q_PROPERTY(int count READ count NOTIFY countChanged);

public:
	explicit FileIndex(QObject* parent = nullptr);
	~FileIndex() override;
	Q_DISABLE_COPY_MOVE(FileIndex);

	[[nodiscard]] bool indexing() const { return this->mIndexing; }
	[[nodiscard]] int count() const { return this->mCount; }

	Q_INVOKABLE void ensureIndexed();
	Q_INVOKABLE QVariantList search(const QString& query, int limit) const;

signals:
	void indexingChanged();
	void countChanged();
	void indexChanged();

private:
	void startBuild();
	void syncFromGlobal();

	bool mIndexing = false;
	int mCount = 0;
	std::shared_ptr<const FileIndexData> data;
};

} // namespace qs::windows::sys
