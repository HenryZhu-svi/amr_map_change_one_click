#include "RbkProtocol.h"

#include <QtEndian>

namespace {
void append16(QByteArray &out, quint16 value)
{
    const quint16 be = qToBigEndian(value);
    out.append(reinterpret_cast<const char *>(&be), sizeof(be));
}

void append32(QByteArray &out, quint32 value)
{
    const quint32 be = qToBigEndian(value);
    out.append(reinterpret_cast<const char *>(&be), sizeof(be));
}

quint16 read16(const char *data)
{
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(data));
}

quint32 read32(const char *data)
{
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(data));
}
} // namespace

namespace RbkProtocol {

QByteArray encodeRequest(quint16 sequence, quint16 command,
                         const QByteArray &payload)
{
    QByteArray packet;
    packet.reserve(HeaderSize + payload.size());
    packet.append(char(0x5A));
    packet.append(char(0x01));
    append16(packet, sequence);
    append32(packet, quint32(payload.size()));
    append16(packet, command);
    append16(packet, 0);
    append32(packet, 0);
    packet.append(payload);
    return packet;
}

bool decodeHeader(const QByteArray &bytes, Header *header, QString *error)
{
    if (bytes.size() < HeaderSize) {
        if (error) *error = QStringLiteral("协议头不足 16 字节");
        return false;
    }
    const auto *p = bytes.constData();
    if (quint8(p[0]) != 0x5A || quint8(p[1]) != 0x01) {
        if (error) *error = QStringLiteral("协议魔数不是 5A 01");
        return false;
    }
    header->sequence = read16(p + 2);
    header->payloadLength = read32(p + 4);
    header->command = read16(p + 8);
    header->requestCommand = read16(p + 10);
    return true;
}

quint16 expectedResponse(quint16 requestCommand)
{
    return quint16(requestCommand + 10000);
}

} // namespace RbkProtocol
