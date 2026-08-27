#pragma once

#include <QObject>
#include <cstdint>
#include "PendingWriteSupport.h"

class WaterCoolerModel : public QObject
{
    Q_OBJECT

    // Status properties (read from input registers)
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(double temperatureC READ temperatureC NOTIFY temperatureCChanged)

    // Control properties (for holding registers)
    Q_PROPERTY(double tempSetC READ tempSetC WRITE setTempSetC NOTIFY tempSetCChanged)

public:
    static constexpr int kControlPendingTimeoutMs = 2500;

    explicit WaterCoolerModel(QObject *parent = nullptr);

    // Status getters/setters
    bool online() const;
    void setOnline(bool v);

    double temperatureC() const;
    void setTemperatureRaw(int16_t raw);   // from 0.1 deg C register

    // Control getters/setters
    double tempSetC() const;
    void setTempSetC(double v);
    void setTempSetRaw(int16_t raw);       // from 0.1 deg C register

    // Raw register helper for control value
    int16_t tempSetRaw() const;            // 0.1 deg C

    // Pending write helpers
    void markPending(const QString &fieldKey, const QVariant &targetValue,
                     int timeoutMs = kControlPendingTimeoutMs);
    void clearPending(const QString &fieldKey);
    bool shouldAcceptPolledValue(const QString &fieldKey, const QVariant &polledValue);
    bool hasPending(const QString &fieldKey) const;

signals:
    void onlineChanged();
    void temperatureCChanged();
    void tempSetCChanged();

private:
    bool m_online = false;
    double m_temperatureC = 0.0;
    double m_tempSetC = 0.0;
    PendingWriteSupport m_pendingWrites;
};
