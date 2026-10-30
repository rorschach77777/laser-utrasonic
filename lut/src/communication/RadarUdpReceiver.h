#pragma once

#include <QObject>
#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QUdpSocket>

#include "RadarUdpProtocol.h"
#include "LstDataExport.h"

// PROTON SL/SLR UDP receive-only path: 13-byte packet layout decoded as raw
// values only. Engineering units, validity semantics and true measurement
// timestamps are NOT inferred here.
class LST_DATA_API RadarUdpReceiver final : public QObject
{
    Q_OBJECT

public:
    static constexpr int ExpectedPayloadBytes = 13;

    struct Stats {
        quint64 totalDatagrams = 0;
        quint64 accepted = 0;
        quint64 wrongSource = 0;
        quint64 wrongLength = 0;
        quint64 readErrors = 0;
        quint64 readReadyEvents = 0;
        quint64 multiDatagramEvents = 0;
        quint64 maxDatagramsPerEvent = 0;
        qint64 lastAcceptedGapUs = -1;
        qint64 minAcceptedGapUs = -1;
        qint64 maxAcceptedGapUs = -1;
    };

    explicit RadarUdpReceiver(QObject *parent = nullptr);

    // Destination/listening interface and port; expected radar source IP.
    bool start(const QHostAddress &localAddress,
               quint16 localPort,
               const QHostAddress &expectedRadarAddress);
    void stop();
    bool isListening() const;
    quint16 listeningPort() const { return m_socket.localPort(); }
    Stats stats() const { return m_stats; }

signals:
    // hostArrivalUs is a MONOTONIC HOST receive callback time measured
    // relative to start(); it is NOT the actual radar acquisition time.
    void rawDatagramReceived(const QByteArray &payload,
                             quint64 hostArrivalUs,
                             const QDateTime &hostUtcReceived,
                             const QHostAddress &sender,
                             quint16 senderPort,
                             qint64 gapUs,
                             quint64 acceptedIndex);
    // Identical host receive metadata, but fields are decoded as raw integers.
    // No physical speed/length scaling or measurement-validity assumption.
    void decodedDatagramReceived(const RadarUdpProtocol::Packet &packet,
                                 quint64 hostArrivalUs,
                                 const QDateTime &hostUtcReceived,
                                 qint64 gapUs,
                                 quint64 acceptedIndex);
    // Process-wide host steady-clock timestamp for cross-thread DAQ matching.
    // Separate from existing receiver-relative hostArrivalUs signal.
    void radarSampleReceived(const RadarUdpProtocol::Packet &packet, qint64 hostSteadyUs);
    void datagramRejected(const QString &reason);
    void receiverError(const QString &message);

private:
    void drainPendingDatagrams();

    QUdpSocket m_socket;
    QElapsedTimer m_clock;
    QHostAddress m_expectedRadarAddress;
    Stats m_stats;
    qint64 m_lastAcceptedUs = -1;
};
