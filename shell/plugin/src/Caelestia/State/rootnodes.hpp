#pragma once

#include "settings/rootnode.hpp"
#include "settings/singleton.hpp"
#include "common.hpp"
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_STATE_API


namespace caelestia::shell::state {

class CAE_STATE_API PStateRoot : public settings::RootNode {
    STATE_NODE_NO_CTOR(PStateRoot, settings::RootNode)
    QML_ANONYMOUS

    STATE_GLOBAL_PROPERTY(bool, wallpaperPlaying, true)
    STATE_GLOBAL_PROPERTY(bool, doNotDisturb, false)

public:
    explicit PStateRoot(const QString& path, PStateRoot* fallback = nullptr, QObject* parent = nullptr);
};

class CAE_STATE_API RStateRoot : public settings::RootNode {
    STATE_NODE_NO_CTOR(RStateRoot, settings::RootNode)
    QML_ANONYMOUS

    STATE_GLOBAL_PROPERTY(bool, gameMode, false)
    STATE_GLOBAL_PROPERTY(bool, inhibitIdle, false)
    STATE_GLOBAL_PROPERTY(qreal, inhibitingIdleSince, -1)

public:
    explicit RStateRoot(const QString& path, RStateRoot* fallback = nullptr, QObject* parent = nullptr);
};

SETTINGS_SINGLETON_DECL(PersistentState, PStateRoot, PersistentState)
SETTINGS_SINGLETON_DECL(RuntimeState, RStateRoot, RuntimeState)

} // namespace caelestia::shell::state
