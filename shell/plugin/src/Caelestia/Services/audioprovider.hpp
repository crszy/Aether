#pragma once

#include <qqmlintegration.h>
#include <qtimer.h>

#include "service.hpp"
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_SERVICES_API


namespace caelestia::shell::services {

class CAE_SERVICES_API AudioProcessor : public QObject {
    Q_OBJECT

public:
    explicit AudioProcessor(QObject* parent = nullptr);
    ~AudioProcessor() override;

    void init();

public slots:
    void start();
    void stop();

protected:
    virtual void process() = 0;

private:
    QTimer* m_timer = nullptr;
};

class CAE_SERVICES_API AudioProvider : public Service {
    Q_OBJECT

public:
    explicit AudioProvider(QObject* parent = nullptr);
    ~AudioProvider() override;

protected:
    AudioProcessor* m_processor;

    void init();

private:
    QThread* m_thread;

    void start() override;
    void stop() override;
};

} // namespace caelestia::shell::services
