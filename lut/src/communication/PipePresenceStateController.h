#pragma once

#include "RadarLaserGeometry.h"

#include "RadarUdpProtocol.h"
#include "LstDataExport.h"

#include <QObject>
#include <QString>
#include <QtGlobal>
#include <limits>
#include <deque>
#include <mutex>

// Inference-only presence controller for installations without LB1/LB2 input.
// "Radar effective output" := fresh nonzero instant velocity OR progressing
// TotalLength. Periodic 13-byte zero-speed/frozen-length UDP is NOT a measurement.
// LB1/LB2 are OPERATIONAL PROXIES, not independently measured photodetector values.
class LST_DATA_API PipePresenceStateController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool radarPresent READ radarPresent NOTIFY stateChanged)
    Q_PROPERTY(bool radarPresenceKnown READ radarPresenceKnown NOTIFY stateChanged)
    Q_PROPERTY(QString pipePhase READ pipePhase NOTIFY stateChanged)
    Q_PROPERTY(QString phaseLabel READ phaseLabel NOTIFY stateChanged)
    Q_PROPERTY(QString motionState READ motionState NOTIFY stateChanged)
    // -1 = unknown; 0/1 = estimated; NOT an independent laser sensor.
    Q_PROPERTY(int laserPresent READ laserPresent NOTIFY stateChanged)
    Q_PROPERTY(double distanceToLaserMm READ distanceToLaserMm NOTIFY stateChanged)
    Q_PROPERTY(QString confidenceLabel READ confidenceLabel NOTIFY stateChanged)
    Q_PROPERTY(QString lb1Label READ lb1Label NOTIFY stateChanged)
    Q_PROPERTY(QString lb2Label READ lb2Label NOTIFY stateChanged)
    Q_PROPERTY(QString radarLabel READ radarLabel NOTIFY stateChanged)
    Q_PROPERTY(QString laserLabel READ laserLabel NOTIFY stateChanged)
    Q_PROPERTY(QString motionLabel READ motionLabel NOTIFY stateChanged)
    Q_PROPERTY(QString radarLinkLabel READ radarLinkLabel NOTIFY stateChanged)
    Q_PROPERTY(double speedMmS READ speedMmS NOTIFY stateChanged)
    Q_PROPERTY(qulonglong receivedPacketCount READ receivedPacketCount NOTIFY stateChanged)
    Q_PROPERTY(qint32 lastRawInstantSpeed READ lastRawInstantSpeed NOTIFY stateChanged)
    Q_PROPERTY(quint32 lastRawTotalLength READ lastRawTotalLength NOTIFY stateChanged)
    Q_PROPERTY(int lastGoodReading READ lastGoodReading NOTIFY stateChanged)
    Q_PROPERTY(int packetAgeMs READ packetAgeMs NOTIFY stateChanged)

public:
    explicit PipePresenceStateController(QObject *parent = nullptr);

    bool radarPresent() const { return m_effectiveOutput; }
    bool radarPresenceKnown() const { return true; } // proxy default, NOT actual sensor known
    QString pipePhase() const { return m_phase; }
    QString phaseLabel() const;
    QString motionState() const { return m_motion; }
    int laserPresent() const { return m_laserPresent; }
    double distanceToLaserMm() const { return m_distanceToLaserMm; }
    void setRadarToLaserDistanceMm(double mm);
    QString confidenceLabel() const { return m_confidence; }
    QString lb1Label() const;
    QString lb2Label() const;
    QString radarLabel() const;
    QString laserLabel() const;
    QString motionLabel() const;
    QString radarLinkLabel() const { return m_radarLink; }
    double speedMmS() const { return m_speedMmS; }
    qulonglong receivedPacketCount() const { return m_receivedPackets; }
    qint32 lastRawInstantSpeed() const { return m_lastSpeedRaw; }
    quint32 lastRawTotalLength() const { return m_previousLengthRaw; }
    int lastGoodReading() const { return int(m_lastGood); }
    int packetAgeMs() const { return m_packetAgeMs; }

    void updateRadarSample(const RadarUdpProtocol::Packet &packet, qint64 hostReceiveUs);
    // Thread-safe, hard-bounded transport for the 100ms GUI update timer.
    void enqueueRadarSample(const RadarUdpProtocol::Packet &packet, qint64 hostReceiveUs);
    void reportRadarServiceStatus(const QString &message);
    void tick(qint64 nowHostUs);

signals:
    void stateChanged();

private:
    void refresh(qint64 nowUs, bool force = false);
    void setInactive();
    void resetLengthOrigin(quint32 raw);
    struct QueuedRadar {
        RadarUdpProtocol::Packet packet;
        qint64 receiveUs = 0;
    };
    static constexpr size_t MaxUiRadarQueued = 256;
    std::mutex m_uiRadarMutex;
    std::deque<QueuedRadar> m_uiRadarQueue;
    quint64 m_uiDiscardedPending = 0;
    bool m_uiPositionInvalid = false;
    bool m_effectiveOutput = false;
    bool m_radarSeen = false;
    bool m_hasPreviousLength = false;
    bool m_hasLengthOrigin = false;
    bool m_everEstimatedLaser = false;
    quint32 m_previousLengthRaw = 0, m_lengthOriginRaw = 0;
    qint64 m_lastRadarUs = -1, m_lastUiUs = -1;
    qint64 m_stationarySinceUs = -1;
    quint32 m_stationaryReferenceLength = 0;
    qint32 m_lastSpeedRaw = 0;
    quint8 m_lastGood = 0;
    qulonglong m_receivedPackets = 0;
    int m_packetAgeMs = -1;
    QString m_phase = QStringLiteral("NOT_ARRIVED");
    QString m_motion = QStringLiteral("UNKNOWN");
    QString m_confidence = QStringLiteral("雷达无测速输出：LB1/LB2默认未工作（推定）");
    QString m_radarLink = QStringLiteral("等待雷达UDP");
    QString m_radarServiceError;
    int m_laserPresent = -1;
    double m_distanceToLaserMm = std::numeric_limits<double>::quiet_NaN();
    double m_radarToLaserMm = RadarLaserGeometry::DefaultRadarToLaserMm; // updated from DataController
    double m_speedMmS = std::numeric_limits<double>::quiet_NaN();
};
