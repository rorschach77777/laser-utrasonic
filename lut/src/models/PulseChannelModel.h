#pragma once

#include <QObject>
#include <QVector>
#include <cstdint>
#include "PendingWriteSupport.h"

// --------------------------------------------------------------------------
// PulseChannelModel -- single channel with period, pulseCount, 8 delays/widths
// --------------------------------------------------------------------------
class PulseChannelModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(uint16_t period READ period WRITE setPeriod NOTIFY periodChanged)
    Q_PROPERTY(uint16_t pulseCount READ pulseCount WRITE setPulseCount NOTIFY pulseCountChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    static constexpr int kTimingPendingTimeoutMs = 6500;

    explicit PulseChannelModel(QObject *parent = nullptr);

    uint16_t period() const;
    void setPeriod(uint16_t v);

    uint16_t pulseCount() const;
    void setPulseCount(uint16_t v);

    int revision() const { return m_revision; }

    Q_INVOKABLE uint16_t getDelay(int index) const;
    Q_INVOKABLE uint16_t getWidth(int index) const;
    Q_INVOKABLE void setDelay(int index, uint16_t v);
    Q_INVOKABLE void setWidth(int index, uint16_t v);

    // Serialise the channel into 18 consecutive register values
    QVector<uint16_t> toRegisters() const;

    // Deserialise 18 consecutive register values into this channel,
    // honoring per-field pending writes
    void fromRegisters(const QVector<uint16_t> &regs);

    // Pending write helpers
    void markPending(const QString &fieldKey, const QVariant &targetValue,
                     int timeoutMs = kTimingPendingTimeoutMs);
    void clearPending(const QString &fieldKey);
    bool shouldAcceptPolledValue(const QString &fieldKey, const QVariant &polledValue);
    bool hasPending(const QString &fieldKey) const;

    // Stable field keys for pending tracking
    static QString keyForPeriod();
    static QString keyForPulseCount();
    static QString keyForDelay(int index);
    static QString keyForWidth(int index);

signals:
    void periodChanged();
    void pulseCountChanged();
    void timingChanged();          // emitted when any delay/width changes
    void revisionChanged();

private:
    static constexpr int kMaxPulses = 8;

    uint16_t m_period = 1000;
    uint16_t m_pulseCount = 1;
    uint16_t m_delays[kMaxPulses] = {};
    uint16_t m_widths[kMaxPulses] = {100, 100, 100, 100, 100, 100, 100, 100};
    int m_revision = 0;
    PendingWriteSupport m_pendingWrites;
};

// --------------------------------------------------------------------------
// PulseTimingModel -- container holding 16 PulseChannelModel* channels
// --------------------------------------------------------------------------
class PulseTimingModel : public QObject
{
    Q_OBJECT

public:
    explicit PulseTimingModel(QObject *parent = nullptr);

    Q_INVOKABLE PulseChannelModel *channel(int index) const;
    Q_INVOKABLE QString channelName(int index) const;

    static constexpr int kChannelCount = 16;

private:
    PulseChannelModel *m_channels[kChannelCount];
};
