#include "RadarUdpReceiver.h"
#include "RadarHostClock.h"

#include <QAbstractSocket>
#include <QNetworkDatagram>

RadarUdpReceiver::RadarUdpReceiver(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<RadarUdpProtocol::Packet>();
    connect(&m_socket, &QUdpSocket::readyRead,
            this, &RadarUdpReceiver::drainPendingDatagrams);

    connect(&m_socket, &QUdpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError error) {
        if (error != QAbstractSocket::UnknownSocketError)
            emit receiverError(m_socket.errorString());
    });
}

bool RadarUdpReceiver::start(const QHostAddress &localAddress,
                             quint16 localPort,
                             const QHostAddress &expectedRadarAddress)
{
    stop();
    if (expectedRadarAddress.isNull() || expectedRadarAddress.protocol() != QAbstractSocket::IPv4Protocol) {
        emit receiverError(QStringLiteral("The expected radar IP must be a valid IPv4 address"));
        return false;
    }

    m_expectedRadarAddress = expectedRadarAddress;
    m_stats = {};
    m_lastAcceptedUs = -1;

    // Exclusive bind: do not silently share the port with PCIS or another logger.
    if (!m_socket.bind(localAddress, localPort, QUdpSocket::DontShareAddress)) {
        emit receiverError(QStringLiteral("UDP bind failed: ") + m_socket.errorString());
        return false;
    }
    m_clock.start();
    return true;
}

void RadarUdpReceiver::stop()
{
    m_socket.close();
    m_clock.invalidate();
    m_lastAcceptedUs = -1;
}

bool RadarUdpReceiver::isListening() const
{
    return m_socket.state() == QAbstractSocket::BoundState;
}

void RadarUdpReceiver::drainPendingDatagrams()
{
    ++m_stats.readReadyEvents;
    quint64 datagramsThisEvent = 0;
    while (m_socket.hasPendingDatagrams()) {
        ++datagramsThisEvent;
        // Timestamp at event-loop processing time; not the kernel arrival,
        // network transmission time or the radar's measurement time.
        const qint64 hostSteadyUs = RadarHostClock::nowUs();
        const quint64 hostUs = m_clock.isValid()
            ? static_cast<quint64>(m_clock.nsecsElapsed() / 1000)
            : 0;
        const QDateTime hostUtc = QDateTime::currentDateTimeUtc();
        const QNetworkDatagram datagram = m_socket.receiveDatagram();
        if (!datagram.isValid()) {
            ++m_stats.readErrors;
            emit receiverError(QStringLiteral("Failed to read UDP datagram: ") + m_socket.errorString());
            continue;
        }
        ++m_stats.totalDatagrams;
        const QHostAddress sender = datagram.senderAddress();
        const QByteArray payload = datagram.data();

        if (sender != m_expectedRadarAddress) {
            ++m_stats.wrongSource;
            emit datagramRejected(QStringLiteral("Ignoring source %1 (expected %2)")
                                  .arg(sender.toString(), m_expectedRadarAddress.toString()));
            continue;
        }
        if (payload.size() != ExpectedPayloadBytes) {
            ++m_stats.wrongLength;
            emit datagramRejected(QStringLiteral("Unexpected UDP payload length: %1, expected 13")
                                  .arg(payload.size()));
            continue;
        }

        const qint64 gapUs = m_lastAcceptedUs >= 0
            ? static_cast<qint64>(hostUs) - m_lastAcceptedUs
            : -1;
        m_lastAcceptedUs = static_cast<qint64>(hostUs);
        ++m_stats.accepted;
        m_stats.lastAcceptedGapUs = gapUs;
        if (gapUs >= 0) {
            if (m_stats.minAcceptedGapUs < 0 || gapUs < m_stats.minAcceptedGapUs)
                m_stats.minAcceptedGapUs = gapUs;
            if (gapUs > m_stats.maxAcceptedGapUs)
                m_stats.maxAcceptedGapUs = gapUs;
        }
        emit rawDatagramReceived(payload, hostUs, hostUtc, sender,
                                 datagram.senderPort(), gapUs, m_stats.accepted);
        RadarUdpProtocol::Packet decoded;
        if (RadarUdpProtocol::decode(payload, decoded)) {
            emit decodedDatagramReceived(decoded, hostUs, hostUtc,
                                         gapUs, m_stats.accepted);
            emit radarSampleReceived(decoded, hostSteadyUs);
        }
    }
    if (datagramsThisEvent > 1)
        ++m_stats.multiDatagramEvents;
    if (datagramsThisEvent > m_stats.maxDatagramsPerEvent)
        m_stats.maxDatagramsPerEvent = datagramsThisEvent;
}
