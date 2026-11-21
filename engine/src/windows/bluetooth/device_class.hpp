#pragma once

#include <qstring.h>
#include <qtypes.h>

namespace qs::bluetooth {

QString iconForClassOfDevice(quint32 cod);

QString iconForAppearance(quint16 appearance);

} // namespace qs::bluetooth
