#include "mapeditor.h"

#include <QApplication>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsSceneHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr qreal sceneExtent = 131072.0;
constexpr qreal vertexRadiusPixels = 3.5;
constexpr qreal hoveredVertexRadiusPixels = 5.0;
constexpr qreal snapRadiusPixels = 10.0;
constexpr qreal wallHitWidth = 10.0;

const QColor wallColor(226, 231, 240);
const QColor vertexColor(255, 190, 72);
const QColor hoverColor(80, 210, 255);
const QColor selectedColor(255, 110, 92);
const QColor sectorColor(50, 116, 158, 38);

QPen cosmeticPen(const QColor &color, qreal width)
{
    QPen pen(color, width);
    pen.setCosmetic(true);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

class WallItem final : public QGraphicsLineItem
{
public:
    explicit WallItem(const QLineF &line)
        : QGraphicsLineItem(line)
    {
        setPen(cosmeticPen(wallColor, 1.6));
        setInteractive(false);
    }

    void setInteractive(bool interactive)
    {
        setFlag(QGraphicsItem::ItemIsSelectable, interactive);
        setAcceptHoverEvents(interactive);
        setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (!interactive) {
            m_hovered = false;
            setSelected(false);
            update();
        }
    }

    QPainterPath shape() const override
    {
        QPainterPath path;
        path.moveTo(line().p1());
        path.lineTo(line().p2());
        QPainterPathStroker stroker;
        stroker.setWidth(wallHitWidth);
        stroker.setCapStyle(Qt::RoundCap);
        return stroker.createStroke(path);
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override
    {
        m_hovered = true;
        update();
        QGraphicsLineItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override
    {
        m_hovered = false;
        update();
        QGraphicsLineItem::hoverLeaveEvent(event);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        const QColor color = isSelected() ? selectedColor : (m_hovered ? hoverColor : wallColor);
        painter->setPen(cosmeticPen(color, (m_hovered || isSelected()) ? 3.0 : 1.6));
        painter->drawLine(line());
    }

private:
    bool m_hovered = false;
};

class VertexItem final : public QGraphicsEllipseItem
{
public:
    VertexItem()
        : QGraphicsEllipseItem(-hoveredVertexRadiusPixels, -hoveredVertexRadiusPixels,
                               hoveredVertexRadiusPixels * 2.0,
                               hoveredVertexRadiusPixels * 2.0)
    {
        setFlag(QGraphicsItem::ItemIgnoresTransformations);
        setInteractive(false);
    }

    void setInteractive(bool interactive)
    {
        setFlag(QGraphicsItem::ItemIsSelectable, interactive);
        setAcceptHoverEvents(interactive);
        setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (!interactive) {
            m_hovered = false;
            setSelected(false);
            update();
        }
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override
    {
        m_hovered = true;
        update();
        QGraphicsEllipseItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override
    {
        m_hovered = false;
        update();
        QGraphicsEllipseItem::hoverLeaveEvent(event);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        const QColor color = isSelected() ? selectedColor : (m_hovered ? hoverColor : vertexColor);
        painter->setPen(cosmeticPen(QColor(24, 26, 31), 1.0));
        painter->setBrush(color);
        const qreal radius = (m_hovered || isSelected())
            ? hoveredVertexRadiusPixels : vertexRadiusPixels;
        painter->drawEllipse(QPointF(0.0, 0.0), radius, radius);
    }

private:
    bool m_hovered = false;
};

class SectorItem final : public QGraphicsPolygonItem
{
public:
    explicit SectorItem(const QPolygonF &polygon)
        : QGraphicsPolygonItem(polygon)
    {
        setPen(Qt::NoPen);
        setInteractive(false);
    }

    void setInteractive(bool interactive)
    {
        setFlag(QGraphicsItem::ItemIsSelectable, interactive);
        setAcceptHoverEvents(interactive);
        setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (!interactive) {
            m_hovered = false;
            setSelected(false);
            update();
        }
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override
    {
        m_hovered = true;
        update();
        QGraphicsPolygonItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override
    {
        m_hovered = false;
        update();
        QGraphicsPolygonItem::hoverLeaveEvent(event);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        QColor color = sectorColor;
        if (isSelected()) {
            color = selectedColor;
            color.setAlpha(105);
        } else if (m_hovered) {
            color = hoverColor;
            color.setAlpha(80);
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawPolygon(polygon());
    }

private:
    bool m_hovered = false;
};
}

MapScene::MapScene(QObject *parent)
    : QGraphicsScene(parent)
{
    setSceneRect(-sceneExtent, -sceneExtent, sceneExtent * 2.0, sceneExtent * 2.0);
    setItemIndexMethod(QGraphicsScene::BspTreeIndex);
}

void MapScene::setGridSize(qreal size)
{
    m_gridSize = std::clamp(size, 1.0, 4096.0);
    invalidate(sceneRect(), QGraphicsScene::BackgroundLayer);
}

void MapScene::setGridVisible(bool visible)
{
    if (m_gridVisible == visible) {
        return;
    }
    m_gridVisible = visible;
    invalidate(sceneRect(), QGraphicsScene::BackgroundLayer);
}

void MapScene::paintBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, QColor(24, 26, 31));

    if (!m_gridVisible) {
        return;
    }

    const qreal visibleSpacing = m_gridSize;

    const qreal left = std::floor(rect.left() / visibleSpacing) * visibleSpacing;
    const qreal top = std::floor(rect.top() / visibleSpacing) * visibleSpacing;

    QList<QLineF> minorLines;
    QList<QLineF> majorLines;
    int xIndex = static_cast<int>(std::llround(left / visibleSpacing));
    for (qreal x = left; x <= rect.right(); x += visibleSpacing, ++xIndex) {
        if (xIndex % 8 == 0) {
            majorLines.append(QLineF(x, rect.top(), x, rect.bottom()));
        } else {
            minorLines.append(QLineF(x, rect.top(), x, rect.bottom()));
        }
    }

    int yIndex = static_cast<int>(std::llround(top / visibleSpacing));
    for (qreal y = top; y <= rect.bottom(); y += visibleSpacing, ++yIndex) {
        if (yIndex % 8 == 0) {
            majorLines.append(QLineF(rect.left(), y, rect.right(), y));
        } else {
            minorLines.append(QLineF(rect.left(), y, rect.right(), y));
        }
    }

    painter->setPen(cosmeticPen(QColor(43, 47, 55), 1.0));
    painter->drawLines(minorLines);
    painter->setPen(cosmeticPen(QColor(60, 66, 77), 1.0));
    painter->drawLines(majorLines);

    painter->setPen(cosmeticPen(QColor(89, 72, 72), 1.25));
    painter->drawLine(QLineF(0.0, rect.top(), 0.0, rect.bottom()));
    painter->setPen(cosmeticPen(QColor(67, 82, 72), 1.25));
    painter->drawLine(QLineF(rect.left(), 0.0, rect.right(), 0.0));
}

MapEditor::MapEditor(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new MapScene(this))
{
    setScene(m_scene);
    setBackgroundBrush(QColor(24, 26, 31));
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::NoDrag);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    scale(1.0, -1.0);
    centerOn(0.0, 0.0);
    rebuildScene();
}

void MapEditor::setStatusCallback(std::function<void(const QString &)> callback)
{
    m_statusCallback = std::move(callback);
    reportStatus("Draw: left click | Finish: right click/Enter | Cancel: Esc | Pan: middle mouse");
}

void MapEditor::newMap()
{
    cancelDrawing();
    m_document.clear();
    rebuildScene();
    reportStatus("New map");
}

void MapEditor::setMode(Mode mode)
{
    if (m_mode == mode) {
        return;
    }

    cancelDrawing();
    m_mode = mode;
    m_scene->clearSelection();

    for (QGraphicsItem *item : m_scene->items()) {
        if (auto *wall = dynamic_cast<WallItem *>(item)) {
            wall->setInteractive(mode == Mode::Lines);
        } else if (auto *vertex = dynamic_cast<VertexItem *>(item)) {
            vertex->setInteractive(mode == Mode::Vertices);
        } else if (auto *sector = dynamic_cast<SectorItem *>(item)) {
            sector->setInteractive(mode == Mode::Sectors);
        }
    }
}

void MapEditor::setGridSize(qreal size)
{
    m_scene->setGridSize(size);
}

void MapEditor::setGridVisible(bool visible)
{
    m_scene->setGridVisible(visible);
    reportStatus(visible ? "Grid shown" : "Grid hidden");
}

bool MapEditor::isGridVisible() const
{
    return m_scene->isGridVisible();
}

QPointF MapEditor::snappedPosition(const QPoint &viewportPosition, bool disableSnapping) const
{
    const QPointF scenePosition = mapToScene(viewportPosition);
    if (disableSnapping) {
        return scenePosition;
    }

    const qreal sceneTolerance = snapRadiusPixels / std::max(std::abs(transform().m11()), 0.0001);
    qreal nearestDistance = sceneTolerance;
    QPointF result;
    bool foundVertex = false;
    for (const MapDocument::Vertex &vertex : m_document.vertices()) {
        const qreal distance = QLineF(scenePosition, vertex.position).length();
        if (distance <= nearestDistance) {
            nearestDistance = distance;
            result = vertex.position;
            foundVertex = true;
        }
    }

    for (const QPointF &point : m_drawingPoints) {
        const qreal distance = QLineF(scenePosition, point).length();
        if (distance <= nearestDistance) {
            nearestDistance = distance;
            result = point;
            foundVertex = true;
        }
    }

    if (foundVertex) {
        return result;
    }

    const qreal grid = m_scene->gridSize();
    return QPointF(std::round(scenePosition.x() / grid) * grid,
                   std::round(scenePosition.y() / grid) * grid);
}

void MapEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanPosition = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (m_mode != Mode::Draw) {
            QGraphicsView::mousePressEvent(event);
            return;
        }
        if (m_drawingPoints.empty()) {
            QGraphicsItem *item = itemAt(event->position().toPoint());
            if (item && item->flags().testFlag(QGraphicsItem::ItemIsSelectable)) {
                QGraphicsView::mousePressEvent(event);
                return;
            }
        }
        const bool disableSnapping = event->modifiers().testFlag(Qt::AltModifier);
        addDrawingPoint(snappedPosition(event->position().toPoint(), disableSnapping));
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton && !m_drawingPoints.empty()) {
        finishDrawing(false);
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void MapEditor::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint delta = event->position().toPoint() - m_lastPanPosition;
        m_lastPanPosition = event->position().toPoint();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    const bool disableSnapping = event->modifiers().testFlag(Qt::AltModifier);
    const QPointF position = snappedPosition(event->position().toPoint(), disableSnapping);
    updatePreview(position);

    if (!m_drawingPoints.empty()) {
        const qreal length = QLineF(m_drawingPoints.back(), position).length();
        const qreal angle = QLineF(m_drawingPoints.back(), position).angle();
        reportStatus(QString("Drawing | X %1  Y %2 | Length %3 | Angle %4°")
                         .arg(position.x(), 0, 'f', 0)
                         .arg(position.y(), 0, 'f', 0)
                         .arg(length, 0, 'f', 1)
                         .arg(angle, 0, 'f', 1));
    } else {
        reportStatus(QString("X %1  Y %2 | Grid %3")
                         .arg(position.x(), 0, 'f', 0)
                         .arg(position.y(), 0, 'f', 0)
                         .arg(m_scene->gridSize(), 0, 'f', 0));
    }

    QGraphicsView::mouseMoveEvent(event);
}

void MapEditor::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void MapEditor::wheelEvent(QWheelEvent *event)
{
    const QPointF before = mapToScene(event->position().toPoint());
    const qreal factor = std::pow(1.0015, event->angleDelta().y());
    const qreal currentScale = std::abs(transform().m11());
    const qreal targetScale = std::clamp(currentScale * factor, 0.02, 64.0);
    scale(targetScale / currentScale, targetScale / currentScale);
    const QPointF after = mapToScene(event->position().toPoint());
    translate(after.x() - before.x(), after.y() - before.y());
    event->accept();
}

void MapEditor::drawBackground(QPainter *painter, const QRectF &rect)
{
    m_scene->paintBackground(painter, rect);
}

void MapEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && !m_drawingPoints.empty()) {
        cancelDrawing();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Backspace && !m_drawingPoints.empty()) {
        m_drawingPoints.pop_back();
        if (m_drawingPoints.empty()) {
            cancelDrawing();
        } else {
            updatePreview(m_drawingPoints.back());
        }
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && !m_drawingPoints.empty()) {
        finishDrawing(false);
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void MapEditor::addDrawingPoint(const QPointF &position)
{
    if (!m_drawingPoints.empty() && position == m_drawingPoints.back()) {
        return;
    }

    if (m_drawingPoints.size() >= 3 && position == m_drawingPoints.front()) {
        finishDrawing(true);
        return;
    }

    m_drawingPoints.push_back(position);
    updatePreview(position);
}

void MapEditor::finishDrawing(bool close)
{
    if (m_drawingPoints.size() >= 2) {
        m_document.addPolyline(m_drawingPoints, close);
    }
    m_drawingPoints.clear();
    rebuildScene();
    reportStatus(close ? "Sector created" : "Line chain created");
}

void MapEditor::cancelDrawing()
{
    m_drawingPoints.clear();
    if (m_previewItem) {
        m_scene->removeItem(m_previewItem);
        delete m_previewItem;
        m_previewItem = nullptr;
    }
    reportStatus("Drawing cancelled");
}

void MapEditor::updatePreview(const QPointF &cursorPosition)
{
    if (m_drawingPoints.empty()) {
        return;
    }

    QPainterPath path(m_drawingPoints.front());
    for (std::size_t index = 1; index < m_drawingPoints.size(); ++index) {
        path.lineTo(m_drawingPoints[index]);
    }
    path.lineTo(cursorPosition);

    if (!m_previewItem) {
        m_previewItem = m_scene->addPath(path, cosmeticPen(QColor(80, 210, 255), 2.0));
        m_previewItem->setZValue(20.0);
    } else {
        m_previewItem->setPath(path);
    }
}

void MapEditor::rebuildScene()
{
    m_scene->clear();
    m_previewItem = nullptr;

    for (const MapDocument::Sector &sector : m_document.sectors()) {
        QPolygonF polygon;
        for (const MapDocument::VertexId vertexId : sector.vertices) {
            polygon.append(m_document.vertices()[vertexId].position);
        }
        auto *item = new SectorItem(polygon);
        item->setInteractive(m_mode == Mode::Sectors);
        m_scene->addItem(item);
        item->setZValue(-10.0);
    }

    for (const MapDocument::Wall &wall : m_document.walls()) {
        const QPointF start = m_document.vertices()[wall.start].position;
        const QPointF end = m_document.vertices()[wall.end].position;
        auto *item = new WallItem(QLineF(start, end));
        item->setInteractive(m_mode == Mode::Lines);
        m_scene->addItem(item);
        item->setZValue(1.0);
    }

    for (const MapDocument::Vertex &vertex : m_document.vertices()) {
        auto *item = new VertexItem();
        item->setInteractive(m_mode == Mode::Vertices);
        m_scene->addItem(item);
        item->setPos(vertex.position);
        item->setZValue(10.0);
    }
}

void MapEditor::reportStatus(const QString &message) const
{
    if (m_statusCallback) {
        m_statusCallback(message);
    }
}
