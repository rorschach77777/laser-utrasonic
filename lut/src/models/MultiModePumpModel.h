#pragma once

#include <QObject>
#include <cstdint>
#include "PendingWriteSupport.h"

class MultiModePumpModel : public QObject
{
    Q_OBJECT

    // Status properties (read from input registers)
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(uint16_t fault READ fault NOTIFY faultChanged)
    Q_PROPERTY(uint16_t currentActual READ currentActual NOTIFY currentActualChanged)
    Q_PROPERTY(uint32_t uptimeSeconds READ uptimeSeconds NOTIFY uptimeSecondsChanged)

    // Control properties (for holding registers)
    Q_PROPERTY(bool pumpSwitch READ pumpSwitch WRITE setPumpSwitch NOTIFY pumpSwitchChanged)
    Q_PROPERTY(uint16_t driveMode READ driveMode WRITE setDriveMode NOTIFY driveModeChanged)
    Q_PROPERTY(uint16_t currentSet READ currentSet WRITE setCurrentSet NOTIFY currentSetChanged)

public:
    static constexpr int kControlPendingTimeoutMs = 2500;

    explicit MultiModePumpModel(QObject *parent = nullptr);

    // Status
    bool online() const;
    void setOnline(bool v);

    uint16_t fault() const;
    void setFault(uint16_t v);

    uint16_t currentActual() const;
    void setCurrentActual(uint16_t v);

    uint32_t uptimeSeconds() const;
    void setUptimeSeconds(uint32_t v);

    // Control
    bool pumpSwitch() const;
    void setPumpSwitch(bool v);

    uint16_t driveMode() const;
    void setDriveMode(uint16_t v);

    uint16_t currentSet() const;
    void setCurrentSet(uint16_t v);

    // Pending write helpers
    void markPending(const QString &fieldKey, const QVariant &targetValue,
                     int timeoutMs = kControlPendingTimeoutMs);
    void clearPending(const QString &fieldKey);
    bool shouldAcceptPolledValue(const QString &fieldKey, const QVariant &polledValue);
    bool hasPending(const QString &fieldKey) const;

signals:
    void onlineChanged();
    void faultChanged();
    void currentActualChanged();
    void uptimeSecondsChanged();
    void pumpSwitchChanged();
    void driveModeChanged();
    void currentSetChanged();

private:
    // Status
    bool m_online = false;
    uint16_t m_fault = 0;
    uint16_t m_currentActual = 0;
    uint32_t m_uptimeSeconds = 0;

    // Control
    bool m_pumpSwitch = false;
    uint16_t m_driveMode = 0;
    uint16_t m_currentSet = 0;
    PendingWriteSupport m_pendingWrites;
};

