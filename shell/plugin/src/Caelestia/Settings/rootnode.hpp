#pragma once

#include "common.hpp"
#include "objectnode.hpp"
#include "settingsfile.hpp"
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_SETTINGS_API


namespace caelestia::shell::settings {

class CAE_SETTINGS_API RootNode : public ObjectNode {
    Q_OBJECT

    Q_PROPERTY(QList<caelestia::shell::settings::Diagnostic> diagnostics READ diagnostics NOTIFY diagnosticsChanged)

public:
    explicit RootNode(bool watch, const QString& path, RootNode* fallback, QObject* parent = nullptr);

    [[nodiscard]] QList<Diagnostic> diagnostics() const;

    void load();

signals:
    void diagnosticsChanged();
    void treeLoaded(caelestia::shell::settings::RootNode* self);
    void treeLoadFailed(caelestia::shell::settings::RootNode* self, const QString& error);
    void treeSaveFailed(caelestia::shell::settings::RootNode* self, const QString& error);

private:
    SettingsFile* const m_file;
    QList<Diagnostic> m_diagnostics;

    void reloadFromFile();
    void saveToFile();
};

} // namespace caelestia::shell::settings
