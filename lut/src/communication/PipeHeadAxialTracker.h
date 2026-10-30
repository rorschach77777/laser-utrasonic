#pragma once

#include "RadarFrameMatcher.h"
#include "RadarLaserGeometry.h"
#include <algorithm>
#include <cmath>
#include <deque>

// The default radar-to-laser spacing is 510mm but the installation distance
// D is configured per DAQ session. First radar motion is NOT verified as an
// LB2 pipe-head edge. The conservative head gate is displacement >= D.
// An unverified radar motion edge is the FIRST effective radar sample
// following >=300ms of continuous, inactive UDP observations in this DAQ run.
// Without independent LB2 input this is a PROXY, not a calibrated true zero.
// The proxy is never re-armed after the first movement (a mid-pipe stop
// otherwise looks exactly like another head). Inactive/unknown intervals do
// not inherit the previous coordinate. Only forward-moving samples are tagged.
class PipeHeadAxialTracker
{
public:
    static constexpr double RadarToLb2Mm = RadarLaserGeometry::RadarToLb2Mm;
    static constexpr double DefaultRadarToLaserMm =
        RadarLaserGeometry::DefaultRadarToLaserMm;
    // Session geometry configured by DataPage.qml; do not substitute this
    // physical separation for raw TotalLength calibration or ultrasonic TOF.
    void setRadarToLaserDistanceMm(double mm) {
        if (std::isfinite(mm) && mm > RadarToLb2Mm &&
            mm <= RadarLaserGeometry::MaximumRadarToLaserMm)
            m_radarToLaserMm = mm;
    }
    double radarToLaserDistanceMm() const { return m_radarToLaserMm; }
    double lb2ToLaserMm() const { return m_radarToLaserMm - RadarToLb2Mm; }
    static constexpr double LengthMmPerCount = 0.1; // NOT independently calibrated

    // Only when the counter is anchored to the pipe head AT the radar beam
    // may the measured displacement be directly reduced by the configured D.
    static double fromRadarHeadTravelMm(double traveledMm, double distanceMm = DefaultRadarToLaserMm)
    {
        return traveledMm - distanceMm;
    }

    // Retained for geometry comparisons only. This is unsafe as the live
    // origin unless an independent LB2 edge has actually been timestamped.
    static double fromLb2ProxyTravelMm(double traveledAfterLb2Mm,
                                       double distanceMm = DefaultRadarToLaserMm)
    {
        return fromRadarHeadTravelMm(traveledAfterLb2Mm + RadarToLb2Mm,
                                     distanceMm);
    }

    struct Tag {
        qint64 receiveUs = 0;
        quint32 lengthRaw = 0;
        bool active = false;
        bool headProxy = false;
        quint32 originRaw = 0;
    };
    struct Position {
        double laserHeadMm = std::numeric_limits<double>::quiet_NaN();
        QString quality = QStringLiteral("HEAD_UNAVAILABLE");
        bool valid() const { return std::isfinite(laserHeadMm); }
    };

    void invalidateContinuity()
    {
        m_tracking = false;
        m_seenMovement = true;
        m_armed = false;
    }

    void clear()
    {
        m_tags.clear();
        m_previousUs = -1;
        m_previousLength = 0;
        m_quietStartUs = -1;
        m_armed = false;
        m_seenMovement = false;
        m_tracking = false;
        m_originRaw = 0;
    }

    bool hasActiveLb2Origin() const { return m_tracking && m_seenMovement; }
    bool hasReachedLaserGate() const
    {
        // Latest actual radar sample must have traveled configured D AFTER the first
        // detected motion. A stable thickness signal alone cannot assert
        // physical pipe presence.
        return m_tracking && m_seenMovement && !m_tags.empty()
            && m_tags.back().active
            && m_tags.back().lengthRaw >= m_originRaw
            && (double(m_tags.back().lengthRaw) - double(m_originRaw))
               * LengthMmPerCount >= m_radarToLaserMm;
    }

    QString state() const
    {
        if (m_tracking) return QStringLiteral("已捕获管头候选零点（未标定）");
        if (m_seenMovement) return QStringLiteral("位置无效：采集中途已有运动或雷达中断；需重新开始新管采集");
        if (m_armed) return QStringLiteral("已观察300ms静默，等待首次雷达有效测速");
        return QStringLiteral("等待采集开始后的300ms无有效测速");
    }

    void add(const RadarFrameMatcher::RadarSample &s)
    {
        if (s.hostReceiveUs <= m_previousUs) return;
        const qint64 intervalUs = m_previousUs < 0 ? -1 : s.hostReceiveUs - m_previousUs;
        const qint64 delta = m_previousUs < 0 ? 0
            : qint64(s.lengthRaw) - qint64(m_previousLength);
        // Guard against counter discontinuities; scaling is provisional.
        const bool continuity = intervalUs > 0 && intervalUs <= 100000 &&
            delta >= 0 && double(delta) * 0.1 <=
            10.0 + std::abs(double(s.speedRaw) / 60.0) *
                double(intervalUs) / 1e6 * 4.0;
        const bool moving = s.speedRaw > 0 || (continuity && delta > 0);
        const bool active = continuity && moving;

        if (m_previousUs >= 0 && (!continuity || s.speedRaw < 0)) {
            m_tracking = false;
            m_seenMovement = true; // do NOT create an origin after discontinuity
            m_armed = false;
        }
        if (!m_seenMovement) {
            if (!active) {
                if (m_quietStartUs < 0) m_quietStartUs = s.hostReceiveUs;
                m_armed = s.hostReceiveUs - m_quietStartUs >= 300000;
            } else {
                m_seenMovement = true;
                if (m_armed) {
                    m_originRaw = s.lengthRaw;
                    m_tracking = true;
                }
                m_armed = false;
            }
        } else if (!active && m_tracking) {
            // Suppress false coordinates when output stops. Do not re-arm.
            m_tracking = false;
        }

        Tag tag;
        tag.receiveUs = s.hostReceiveUs;
        tag.lengthRaw = s.lengthRaw;
        tag.active = active && m_tracking;
        tag.headProxy = m_tracking;
        tag.originRaw = m_originRaw;
        m_tags.push_back(tag);
        while (m_tags.size() > 2 &&
               m_tags.front().receiveUs < s.hostReceiveUs - 30000000)
            m_tags.pop_front();
        m_previousUs = s.hostReceiveUs;
        m_previousLength = s.lengthRaw;
    }

    Position at(const RadarFrameMatcher::Match &match) const
    {
        Position p;
        if (!std::isfinite(match.rawInterpolated)) {
            p.quality = QStringLiteral("RADAR_MATCH_INVALID");
            return p;
        }
        const auto it = std::lower_bound(m_tags.begin(), m_tags.end(),
            match.beforeUs, [](const Tag &tag, qint64 t) { return tag.receiveUs < t; });
        if (it == m_tags.end() || it->receiveUs != match.beforeUs ||
            (it + 1) == m_tags.end() ||
            (it + 1)->receiveUs != match.afterUs) {
            p.quality = QStringLiteral("RADAR_TAG_MISSING");
            return p;
        }
        const Tag &a = *it, &b = *(it + 1);
        if (!a.headProxy || !b.headProxy) {
            p.quality = QStringLiteral("HEAD_ORIGIN_UNCONFIRMED");
            return p;
        }
        if (!a.active || !b.active || a.originRaw != b.originRaw) {
            p.quality = QStringLiteral("MOTION_NOT_CONFIRMED");
            return p;
        }
        if (match.quality != QStringLiteral("INTERPOLATED") &&
            match.quality != QStringLiteral("REVIEW_GOOD")) {
            p.quality = QStringLiteral("RADAR_MATCH_INVALID");
            return p;
        }
        // First reliable radar motion may begin AT the radar rather than
        // LB2. Requiring the full configured radar-to-laser D suppresses x>=0
        // BEFORE the head reaches the laser. Uncalibrated conservative proxy.
        const double travelSinceFirstRadarMotionMm =
            (match.rawInterpolated - double(a.originRaw)) * LengthMmPerCount;
        const double xMm = fromRadarHeadTravelMm(travelSinceFirstRadarMotionMm, m_radarToLaserMm);
        if (xMm < 0) {
            p.quality = QStringLiteral("HEAD_BEFORE_LASER");
            return p;
        }
        p.laserHeadMm = xMm;
        p.quality = match.quality == QStringLiteral("INTERPOLATED")
            ? QStringLiteral("RADAR_MOTION_DISTANCE_UNCALIBRATED")
            : QStringLiteral("RADAR_MOTION_DISTANCE_REVIEW_GOOD");
        return p;
    }

private:
    double m_radarToLaserMm = DefaultRadarToLaserMm;
    std::deque<Tag> m_tags;
    qint64 m_previousUs = -1;
    qint64 m_quietStartUs = -1;
    quint32 m_previousLength = 0, m_originRaw = 0;
    bool m_armed = false, m_seenMovement = false, m_tracking = false;
};
