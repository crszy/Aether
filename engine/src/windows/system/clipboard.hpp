#pragma once

#include <functional>
#include <vector>

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtmetamacros.h>
#include <qtypes.h>

class QImage;

namespace qs::windows::sys {

struct ClipboardEntry {
	qint64 id = 0;
	bool isImage = false;
	QString text;
	QString textPath;
	qsizetype textLength = 0;
	size_t textHash = 0;
	QString preview;
	QString imagePath;
	int width = 0;
	int height = 0;
};

struct ClipboardCapture {
	quint32 sequence = 0;
	bool captured = false;
	ClipboardEntry entry;
};

class Clipboard: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	Q_PROPERTY(QStringList entries READ default NOTIFY entriesChanged BINDABLE bindableEntries);

public:
	explicit Clipboard(QObject* parent = nullptr);
	~Clipboard() override;
	Q_DISABLE_COPY_MOVE(Clipboard);

	[[nodiscard]] QBindable<QStringList> bindableEntries() const { return &this->bEntries; }

	Q_INVOKABLE void copy(qint64 id);
	Q_INVOKABLE void deleteEntry(qint64 id);
	Q_INVOKABLE void wipe();

	Q_INVOKABLE QString imagePath(qint64 id) const;

	Q_INVOKABLE bool copyImageFile(const QString& path);

	Q_INVOKABLE void copyText(const QString& text);

signals:
	void entriesChanged();

private:
	void onClipboardUpdate();
	void startCapture();
	void finishCapture(const ClipboardCapture& capture);
	void rebuildEntriesProperty();
	void trimHistory();
	void captureText(ClipboardEntry entry);
	void captureImage(const ClipboardEntry& entry);
	void noteOwnWrite();
	QString cacheDir();

	void writeData(quint32 format, void* data, std::function<void(Clipboard*)> done, int attempt = 1);

	std::vector<ClipboardEntry> storage;
	QString mCacheDir;
	qint64 nextId = 1;
	qint64 suppressNextCaptureFor = -1;
	bool capturing = false;
	bool captureAgain = false;
	quint32 capturedSequence = 0;
	quint32 writtenSequence = 0;
	quint32 suppressedSequence = 0;

	Q_OBJECT_BINDABLE_PROPERTY(Clipboard, QStringList, bEntries, &Clipboard::entriesChanged);
};

} // namespace qs::windows::sys
