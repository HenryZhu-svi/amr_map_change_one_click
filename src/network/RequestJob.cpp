#include "RequestJob.h"

#include "protocol/RbkProtocol.h"

#include <QAbstractSocket>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>

void RequestJob::start(QObject *owner, const QString &host, quint16 port,
                       quint16 command, const QByteArray &payload,
                       Callback callback, int timeoutMs)
{
    new RequestJob(host, port, command, payload, std::move(callback), timeoutMs,
                   owner);
}

RequestJob::RequestJob(const QString &host, quint16 port, quint16 command,
                       const QByteArray &payload, Callback callback,
                       int timeoutMs, QObject *parent)
    : QObject(parent), m_callback(std::move(callback)), m_command(command)
{
    m_sequence = quint16(QRandomGenerator::global()->bounded(1, 65536));
    m_socket = new QTcpSocket(this);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);

    connect(m_socket, &QTcpSocket::connected, this, [this, payload] {
        m_socket->write(RbkProtocol::encodeRequest(m_sequence, m_command, payload));
    });
    connect(m_socket, &QTcpSocket::readyRead, this, [this] {
        m_buffer += m_socket->readAll();
        consume();
    });
    connect(m_socket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
        finish({false, m_socket->errorString(), 0, {}});
    });
    connect(m_timer, &QTimer::timeout, this, [this] {
        finish({false, QStringLiteral("连接或响应超时"), 0, {}});
    });

    m_timer->start(timeoutMs);
    m_socket->connectToHost(host, port);
}

void RequestJob::consume()
{
    if (!m_headerReady) {
        if (m_buffer.size() < RbkProtocol::HeaderSize) return;
        RbkProtocol::Header header;
        QString error;
        if (!RbkProtocol::decodeHeader(m_buffer.left(RbkProtocol::HeaderSize),
                                       &header, &error)) {
            finish({false, error, 0, {}});
            return;
        }
        if (header.sequence != m_sequence) {
            finish({false, QStringLiteral("响应序列号不匹配"), header.command, {}});
            return;
        }
        if (header.command != RbkProtocol::expectedResponse(m_command)) {
            finish({false, QStringLiteral("响应编号不匹配"), header.command, {}});
            return;
        }
        m_expectedPayload = header.payloadLength;
        m_headerReady = true;
    }

    const auto total = RbkProtocol::HeaderSize + qsizetype(m_expectedPayload);
    if (m_buffer.size() < total) return;
    finish({true, {}, RbkProtocol::expectedResponse(m_command),
            m_buffer.mid(RbkProtocol::HeaderSize, m_expectedPayload)});
}

void RequestJob::finish(RequestResult result)
{
    if (m_finished) return;
    m_finished = true;
    m_timer->stop();
    m_socket->abort();
    auto callback = std::move(m_callback);
    if (callback) callback(std::move(result));
    deleteLater();
}
