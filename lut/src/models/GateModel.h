#pragma once

#include <QObject>
#include <cstdint>
#include "PendingWriteSupport.h"

class GateModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(uint16_t gateLevel READ gateLevel NOTIFY gateLevelChanged)
    Q_PROPERTY(uint16_t gateState READ gateState NOTIFY gateStateChanged)
    Q_PROPERTY(bool gateEnable READ gateEnable WRITE setGateEnable NOTIFY gateEnableChanged)
    Q_PROPERTY(uint16_t riseDelayUs READ riseDelayUs WRITE setRiseDelayUs NOTIFY riseDelayUsChanged)
    Q_PROPERTY(uint16_t fallDelayUs READ fallDelayUs WRITE setFallDelayUs NOTIFY fallDelayUsChanged)
    Q_PROPERTY(uint16_t polarity READ polarity WRITE setPolarity NOTIFY polarityChanged)

public:
    static constexpr int kControlPendingTimeoutMs = 2500;

    explicit GateModel(QObject *parent = nullptr);

    uint16_t gateLevel() const;
    void setGateLevel(uint16_t v);

    uint16_t gateState() const;
    void setGateState(uint16_t v);

    bool gateEnable() const;
    void setGateEnable(bool v);

    uint16_t riseDelayUs() const;
    void setRiseDelayUs(uint16_t v);

    uint16_t fallDelayUs() const;
    void setFallDelayUs(uint16_t v);

    uint16_t polarity() const;
    void setPolarity(uint16_t v);

    void markPending(const QString &fieldKey, const QVariant &targetValue,
                     int timeoutMs = kControlPendingTimeoutMs);
    void clearPending(const QString &fieldKey);
    bool shouldAcceptPolledValue(const QString &fieldKey, const QVariant &polledValue);
    bool hasPending(const QString &fieldKey) const;

signals:
    void gateLevelChanged();
    void gateStateChanged();
    void gateEnableChanged();
    void riseDelayUsChanged();
    void fallDelayUsChanged();
    void polarityChanged();

private:
    uint16_t m_gateLevel = 0;
    uint16_t m_gateState = 0;
    bool m_gateEnable = false;
    uint16_t m_riseDelayUs = 0;
    uint16_t m_fallDelayUs = 0;
    uint16_t m_polarity = 1;
    PendingWriteSupport m_pendingWrites;
};
