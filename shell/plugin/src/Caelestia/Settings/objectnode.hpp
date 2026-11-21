#pragma once

#include <qjsonobject.h>
#include <qobject.h>
#include <qset.h>

#include "node.hpp"
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_SETTINGS_API


namespace caelestia::shell::settings {

class CAE_SETTINGS_API ObjectNode : public Node {
    Q_OBJECT

public:
    explicit ObjectNode(ObjectNode* fallback, QObject* parent = nullptr, bool globalOnly = false);

    Q_INVOKABLE void resetOption(const QString& key);
    [[nodiscard]] Q_INVOKABLE Descriptor descriptorFor(const QString& key) const;
    // quiet check for walkers (for-in over a node also yields signals/methods): no warning per unknown key
    [[nodiscard]] Q_INVOKABLE bool hasOption(const QString& key) const;

    [[nodiscard]] QJsonValue toJson(bool sparse = true) const override;
    bool syncJson(const QJsonValue& json, QList<Diagnostic>& diagnostics) override;

private:
    void quarantineKey(const QString& key, const QJsonValue& value);

    QSet<QString> loadFromJson(const QJsonObject& json, QList<Diagnostic>& diagnostics);
    void resetUnvisited(const QSet<QString>& visited);
};

} // namespace caelestia::shell::settings
