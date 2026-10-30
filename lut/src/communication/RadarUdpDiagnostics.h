#pragma once

#include "RadarUdpProtocol.h"

#include <QtGlobal>

// Diagnostics only. A repeated packet can be a perfectly valid stationary
// measurement; a large host receive gap is NOT proof of a UDP packet loss.
// Since the 13-byte protocol contains no sequence or radar sample timestamp,
// neither precise loss count nor true radar sampling rate can be inferred here.
class RadarUdpDiagnostics
{
public:
    struct Counters {
        quint64 packets = 0;
        quint64 unchangedValues = 0;
        quint64 instantSpeedChanged = 0;
        quint64 averageSpeedChanged = 0;
        quint64 totalLengthChanged = 0;
        quint64 goodReadingChanged = 0;
        quint64 zeroInstantSpeed = 0;
        quint64 zeroAverageSpeed = 0;
        quint64 zeroGoodReading = 0;
        quint64 gapsOverWarning = 0;
        quint64 gapsOver100ms = 0;
        quint64 gapsUnder2ms = 0;
        qint64 maxGapUs = -1;
        RadarUdpProtocol::Packet last;
    };

    explicit RadarUdpDiagnostics(qint64 gapWarningUs = 25000)
        : m_gapWarningUs(gapWarningUs)
    {}

    void observe(const RadarUdpProtocol::Packet &current, qint64 gapUs)
    {
        if (m_counters.packets > 0) {
            const auto &last = m_counters.last;
            if (current.instantSpeedBits == last.instantSpeedBits
                && current.averagedSpeedBits == last.averagedSpeedBits
                && current.totalLengthBits == last.totalLengthBits
                && current.goodReadingRaw == last.goodReadingRaw)
                ++m_counters.unchangedValues;
            if (current.instantSpeedBits != last.instantSpeedBits)
                ++m_counters.instantSpeedChanged;
            if (current.averagedSpeedBits != last.averagedSpeedBits)
                ++m_counters.averageSpeedChanged;
            if (current.totalLengthBits != last.totalLengthBits)
                ++m_counters.totalLengthChanged;
            if (current.goodReadingRaw != last.goodReadingRaw)
                ++m_counters.goodReadingChanged;
        }

        ++m_counters.packets;
        if (current.instantSpeedBits == 0)
            ++m_counters.zeroInstantSpeed;
        if (current.averagedSpeedBits == 0)
            ++m_counters.zeroAverageSpeed;
        if (current.goodReadingRaw == 0)
            ++m_counters.zeroGoodReading;
        if (gapUs >= 0) {
            if (gapUs < 2000)
                ++m_counters.gapsUnder2ms;
            if (gapUs > m_gapWarningUs)
                ++m_counters.gapsOverWarning;
            if (gapUs > 100000)
                ++m_counters.gapsOver100ms;
            if (gapUs > m_counters.maxGapUs)
                m_counters.maxGapUs = gapUs;
        }
        m_counters.last = current;
    }

    bool shouldWarnAboutGap(qint64 gapUs) const {
        return gapUs > m_gapWarningUs;
    }
    const Counters &counters() const { return m_counters; }

private:
    qint64 m_gapWarningUs;
    Counters m_counters;
};
