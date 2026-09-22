#include "MapView.h"

#include <QFile>
#include <QFont>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
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

class RobotTrackItem final : public QGraphicsItem {
public:
    QRectF boundingRect() const override
    {
        return m_bounds.isValid() ? m_bounds.adjusted(-20.0, -20.0, 20.0, 20.0)
                                  : QRectF();
    }

    void append(const QPointF &point, double confidence, bool anomaly, bool breakBefore)
    {
        const QPointF previous = m_previous;
        const bool boundsChange = !m_bounds.isValid() || !m_bounds.contains(point);
        if (boundsChange) prepareGeometryChange();
        if (!m_bounds.isValid()) m_bounds = QRectF(point, QSizeF(0.001, 0.001));
        else if (boundsChange) m_bounds |= QRectF(point, QSizeF(0.001, 0.001));

        if (m_hasPrevious && !breakBefore) {
            QPainterPath *path = &m_unknownPath;
            if (std::isfinite(confidence)) {
                if (confidence >= 0.8) path = &m_highPath;
                else if (confidence >= 0.6) path = &m_mediumPath;
                else path = &m_lowPath;
            }
            path->moveTo(m_previous);
            path->lineTo(point);
        }
        if (anomaly) m_anomalies.append(point);
        m_previous = point;
        m_hasPrevious = true;
        QRectF dirty(point, QSizeF(0.001, 0.001));
        if (!breakBefore && previous != point) dirty |= QRectF(previous, point).normalized();
        update(dirty.adjusted(-1.0, -1.0, 1.0, 1.0));
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *) override
    {
        const qreal lod = std::max<qreal>(0.001,
            QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform()));
        painter->setRenderHint(QPainter::Antialiasing, true);
        drawPath(painter, m_unknownPath, QColor(105, 112, 120, 190));
        drawPath(painter, m_highPath, QColor(36, 164, 85, 215));
        drawPath(painter, m_mediumPath, QColor(239, 174, 29, 225));
        drawPath(painter, m_lowPath, QColor(220, 60, 55, 230));

        QPen anomalyPen(QColor(170, 25, 35), 2.0);
        anomalyPen.setCosmetic(true);
        painter->setPen(anomalyPen);
        painter->setBrush(QColor(255, 255, 255, 160));
        const qreal anomalyRadius = std::clamp<qreal>(6.0 / lod, 0.001, 20.0);
        const QRectF exposed = option ? option->exposedRect : boundingRect();
        const QRectF anomalyExposed = exposed.adjusted(
            -anomalyRadius, -anomalyRadius, anomalyRadius, anomalyRadius);
        for (const QPointF &point : m_anomalies) {
            if (anomalyExposed.contains(point)) {
                painter->drawEllipse(point, anomalyRadius, anomalyRadius);
            }
        }
    }

private:
    static void drawPath(QPainter *painter, const QPainterPath &path, const QColor &color)
    {
        if (path.isEmpty()) return;
        QPen pen(color, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    }

    QRectF m_bounds;
    QPointF m_previous;
    QPainterPath m_highPath;
    QPainterPath m_mediumPath;
    QPainterPath m_lowPath;
    QPainterPath m_unknownPath;
    QPolygonF m_anomalies;
    bool m_hasPrevious = false;
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

        // Render at twice the declared map resolution so level 0 remains crisp
        // during ordinary zoom-in, while lower levels serve overview scales.
        m_pixelsPerMeter = summary->resolution > 0.0
            ? std::clamp(2.0 / summary->resolution, 8.0, 160.0)
            : 20.0;
        m_bucketWorldSize = TilePixels / m_pixelsPerMeter;
        m_maxTileLevel = std::clamp(
            int(std::floor(std::log2(m_pixelsPerMeter / MinimumPixelsPerMeter))), 0, 12);
        const int normalPointCount = m_normalPoints.size();
        buildPointBuckets();
        m_normalPoints.clear();
        m_normalPoints.squeeze();

        summary->normalPointCount = normalPointCount;
        summary->stationCount = m_points.size();
        summary->pathCount = m_curves.size() + m_advancedLines.size();
        summary->areaCount = m_areas.size();
    }

    QRectF boundingRect() const override { return m_bounds; }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *) override
    {
        const qreal lod = std::max<qreal>(0.001,
            QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform()));
        drawRasterTiles(painter, option ? option->exposedRect : m_bounds, lod);

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
    static constexpr int TilePixels = 256;
    static constexpr int MaximumCachedTiles = 256;
    static constexpr double MinimumPixelsPerMeter = 0.25;

    struct CachedTile {
        QPixmap pixmap;
        quint64 lastUsed = 0;
    };

    static quint64 gridKey(int x, int y)
    {
        return (quint64(quint32(x)) << 32) | quint32(y);
    }

    quint64 tileKey(int level, int x, int y) const
    {
        return (quint64(level & 0xff) << 56)
             | (quint64(x & 0x0fffffff) << 28)
             | quint64(y & 0x0fffffff);
    }

    void buildPointBuckets()
    {
        m_pointBuckets.clear();
        m_pointBuckets.reserve(std::max<qsizetype>(64, m_normalPoints.size() / 32));
        for (const QPointF &point : m_normalPoints) {
            const int x = int(std::floor((point.x() - m_bounds.left()) / m_bucketWorldSize));
            const int y = int(std::floor((point.y() - m_bounds.top()) / m_bucketWorldSize));
            m_pointBuckets[gridKey(x, y)].append(point);
        }
    }

    int tileLevelForLod(qreal lod) const
    {
        if (lod <= 0.0 || m_pixelsPerMeter <= lod) return 0;
        return std::clamp(int(std::lround(std::log2(m_pixelsPerMeter / lod))),
                          0, m_maxTileLevel);
    }

    QPixmap renderTile(int level, int tileX, int tileY)
    {
        const double pixelsPerMeter = m_pixelsPerMeter / double(quint64(1) << level);
        const double tileWorldSize = TilePixels / pixelsPerMeter;
        const QRectF tileRect(m_bounds.left() + tileX * tileWorldSize,
                              m_bounds.top() + tileY * tileWorldSize,
                              tileWorldSize, tileWorldSize);

        QImage image(TilePixels, TilePixels, QImage::Format_RGB32);
        image.fill(QColor(250, 251, 252));
        QPainter tilePainter(&image);
        tilePainter.setRenderHint(QPainter::Antialiasing, false);
        tilePainter.setPen(QPen(QColor(45, 51, 57, 205), 1.0));

        const int firstBucketX = std::max(0, int(std::floor(
            (tileRect.left() - m_bounds.left()) / m_bucketWorldSize)));
        const int firstBucketY = std::max(0, int(std::floor(
            (tileRect.top() - m_bounds.top()) / m_bucketWorldSize)));
        const int lastBucketX = std::max(firstBucketX, int(std::floor(
            (tileRect.right() - m_bounds.left()) / m_bucketWorldSize)));
        const int lastBucketY = std::max(firstBucketY, int(std::floor(
            (tileRect.bottom() - m_bounds.top()) / m_bucketWorldSize)));

        QPolygonF pixels;
        for (int bucketY = firstBucketY; bucketY <= lastBucketY; ++bucketY) {
            for (int bucketX = firstBucketX; bucketX <= lastBucketX; ++bucketX) {
                const auto found = m_pointBuckets.constFind(gridKey(bucketX, bucketY));
                if (found == m_pointBuckets.cend()) continue;
                for (const QPointF &point : found.value()) {
                    if (!tileRect.contains(point)) continue;
                    pixels.append(QPointF((point.x() - tileRect.left()) * pixelsPerMeter,
                                          (point.y() - tileRect.top()) * pixelsPerMeter));
                }
            }
        }
        if (!pixels.isEmpty()) tilePainter.drawPoints(pixels);

        tilePainter.setPen(QPen(QColor(35, 40, 45), 1.0));
        for (const QLineF &line : m_normalLines) {
            const double margin = 1.0 / pixelsPerMeter;
            const QRectF lineBounds = QRectF(line.p1(), line.p2()).normalized()
                                          .adjusted(-margin, -margin, margin, margin);
            if (!tileRect.intersects(lineBounds)) continue;
            tilePainter.drawLine(
                QPointF((line.x1() - tileRect.left()) * pixelsPerMeter,
                        (line.y1() - tileRect.top()) * pixelsPerMeter),
                QPointF((line.x2() - tileRect.left()) * pixelsPerMeter,
                        (line.y2() - tileRect.top()) * pixelsPerMeter));
        }
        tilePainter.end();
        return QPixmap::fromImage(std::move(image));
    }

    void evictOldestTile()
    {
        if (m_tileCache.size() < MaximumCachedTiles) return;
        auto oldest = m_tileCache.begin();
        for (auto it = m_tileCache.begin(); it != m_tileCache.end(); ++it) {
            if (it.value().lastUsed < oldest.value().lastUsed) oldest = it;
        }
        m_tileCache.erase(oldest);
    }

    void drawRasterTiles(QPainter *painter, const QRectF &exposedRect, qreal lod)
    {
        const QRectF visible = exposedRect.intersected(m_bounds);
        if (visible.isEmpty()) return;

        const int level = tileLevelForLod(lod);
        const double pixelsPerMeter = m_pixelsPerMeter / double(quint64(1) << level);
        const double tileWorldSize = TilePixels / pixelsPerMeter;
        const int columns = std::max(1, int(std::ceil(m_bounds.width() / tileWorldSize)));
        const int rows = std::max(1, int(std::ceil(m_bounds.height() / tileWorldSize)));
        const int firstX = std::clamp(int(std::floor(
            (visible.left() - m_bounds.left()) / tileWorldSize)), 0, columns - 1);
        const int firstY = std::clamp(int(std::floor(
            (visible.top() - m_bounds.top()) / tileWorldSize)), 0, rows - 1);
        const int lastX = std::clamp(int(std::floor(
            (visible.right() - m_bounds.left()) / tileWorldSize)), firstX, columns - 1);
        const int lastY = std::clamp(int(std::floor(
            (visible.bottom() - m_bounds.top()) / tileWorldSize)), firstY, rows - 1);

        painter->save();
        painter->setRenderHint(QPainter::SmoothPixmapTransform,
                               lod <= pixelsPerMeter * 2.0);
        for (int y = firstY; y <= lastY; ++y) {
            for (int x = firstX; x <= lastX; ++x) {
                const quint64 key = tileKey(level, x, y);
                auto found = m_tileCache.find(key);
                if (found == m_tileCache.end()) {
                    evictOldestTile();
                    CachedTile tile;
                    tile.pixmap = renderTile(level, x, y);
                    tile.lastUsed = ++m_tileUseCounter;
                    found = m_tileCache.insert(key, std::move(tile));
                } else {
                    found.value().lastUsed = ++m_tileUseCounter;
                }
                const QRectF target(m_bounds.left() + x * tileWorldSize,
                                    m_bounds.top() + y * tileWorldSize,
                                    tileWorldSize, tileWorldSize);
                painter->drawPixmap(target, found.value().pixmap,
                                    QRectF(0.0, 0.0, TilePixels, TilePixels));
            }
        }
        painter->restore();
    }

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
    QHash<quint64, QPolygonF> m_pointBuckets;
    QHash<quint64, CachedTile> m_tileCache;
    double m_pixelsPerMeter = 20.0;
    double m_bucketWorldSize = 12.8;
    int m_maxTileLevel = 7;
    quint64 m_tileUseCounter = 0;
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
    item->setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
    m_robotPoseItem = nullptr;
    m_robotTrackItem = nullptr;
    m_scene->clear();
    m_scene->addItem(item);
    m_mapBounds = item->boundingRect();
    m_nativePixelsPerMeter = summary->resolution > 0.0
        ? std::clamp(2.0 / summary->resolution, 8.0, 160.0)
        : 20.0;
    m_maxZoom = std::min<qreal>(800.0, m_nativePixelsPerMeter * 4.0);
    m_scene->setSceneRect(m_mapBounds);
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
    m_maxZoom = std::min<qreal>(800.0,
        std::max(m_nativePixelsPerMeter * 4.0, transform().m11() * 4.0));
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

void MapView::appendRobotTrackSample(double x, double y, double confidence,
                                     bool anomaly, bool breakBefore)
{
    if (!m_hasMap || !std::isfinite(x) || !std::isfinite(y)) return;
    if (!m_robotTrackItem) {
        auto *item = new RobotTrackItem;
        item->setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
        item->setZValue(900.0);
        m_scene->addItem(item);
        m_robotTrackItem = item;
    }
    static_cast<RobotTrackItem *>(m_robotTrackItem)->append(
        QPointF(x, -y), confidence, anomaly, breakBefore);
}

void MapView::clearRobotTrack()
{
    if (!m_robotTrackItem) return;
    m_scene->removeItem(m_robotTrackItem);
    delete m_robotTrackItem;
    m_robotTrackItem = nullptr;
}

bool MapView::containsMapPosition(double x, double y) const
{
    return m_hasMap && std::isfinite(x) && std::isfinite(y)
        && m_mapBounds.contains(QPointF(x, -y));
}

void MapView::wheelEvent(QWheelEvent *event)
{
    if (!m_hasMap) return QGraphicsView::wheelEvent(event);
    const qreal factor = event->angleDelta().y() > 0 ? 1.2 : (1.0 / 1.2);
    const qreal current = transform().m11();
    const qreal target = std::clamp(current * factor, qreal(0.01), m_maxZoom);
    if (!qFuzzyCompare(target, current)) scale(target / current, target / current);
    event->accept();
}

void MapView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_firstResizeAfterLoad) fitMap();
}
