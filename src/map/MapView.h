#pragma once

#include <QGraphicsView>
#include <QRectF>
#include <QString>

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
    bool containsMapPosition(double x, double y) const;

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QGraphicsScene *m_scene = nullptr;
    QGraphicsItem *m_robotPoseItem = nullptr;
    QGraphicsItem *m_robotTrackItem = nullptr;
    QRectF m_mapBounds;
    bool m_hasMap = false;
    bool m_firstResizeAfterLoad = false;
};
