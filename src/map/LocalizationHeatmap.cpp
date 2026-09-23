#include "LocalizationHeatmap.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
QString normalizedMapName(QString name)
{
    name = name.trimmed();
    while (name.endsWith(QStringLiteral(".smap"), Qt::CaseInsensitive)) {
        name.chop(5);
        name = name.trimmed();
    }
    return name.toCaseFolded();
}

QStringList parseCsvLine(const QString &line)
{
    QStringList fields;
    QString field;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"')) {
            if (quoted && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                field += QLatin1Char('"');
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == QLatin1Char(',') && !quoted) {
            fields.append(field);
            field.clear();
        } else {
            field += ch;
        }
    }
    fields.append(field);
    return fields;
}

bool finiteDouble(const QString &text, double *value)
{
    bool ok = false;
    const double parsed = text.toDouble(&ok);
    if (!ok || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
}
} // namespace

quint64 LocalizationHeatmapBuilder::cellKey(int x, int y)
{
    return (quint64(quint32(x)) << 32) | quint32(y);
}

void LocalizationHeatmapBuilder::addMovingSample(int fileId, qint64 timestampMs,
                                                  double x, double y, double confidence,
                                                  double cumulativeDistance)
{
    const int gridX = int(std::floor(x / CellSizeMeters));
    const int gridY = int(std::floor(y / CellSizeMeters));
    QVector<Visit> &visits = m_cellVisits[cellKey(gridX, gridY)];
    if (visits.isEmpty() || visits.last().fileId != fileId
        || cumulativeDistance - visits.last().lastDistance >= 2.0
        || timestampMs - visits.last().lastTime > 60000) {
        Visit visit;
        visit.fileId = fileId;
        visits.append(visit);
    }
    Visit &visit = visits.last();
    visit.scores.append(confidence);
    visit.xSum += x;
    visit.ySum += y;
    ++visit.count;
    visit.lastDistance = cumulativeDistance;
    visit.lastTime = timestampMs;
}

bool LocalizationHeatmapBuilder::addCsvFile(const QString &fileName,
                                            const QString &expectedMap, QString *error)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }

    QString header = QString::fromUtf8(file.readLine()).trimmed();
    if (header.startsWith(QChar(0xfeff))) header.remove(0, 1);
    const QStringList columns = parseCsvLine(header);
    const auto column = [&columns](const char *name) {
        return columns.indexOf(QString::fromLatin1(name));
    };
    const int timeColumn = column("timestamp");
    const int robotColumn = column("robot");
    const int ipColumn = column("ip");
    const int mapColumn = column("map");
    const int xColumn = column("x");
    const int yColumn = column("y");
    const int confidenceColumn = column("confidence");
    const int distanceColumn = column("distance_m");
    const int speedColumn = column("speed_m_s");
    if (timeColumn < 0 || robotColumn < 0 || ipColumn < 0 || mapColumn < 0
        || xColumn < 0 || yColumn < 0 || confidenceColumn < 0
        || distanceColumn < 0 || speedColumn < 0) {
        if (error) *error = QStringLiteral("CSV is missing localization columns: %1")
                                .arg(QFileInfo(fileName).fileName());
        return false;
    }

    const int requiredColumns = std::max({timeColumn, robotColumn, ipColumn, mapColumn,
                                           xColumn, yColumn, confidenceColumn,
                                           distanceColumn, speedColumn}) + 1;
    const int fileId = m_fileCount + 1;
    double cumulativeDistance = 0.0;
    int rowsInFile = 0;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty()) continue;
        ++rowsInFile;
        ++m_rowCount;
        const QStringList fields = parseCsvLine(line);
        if (fields.size() < requiredColumns) {
            ++m_rejectedSamples;
            continue;
        }

        const QString csvMap = fields.at(mapColumn).trimmed();
        const QString csvIp = fields.at(ipColumn).trimmed();
        if (csvIp.isEmpty() || normalizedMapName(csvMap).isEmpty()
            || normalizedMapName(csvMap) != normalizedMapName(expectedMap)
            || (!m_robotIp.isEmpty() && csvIp != m_robotIp)) {
            if (error) *error = QStringLiteral("Map or robot mismatch in %1 at row %2")
                                    .arg(QFileInfo(fileName).fileName()).arg(rowsInFile + 1);
            return false;
        }
        if (m_robotIp.isEmpty()) {
            m_robotName = fields.at(robotColumn).trimmed();
            m_robotIp = csvIp;
            m_mapName = csvMap;
        }

        double x = 0.0, y = 0.0, confidence = 0.0, distance = 0.0, speed = 0.0;
        const QDateTime timestamp = QDateTime::fromString(fields.at(timeColumn), Qt::ISODateWithMs);
        if (!timestamp.isValid()
            || !finiteDouble(fields.at(xColumn), &x)
            || !finiteDouble(fields.at(yColumn), &y)
            || !finiteDouble(fields.at(confidenceColumn), &confidence)
            || !finiteDouble(fields.at(distanceColumn), &distance)
            || !finiteDouble(fields.at(speedColumn), &speed)
            || confidence < 0.0 || confidence > 1.0 || distance < 0.0) {
            ++m_rejectedSamples;
            continue;
        }
        if (speed > 3.0) {
            ++m_rejectedSamples;
            continue;
        }
        cumulativeDistance += distance;
        // The exported file retains every response. Distance gating prevents a
        // stationary robot from contributing thousands of votes at one position.
        if (distance < 0.05) continue;
        ++m_movingSamples;
        addMovingSample(fileId, timestamp.toMSecsSinceEpoch(), x, y,
                        confidence, cumulativeDistance);
    }
    if (rowsInFile == 0) {
        if (error) *error = QStringLiteral("CSV contains no samples: %1")
                                .arg(QFileInfo(fileName).fileName());
        return false;
    }
    ++m_fileCount;
    return true;
}

QVector<ConfidenceHeatmapCell> LocalizationHeatmapBuilder::finish() const
{
    QVector<ConfidenceHeatmapCell> cells;
    cells.reserve(m_cellVisits.size());
    for (auto it = m_cellVisits.cbegin(); it != m_cellVisits.cend(); ++it) {
        QVector<double> visitScores;
        visitScores.reserve(it.value().size());
        double positionX = 0.0;
        double positionY = 0.0;
        for (const Visit &visit : it.value()) {
            if (visit.scores.isEmpty()) continue;
            QVector<double> scores = visit.scores;
            std::sort(scores.begin(), scores.end());
            visitScores.append(scores.at(qsizetype(std::floor(0.2 * (scores.size() - 1)))));
            positionX += visit.xSum / visit.count;
            positionY += visit.ySum / visit.count;
        }
        if (visitScores.isEmpty()) continue;
        std::sort(visitScores.begin(), visitScores.end());

        ConfidenceHeatmapCell cell;
        const quint64 key = it.key();
        cell.gridX = qint32(key >> 32);
        cell.gridY = qint32(key & 0xffffffffu);
        cell.visits = int(visitScores.size());
        cell.positionX = positionX / cell.visits;
        cell.positionY = positionY / cell.visits;
        const qsizetype lower = (visitScores.size() - 1) / 2;
        const qsizetype upper = visitScores.size() / 2;
        cell.confidence = (visitScores.at(lower) + visitScores.at(upper)) * 0.5;
        const int lowVisits = int(std::lower_bound(visitScores.cbegin(), visitScores.cend(), 0.6)
                                  - visitScores.cbegin());
        cell.lowVisitRatio = double(lowVisits) / cell.visits;
        cells.append(cell);
    }
    return cells;
}
