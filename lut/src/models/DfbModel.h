#pragma once

#include <QObject>
#include <cstdint>
#include "PendingWriteSupport.h"

class DfbModel : public QObject
{
    Q_OBJECT

    // Status properties (read from input registers)
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(uint16_t fault READ fault NOTIFY faultChanged)
    Q_PROPERTY(uint16_t tecStatus READ tecStatus NOTIFY tecStatusChanged)
    Q_PROPERTY(double temperature READ temperature NOTIFY temperatureChanged)
    Q_PROPERTY(double current READ current NOTIFY currentChanged)
    Q_PROPERTY(double pdVoltage READ pdVoltage NOTIFY pdVoltageChanged)
    Q_PROPERTY(uint32_t uptimeSeconds READ uptimeSeconds NOTIFY uptimeSecondsChanged)

    // Control properties (for holding registers)
    Q_PROPERTY(bool dfbSwitch READ dfbSwitch WRITE setDfbSwitch NOTIFY dfbSwitchChanged)
    Q_PROPERTY(bool ldSwitch READ ldSwitch WRITE setLdSwitch NOTIFY ldSwitchChanged)
    Q_PROPERTY(double tempTarget READ tempTarget WRITE setTempTarget NOTIFY tempTargetChanged)
    Q_PROPERTY(int driveMode READ driveMode WRITE setDriveMode NOTIFY driveModeChanged)
    Q_PROPERTY(double currentSet READ currentSet WRITE setCurrentSet NOTIFY currentSetChanged)
    Q_PROPERTY(double currentMax READ currentMax WRITE setCurrentMax NOTIFY currentMaxChanged)

public:
    static constexpr int kControlPendingTimeoutMs = 2500;

    explicit DfbModel(QObject *parent = nullptr);

    // Status getters/setters
    bool online() const;
    void setOnline(bool v);

    uint16_t fault() const;
    void setFault(uint16_t v);

    uint16_t tecStatus() const;
    void setTecStatus(uint16_t v);

    double temperature() const;
    void setTemperatureRaw(uint16_t raw);   // from 0.01 deg C register

    double current() const;
    void setCurrentRaw(uint16_t raw);      // from 0.1 mA register

    double pdVoltage() const;
    void setPdVoltageRaw(uint16_t raw);    // from 0.1 mV register

    uint32_t uptimeSeconds() const;
    void setUptimeSeconds(uint32_t v);

    // Control getters/setters
    bool dfbSwitch() const;
    void setDfbSwitch(bool v);

    bool ldSwitch() const;
    void setLdSwitch(bool v);

    double tempTarget() const;
    void setTempTarget(double v);

    int driveMode() const;
    void setDriveMode(int v);

    double currentSet() const;
    void setCurrentSet(double v);

    double currentMax() const;
    void setCurrentMax(double v);

    // Pending write helpers
    void markPending(const QString &fieldKey, const QVariant &targetValue,
                     int timeoutMs = kControlPendingTimeoutMs);
    void clearPending(const QString &fieldKey);
    bool shouldAcceptPolledValue(const QString &fieldKey, const QVariant &polledValue);
    bool hasPending(const QString &fieldKey) const;

    // Raw register helpers for control values
    int16_t tempTargetRaw() const;        // 0.01 deg C
    uint16_t currentSetRaw() const;       // 0.1 mA
    uint16_t currentMaxRaw() const;       // 0.1 mA

signals:
    void onlineChanged();
    void faultChanged();
    void tecStatusChanged();
    void temperatureChanged();
    void currentChanged();
    void pdVoltageChanged();
    void uptimeSecondsChanged();
    void dfbSwitchChanged();
    void ldSwitchChanged();
    void tempTargetChanged();
    void driveModeChanged();
    void currentSetChanged();
    void currentMaxChanged();

private:
    // Status
    bool m_online = false;
    uint16_t m_fault = 0;
    uint16_t m_tecStatus = 0;
    double m_temperature = 0.0;
    double m_current = 0.0;
    double m_pdVoltage = 0.0;
    uint32_t m_uptimeSeconds = 0;

    // Control
    bool m_dfbSwitch = false;
    bool m_ldSwitch = false;
    double m_tempTarget = 25.0;
    int m_driveMode = 0;
    double m_currentSet = 0.0;
    double m_currentMax = 200.0;
    PendingWriteSupport m_pendingWrites;
};

