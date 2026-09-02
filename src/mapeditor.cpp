#include "mapeditor.h"

#include <QApplication>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsSceneHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QScrollBar>
#include <QStyleOption>
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
constexpr int vertexIdRole = Qt::UserRole;
constexpr int wallIdRole = Qt::UserRole + 1;
constexpr int sectorIdRole = Qt::UserRole + 2;
constexpr int spriteIdRole = Qt::UserRole + 3;

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

class OutlineRubberBandStyle final : public QProxyStyle
{
public:
    OutlineRubberBandStyle()
        : QProxyStyle()
    {
    }

    void drawControl(ControlElement element, const QStyleOption *option,
                     QPainter *painter, const QWidget *widget = nullptr) const override
    {
        if (element != CE_RubberBand) {
            QProxyStyle::drawControl(element, option, painter, widget);
            return;
        }

        painter->save();
        painter->setBrush(Qt::NoBrush);
        painter->setPen(cosmeticPen(hoverColor, 1.0));
        painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
        painter->restore();
    }
};

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

class SpriteItem final : public QGraphicsRectItem
{
public:
    explicit SpriteItem(const QImage &texture)
        : QGraphicsRectItem(-22.0, -18.0, 44.0, 36.0)
        , m_texture(texture)
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
        QGraphicsRectItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override
    {
        m_hovered = false;
        update();
        QGraphicsRectItem::hoverLeaveEvent(event);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        const QRectF body = rect();
        QPainterPath rounded;
        rounded.addRoundedRect(body, 8.0, 8.0);
        painter->setClipPath(rounded);
        painter->fillRect(body, QColor(42, 46, 54));
        if (!m_texture.isNull()) {
            painter->drawImage(body.adjusted(4.0, 4.0, -4.0, -4.0), m_texture);
        }
        painter->setClipping(false);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(cosmeticPen(
            isSelected() ? selectedColor : (m_hovered ? hoverColor : vertexColor),
            (isSelected() || m_hovered) ? 2.5 : 1.5));
        painter->drawRoundedRect(body, 8.0, 8.0);
    }

private:
    QImage m_texture;
    bool m_hovered = false;
};

class PlayerStartItem final : public QGraphicsPathItem
{
public:
    PlayerStartItem()
    {
        QPainterPath arrow;
        arrow.moveTo(0.0, -13.0);
        arrow.lineTo(8.0, -3.0);
        arrow.lineTo(3.0, -3.0);
        arrow.lineTo(3.0, 10.0);
        arrow.lineTo(-3.0, 10.0);
        arrow.lineTo(-3.0, -3.0);
        arrow.lineTo(-8.0, -3.0);
        arrow.closeSubpath();
        setPath(arrow);
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
        QGraphicsPathItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override
    {
        m_hovered = false;
        update();
        QGraphicsPathItem::hoverLeaveEvent(event);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setPen(cosmeticPen(QColor(24, 26, 31), 1.5));
        painter->setBrush(isSelected() ? selectedColor
                                       : (m_hovered ? hoverColor : QColor(88, 220, 118)));
        painter->drawPath(path());
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
    painter->fillRect(rect, Qt::black);

    const QRectF gridRect = rect.intersected(sceneRect());
    if (gridRect.isEmpty()) {
        return;
    }
    painter->fillRect(gridRect, QColor(24, 26, 31));

    if (!m_gridVisible) {
        return;
    }

    const qreal visibleSpacing = m_gridSize;

    const qreal left = std::floor(gridRect.left() / visibleSpacing) * visibleSpacing;
    const qreal top = std::floor(gridRect.top() / visibleSpacing) * visibleSpacing;

    QList<QLineF> minorLines;
    QList<QLineF> majorLines;
    int xIndex = static_cast<int>(std::llround(left / visibleSpacing));
    for (qreal x = left; x <= gridRect.right(); x += visibleSpacing, ++xIndex) {
        if (xIndex % 8 == 0) {
            majorLines.append(QLineF(x, gridRect.top(), x, gridRect.bottom()));
        } else {
            minorLines.append(QLineF(x, gridRect.top(), x, gridRect.bottom()));
        }
    }

    int yIndex = static_cast<int>(std::llround(top / visibleSpacing));
    for (qreal y = top; y <= gridRect.bottom(); y += visibleSpacing, ++yIndex) {
        if (yIndex % 8 == 0) {
            majorLines.append(QLineF(gridRect.left(), y, gridRect.right(), y));
        } else {
            minorLines.append(QLineF(gridRect.left(), y, gridRect.right(), y));
        }
    }

    painter->setPen(cosmeticPen(QColor(43, 47, 55), 1.0));
    painter->drawLines(minorLines);
    painter->setPen(cosmeticPen(QColor(60, 66, 77), 1.0));
    painter->drawLines(majorLines);

    painter->setPen(cosmeticPen(QColor(89, 72, 72), 1.25));
    painter->drawLine(QLineF(0.0, gridRect.top(), 0.0, gridRect.bottom()));
    painter->setPen(cosmeticPen(QColor(67, 82, 72), 1.25));
    painter->drawLine(QLineF(gridRect.left(), 0.0, gridRect.right(), 0.0));
}

MapEditor::MapEditor(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new MapScene(this))
{
    setScene(m_scene);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        if (m_mode != Mode::Sprites) {
            return;
        }

        PlayerStartItem *selectedPlayerStart = nullptr;
        bool ordinarySpriteSelected = false;
        for (QGraphicsItem *item : m_scene->selectedItems()) {
            if (auto *playerStart = dynamic_cast<PlayerStartItem *>(item)) {
                selectedPlayerStart = playerStart;
            } else if (dynamic_cast<SpriteItem *>(item)) {
                ordinarySpriteSelected = true;
            }
        }
        if (selectedPlayerStart && ordinarySpriteSelected) {
            selectedPlayerStart->setSelected(false);
        }
    });
    setBackgroundBrush(Qt::black);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::NoDrag);
    setMouseTracking(true);
    auto *rubberBandStyle = new OutlineRubberBandStyle();
    rubberBandStyle->setParent(this);
    setStyle(rubberBandStyle);
    viewport()->setStyle(rubberBandStyle);
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
    reportStatus("Draw: left click | Finish: right click/Enter | Close: click first vertex | Cancel: Esc | Pan: middle mouse");
}

void MapEditor::setTextureSelector(
    std::function<std::optional<SpriteTexture>(std::optional<int>)> selector)
{
    m_textureSelector = std::move(selector);
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
    setDragMode(mode == Mode::Vertices || mode == Mode::Lines || mode == Mode::Sectors
                    || mode == Mode::Sprites
                    ? QGraphicsView::RubberBandDrag
                    : QGraphicsView::NoDrag);

    for (QGraphicsItem *item : m_scene->items()) {
        if (auto *wall = dynamic_cast<WallItem *>(item)) {
            wall->setInteractive(mode == Mode::Lines);
        } else if (auto *vertex = dynamic_cast<VertexItem *>(item)) {
            vertex->setInteractive(mode == Mode::Vertices);
        } else if (auto *sector = dynamic_cast<SectorItem *>(item)) {
            sector->setInteractive(mode == Mode::Sectors);
        } else if (auto *sprite = dynamic_cast<SpriteItem *>(item)) {
            sprite->setInteractive(mode == Mode::Sprites);
        } else if (auto *playerStart = dynamic_cast<PlayerStartItem *>(item)) {
            playerStart->setInteractive(mode == Mode::Sprites);
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

void MapEditor::setZoomPercent(qreal percent)
{
    const qreal currentScale = std::abs(transform().m11());
    const qreal targetScale = std::clamp(percent / 100.0, 0.001, 64.0);
    const QPointF center = mapToScene(viewport()->rect().center());
    scale(targetScale / currentScale, targetScale / currentScale);
    centerOn(center);
    if (m_zoomCallback) {
        m_zoomCallback(targetScale * 100.0);
    }
}

void MapEditor::setZoomCallback(std::function<void(qreal)> callback)
{
    m_zoomCallback = std::move(callback);
    if (m_zoomCallback) {
        m_zoomCallback(std::abs(transform().m11()) * 100.0);
    }
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

    if (event->button() == Qt::RightButton && m_mode == Mode::Sprites) {
        SpriteItem *sprite = nullptr;
        PlayerStartItem *playerStart = nullptr;
        for (QGraphicsItem *item : items(event->position().toPoint())) {
            if ((sprite = dynamic_cast<SpriteItem *>(item))) {
                break;
            }
            if ((playerStart = dynamic_cast<PlayerStartItem *>(item))) {
                break;
            }
        }
        if (!sprite && !playerStart) {
            const QPointF scenePosition = mapToScene(event->position().toPoint());
            if (!m_scene->sceneRect().contains(scenePosition)) {
                reportStatus("Outside Build map coordinate range");
            } else {
                const bool disableSnapping = event->modifiers().testFlag(Qt::AltModifier);
                m_document.addSprite(snappedPosition(
                    event->position().toPoint(), disableSnapping));
                rebuildScene();
                reportStatus("Sprite created");
            }
            event->accept();
            return;
        }

        QGraphicsItem *clickedItem = sprite
            ? static_cast<QGraphicsItem *>(sprite)
            : static_cast<QGraphicsItem *>(playerStart);
        if (!clickedItem->isSelected()) {
            m_scene->clearSelection();
            clickedItem->setSelected(true);
        }
        m_draggingSprites = true;
        m_spriteDragMoved = false;
        m_spriteRightPressPosition = event->position().toPoint();
        m_vertexDragStart = mapToScene(m_spriteRightPressPosition);
        m_clickedPlayerStart = playerStart != nullptr;
        if (sprite) {
            m_clickedSprite = static_cast<MapDocument::SpriteId>(
                sprite->data(spriteIdRole).toULongLong());
        }
        m_draggedSprites.clear();
        m_draggingPlayerStart = false;
        for (QGraphicsItem *selectedItem : m_scene->selectedItems()) {
            if (auto *selectedSprite = dynamic_cast<SpriteItem *>(selectedItem)) {
                const auto spriteId = static_cast<MapDocument::SpriteId>(
                    selectedSprite->data(spriteIdRole).toULongLong());
                m_draggedSprites.emplace_back(
                    spriteId, m_document.sprites()[spriteId].position);
            } else if (dynamic_cast<PlayerStartItem *>(selectedItem)) {
                m_draggingPlayerStart = true;
                m_draggedPlayerStart = m_document.playerStart().position;
            }
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton && m_mode == Mode::Vertices) {
        VertexItem *vertex = nullptr;
        for (QGraphicsItem *item : items(event->position().toPoint())) {
            if ((vertex = dynamic_cast<VertexItem *>(item))) {
                break;
            }
        }
        if (vertex && vertex->isSelected()) {
            m_draggingVertices = true;
            m_vertexDragStart = mapToScene(event->position().toPoint());
            m_draggedVertices.clear();
            for (QGraphicsItem *selectedItem : m_scene->selectedItems()) {
                if (auto *selectedVertex = dynamic_cast<VertexItem *>(selectedItem)) {
                    const auto vertexId = static_cast<MapDocument::VertexId>(
                        selectedVertex->data(vertexIdRole).toULongLong());
                    m_draggedVertices.emplace_back(vertexId, selectedVertex->pos());
                }
            }
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::RightButton && m_mode == Mode::Lines) {
        WallItem *wall = nullptr;
        for (QGraphicsItem *item : items(event->position().toPoint())) {
            if ((wall = dynamic_cast<WallItem *>(item))) {
                break;
            }
        }
        if (wall && wall->isSelected()) {
            m_draggingVertices = true;
            m_vertexDragStart = mapToScene(event->position().toPoint());
            m_draggedVertices.clear();
            m_draggedWalls.clear();

            std::vector<MapDocument::VertexId> vertexIds;
            for (QGraphicsItem *selectedItem : m_scene->selectedItems()) {
                if (auto *selectedWall = dynamic_cast<WallItem *>(selectedItem)) {
                    const auto wallId = static_cast<MapDocument::WallId>(
                        selectedWall->data(wallIdRole).toULongLong());
                    m_draggedWalls.push_back(wallId);
                    const MapDocument::Wall &selectedDocumentWall = m_document.walls()[wallId];
                    for (MapDocument::VertexId vertexId
                         : {selectedDocumentWall.start, selectedDocumentWall.end}) {
                        if (std::find(vertexIds.begin(), vertexIds.end(), vertexId) == vertexIds.end()) {
                            vertexIds.push_back(vertexId);
                            m_draggedVertices.emplace_back(
                                vertexId, m_document.vertices()[vertexId].position);
                        }
                    }
                }
            }
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::RightButton && m_mode == Mode::Sectors) {
        SectorItem *sector = nullptr;
        for (QGraphicsItem *item : items(event->position().toPoint())) {
            if ((sector = dynamic_cast<SectorItem *>(item))) {
                break;
            }
        }
        if (sector && sector->isSelected()) {
            m_draggingVertices = true;
            m_vertexDragStart = mapToScene(event->position().toPoint());
            m_draggedVertices.clear();
            m_draggedSectors.clear();

            std::vector<MapDocument::VertexId> vertexIds;
            for (QGraphicsItem *selectedItem : m_scene->selectedItems()) {
                if (auto *selectedSector = dynamic_cast<SectorItem *>(selectedItem)) {
                    const std::size_t sectorId = static_cast<std::size_t>(
                        selectedSector->data(sectorIdRole).toULongLong());
                    m_draggedSectors.push_back(sectorId);
                    for (MapDocument::VertexId vertexId
                         : m_document.sectors()[sectorId].vertices) {
                        if (std::find(vertexIds.begin(), vertexIds.end(), vertexId) == vertexIds.end()) {
                            vertexIds.push_back(vertexId);
                            m_draggedVertices.emplace_back(
                                vertexId, m_document.vertices()[vertexId].position);
                        }
                    }
                }
            }
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::LeftButton) {
        if (m_mode == Mode::Sprites) {
            SpriteItem *sprite = nullptr;
            PlayerStartItem *playerStart = nullptr;
            for (QGraphicsItem *item : items(event->position().toPoint())) {
                if ((sprite = dynamic_cast<SpriteItem *>(item))) {
                    break;
                }
                if ((playerStart = dynamic_cast<PlayerStartItem *>(item))) {
                    break;
                }
            }
            const bool extendSelection = event->modifiers().testFlag(Qt::ShiftModifier);
            QGraphicsItem *clickedItem = sprite
                ? static_cast<QGraphicsItem *>(sprite)
                : static_cast<QGraphicsItem *>(playerStart);
            if (clickedItem) {
                if (!extendSelection) {
                    m_scene->clearSelection();
                }
                clickedItem->setSelected(
                    extendSelection ? !clickedItem->isSelected() : true);
                event->accept();
                return;
            }
            QGraphicsView::mousePressEvent(event);
            return;
        }

        if (m_mode == Mode::Vertices) {
            VertexItem *vertex = nullptr;
            for (QGraphicsItem *item : items(event->position().toPoint())) {
                if ((vertex = dynamic_cast<VertexItem *>(item))) {
                    break;
                }
            }
            const bool extendSelection = event->modifiers().testFlag(Qt::ShiftModifier);

            if (vertex) {
                if (!extendSelection) {
                    m_scene->clearSelection();
                }
                vertex->setSelected(extendSelection ? !vertex->isSelected() : true);
                event->accept();
                return;
            }

            QGraphicsView::mousePressEvent(event);
            return;
        }

        if (m_mode == Mode::Lines) {
            WallItem *wall = nullptr;
            for (QGraphicsItem *item : items(event->position().toPoint())) {
                if ((wall = dynamic_cast<WallItem *>(item))) {
                    break;
                }
            }
            const bool extendSelection = event->modifiers().testFlag(Qt::ShiftModifier);

            if (wall) {
                if (!extendSelection) {
                    m_scene->clearSelection();
                }
                wall->setSelected(extendSelection ? !wall->isSelected() : true);
                event->accept();
                return;
            }

            QGraphicsView::mousePressEvent(event);
            return;
        }

        if (m_mode == Mode::Sectors) {
            SectorItem *sector = nullptr;
            for (QGraphicsItem *item : items(event->position().toPoint())) {
                if ((sector = dynamic_cast<SectorItem *>(item))) {
                    break;
                }
            }
            const bool extendSelection = event->modifiers().testFlag(Qt::ShiftModifier);

            if (sector) {
                if (!extendSelection) {
                    m_scene->clearSelection();
                }
                sector->setSelected(extendSelection ? !sector->isSelected() : true);
                event->accept();
                return;
            }

            QGraphicsView::mousePressEvent(event);
            return;
        }

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
        if (!m_scene->sceneRect().contains(mapToScene(event->position().toPoint()))) {
            reportStatus("Outside Build map coordinate range");
            event->accept();
            return;
        }
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
    if (m_draggingSprites) {
        const QPoint viewportDelta = event->position().toPoint()
            - m_spriteRightPressPosition;
        if (!m_spriteDragMoved
                && viewportDelta.manhattanLength() < QApplication::startDragDistance()) {
            event->accept();
            return;
        }
        m_spriteDragMoved = true;
        setCursor(Qt::ClosedHandCursor);
        QPointF delta = mapToScene(event->position().toPoint()) - m_vertexDragStart;
        qreal minimumX = std::numeric_limits<qreal>::max();
        qreal maximumX = std::numeric_limits<qreal>::lowest();
        qreal minimumY = std::numeric_limits<qreal>::max();
        qreal maximumY = std::numeric_limits<qreal>::lowest();
        for (const auto &[spriteId, originalPosition] : m_draggedSprites) {
            Q_UNUSED(spriteId);
            minimumX = std::min(minimumX, originalPosition.x());
            maximumX = std::max(maximumX, originalPosition.x());
            minimumY = std::min(minimumY, originalPosition.y());
            maximumY = std::max(maximumY, originalPosition.y());
        }
        if (m_draggingPlayerStart) {
            minimumX = std::min(minimumX, m_draggedPlayerStart.x());
            maximumX = std::max(maximumX, m_draggedPlayerStart.x());
            minimumY = std::min(minimumY, m_draggedPlayerStart.y());
            maximumY = std::max(maximumY, m_draggedPlayerStart.y());
        }
        const QRectF bounds = m_scene->sceneRect();
        delta.setX(std::clamp(delta.x(), bounds.left() - minimumX,
                              bounds.right() - maximumX));
        delta.setY(std::clamp(delta.y(), bounds.top() - minimumY,
                              bounds.bottom() - maximumY));

        std::vector<std::pair<MapDocument::SpriteId, QPointF>> positions;
        positions.reserve(m_draggedSprites.size());
        for (const auto &[spriteId, originalPosition] : m_draggedSprites) {
            positions.emplace_back(spriteId, originalPosition + delta);
        }
        m_document.setSpritePositions(positions);
        if (m_draggingPlayerStart) {
            m_document.setPlayerStartPosition(m_draggedPlayerStart + delta);
        }
        rebuildScene();
        for (QGraphicsItem *item : m_scene->items()) {
            if (auto *playerStart = dynamic_cast<PlayerStartItem *>(item)) {
                playerStart->setSelected(m_draggingPlayerStart);
                continue;
            }
            auto *sprite = dynamic_cast<SpriteItem *>(item);
            if (!sprite) {
                continue;
            }
            const auto spriteId = static_cast<MapDocument::SpriteId>(
                sprite->data(spriteIdRole).toULongLong());
            const bool wasDragged = std::any_of(
                m_draggedSprites.begin(), m_draggedSprites.end(),
                [spriteId](const auto &entry) { return entry.first == spriteId; });
            sprite->setSelected(wasDragged);
        }
        event->accept();
        return;
    }

    if (m_draggingVertices) {
        QPointF delta = mapToScene(event->position().toPoint()) - m_vertexDragStart;
        qreal minimumX = std::numeric_limits<qreal>::max();
        qreal maximumX = std::numeric_limits<qreal>::lowest();
        qreal minimumY = std::numeric_limits<qreal>::max();
        qreal maximumY = std::numeric_limits<qreal>::lowest();
        for (const auto &[vertexId, originalPosition] : m_draggedVertices) {
            Q_UNUSED(vertexId);
            minimumX = std::min(minimumX, originalPosition.x());
            maximumX = std::max(maximumX, originalPosition.x());
            minimumY = std::min(minimumY, originalPosition.y());
            maximumY = std::max(maximumY, originalPosition.y());
        }
        const QRectF bounds = m_scene->sceneRect();
        delta.setX(std::clamp(delta.x(), bounds.left() - minimumX,
                              bounds.right() - maximumX));
        delta.setY(std::clamp(delta.y(), bounds.top() - minimumY,
                              bounds.bottom() - maximumY));
        std::vector<std::pair<MapDocument::VertexId, QPointF>> positions;
        positions.reserve(m_draggedVertices.size());
        for (const auto &[vertexId, originalPosition] : m_draggedVertices) {
            positions.emplace_back(vertexId, originalPosition + delta);
        }
        m_document.setVertexPositions(positions);
        rebuildScene();

        for (QGraphicsItem *item : m_scene->items()) {
            if (m_mode == Mode::Vertices) {
                auto *vertex = dynamic_cast<VertexItem *>(item);
                if (!vertex) {
                    continue;
                }
                const auto vertexId = static_cast<MapDocument::VertexId>(
                    vertex->data(vertexIdRole).toULongLong());
                const bool wasDragged = std::any_of(
                    m_draggedVertices.begin(), m_draggedVertices.end(),
                    [vertexId](const auto &entry) { return entry.first == vertexId; });
                vertex->setSelected(wasDragged);
            } else if (m_mode == Mode::Lines) {
                auto *wall = dynamic_cast<WallItem *>(item);
                if (!wall) {
                    continue;
                }
                const auto wallId = static_cast<MapDocument::WallId>(
                    wall->data(wallIdRole).toULongLong());
                wall->setSelected(std::find(m_draggedWalls.begin(), m_draggedWalls.end(), wallId)
                                  != m_draggedWalls.end());
            } else if (m_mode == Mode::Sectors) {
                auto *sector = dynamic_cast<SectorItem *>(item);
                if (!sector) {
                    continue;
                }
                const std::size_t sectorId = static_cast<std::size_t>(
                    sector->data(sectorIdRole).toULongLong());
                sector->setSelected(
                    std::find(m_draggedSectors.begin(), m_draggedSectors.end(), sectorId)
                    != m_draggedSectors.end());
            }
        }
        event->accept();
        return;
    }

    if (m_panning) {
        const QPoint delta = event->position().toPoint() - m_lastPanPosition;
        m_lastPanPosition = event->position().toPoint();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    const bool disableSnapping = event->modifiers().testFlag(Qt::AltModifier);
    QPointF position = snappedPosition(event->position().toPoint(), disableSnapping);
    const QRectF bounds = m_scene->sceneRect();
    position.setX(std::clamp(position.x(), bounds.left(), bounds.right()));
    position.setY(std::clamp(position.y(), bounds.top(), bounds.bottom()));
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
    if (event->button() == Qt::RightButton && m_draggingSprites) {
        const bool chooseTexture = !m_spriteDragMoved && !m_clickedPlayerStart;
        const MapDocument::SpriteId spriteId = m_clickedSprite;
        m_draggingSprites = false;
        m_spriteDragMoved = false;
        m_draggedSprites.clear();
        m_draggingPlayerStart = false;
        m_clickedPlayerStart = false;
        setCursor(Qt::CrossCursor);

        if (chooseTexture && spriteId < m_document.sprites().size()
                && m_textureSelector) {
            const int currentTexture = m_document.sprites()[spriteId].texture;
            const std::optional<SpriteTexture> selection = m_textureSelector(
                currentTexture >= 0 ? std::optional<int>(currentTexture)
                                    : std::nullopt);
            if (selection) {
                m_document.setSpriteTexture(spriteId, selection->tile);
                m_spriteTextures.insert(selection->tile, selection->image);
                rebuildScene();
                for (QGraphicsItem *item : m_scene->items()) {
                    auto *sprite = dynamic_cast<SpriteItem *>(item);
                    if (sprite && static_cast<MapDocument::SpriteId>(
                            sprite->data(spriteIdRole).toULongLong()) == spriteId) {
                        sprite->setSelected(true);
                        break;
                    }
                }
                reportStatus(QString("Sprite texture set to tile %1")
                                 .arg(selection->tile));
            }
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton && m_draggingVertices) {
        m_draggingVertices = false;
        m_draggedVertices.clear();
        m_draggedWalls.clear();
        m_draggedSectors.clear();
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }

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
    const qreal targetScale = std::clamp(currentScale * factor, 0.001, 64.0);
    scale(targetScale / currentScale, targetScale / currentScale);
    const QPointF after = mapToScene(event->position().toPoint());
    translate(after.x() - before.x(), after.y() - before.y());
    if (m_zoomCallback) {
        m_zoomCallback(targetScale * 100.0);
    }
    event->accept();
}

void MapEditor::drawBackground(QPainter *painter, const QRectF &rect)
{
    m_scene->paintBackground(painter, rect);
}

void MapEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete && m_mode == Mode::Sprites) {
        std::vector<MapDocument::SpriteId> spriteIds;
        for (QGraphicsItem *item : m_scene->selectedItems()) {
            if (auto *sprite = dynamic_cast<SpriteItem *>(item)) {
                spriteIds.push_back(static_cast<MapDocument::SpriteId>(
                    sprite->data(spriteIdRole).toULongLong()));
            }
        }
        if (!spriteIds.empty()) {
            m_document.removeSprites(spriteIds);
            rebuildScene();
            reportStatus(QString("%1 sprite(s) deleted").arg(spriteIds.size()));
        }
        event->accept();
        return;
    }
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
    const bool sectorCreated = m_document.addPolyline(m_drawingPoints, close);
    m_drawingPoints.clear();
    rebuildScene();
    reportStatus(sectorCreated ? "Sector created" : "Drawing discarded");
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

    for (std::size_t sectorId = 0; sectorId < m_document.sectors().size(); ++sectorId) {
        const MapDocument::Sector &sector = m_document.sectors()[sectorId];
        QPolygonF polygon;
        for (const MapDocument::VertexId vertexId : sector.vertices) {
            polygon.append(m_document.vertices()[vertexId].position);
        }
        auto *item = new SectorItem(polygon);
        item->setInteractive(m_mode == Mode::Sectors);
        item->setData(sectorIdRole, static_cast<qulonglong>(sectorId));
        m_scene->addItem(item);
        item->setZValue(-10.0);
    }

    for (MapDocument::WallId wallId = 0; wallId < m_document.walls().size(); ++wallId) {
        const MapDocument::Wall &wall = m_document.walls()[wallId];
        const QPointF start = m_document.vertices()[wall.start].position;
        const QPointF end = m_document.vertices()[wall.end].position;
        auto *item = new WallItem(QLineF(start, end));
        item->setInteractive(m_mode == Mode::Lines);
        item->setData(wallIdRole, static_cast<qulonglong>(wallId));
        m_scene->addItem(item);
        item->setZValue(1.0);
    }

    for (MapDocument::VertexId vertexId = 0;
         vertexId < m_document.vertices().size(); ++vertexId) {
        const MapDocument::Vertex &vertex = m_document.vertices()[vertexId];
        auto *item = new VertexItem();
        item->setInteractive(m_mode == Mode::Vertices);
        item->setData(vertexIdRole, static_cast<qulonglong>(vertexId));
        m_scene->addItem(item);
        item->setPos(vertex.position);
        item->setZValue(10.0);
    }

    for (MapDocument::SpriteId spriteId = 0;
         spriteId < m_document.sprites().size(); ++spriteId) {
        const MapDocument::Sprite &sprite = m_document.sprites()[spriteId];
        auto *item = new SpriteItem(m_spriteTextures.value(sprite.texture));
        item->setInteractive(m_mode == Mode::Sprites);
        item->setData(spriteIdRole, static_cast<qulonglong>(spriteId));
        m_scene->addItem(item);
        item->setPos(sprite.position);
        item->setZValue(12.0);
    }

    auto *playerStart = new PlayerStartItem();
    playerStart->setInteractive(m_mode == Mode::Sprites);
    m_scene->addItem(playerStart);
    playerStart->setPos(m_document.playerStart().position);
    playerStart->setZValue(13.0);
}

void MapEditor::reportStatus(const QString &message) const
{
    if (m_statusCallback) {
        m_statusCallback(message);
    }
}
