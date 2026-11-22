#pragma once

#include <qobject.h>
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_SETTINGS_API


namespace caelestia::shell::settings {

class CAE_SETTINGS_API ChangeBatcher : public QObject {
    Q_OBJECT

public:
    explicit ChangeBatcher(QObject* parent = nullptr);

    void dirty();

signals:
    void dirtied();

private:
    bool m_dirty;

    void flush();
};

} // namespace caelestia::shell::settings
