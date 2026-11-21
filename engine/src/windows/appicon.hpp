#pragma once

#include <functional>

#include <qimage.h>
#include <qpixmap.h>
#include <qsize.h>
#include <qstring.h>

namespace qs::windows {

[[nodiscard]] QPixmap iconForKey(const QString& key, const QSize& size);

[[nodiscard]] bool isShellIconKey(const QString& key);
// the shell's icon for a parsing name (a file path, or shell:AppsFolder\<app id>); safe off the GUI thread
[[nodiscard]] QImage shellImage(const QString& parsingName, const QSize& size);

[[nodiscard]] bool hasShellIcon(const QString& key);

void requestShellIcon(
    const QString& key,
    const QSize& size,
    std::function<bool()> cancelled,
    std::function<void(QImage)> done
);

} // namespace qs::windows
