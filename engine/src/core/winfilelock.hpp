#pragma once

#include <qfile.h>

namespace qs::core::winlock {

bool lockExclusive(QFile& file);

bool isLocked(QFile& file);

bool waitUnlocked(QFile& file);

} // namespace qs::core::winlock
