#pragma once

#include <QtGlobal>
#include <QString>
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

// BDAQ WaveformAiCtrl::GetData(startTime): its period can be tested, but its
// physical relation to the 100 Hz hardware trigger has NOT been established.
// A lower-envelope host mapping removes callback jitter up to an unknown
// constant lag. This is NOT a calibrated radar/laser trigger timestamp.
class BdaqHostTimeAligner
{
public:
    struct Observation {
        qint64 estimatedHostUs = -1;
        QString status = QStringLiteral("NO_BDAQ");
        qint64 periodUs = -1;
        qint64 callbackLagUs = -1;
        int stableIntervals = 0;
        bool usable() const {
            return status == QStringLiteral("RELATIVE_ONLY") ||
                   status == QStringLiteral("EPOCH_BRIDGED_UNCALIBRATED");
        }
    };

    void clear()
    {
        m_samples = 0;
        m_stableIntervals = 0;
        m_originBdaq = m_previousBdaq = 0.0;
        m_originCallbackUs = 0;
        m_recentResiduals.clear();
    }

    Observation observe(double bdaqStartTime, qint64 hostCallbackUs,
                        qint64 hostCallbackUnixUs = 0, qint64 clockPairSpreadUs = -1)
    {
        Observation result;
        if (!std::isfinite(bdaqStartTime) || hostCallbackUs <= 0) {
            result.status = QStringLiteral("INVALID_BDAQ");
            return result;
        }
        if (m_samples == 0) {
            resetOrigin(bdaqStartTime, hostCallbackUs);
            result.status = QStringLiteral("WARMUP");
            return result;
        }
        // DAQNavi's startTime is handled as seconds only if its observed
        // increments agree with the known 100 Hz trigger period.
        const double periodUs = (bdaqStartTime - m_previousBdaq) * 1000000.0;
        if (!std::isfinite(periodUs) || periodUs <= 0.0 ||
            periodUs < 8500.0 || periodUs > 11500.0) {
            resetOrigin(bdaqStartTime, hostCallbackUs);
            result.status = QStringLiteral("PERIOD_RESET");
            return result;
        }
        m_previousBdaq = bdaqStartTime;
        ++m_samples;
        ++m_stableIntervals;
        result.periodUs = qint64(std::llround(periodUs));
        result.stableIntervals = m_stableIntervals;

        const double deltaUs = (bdaqStartTime - m_originBdaq) * 1000000.0;
        if (!std::isfinite(deltaUs) || std::abs(deltaUs) > 9000000000000.0) {
            clear();
            result.status = QStringLiteral("INVALID_BDAQ");
            return result;
        }
        const qint64 hardwareDeltaUs = qint64(std::llround(deltaUs));
        const qint64 nominalHostUs = m_originCallbackUs + hardwareDeltaUs;
        const qint64 residualUs = hostCallbackUs - nominalHostUs;
        m_recentResiduals.push_back(residualUs);
        if (m_recentResiduals.size() > 256)
            m_recentResiduals.pop_front();
        // Lowest callback latency in a 256-frame window; unknown trigger
        // transport/DAQ/readout delay is still included.
        const qint64 minimumResidual = *std::min_element(m_recentResiduals.begin(),
                                                         m_recentResiduals.end());
        const qint64 estimated = nominalHostUs + minimumResidual;
        result.callbackLagUs = hostCallbackUs - estimated;
        if (m_stableIntervals < 20) {
            result.status = QStringLiteral("WARMUP");
        } else if (result.callbackLagUs > 25000 ||
                   result.callbackLagUs < 0) {
            result.status = QStringLiteral("CALLBACK_OUTLIER");
        } else {
            result.status = QStringLiteral("RELATIVE_ONLY");
            result.estimatedHostUs = estimated;
        }
        // Real historical BDAQ data resemble Unix UTC seconds. Use that
        // interpretation ONLY when independently sampled PC wall time is
        // consistent with the value and its 10 ms period is already stable.
        // This still cannot verify timestamp-to-TTL trigger latency.
        if (m_stableIntervals >= 20 && hostCallbackUnixUs > 0 &&
            clockPairSpreadUs >= 0 && clockPairSpreadUs <= 1000) {
            const double bdaqUnixUs = bdaqStartTime * 1000000.0;
            if (std::isfinite(bdaqUnixUs) &&
                std::abs(bdaqUnixUs - double(hostCallbackUnixUs)) < 500000.0) {
                const qint64 epochHostUs = hostCallbackUs +
                    qint64(std::llround(bdaqUnixUs - double(hostCallbackUnixUs)));
                const qint64 epochLagUs = hostCallbackUs - epochHostUs;
                if (epochLagUs >= -2000 && epochLagUs <= 100000) {
                    result.estimatedHostUs = epochHostUs;
                    result.callbackLagUs = epochLagUs;
                    result.status = QStringLiteral("EPOCH_BRIDGED_UNCALIBRATED");
                }
            }
        }
        return result;
    }

private:
    void resetOrigin(double timestamp, qint64 hostUs)
    {
        m_originBdaq = m_previousBdaq = timestamp;
        m_originCallbackUs = hostUs;
        m_samples = 1;
        m_stableIntervals = 0;
        m_recentResiduals.clear();
        m_recentResiduals.push_back(0);
    }
    int m_samples = 0;
    int m_stableIntervals = 0;
    double m_originBdaq = 0.0, m_previousBdaq = 0.0;
    qint64 m_originCallbackUs = 0;
    std::deque<qint64> m_recentResiduals;
};
