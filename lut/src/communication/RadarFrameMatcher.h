#pragma once

#include "RadarUdpProtocol.h"

#include <QtGlobal>
#include <algorithm>
#include <deque>
#include <limits>
#include <QString>

// All positions in this tracker are diagnostic radar-relative displacement,
// NOT the pipe-head/ LB2 position, because no LB2 event is integrated yet.
class RadarFrameMatcher
{
public:
    struct RadarSample {
        qint64 hostReceiveUs = 0;
        quint32 lengthRaw = 0;
        qint32 speedRaw = 0;
        quint8 goodRaw = 0;
    };
    struct Frame {
        quint64 frameId = 0;
        qint64 daqCallbackUs = 0;
        double bdaqTimestamp = 0.0; // BDAQ GetData(startTime), semantics not trigger-verified
        qint64 daqCallbackUnixUs = 0; // paired PC wall-clock observation
        qint64 callbackClockPairSpreadUs = -1;
        qint64 bdaqEstimatedHostUs = -1;
        qint64 bdaqPeriodUs = -1;
        qint64 bdaqCallbackLagUs = -1;
        QString bdaqModelStatus = QStringLiteral("NO_BDAQ");
    };
    struct Match {
        Frame frame;
        QString quality;    // INTERPOLATED, REVIEW_GOOD, RADAR_GAP, etc.
        double relativeMm = std::numeric_limits<double>::quiet_NaN();
        double rawInterpolated = std::numeric_limits<double>::quiet_NaN();
        qint64 beforeUs = -1;
        qint64 afterUs = -1;
        quint32 beforeLengthRaw = 0;
        quint32 afterLengthRaw = 0;
        quint8 beforeGood = 0;
        quint8 afterGood = 0;
        qint64 bracketSpanUs = -1;
    };

    // Max gap between bracketing radar samples; no unbounded extrapolation.
    static constexpr qint64 MaxBracketUs = 25000;
    static constexpr qint64 FrameTimeoutUs = 100000;
    static constexpr qint64 BufferAgeUs = 30000000;

    // Lost UDP events break the sample sequence. Do not interpolate across
    // the discontinuity or reuse the previous bracket after queue overflow.
    void invalidateRadarSamples() { m_samples.clear(); }

    void clear()
    {
        m_samples.clear();
        m_pending.clear();
        m_hasOrigin = false;
        m_originRaw = 0;
        m_lastRadarUs = -1;
    }

    void addRadarSample(RadarSample sample)
    {
        if (sample.hostReceiveUs <= m_lastRadarUs)
            return;
        if (!m_hasOrigin) {
            m_originRaw = sample.lengthRaw;
            m_hasOrigin = true;
        }
        m_lastRadarUs = sample.hostReceiveUs;
        m_samples.push_back(sample);
        while (m_samples.size() > 2
               && m_samples.front().hostReceiveUs < sample.hostReceiveUs - BufferAgeUs)
            m_samples.pop_front();
    }

    void addFrame(Frame frame) { m_pending.push_back(frame); }
    size_t pendingCount() const { return m_pending.size(); }
    size_t sampleCount() const { return m_samples.size(); }
    bool hasRadarOrigin() const { return m_hasOrigin; }

    // Invalid or incomplete matches are returned with NaN position.
    // A sample with GoodReading=0 is retained for review, never silently trusted.
    Match match(Frame f, qint64 nowUs) const
    {
        Match m;
        m.frame = f;
        if (m_samples.size() < 2) {
            m.quality = (nowUs - f.daqCallbackUs > FrameTimeoutUs)
                ? QStringLiteral("NO_RADAR") : QStringLiteral("WAIT");
            return m;
        }
        const auto it = std::lower_bound(m_samples.begin(), m_samples.end(),
            f.daqCallbackUs, [](const RadarSample &a, qint64 timeUs) {
                return a.hostReceiveUs < timeUs;
            });
        if (it == m_samples.end()) {
            m.quality = (nowUs - f.daqCallbackUs > FrameTimeoutUs)
                ? QStringLiteral("NO_FUTURE_RADAR") : QStringLiteral("WAIT");
            return m;
        }
        if (it == m_samples.begin()) {
            m.quality = QStringLiteral("NO_PAST_RADAR");
            return m;
        }
        const auto &a = *(it - 1);
        const auto &b = *it;
        m.beforeUs = a.hostReceiveUs;
        m.afterUs = b.hostReceiveUs;
        m.beforeLengthRaw = a.lengthRaw;
        m.afterLengthRaw = b.lengthRaw;
        m.beforeGood = a.goodRaw;
        m.afterGood = b.goodRaw;
        m.bracketSpanUs = b.hostReceiveUs - a.hostReceiveUs;
        if (m.bracketSpanUs <= 0 || m.bracketSpanUs > MaxBracketUs) {
            m.quality = QStringLiteral("RADAR_GAP");
            return m;
        }

        const qint64 deltaRaw = qint64(b.lengthRaw) - qint64(a.lengthRaw);
        const double speedEstimateMmS =
            (std::max)(std::abs(double(a.speedRaw)), std::abs(double(b.speedRaw))) / 60.0;
        const double allowedMm = speedEstimateMmS * (double(m.bracketSpanUs) / 1e6) * 3.0 + 10.0;
        if (deltaRaw < 0 || double(deltaRaw) * 0.1 > allowedMm) {
            m.quality = QStringLiteral("LENGTH_DISCONTINUITY");
            return m;
        }

        const double ratio = double(f.daqCallbackUs - a.hostReceiveUs)
            / double(m.bracketSpanUs);
        m.rawInterpolated = double(a.lengthRaw) + ratio * double(deltaRaw);
        m.relativeMm = (m.rawInterpolated - double(m_originRaw)) * 0.1;
        m.quality = (a.goodRaw == 0 || b.goodRaw == 0)
            ? QStringLiteral("REVIEW_GOOD") : QStringLiteral("INTERPOLATED");
        return m;
    }

    // Resolver returns only completed or timed-out frames, ordered by DAQ frame ID.
    template <typename Callback>
    void resolve(qint64 nowUs, Callback output)
    {
        while (!m_pending.empty()) {
            Match m = match(m_pending.front(), nowUs);
            if (m.quality == QStringLiteral("WAIT"))
                break;
            output(m);
            m_pending.pop_front();
        }
    }

private:
    std::deque<RadarSample> m_samples;
    std::deque<Frame> m_pending;
    bool m_hasOrigin = false;
    quint32 m_originRaw = 0;
    qint64 m_lastRadarUs = -1;
};
