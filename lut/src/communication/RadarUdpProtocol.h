#pragma once

#include <QByteArray>
#include <QtGlobal>
#include <QMetaType>

// PROTON SL/SLR 13-byte UDP output layout, verified from radar firmware code:
// 0..3  DataOut.AveragedSpeed big-endian 32-bit
// 4..7  DataOut.InstantSpeed  big-endian 32-bit
// 8..11 DataOut.TotalLength   big-endian 32-bit
// 12    DataOut.GoodReading   8-bit
//
// Firmware excerpt establishes layout and byte order only. It DOES NOT
// establish whether the 32-bit fields are signed, their engineering scaling,
// the GoodReading thresholds, or the radar's acquisition timestamp.
namespace RadarUdpProtocol {

constexpr qsizetype PacketBytes = 13;

struct Packet {
    quint32 averagedSpeedBits = 0;
    quint32 instantSpeedBits = 0;
    quint32 totalLengthBits = 0;
    quint8 goodReadingRaw = 0;

    // Two's-complement interpretation for inspection/comparison only.
    // Do not assume sign convention is confirmed by the firmware snippet.
    static qint32 asSigned(quint32 bits)
    {
        const qint64 signedValue = bits < 0x80000000u
            ? static_cast<qint64>(bits)
            : static_cast<qint64>(bits) - 0x100000000LL;
        return static_cast<qint32>(signedValue);
    }

    qint32 averagedSpeedSigned() const { return asSigned(averagedSpeedBits); }
    qint32 instantSpeedSigned() const { return asSigned(instantSpeedBits); }
    qint32 totalLengthSigned() const { return asSigned(totalLengthBits); }
};

inline quint32 readBigEndianU32(const QByteArray &payload, qsizetype offset)
{
    const auto *p = reinterpret_cast<const unsigned char *>(payload.constData() + offset);
    return (quint32(p[0]) << 24u)
        | (quint32(p[1]) << 16u)
        | (quint32(p[2]) << 8u)
        | quint32(p[3]);
}

// Pure byte parsing: no undocumented mm/s or mm scaling and no validity claims.
inline bool decode(const QByteArray &payload, Packet &out)
{
    if (payload.size() != PacketBytes)
        return false;
    Packet decoded;
    decoded.averagedSpeedBits = readBigEndianU32(payload, 0);
    decoded.instantSpeedBits = readBigEndianU32(payload, 4);
    decoded.totalLengthBits = readBigEndianU32(payload, 8);
    decoded.goodReadingRaw = static_cast<quint8>(payload.at(12));
    out = decoded;
    return true;
}

} // namespace RadarUdpProtocol

Q_DECLARE_METATYPE(RadarUdpProtocol::Packet)
