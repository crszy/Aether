#pragma once

#include <qobject.h>
#include <qset.h>
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_SERVICES_API


namespace caelestia::shell::services {

class CAE_SERVICES_API Service : public QObject {
    Q_OBJECT

public:
    explicit Service(QObject* parent = nullptr);

    void ref(QObject* sender);
    void unref(QObject* sender);

private:
    QSet<QObject*> m_refs;

    virtual void start() = 0;
    virtual void stop() = 0;
};

} // namespace caelestia::shell::services
