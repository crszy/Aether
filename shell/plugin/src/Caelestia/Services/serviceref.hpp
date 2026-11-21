#pragma once

#include <qpointer.h>
#include <qqmlintegration.h>

#include "service.hpp"
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_SERVICES_API


namespace caelestia::shell::services {

class CAE_SERVICES_API ServiceRef : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(caelestia::shell::services::Service* service READ service WRITE setService NOTIFY serviceChanged)

public:
    explicit ServiceRef(Service* service = nullptr, QObject* parent = nullptr);

    [[nodiscard]] Service* service() const;
    void setService(Service* service);

signals:
    void serviceChanged();

private:
    QPointer<Service> m_service;
};

} // namespace caelestia::shell::services
