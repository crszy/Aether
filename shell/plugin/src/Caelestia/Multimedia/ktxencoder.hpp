#pragma once

#include <qbytearray.h>
#include <qimage.h>
#include <qnamespace.h>
#include <qrect.h>
#include <qsize.h>
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_MULTIMEDIA_API


namespace caelestia::shell::multimedia::ktx {

struct Fit {
    QSize scaled;
    QRect crop;
};

[[nodiscard]] Fit fitFor(const QSize& native, const QSize& bucket, Qt::AspectRatioMode mode);
[[nodiscard]] QImage fitted(const QImage& image, const QSize& bucket, Qt::AspectRatioMode mode);
[[nodiscard]] QImage readFitted(const QString& path, const QSize& bucket, Qt::AspectRatioMode mode);
[[nodiscard]] QByteArray encodeBc3(const QImage& source);

} // namespace caelestia::shell::multimedia::ktx
