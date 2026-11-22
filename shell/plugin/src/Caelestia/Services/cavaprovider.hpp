#pragma once

#include <qqmlintegration.h>

#include <vector>   // Windows build: own FFT instead of libcava

#include "audioprovider.hpp"
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_SERVICES_API


namespace caelestia::shell::services {

class CAE_SERVICES_API CavaProcessor : public AudioProcessor {
    Q_OBJECT

public:
    explicit CavaProcessor(QObject* parent = nullptr);
    ~CavaProcessor() override;

    void setBars(int bars);

signals:
    void valuesChanged(QVector<double> values);

protected:
    void process() override;

private:
    void* m_plan;   // non-null while bars > 0 (kept so the upstream logic reads the same)
    std::vector<double> m_smooth;
    double* m_in;
    double* m_out;

    int m_bars;
    QVector<double> m_values;

    void reload();
    void initCava();
    void cleanup();
};

class CAE_SERVICES_API CavaProvider : public AudioProvider {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int bars READ bars WRITE setBars NOTIFY barsChanged)

    Q_PROPERTY(QVector<double> values READ values NOTIFY valuesChanged)

public:
    explicit CavaProvider(QObject* parent = nullptr);

    [[nodiscard]] int bars() const;
    void setBars(int bars);

    [[nodiscard]] QVector<double> values() const;

signals:
    void barsChanged();
    void valuesChanged();

private:
    int m_bars;
    QVector<double> m_values;

    void updateValues(const QVector<double>& values);
};

} // namespace caelestia::shell::services
