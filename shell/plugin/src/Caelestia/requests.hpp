#pragma once

#include <qnetworkaccessmanager.h>
#include <qobject.h>
#include <qqmlengine.h>
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_CORE_API


namespace caelestia::shell {

class CAE_CORE_API Requests : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Requests(QObject* parent = nullptr);

    Q_INVOKABLE void get(const QUrl& url, const QJSValue& onSuccess, const QJSValue& onError = QJSValue(),
        const QJSValue& headers = QJSValue()) const;
    Q_INVOKABLE void resetCookies() const;

private:
    QNetworkAccessManager* m_manager;
};

} // namespace caelestia::shell
