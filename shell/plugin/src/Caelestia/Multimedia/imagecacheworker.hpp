#pragma once

#include <qimage.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qsize.h>
#include <qsqldatabase.h>
#include <qstring.h>
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_MULTIMEDIA_API


namespace caelestia::shell::multimedia {

class CAE_MULTIMEDIA_API ImageCacheWorker : public QObject {
    Q_OBJECT

public:
    explicit ImageCacheWorker(QString dir, QObject* parent = nullptr);
    ~ImageCacheWorker() override;

    void open();
    void touch(const QString& key);
    void storeFile(const QString& key, const QString& path, const QSize& bucket, Qt::AspectRatioMode mode);
    void storeImage(const QString& key, const QImage& image, const QSize& bucket, Qt::AspectRatioMode mode);

private:
    void store(const QString& key, const QImage& image);
    void reconcile();
    void evict();
    void remove(const QString& key);
    [[nodiscard]] QString pathFor(const QString& key) const;

    QString m_dir;
    QString m_connection;
    QSqlDatabase m_db;
};

} // namespace caelestia::shell::multimedia
