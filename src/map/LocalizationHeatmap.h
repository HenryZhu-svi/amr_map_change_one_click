#pragma once

#include <QHash>
#include <QString>
#include <QVector>

struct ConfidenceHeatmapCell {
    int gridX = 0;
    int gridY = 0;
    double positionX = 0.0;
    double positionY = 0.0;
    double confidence = 0.0;
    double lowVisitRatio = 0.0;
    int visits = 0;
};

class LocalizationHeatmapBuilder {
public:
    static constexpr double CellSizeMeters = 0.5;

    bool addCsvFile(const QString &fileName, const QString &expectedMap, QString *error);
    QVector<ConfidenceHeatmapCell> finish() const;

    QString robotName() const { return m_robotName; }
    QString robotIp() const { return m_robotIp; }
    QString mapName() const { return m_mapName; }
    int rowCount() const { return m_rowCount; }
    int movingSamples() const { return m_movingSamples; }
    int rejectedSamples() const { return m_rejectedSamples; }
    int fileCount() const { return m_fileCount; }

private:
    struct Visit {
        QVector<double> scores;
        double lastDistance = 0.0;
        qint64 lastTime = 0;
        int fileId = 0;
        double xSum = 0.0;
        double ySum = 0.0;
        int count = 0;
    };

    static quint64 cellKey(int x, int y);
    void addMovingSample(int fileId, qint64 timestampMs, double x, double y,
                         double confidence, double cumulativeDistance);

    QHash<quint64, QVector<Visit>> m_cellVisits;
    QString m_robotName;
    QString m_robotIp;
    QString m_mapName;
    int m_rowCount = 0;
    int m_movingSamples = 0;
    int m_rejectedSamples = 0;
    int m_fileCount = 0;
};
