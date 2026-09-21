#include "MapView.h"

#include <QFile>
#include <QFont>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyleOptionGraphicsItem>
#include <QWheelEvent>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

QJsonValue field(const QJsonObject &object, const char *camelCase, const char *snakeCase)
{
    const QString camel = QString::fromLatin1(camelCase);
    if (object.contains(camel)) return object.value(camel);
    return object.value(QString::fromLatin1(snakeCase));
}

QPointF mapPosition(const QJsonObject &object)
{
    // Scene Y points down. Negating map Y keeps the conventional map Y-up view.
    return {object.value(QStringLiteral("x")).toDouble(),
            -object.value(QStringLiteral("y")).toDouble()};
}

QJsonObject positionObject(const QJsonObject &object, const char *camelCase,
                           const char *snakeCase)
{
    return field(object, camelCase, snakeCase).toObject();
}

QColor pointColor(const QString &className)
{
    if (className.contains(QStringLiteral("Charge"), Qt::CaseInsensitive)) return QColor(36, 164, 85);
    if (className.contains(QStringLiteral("Park"), Qt::CaseInsensitive)) return QColor(132, 81, 214);
    if (className.contains(QStringLiteral("Action"), Qt::CaseInsensitive)) return QColor(236, 142, 38);
    return QColor(30, 113, 190);
}

struct NamedPoint {
    QPointF position;
    QString name;
    QString className;
    double direction = 0.0;
};

struct Curve {
    QPointF start;
    QPointF control1;
    QPointF control2;
    QPointF end;
};

struct NamedLine {
    QLineF line;
    QString className;
};

class RobotPoseItem final : public QGraphicsItem {
public:
    QRectF boundingRect() const override
    {
        return QRectF(-22.0, -24.0, 300.0, 55.0);
    }

    void setPose(double angle, double confidence, const QString &label)
    {
        prepareGeometryChange();
        m_angle = angle;
        m_confidence = confidence;
        m_label = label;
        update();
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        QColor color(110, 118, 126);
        if (std::isfinite(m_confidence)) {
            if (m_confidence >= 0.8) color = QColor(36, 164, 85);
            else if (m_confidence >= 0.6) color = QColor(239, 174, 29);
            else color = QColor(220, 60, 55);
        }

        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(color.darker(130), 2.0));
        painter->setBrush(QColor(color.red(), color.green(), color.blue(), 55));
        painter->drawEllipse(QPointF(0.0, 0.0), 18.0, 18.0);

        painter->save();
        painter->rotate(-m_angle * 180.0 / 3.14159265358979323846);
        painter->setBrush(color);
        painter->setPen(QPen(Qt::white, 1.2));
        painter->drawPolygon(QPolygonF{
            QPointF(17.0, 0.0), QPointF(-10.0, -9.0),
            QPointF(-5.0, 0.0), QPointF(-10.0, 9.0)});
        painter->restore();

        QFont font = painter->font();
        font.setPointSizeF(9.0);
        font.setBold(true);
        painter->setFont(font);
        const QRectF textRect(24.0, -18.0, 250.0, 36.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 225));
        painter->drawRoundedRect(textRect.adjusted(-5.0, -2.0, 5.0, 2.0), 4.0, 4.0);
        painter->setPen(QPen(color.darker(150), 1.0));
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_label);
    }

private:
    double m_angle = 0.0;
    double m_confidence = std::numeric_limits<double>::quiet_NaN();
    QString m_label;
};

class MapGraphicsItem final : public QGraphicsItem {
public:
    explicit MapGraphicsItem(const QJsonObject &root, MapSummary *summary)
    {
        const QJsonObject header = root.value(QStringLiteral("header")).toObject();
        summary->name = field(header, "mapName", "map_name").toString();
        summary->type = field(header, "mapType", "map_type").toString();
        summary->version = header.value(QStringLiteral("version")).toString();
        summary->resolution = header.value(QStringLiteral("resolution")).toDouble();

        const QJsonObject minObject = positionObject(header, "minPos", "min_pos");
        const QJsonObject maxObject = positionObject(header, "maxPos", "max_pos");
        if (!minObject.isEmpty() && !maxObject.isEmpty()) {
            const double minX = minObject.value(QStringLiteral("x")).toDouble();
            const double minY = minObject.value(QStringLiteral("y")).toDouble();
            const double maxX = maxObject.value(QStringLiteral("x")).toDouble();
            const double maxY = maxObject.value(QStringLiteral("y")).toDouble();
            m_bounds = QRectF(QPointF(minX, -maxY), QPointF(maxX, -minY)).normalized();
        }

        const QJsonArray normalPoints = field(root, "normalPosList", "normal_pos_list").toArray();
        m_normalPoints.reserve(normalPoints.size());
        for (const QJsonValue &value : normalPoints) {
            const QPointF point = mapPosition(value.toObject());
            m_normalPoints.append(point);
            include(point);
        }

        const QJsonArray normalLines = field(root, "normalLineList", "normal_line_list").toArray();
        m_normalLines.reserve(normalLines.size());
        for (const QJsonValue &value : normalLines) {
            const QJsonObject line = value.toObject();
            const QPointF start = mapPosition(positionObject(line, "startPos", "start_pos"));
            const QPointF end = mapPosition(positionObject(line, "endPos", "end_pos"));
            m_normalLines.append(QLineF(start, end));
            include(start); include(end);
        }

        const QJsonArray advancedPoints = field(root, "advancedPointList", "advanced_point_list").toArray();
        m_points.reserve(advancedPoints.size());
        for (const QJsonValue &value : advancedPoints) {
            const QJsonObject point = value.toObject();
            NamedPoint item;
            item.position = mapPosition(point.value(QStringLiteral("pos")).toObject());
            item.name = field(point, "instanceName", "instance_name").toString();
            item.className = field(point, "className", "class_name").toString();
            item.direction = point.value(QStringLiteral("dir")).toDouble();
            m_points.append(item);
            include(item.position);
        }

        const QJsonArray advancedLines = field(root, "advancedLineList", "advanced_line_list").toArray();
        m_advancedLines.reserve(advancedLines.size());
        for (const QJsonValue &value : advancedLines) {
            const QJsonObject object = value.toObject();
            const QJsonObject line = object.value(QStringLiteral("line")).toObject();
            NamedLine item;
            item.line = QLineF(mapPosition(positionObject(line, "startPos", "start_pos")),
                               mapPosition(positionObject(line, "endPos", "end_pos")));
            item.className = field(object, "className", "class_name").toString();
            m_advancedLines.append(item);
            include(item.line.p1()); include(item.line.p2());
        }

        const QJsonArray curves = field(root, "advancedCurveList", "advanced_curve_list").toArray();
        m_curves.reserve(curves.size());
        for (const QJsonValue &value : curves) {
            const QJsonObject object = value.toObject();
            Curve curve;
            curve.start = mapPosition(positionObject(
                positionObject(object, "startPos", "start_pos"), "pos", "pos"));
            curve.end = mapPosition(positionObject(
                positionObject(object, "endPos", "end_pos"), "pos", "pos"));
            curve.control1 = mapPosition(positionObject(object, "controlPos1", "control_pos1"));
            curve.control2 = mapPosition(positionObject(object, "controlPos2", "control_pos2"));
            m_curves.append(curve);
            include(curve.start); include(curve.end); include(curve.control1); include(curve.control2);
        }

        const QJsonArray areas = field(root, "advancedAreaList", "advanced_area_list").toArray();
        m_areas.reserve(areas.size());
        for (const QJsonValue &value : areas) {
            const QJsonObject object = value.toObject();
            const QJsonArray positions = field(object, "posGroup", "pos_group").toArray();
            QPolygonF polygon;
            polygon.reserve(positions.size());
            for (const QJsonValue &position : positions) {
                const QPointF point = mapPosition(position.toObject());
                polygon.append(point);
                include(point);
            }
            if (polygon.size() >= 3) m_areas.append(polygon);
        }

        if (!m_bounds.isValid() || m_bounds.isEmpty()) m_bounds = QRectF(-1, -1, 2, 2);
        const double margin = std::max(0.5, std::max(m_bounds.width(), m_bounds.height()) * 0.015);
        m_bounds.adjust(-margin, -margin, margin, margin);

        summary->normalPointCount = m_normalPoints.size();
        summary->stationCount = m_points.size();
        summary->pathCount = m_curves.size() + m_advancedLines.size();
        summary->areaCount = m_areas.size();
    }

    QRectF boundingRect() const override { return m_bounds; }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        const qreal lod = std::max<qreal>(0.001,
            QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform()));
        painter->fillRect(m_bounds, QColor(250, 251, 252));

        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(QPen(QColor(45, 51, 57, 205), 0));
        if (!m_normalPoints.isEmpty()) painter->drawPoints(m_normalPoints);
        painter->setPen(QPen(QColor(35, 40, 45), 0));
        if (!m_normalLines.isEmpty()) painter->drawLines(m_normalLines);

        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(QColor(224, 73, 73), 0));
        painter->setBrush(QColor(224, 73, 73, 45));
        for (const QPolygonF &area : m_areas) painter->drawPolygon(area);

        for (const NamedLine &line : m_advancedLines) {
            const QColor color = line.className.contains(QStringLiteral("Forbidden"), Qt::CaseInsensitive)
                               ? QColor(220, 50, 47) : QColor(230, 126, 34);
            painter->setPen(QPen(color, 0));
            painter->drawLine(line.line);
        }

        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(38, 132, 191, 210), 0));
        for (const Curve &curve : m_curves) {
            QPainterPath path(curve.start);
            path.cubicTo(curve.control1, curve.control2, curve.end);
            painter->drawPath(path);
        }

        const qreal radius = 4.5 / lod;
        QFont labelFont = painter->font();
        labelFont.setPointSizeF(std::clamp(8.0 / lod, 0.08, 10.0));
        for (const NamedPoint &point : m_points) {
            const QColor color = pointColor(point.className);
            painter->setPen(QPen(color.darker(130), 1.2 / lod));
            painter->setBrush(color);
            painter->drawEllipse(point.position, radius, radius);
            const QPointF heading(point.position.x() + std::cos(point.direction) * radius * 2.0,
                                  point.position.y() - std::sin(point.direction) * radius * 2.0);
            painter->drawLine(point.position, heading);
            if (lod >= 3.0 && !point.name.isEmpty()) {
                painter->setFont(labelFont);
                painter->setPen(QPen(QColor(20, 24, 28), 0));
                painter->drawText(point.position + QPointF(radius * 1.4, -radius * 1.2), point.name);
            }
        }
    }

private:
    void include(const QPointF &point)
    {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) return;
        if (!m_bounds.isValid() || m_bounds.isNull()) m_bounds = QRectF(point, QSizeF(0.001, 0.001));
        else m_bounds |= QRectF(point, QSizeF(0.001, 0.001));
    }

    QRectF m_bounds;
    QPolygonF m_normalPoints;
    QVector<QLineF> m_normalLines;
    QVector<NamedPoint> m_points;
    QVector<NamedLine> m_advancedLines;
    QVector<Curve> m_curves;
    QVector<QPolygonF> m_areas;
};

} // namespace

MapView::MapView(QWidget *parent) : QGraphicsView(parent), m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setBackgroundBrush(QColor(232, 235, 238));
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setRenderHint(QPainter::Antialiasing, true);
}

bool MapView::loadFile(const QString &fileName, MapSummary *summary, QString *error)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    return loadBytes(file.readAll(), summary, error);
}

bool MapView::loadBytes(const QByteArray &bytes, MapSummary *summary, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (!document.isObject()) {
        if (error) *error = parseError.errorString();
        return false;
    }
    auto *item = new MapGraphicsItem(document.object(), summary);
    m_robotPoseItem = nullptr;
    m_scene->clear();
    m_scene->addItem(item);
    m_scene->setSceneRect(item->boundingRect());
    m_hasMap = true;
    m_firstResizeAfterLoad = true;
    fitMap();
    return true;
}

void MapView::fitMap()
{
    if (!m_hasMap) return;
    resetTransform();
    fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    m_firstResizeAfterLoad = false;
}

bool MapView::hasMap() const
{
    return m_hasMap;
}

void MapView::setRobotPose(double x, double y, double angle, double confidence,
                           const QString &label)
{
    if (!m_hasMap || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(angle)) return;
    if (!m_robotPoseItem) {
        auto *item = new RobotPoseItem;
        item->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        item->setZValue(1000.0);
        m_scene->addItem(item);
        m_robotPoseItem = item;
    }
    auto *item = static_cast<RobotPoseItem *>(m_robotPoseItem);
    item->setPos(x, -y);
    item->setPose(angle, confidence, label);
    item->setVisible(true);
}

void MapView::clearRobotPose()
{
    if (m_robotPoseItem) m_robotPoseItem->setVisible(false);
}

void MapView::wheelEvent(QWheelEvent *event)
{
    if (!m_hasMap) return QGraphicsView::wheelEvent(event);
    const qreal factor = event->angleDelta().y() > 0 ? 1.2 : (1.0 / 1.2);
    const qreal current = transform().m11();
    if ((factor > 1.0 && current < 5000.0) || (factor < 1.0 && current > 0.01)) scale(factor, factor);
    event->accept();
}

void MapView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_firstResizeAfterLoad) fitMap();
}
