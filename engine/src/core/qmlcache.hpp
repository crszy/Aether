#pragma once

#include <qbytearray.h>
#include <qdir.h>
#include <qhash.h>
#include <qstring.h>

namespace qs::qmlcache {

void preload(const QDir& configRoot);

void activate(
    const QDir& configRoot,
    const QHash<QString, QByteArray>& fileHashes,
    const QHash<QString, QString>& fileIntercepts
);

} // namespace qs::qmlcache
