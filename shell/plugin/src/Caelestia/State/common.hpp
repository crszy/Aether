#pragma once

#include <qloggingcategory.h>
#include <qstring.h>

#include "settings/macros.hpp"
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_STATE_API


#define STATE_NODE_NO_CTOR SETTINGS_NODE_NO_CTOR
#define STATE_GLOBAL_PROPERTY SETTINGS_GLOBAL_PROPERTY

namespace caelestia::shell::state {

Q_DECLARE_LOGGING_CATEGORY(lcState)

QString stateDir();
QString monitorStateDir();

QString runtimeDir();
QString monitorRuntimeDir();

} // namespace caelestia::shell::state
