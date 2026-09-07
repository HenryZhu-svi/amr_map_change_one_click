#pragma once

#include <QByteArray>
#include <QString>

namespace RbkProtocol {

constexpr qsizetype HeaderSize = 16;

enum Command : quint16 {
    QueryTask = 1020,
    QueryMap = 1300,
    QueryStations = 1301,
    QueryMapMd5 = 1302,
    LoadMap = 2022,
    UploadAndLoadMap = 2025,
    CancelTask = 3003,
    UploadMap = 4010,
    DownloadMap = 4011,
};

struct Header {
    quint16 sequence = 0;
    quint32 payloadLength = 0;
    quint16 command = 0;
    quint16 requestCommand = 0;
};

QByteArray encodeRequest(quint16 sequence, quint16 command,
                         const QByteArray &payload = {});
bool decodeHeader(const QByteArray &bytes, Header *header, QString *error);
quint16 expectedResponse(quint16 requestCommand);

} // namespace RbkProtocol
