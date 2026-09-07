#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <functional>

class QTcpSocket;
class QTimer;

struct RequestResult {
    bool ok = false;
    QString error;
    quint16 responseCommand = 0;
    QByteArray payload;
};

class RequestJob final : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(RequestResult)>;

    static void start(QObject *owner, const QString &host, quint16 port,
                      quint16 command, const QByteArray &payload,
                      Callback callback, int timeoutMs = 10000);

private:
    RequestJob(const QString &host, quint16 port, quint16 command,
               const QByteArray &payload, Callback callback, int timeoutMs,
               QObject *parent);
    void finish(RequestResult result);
    void consume();

    QTcpSocket *m_socket = nullptr;
    QTimer *m_timer = nullptr;
    QByteArray m_buffer;
    Callback m_callback;
    quint16 m_sequence = 0;
    quint16 m_command = 0;
    quint32 m_expectedPayload = 0;
    bool m_headerReady = false;
    bool m_finished = false;
};
