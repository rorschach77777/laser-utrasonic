#pragma once

#include <QObject>
#include <cstdint>
#include "PendingWriteSupport.h"

class PumpModel : public QObject
{
    Q_OBJECT

    // Status properties (read from input registers)
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(bool enableRb READ enableRb NOTIFY enableRbChanged)
    Q_PROPERTY(uint16_t statusCode READ statusCode NOTIFY statusCodeChanged)
    Q_PROPERTY(uint16_t addrRb READ addrRb NOTIFY addrRbChanged)
    Q_PROPERTY(uint16_t currentActual READ currentActual NOTIFY currentActualChanged)
    Q_PROPERTY(double voltageActual READ voltageActual NOTIFY voltageActualChanged)
    Q_PROPERTY(uint16_t freqActual READ freqActual NOTIFY freqActualChanged)
    Q_PROPERTY(uint16_t currentRb READ currentRb NOTIFY currentRbChanged)
    Q_PROPERTY(uint16_t pulseWidthRb READ pulseWidthRb NOTIFY pulseWidthRbChanged)
    Q_PROPERTY(uint16_t voltageLimitRb READ voltageLimitRb NOTIFY voltageLimitRbChanged)
    Q_PROPERTY(uint16_t intFreqRb READ intFreqRb NOTIFY intFreqRbChanged)
    Q_PROPERTY(uint16_t extDelayRb READ extDelayRb NOTIFY extDelayRbChanged)
    Q_PROPERTY(uint16_t delayOutRb READ delayOutRb NOTIFY delayOutRbChanged)

    // Control properties (for holding registers)
    Q_PROPERTY(bool enable READ enable WRITE setEnable NOTIFY enableChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(uint16_t current READ current WRITE setCurrent NOTIFY currentChanged)
    Q_PROPERTY(uint16_t pulseWidth READ pulseWidth WRITE setPulseWidth NOTIFY pulseWidthChanged)
    Q_PROPERTY(uint16_t voltageLimit READ voltageLimit WRITE setVoltageLimit NOTIFY voltageLimitChanged)
    Q_PROPERTY(uint16_t intFreq READ intFreq WRITE setIntFreq NOTIFY intFreqChanged)
    Q_PROPERTY(uint16_t extDelay READ extDelay WRITE setExtDelay NOTIFY extDelayChanged)
    Q_PROPERTY(uint16_t delayOut READ delayOut WRITE setDelayOut NOTIFY delayOutChanged)
    Q_PROPERTY(uint16_t addr READ addr WRITE setAddr NOTIFY addrChanged)

public:
    static constexpr int kControlPendingTimeoutMs = 2500;

    explicit PumpModel(QObject *parent = nullptr);

    // Status getters/setters
    bool online() const;
    void setOnline(bool v);

    bool enableRb() const;
    void setEnableRb(bool v);

    uint16_t statusCode() const;
    void setStatusCode(uint16_t v);

    uint16_t addrRb() const;
    void setAddrRb(uint16_t v);

    uint16_t currentActual() const;
    void setCurrentActual(uint16_t v);

    double voltageActual() const;
    void setVoltageActualRaw(uint16_t v);

    uint16_t freqActual() const;
    void setFreqActual(uint16_t v);

    uint16_t currentRb() const;
    void setCurrentRb(uint16_t v);

    uint16_t pulseWidthRb() const;
    void setPulseWidthRb(uint16_t v);

    uint16_t voltageLimitRb() const;
    void setVoltageLimitRb(uint16_t v);

    uint16_t intFreqRb() const;
    void setIntFreqRb(uint16_t v);

    uint16_t extDelayRb() const;
    void setExtDelayRb(uint16_t v);

    uint16_t delayOutRb() const;
    void setDelayOutRb(uint16_t v);

    // Control getters/setters
    bool enable() const;
    void setEnable(bool v);

    int mode() const;
    void setMode(int v);

    uint16_t current() const;
    void setCurrent(uint16_t v);

    uint16_t pulseWidth() const;
    void setPulseWidth(uint16_t v);

    uint16_t voltageLimit() const;
    void setVoltageLimit(uint16_t v);

    uint16_t intFreq() const;
    void setIntFreq(uint16_t v);

    uint16_t extDelay() const;
    void setExtDelay(uint16_t v);

    uint16_t delayOut() const;
    void setDelayOut(uint16_t v);

    uint16_t addr() const;
    void setAddr(uint16_t v);

    // Pending write helpers
    void markPending(const QString &fieldKey, const QVariant &targetValue,
                     int timeoutMs = kControlPendingTimeoutMs);
    void clearPending(const QString &fieldKey);
    bool shouldAcceptPolledValue(const QString &fieldKey, const QVariant &polledValue);
    bool hasPending(const QString &fieldKey) const;

signals:
    // Status signals
    void onlineChanged();
    void enableRbChanged();
    void statusCodeChanged();
    void addrRbChanged();
    void currentActualChanged();
    void voltageActualChanged();
    void freqActualChanged();
    void currentRbChanged();
    void pulseWidthRbChanged();
    void voltageLimitRbChanged();
    void intFreqRbChanged();
    void extDelayRbChanged();
    void delayOutRbChanged();

    // Control signals
    void enableChanged();
    void modeChanged();
    void currentChanged();
    void pulseWidthChanged();
    void voltageLimitChanged();
    void intFreqChanged();
    void extDelayChanged();
    void delayOutChanged();
    void addrChanged();

private:
    // Status
    bool m_online = false;
    bool m_enableRb = false;
    uint16_t m_statusCode = 0;
    uint16_t m_addrRb = 0;
    uint16_t m_currentActual = 0;
    double m_voltageActual = 0.0;
    uint16_t m_freqActual = 0;
    uint16_t m_currentRb = 0;
    uint16_t m_pulseWidthRb = 0;
    uint16_t m_voltageLimitRb = 0;
    uint16_t m_intFreqRb = 0;
    uint16_t m_extDelayRb = 0;
    uint16_t m_delayOutRb = 0;

    // Control
    bool m_enable = false;
    int m_mode = 0;
    uint16_t m_current = 0;
    uint16_t m_pulseWidth = 100;
    uint16_t m_voltageLimit = 30;
    uint16_t m_intFreq = 10;
    uint16_t m_extDelay = 0;
    uint16_t m_delayOut = 100;
    uint16_t m_addr = 1;
    PendingWriteSupport m_pendingWrites;
};
