#include "PipePresenceStateController.h"
#include "RadarHostClock.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr qint64 kRadarTimeoutUs = 100000;
constexpr qint64 kUiPeriodUs = 50000;
constexpr qint64 kSlowDurationUs = 200000;
constexpr double kSlowSpeedMmS = 10.0;
constexpr double kSlowLengthMm = 0.5;
constexpr double kCandidateLengthMm = 0.1; // experimental, not calibrated
}

PipePresenceStateController::PipePresenceStateController(QObject *parent)
    : QObject(parent)
{
}

void PipePresenceStateController::setRadarToLaserDistanceMm(double mm)
{
    if (!std::isfinite(mm) || mm <= RadarLaserGeometry::RadarToLb2Mm ||
        mm > RadarLaserGeometry::MaximumRadarToLaserMm ||
        std::abs(mm-m_radarToLaserMm) < 1e-9)
        return;
    m_radarToLaserMm = mm;
    if (m_hasLengthOrigin && m_hasPreviousLength &&
        m_previousLengthRaw >= m_lengthOriginRaw) {
        const double traveledMm =
            (double(m_previousLengthRaw)-double(m_lengthOriginRaw))*kCandidateLengthMm;
        m_distanceToLaserMm = (std::max)(0.0, mm-traveledMm);
        m_laserPresent = traveledMm >= mm ? 1 : 0;
        if (m_effectiveOutput) {
            m_phase = m_laserPresent == 1
                ? QStringLiteral("RADAR_LASER")
                : QStringLiteral("RADAR_NOT_LASER");
            if (m_laserPresent == 1)
                m_everEstimatedLaser = true;
        }
    }
    emit stateChanged();
}

QString PipePresenceStateController::phaseLabel() const
{
    if (m_phase == QStringLiteral("RADAR_NOT_LASER"))
        return QStringLiteral("雷达有测速，激光未到（推测）");
    if (m_phase == QStringLiteral("RADAR_LASER"))
        return QStringLiteral("雷达有测速，激光已到（估计）");
    if (m_phase == QStringLiteral("LASER_NOT_RADAR"))
        return QStringLiteral("雷达无测速，激光可能仍有管");
    if (m_phase == QStringLiteral("NOT_ARRIVED"))
        return QStringLiteral("雷达无有效测速（默认未到）");
    return QStringLiteral("雷达测速中，激光位置待确认");
}

QString PipePresenceStateController::lb1Label() const
{
    return m_effectiveOutput ? QStringLiteral("推定工作") : QStringLiteral("默认未工作");
}
QString PipePresenceStateController::lb2Label() const
{
    return m_effectiveOutput ? QStringLiteral("推定工作") : QStringLiteral("默认未工作");
}
QString PipePresenceStateController::radarLabel() const
{
    return m_effectiveOutput ? QStringLiteral("已到雷达（根据测速推定）")
                             : QStringLiteral("未到雷达（无有效测速）");
}
QString PipePresenceStateController::laserLabel() const
{
    if (m_laserPresent == 1) return QStringLiteral("可能覆盖（按雷达位移估计）");
    if (m_laserPresent == 0) return QStringLiteral("可能未到（按雷达起点估计）");
    return QStringLiteral("未知（无激光存在传感器）");
}
QString PipePresenceStateController::motionLabel() const
{
    if (m_motion == QStringLiteral("MOVING")) return QStringLiteral("运动中");
    if (m_motion == QStringLiteral("STATIONARY_CANDIDATE")) return QStringLiteral("疑似静止");
    return QStringLiteral("未知（无测速≠静止）");
}

void PipePresenceStateController::resetLengthOrigin(quint32 raw)
{
    m_lengthOriginRaw = raw;
    m_hasLengthOrigin = true;
    m_distanceToLaserMm = m_radarToLaserMm;
    m_laserPresent = 0;
    m_everEstimatedLaser = false;
    m_stationarySinceUs = -1;
    m_phase = QStringLiteral("RADAR_NOT_LASER");
}

void PipePresenceStateController::setInactive()
{
    if (!m_effectiveOutput)
        return;
    m_effectiveOutput = false;
    m_hasLengthOrigin = false;
    m_stationarySinceUs = -1;
    m_motion = QStringLiteral("UNKNOWN");
    m_speedMmS = std::numeric_limits<double>::quiet_NaN();
    m_distanceToLaserMm = std::numeric_limits<double>::quiet_NaN();
    // Without downstream sensor we cannot tell stopping from the pipe tail.
    m_phase = m_everEstimatedLaser ? QStringLiteral("LASER_NOT_RADAR")
                                   : QStringLiteral("NOT_ARRIVED");
    m_laserPresent = -1;
}

void PipePresenceStateController::enqueueRadarSample(
    const RadarUdpProtocol::Packet &packet, qint64 hostReceiveUs)
{
    // Called synchronously from the UDP thread: NEVER touch QML or GUI state.
    // At ~211 packets/s the GUI's 100ms timer drains ~21 packets per tick.
    std::lock_guard<std::mutex> guard(m_uiRadarMutex);
    if (m_uiRadarQueue.size() == MaxUiRadarQueued) {
        m_uiRadarQueue.pop_front();
        ++m_uiDiscardedPending;
    }
    m_uiRadarQueue.push_back({packet, hostReceiveUs});
}

void PipePresenceStateController::updateRadarSample(
    const RadarUdpProtocol::Packet &packet, qint64 hostReceiveUs)
{
    if (m_lastRadarUs >= 0 && hostReceiveUs <= m_lastRadarUs)
        return;

    const qint64 gapUs = m_lastRadarUs < 0 ? -1 : hostReceiveUs - m_lastRadarUs;
    const quint32 newLength = packet.totalLengthBits;
    const qint32 newInstant = packet.instantSpeedSigned();
    const qint64 delta = m_hasPreviousLength ?
        qint64(newLength) - qint64(m_previousLengthRaw) : 0;

    // A changing historical average, GoodReading or periodic 13-byte UDP
    // heartbeat does NOT establish a fresh, instantaneous speed measurement.
    // Check that a length progression is physically plausible, not a counter reset.
    const double physicalMaxStepMm = 10.0 +
        std::abs(double(newInstant) / 60.0) *
        (std::max)(qint64(0), gapUs) / 1000000.0 * 4.0;
    const bool progressing = m_hasPreviousLength && gapUs > 0 &&
        gapUs <= kRadarTimeoutUs && delta > 0 &&
        delta * kCandidateLengthMm <= physicalMaxStepMm;
    const bool lengthReset = m_hasPreviousLength && delta < 0;
    const bool lengthSpike = m_hasPreviousLength && delta > 0 && !progressing;
    const bool effective = !lengthReset && !lengthSpike &&
        (newInstant != 0 || progressing);

    const bool wasActive = m_effectiveOutput;
    m_lastRadarUs = hostReceiveUs;
    m_radarSeen = true;
    ++m_receivedPackets;
    m_lastGood = packet.goodReadingRaw;
    m_lastSpeedRaw = newInstant;
    m_previousLengthRaw = newLength;
    m_hasPreviousLength = true;

    if (effective) {
        m_effectiveOutput = true; // inferred LB1=1 & LB2=1
        if (!wasActive || !m_hasLengthOrigin) {
            // First radar motion is NOT verified as LB2 head edge. Use
            // full shared radar->laser distance for the arrival proxy.
            resetLengthOrigin(newLength);
        } else {
            const qint64 traveledCounts = qint64(newLength) - qint64(m_lengthOriginRaw);
            if (traveledCounts < 0) {
                m_hasLengthOrigin = false;
                m_distanceToLaserMm = std::numeric_limits<double>::quiet_NaN();
                m_laserPresent = -1;
                m_phase = QStringLiteral("RADAR_UNRESOLVED");
            } else {
                const double traveledMm = traveledCounts * kCandidateLengthMm;
                m_distanceToLaserMm = (std::max)(0.0, m_radarToLaserMm - traveledMm);
                if (traveledMm >= m_radarToLaserMm) {
                    m_laserPresent = 1;
                    m_everEstimatedLaser = true;
                    m_phase = QStringLiteral("RADAR_LASER");
                }
            }
        }
        m_speedMmS = double(newInstant) / 60.0;
        if (std::abs(m_speedMmS) >= kSlowSpeedMmS) {
            m_stationarySinceUs = -1;
            m_motion = QStringLiteral("MOVING");
        } else {
            if (m_stationarySinceUs < 0) {
                m_stationarySinceUs = hostReceiveUs;
                m_stationaryReferenceLength = newLength;
            } else if (std::abs(qint64(newLength) - qint64(m_stationaryReferenceLength)) *
                       kCandidateLengthMm >= kSlowLengthMm) {
                m_stationarySinceUs = hostReceiveUs;
                m_stationaryReferenceLength = newLength;
            }
            m_motion = hostReceiveUs - m_stationarySinceUs >= kSlowDurationUs
                ? QStringLiteral("STATIONARY_CANDIDATE") : QStringLiteral("UNKNOWN");
        }
    } else {
        // No active measurement: infer both photodetectors OFF immediately,
        // even if the device continues transmitting all-zero UDP datagrams.
        setInactive();
    }

    if (m_uiPositionInvalid) {
        // An overflow destroys the continuous LB2/radar distance basis.
        // Speed and packet diagnostics remain visible, but do not silently
        // invent a fresh laser/pipe-head position after a gap.
        m_hasLengthOrigin = false;
        m_distanceToLaserMm = std::numeric_limits<double>::quiet_NaN();
        m_laserPresent = -1;
        m_phase = QStringLiteral("RADAR_UNRESOLVED");
    }
    const bool changed = (wasActive != m_effectiveOutput);
    refresh(hostReceiveUs, changed || m_lastUiUs < 0 ||
            hostReceiveUs - m_lastUiUs >= kUiPeriodUs);
}

void PipePresenceStateController::reportRadarServiceStatus(const QString &message)
{
    if (message.contains(QStringLiteral("失败")) ||
        message.contains(QStringLiteral("UDP bind failed")))
        m_radarServiceError = message;
    else
        m_radarServiceError.clear();
    refresh(RadarHostClock::nowUs(), true);
}

void PipePresenceStateController::tick(qint64 nowHostUs)
{
    // One bounded batch replaces hundreds of unbounded Qt MetaCall events.
    std::deque<QueuedRadar> batch;
    quint64 lost = 0;
    {
        std::lock_guard<std::mutex> guard(m_uiRadarMutex);
        batch.swap(m_uiRadarQueue);
        lost = m_uiDiscardedPending;
        m_uiDiscardedPending = 0;
    }
    if (lost) {
        m_receivedPackets += lost; // account for ALL received valid UDP packets
        m_uiPositionInvalid = true;
        m_hasPreviousLength = false;
        m_hasLengthOrigin = false;
        setInactive();
    }
    for (const auto &sample : batch)
        updateRadarSample(sample.packet, sample.receiveUs);

    if (m_effectiveOutput &&
        (m_lastRadarUs < 0 || nowHostUs - m_lastRadarUs > kRadarTimeoutUs))
        setInactive();
    refresh(nowHostUs, true);
}

void PipePresenceStateController::refresh(qint64 nowUs, bool force)
{
    m_packetAgeMs = m_radarSeen && nowUs >= m_lastRadarUs
        ? int((std::min)(qint64(2147483647), (nowUs - m_lastRadarUs) / 1000))
        : -1;
    const bool fresh = m_radarSeen && m_lastRadarUs >= 0 &&
        nowUs >= m_lastRadarUs && nowUs - m_lastRadarUs <= kRadarTimeoutUs;
    m_radarLink = !m_radarServiceError.isEmpty() ? m_radarServiceError
        : m_uiPositionInvalid ? QStringLiteral("UDP状态更新队列曾溢出，位置估计失效")
        : !m_radarSeen ? QStringLiteral("等待UDP")
        : !fresh ? QStringLiteral("UDP接收超时")
        : m_effectiveOutput ? QStringLiteral("UDP有有效测速")
        : QStringLiteral("UDP有报文，未检测到有效测速");

    if (!m_effectiveOutput || !fresh) {
        m_speedMmS = std::numeric_limits<double>::quiet_NaN();
        m_motion = QStringLiteral("UNKNOWN");
    }

    if (m_uiPositionInvalid)
        m_confidence = QStringLiteral("GUI雷达队列溢出：速度可观察，但管头及激光位置须重标定");
    else if (m_effectiveOutput && m_phase == QStringLiteral("RADAR_NOT_LASER"))
        m_confidence = QStringLiteral("估计：首次雷达测速作为管头候选起点，雷达到激光距离%1mm待标定")
            .arg(m_radarToLaserMm, 0, 'f', 1);
    else if (m_effectiveOutput && m_phase == QStringLiteral("RADAR_LASER"))
        m_confidence = QStringLiteral("估计：雷达相对位移≥%1mm，非独立激光检测")
            .arg(m_radarToLaserMm, 0, 'f', 1);
    else if (m_effectiveOutput)
        m_confidence = QStringLiteral("测速有效，但长度/激光起点异常");
    else if (m_phase == QStringLiteral("LASER_NOT_RADAR"))
        m_confidence = QStringLiteral("雷达无输出：LB1/LB2默认未工作，激光未知");
    else
        m_confidence = QStringLiteral("雷达无有效输出：LB1/LB2默认未工作（推定）");

    if (force) {
        m_lastUiUs = nowUs;
        emit stateChanged();
    }
}
