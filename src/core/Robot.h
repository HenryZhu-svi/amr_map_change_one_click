#pragma once

#include <QString>

struct Robot {
    QString name;
    QString host;
    quint16 statusPort = 19204;
    quint16 controlPort = 19205;
    quint16 navigationPort = 19206;
    quint16 configPort = 19207;
};
