#pragma once

#include "LocalizationHeatmap.h"

#include <QGraphicsView>
#include <QRectF>
#include <QString>
#include <QVector>

class QGraphicsScene;
class QGraphicsItem;

struct MapSummary {
    QString name;
    QString type;
    QString version;
    double resolution = 0.0;
    int normalPointCount = 0;
    int stationCount = 0;
    int pathCount = 0;
    int areaCount = 0;
};

class MapView final : public QGraphicsView {
public:
    explicit MapView(QWidget *parent = nullptr);

    bool loadFile(const QString &fileName, MapSummary *summary, QString *error);
    bool loadBytes(const QByteArray &bytes, MapSummary *summary, QString *error);
    void fitMap();
    bool hasMap() const;
    void setRobotPose(double x, double y, double angle, double confidence,
                      const QString &label);
    void clearRobotPose();
    void appendRobotTrackSample(double x, double y, double confidence,
                                bool anomaly, bool breakBefore = false);
    void clearRobotTrack();
    void setRobotTrackVisible(bool visible);
    bool containsMapPosition(double x, double y) const;
    int setHeatmapCells(const QVector<ConfidenceHeatmapCell> &cells);
    void clearHeatmap();
    void setHeatmapVisible(bool visible);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QGraphicsScene *m_scene = nullptr;
    QGraphicsItem *m_robotPoseItem = nullptr;
    QGraphicsItem *m_robotTrackItem = nullptr;
    QGraphicsItem *m_mapItem = nullptr;
    QGraphicsItem *m_heatmapItem = nullptr;
    QRectF m_mapBounds;
    qreal m_nativePixelsPerMeter = 20.0;
    qreal m_maxZoom = 80.0;
    bool m_hasMap = false;
    bool m_firstResizeAfterLoad = false;
    bool m_trackVisible = true;
};
