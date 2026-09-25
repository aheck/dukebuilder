#include "mapeditor.h"
#include "mapsave.h"

#include <QApplication>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QScrollBar>
#include <QStyleOption>
#include <QWheelEvent>
#include <QTimer>
#include <QDateTime>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr qreal sceneExtent = 131072.0;
// 100% is a room-scale working view: 1024 Build units occupy about 82 pixels.
constexpr qreal baseZoomScale = 0.08;
constexpr qreal minimumZoomScale = 0.001;
constexpr qreal maximumZoomScale = 64.0;
constexpr qreal vertexRadiusPixels = 3.5;
constexpr qreal hoveredVertexRadiusPixels = 5.0;
constexpr qreal snapRadiusPixels = 10.0;
constexpr qreal wallHitWidth = 10.0;
constexpr int vertexIdRole = Qt::UserRole;
constexpr int wallIdRole = Qt::UserRole + 1;
constexpr int sectorIdRole = Qt::UserRole + 2;
constexpr int spriteIdRole = Qt::UserRole + 3;

const QColor wallColor(226, 231, 240);
const QColor twoSidedWallColor(235, 55, 65);
const QColor vertexColor(255, 190, 72);
const QColor hoverColor(80, 210, 255);
const QColor selectedColor(255, 110, 92);
const QColor sectorColor(50, 116, 158, 38);

class LineLengthItem final : public QGraphicsSimpleTextItem
{
public:
    explicit LineLengthItem(QGraphicsItem *parent) : QGraphicsSimpleTextItem(parent)
    {
        setFlag(ItemIgnoresTransformations);
        setAcceptedMouseButtons(Qt::NoButton);
        setBrush(QColor(80, 210, 255));
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override
    {
        painter->fillRect(boundingRect(), QColor(24, 26, 31, 235));
        QGraphicsSimpleTextItem::paint(painter, option, widget);
    }
};

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

class WallSideMarker final : public QGraphicsItem
{
public:
    explicit WallSideMarker(QGraphicsItem *parent) : QGraphicsItem(parent)
    {
        setFlag(ItemIgnoresTransformations);
        setAcceptedMouseButtons(Qt::NoButton);
        setZValue(2.0);
    }

    QRectF boundingRect() const override { return {-14.0, -14.0, 28.0, 28.0}; }

    void setDirection(const QPointF &direction)
    {
        m_direction = direction;
        update();
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        if (!scene() || scene()->views().isEmpty()) return;
        // The marker stays pixel-sized, but its direction must follow the view's
        // transform just like the wall does.
        const QTransform transform = parentItem()->deviceTransform(scene()->views().front()->viewportTransform());
        const QPointF direction = transform.map(m_direction) - transform.map(QPointF());
        const qreal length = std::hypot(direction.x(), direction.y());
        if (length == 0.0) return;
        painter->setPen(cosmeticPen(selectedColor, 2.0));
        painter->drawLine(QPointF(), direction / length * 12.0);
    }

private:
    QPointF m_direction;
};

class WallItem final : public QGraphicsLineItem
{
public:
    WallItem(const QLineF &line, bool twoSided)
        : QGraphicsLineItem(line)
        , m_twoSided(twoSided)
    {
        m_sideMarker = new WallSideMarker(this);
        m_sideMarker->setPos(line.center());
        m_sideMarker->hide();
        setPen(cosmeticPen(m_twoSided ? twoSidedWallColor : wallColor, 1.6));
        setToolTip(m_twoSided ? "Two-sided wall between neighboring sectors" : "One-sided wall");
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

    void setFirstWallHighlighted(bool highlighted)
    {
        if (m_firstWallHighlighted == highlighted) return;
        m_firstWallHighlighted = highlighted;
        update();
    }

    void setEditingSide(bool reversed)
    {
        const QPointF normal(-line().dy(), line().dx());
        const QPointF tick = line().length() > 0.0
            ? normal / line().length() * (reversed ? -12.0 : 12.0) : QPointF();
        m_sideMarker->setDirection(tick);
        m_sideMarker->setVisible(isSelected());
        update();
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
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override
    {
        if (change == ItemSelectedHasChanged) m_sideMarker->setVisible(value.toBool());
        return QGraphicsLineItem::itemChange(change, value);
    }

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
        const QColor baseColor = m_firstWallHighlighted ? QColor(180, 120, 255)
            : (m_twoSided ? twoSidedWallColor : wallColor);
        const QColor color = isSelected() ? selectedColor : (m_hovered ? hoverColor : baseColor);
        painter->setPen(cosmeticPen(color, (m_hovered || isSelected() || m_firstWallHighlighted) ? 3.0 : 1.6));
        painter->drawLine(line());

    }

private:
    bool m_twoSided = false;
    bool m_firstWallHighlighted = false;
    WallSideMarker *m_sideMarker = nullptr;
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

class SectorItem final : public QGraphicsPathItem
{
public:
    explicit SectorItem(const QPainterPath &path)
        : QGraphicsPathItem(path)
    {
        setPen(Qt::NoPen);
        setInteractive(false);
    }

    void setTexture(const QImage &image)
    {
        m_texture = image.isNull() ? QBrush(Qt::NoBrush) : QBrush(image);
        // Anchor the repeating preview in map coordinates.
        m_texture.setTransform(QTransform::fromScale(8.0, 8.0));
        update();
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
        painter->setPen(Qt::NoPen);
        if (m_texture.style() != Qt::NoBrush) {
            painter->setBrush(m_texture);
            painter->drawPath(path());
            if (!isSelected() && !m_hovered) return;
        }
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
        painter->drawPath(path());
    }

private:
    QBrush m_texture = Qt::NoBrush;
    bool m_hovered = false;
};

class SpriteItem final : public QGraphicsRectItem
{
public:
    explicit SpriteItem(const QImage &texture, qreal angle)
        : QGraphicsRectItem(-22.0, -18.0, 44.0, 36.0)
        , m_texture(texture)
        , m_monochromeTexture(texture.convertToFormat(QImage::Format_Grayscale8))
        , m_angle(angle)
    {
        setFlag(QGraphicsItem::ItemIgnoresTransformations);
        setInteractive(false);
    }

    void setTexture(const QImage &texture)
    {
        m_texture = texture;
        m_monochromeTexture = texture.convertToFormat(QImage::Format_Grayscale8);
        update();
    }

    void setInteractive(bool interactive)
    {
        m_interactive = interactive;
        setFlag(QGraphicsItem::ItemIsSelectable, interactive);
        setAcceptHoverEvents(interactive);
        setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (!interactive) {
            m_hovered = false;
            setSelected(false);
            update();
        }
    }

    QRectF boundingRect() const override
    {
        return QRectF(-39, -39, 78, 78);
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
        const QImage &texture = m_interactive ? m_texture : m_monochromeTexture;
        if (!texture.isNull()) {
            painter->drawImage(body.adjusted(4.0, 4.0, -4.0, -4.0), texture);
        }
        painter->setClipping(false);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(cosmeticPen(
            isSelected() ? selectedColor : (m_hovered ? hoverColor : vertexColor),
            (isSelected() || m_hovered) ? 2.5 : 1.5));
        painter->drawRoundedRect(body, 8.0, 8.0);
        // Build angle zero points right; positive angles turn clockwise in
        // the map's Y-down coordinates. Keep the thumbnail upright and place
        // a fixed-pixel arrow outside it, visible at every zoom level.
        painter->save();
        painter->rotate(m_angle);
        QPainterPath facing;
        facing.moveTo(35, 0);
        facing.lineTo(26, -5);
        facing.lineTo(26, 5);
        facing.closeSubpath();
        painter->setPen(cosmeticPen(QColor(24,26,31), 1.5));
        painter->setBrush(isSelected() ? selectedColor : (m_hovered ? hoverColor : vertexColor));
        painter->drawPath(facing);
        painter->restore();

    }

private:
    QImage m_texture;
    QImage m_monochromeTexture;
    qreal m_angle = 0;
    bool m_hovered = false;
    bool m_interactive = false;
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

// Snapshots retain topology and references together, including imported maps.
class MapEditor::SnapshotCommand final : public QUndoCommand
{
public:
    SnapshotCommand(MapEditor *editor, Snapshot before, Snapshot after,
                    const QString &label, const QString &key)
        : QUndoCommand(label), editor(editor), before(std::move(before)),
          after(std::move(after)), key(key), time(QDateTime::currentMSecsSinceEpoch()) {}
    void undo() override { editor->restore(*before); }
    void redo() override {
        if (first) { first = false; return; }
        editor->restore(*after);
    }
    int id() const override { return key.isEmpty() ? -1 : 1; }
    bool mergeWith(const QUndoCommand *command) override {
        const auto *other = dynamic_cast<const SnapshotCommand *>(command);
        if (!other || !before || key != other->key || other->time - time > 500) return false;
        after = other->after;
        time = other->time;
        if (before->document == after->document) setObsolete(true);
        return true;
    }
    std::size_t bytes() const {
        const auto size = [](const MapDocument &d) {
            std::size_t n = sizeof(d) + d.vertices().size() * sizeof(MapDocument::Vertex)
                + d.walls().size() * sizeof(MapDocument::Wall)
                + d.sectors().size() * sizeof(MapDocument::Sector)
                + d.sprites().size() * sizeof(MapDocument::Sprite);
            for (const auto &s : d.sectors())
                n += (s.walls.size() + s.vertices.size() + s.loopStarts.size()) * sizeof(std::size_t);
            return n;
        };
        return before ? size(before->document) + size(after->document) : 0;
    }
    void expire() { before.reset(); after.reset(); setObsolete(true); }
private:
    MapEditor *editor;
    std::optional<Snapshot> before, after;
    QString key;
    qint64 time;
    bool first = true;
};

class MapEditor::Edit {
public:
    Edit(MapEditor *editor, const QString &label, const QString &key = {}) : editor(editor) {
        editor->beginEdit(label, key);
    }
    ~Edit() { editor->endEdit(); }
private:
    MapEditor *editor;
};

MapEditor::Selection MapEditor::selection() const
{
    Selection result{{}, m_sectorSelectionOrder, m_mode, m_wallSideReversed};
    for (auto *item : m_scene->selectedItems()) {
        int role = dynamic_cast<VertexItem *>(item) ? vertexIdRole
            : dynamic_cast<WallItem *>(item) ? wallIdRole
            : dynamic_cast<SectorItem *>(item) ? sectorIdRole
            : dynamic_cast<SpriteItem *>(item) ? spriteIdRole : -1;
        result.items.emplace_back(role, role < 0 ? 0 : item->data(role).toULongLong());
    }
    return result;
}

void MapEditor::restore(const Snapshot &snapshot)
{
    cancelDrawing();
    m_draggingVertices = m_draggingSprites = m_draggingPlayerStart = false;
    m_draggedVertices.clear(); m_draggedWalls.clear(); m_draggedSectors.clear();
    m_draggedSprites.clear();
    m_document = snapshot.document;
    m_wallSideReversed = snapshot.selection.reversed;
    for (const auto &sprite : m_document.sprites()) {
        const int key = sprite.texture * 256 + sprite.palette;
        if (m_textureResolver && sprite.texture >= 0 && !m_spriteTextures.contains(key))
            m_spriteTextures.insert(key, m_textureResolver(sprite.texture, sprite.palette));
    }
    rebuildScene();
    // Keep the current mode, camera and zoom. Restore selection if its mode is active.
    if (m_mode == snapshot.selection.mode) {
        const QSignalBlocker blocker(m_scene);
        for (auto *item : m_scene->items()) {
            for (const auto &[role, id] : snapshot.selection.items) {
                if ((role < 0 && dynamic_cast<PlayerStartItem *>(item))
                    || (role >= 0 && item->data(role).isValid() && item->data(role).toULongLong() == id))
                    item->setSelected(true);
            }
        }
        m_sectorSelectionOrder = snapshot.selection.sectorOrder;
    }
    updateProperties();
    if (joinAvailabilityChanged) joinAvailabilityChanged(canJoinSelectedSectors());
    if (documentRestored) documentRestored();
}

void MapEditor::beginEdit(const QString &label, const QString &key)
{
    if (m_editDepth++ != 0) return;
    m_beforeEdit = Snapshot{m_document, selection()};
    m_editLabel = label;
    m_editKey = key.isEmpty() ? QString{} : key + ":" + QString::number(m_historyGeneration);
}

void MapEditor::endEdit()
{
    if (--m_editDepth != 0) return;
    auto before = std::move(*m_beforeEdit);
    m_beforeEdit.reset();
    if (before.document == m_document) return;
    auto afterSelection = selection();
    if (m_editLabel.startsWith("Change") && afterSelection.items.empty())
        afterSelection = before.selection;
    m_undoStack.push(new SnapshotCommand(this, std::move(before),
        Snapshot{m_document, std::move(afterSelection)}, m_editLabel, m_editKey));
    // Drop oldest payloads above 128 MiB; always retain the newest operation.
    std::size_t bytes = 0;
    for (int i = m_undoStack.count() - 1; i >= 0; --i) {
        auto *command = const_cast<SnapshotCommand *>(
            static_cast<const SnapshotCommand *>(m_undoStack.command(i)));
        bytes += command->bytes();
        if (bytes > 128u * 1024u * 1024u && i != m_undoStack.count() - 1) command->expire();
    }
}

void MapEditor::finishPendingEdit()
{
    if (m_mouseEdit) {
        m_mouseEdit = false;
        m_draggingVertices = m_draggingSprites = m_draggingPlayerStart = false;
        endEdit();
    }
}

void MapEditor::undo()
{
    finishPendingEdit();
    if (!m_drawingPoints.empty()) { cancelDrawing(); return; }
    ++m_historyGeneration;
    m_undoStack.undo();
    // Obsolete commands at the memory boundary have no remaining payload.
    while (m_undoStack.index() > 0 && m_undoStack.command(m_undoStack.index() - 1)->isObsolete())
        m_undoStack.undo();
}

void MapEditor::redo()
{
    finishPendingEdit();
    cancelDrawing();
    ++m_historyGeneration;
    m_undoStack.redo();
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

void MapScene::setGridAngle(qreal angle)
{
    m_gridAngle = angle;
    invalidate(sceneRect(), QGraphicsScene::BackgroundLayer);
}

QPointF MapScene::toGrid(const QPointF &point) const
{
    return QTransform().rotate(-m_gridAngle).map(point);
}

QPointF MapScene::fromGrid(const QPointF &point) const
{
    return QTransform().rotate(m_gridAngle).map(point);
}

QPointF MapScene::snapToGrid(const QPointF &point) const
{
    const QPointF local = toGrid(point);
    return fromGrid({std::round(local.x() / m_gridSize) * m_gridSize,
                     std::round(local.y() / m_gridSize) * m_gridSize});
}

void MapScene::paintBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, Qt::black);

    const QRectF visibleRect = rect.intersected(sceneRect());
    const QRectF gridRect = QTransform().rotate(-m_gridAngle).mapRect(visibleRect);
    if (gridRect.isEmpty()) {
        return;
    }
    painter->fillRect(visibleRect, QColor(24, 26, 31));

    if (!m_gridVisible) {
        return;
    }

    // Draw the exact snapping grid, independent of zoom.
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

    painter->save();
    painter->setClipRect(visibleRect, Qt::IntersectClip);
    painter->rotate(m_gridAngle);
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(cosmeticPen(QColor(57, 62, 72), 1.0));
    painter->drawLines(minorLines);
    painter->setPen(cosmeticPen(QColor(77, 84, 97), 1.0));
    painter->drawLines(majorLines);

    painter->setPen(cosmeticPen(QColor(89, 72, 72), 1.25));
    painter->drawLine(QLineF(0.0, gridRect.top(), 0.0, gridRect.bottom()));
    painter->setPen(cosmeticPen(QColor(67, 82, 72), 1.25));
    painter->drawLine(QLineF(gridRect.left(), 0.0, gridRect.right(), 0.0));
    painter->restore();
}

MapEditor::MapEditor(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new MapScene(this))
{
    m_undoStack.setUndoLimit(100);
    setScene(m_scene);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        std::set<MapDocument::SectorId> selectedSectors;
        if (m_mode == Mode::Sectors) {
            for (auto *item : m_scene->selectedItems()) {
                if (dynamic_cast<SectorItem *>(item)) {
                    selectedSectors.insert(item->data(sectorIdRole).toULongLong());
                }
            }
        }
        m_sectorSelectionOrder.erase(std::remove_if(m_sectorSelectionOrder.begin(), m_sectorSelectionOrder.end(),
            [&](auto id) { return !selectedSectors.count(id); }), m_sectorSelectionOrder.end());
        // Simultaneous rubber-band additions use sector number as a stable tie-break.
        for (auto id : selectedSectors) {
            if (std::find(m_sectorSelectionOrder.begin(), m_sectorSelectionOrder.end(), id) == m_sectorSelectionOrder.end()) {
                m_sectorSelectionOrder.push_back(id);
            }
        }
        if (joinAvailabilityChanged) { joinAvailabilityChanged(canJoinSelectedSectors()); }
        if (m_mode != Mode::Sprites) {
            updateProperties();
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
        updateProperties();
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

    // Build map coordinates use positive Y downward, matching Qt's default view.
    scale(baseZoomScale, baseZoomScale);
    centerOn(0.0, 0.0);
    rebuildScene();
}

void MapEditor::setStatusCallback(std::function<void(const QString &)> callback)
{
    m_statusCallback = std::move(callback);
    reportStatus("Draw: left click | Finish: right click/Enter | Close: click first vertex | Cancel: Esc | Pan: middle mouse");
}

void MapEditor::setCursorStatusCallback(std::function<void(const QString &)> callback)
{
    m_cursorStatusCallback = std::move(callback);
}

void MapEditor::setTextureSelector(
    std::function<std::optional<SpriteTexture>(std::optional<int>)> selector)
{
    m_textureSelector = std::move(selector);
}

void MapEditor::setTextureResolver(std::function<QImage(int, int)> resolver)
{
    m_textureResolver = std::move(resolver);
    updateSectorTextures();
}

MapEditor::~MapEditor()
{
    // Scene teardown changes selection after the properties dock may be gone.
    disconnect(m_scene, nullptr, this, nullptr);
    m_propertiesCallback = {};
}

void MapEditor::setPropertiesCallback(
    std::function<void(std::optional<SelectionProperties>)> callback)
{
    m_propertiesCallback = std::move(callback);
    updateProperties();
}

void MapEditor::setSelectedWallSide(bool reversed)
{
    if (m_mode != Mode::Lines || m_scene->selectedItems().size() != 1) {
        return;
    }
    m_wallSideReversed = reversed;
    updateProperties();
}

void MapEditor::resetSelectedWallTextureScale()
{
    if (m_mode != Mode::Lines || m_scene->selectedItems().size() != 1) { return; }
    auto *item = dynamic_cast<WallItem *>(m_scene->selectedItems().front());
    if (!item) { return; }
    const auto id = static_cast<MapDocument::WallId>(item->data(wallIdRole).toULongLong());
    if (id >= m_document.walls().size()) { return; }
    const auto &wall = m_document.walls()[id];
    const bool reversed = wall.isTwoSided() ? m_wallSideReversed : wall.reverseSector.has_value();
    auto side = reversed ? wall.reverseSide : wall.forwardSide;
    side.xrepeat = m_document.defaultWallXRepeat(id);
    side.yrepeat = 8;
    setWallSideValues(id, reversed, side);
}

void MapEditor::setSelectedProperty(Property property, qreal value)
{
    Edit edit(this, "Change property");
    if ((m_mode != Mode::Sprites && m_mode != Mode::Sectors && m_mode != Mode::Lines)
        || m_scene->selectedItems().size() != 1) {
        return;
    }
    if (!std::isfinite(value)) {
        updateProperties();
        return;
    }

    const auto integer = [value](int low, int high) {
        return static_cast<int>(std::round(std::clamp(value, qreal(low), qreal(high))));
    };
    QGraphicsItem *selectedItem = m_scene->selectedItems().front();
    if (m_mode == Mode::Lines) {
        if (!dynamic_cast<WallItem *>(selectedItem)) return;
        const auto wallId = static_cast<MapDocument::WallId>(
            selectedItem->data(wallIdRole).toULongLong());
        if (wallId >= m_document.walls().size()) return;
        const auto &wall = m_document.walls()[wallId];
        bool reversed = wall.isTwoSided() ? m_wallSideReversed : wall.reverseSector.has_value();
        if (property == Property::OppositeTexture) {
            if (!wall.isTwoSided()) return;
            reversed = !reversed;
        }
        auto side = reversed ? wall.reverseSide : wall.forwardSide;
        const auto integer = [value](int low, int high) {
            return static_cast<int>(std::round(std::clamp(value, qreal(low), qreal(high))));
        };
        switch (property) {
        case Property::Texture:
        case Property::OppositeTexture: side.texture = integer(0, 32767); break;
        case Property::OverlayTexture: side.overlayTexture = integer(0, 32767); break;
        case Property::Shade: side.shade = integer(-128, 127); break;
        case Property::Palette: side.palette = integer(0, 255); break;
        case Property::XRepeat: side.xrepeat = integer(0, 255); break;
        case Property::YRepeat: side.yrepeat = integer(0, 255); break;
        case Property::XPanning: side.xpanning = integer(0, 255); break;
        case Property::YPanning: side.ypanning = integer(0, 255); break;
        case Property::Cstat: side.cstat = integer(0, 65535); break;
        case Property::Hitag: side.hitag = integer(-32768, 32767); break;
        case Property::WallLotag: side.lotag = integer(-32768, 32767); break;
        case Property::Extra: side.extra = integer(-32768, 32767); break;
        default: return;
        }
        m_document.setWallSide(wallId, reversed, side);
        updateProperties();
        return;
    }
    if (m_mode == Mode::Sectors) {
        if (!dynamic_cast<SectorItem *>(selectedItem)) {
            return;
        }
        const auto sectorId = static_cast<std::size_t>(
            selectedItem->data(sectorIdRole).toULongLong());
        if (sectorId >= m_document.sectors().size()) return;
        auto updatedSector = m_document.sectors()[sectorId];
        bool changedAdditional = true;
        switch (property) {
        case Property::FloorStat: updatedSector.floorstat = integer(0, 65535); break;
        case Property::CeilingStat: updatedSector.ceilingstat = integer(0, 65535); break;
        case Property::FloorSlope: updatedSector.floorheinum = integer(-32768, 32767); break;
        case Property::CeilingSlope: updatedSector.ceilingheinum = integer(-32768, 32767); break;
        case Property::FloorShade: updatedSector.floorshade = integer(-128, 127); break;
        case Property::CeilingShade: updatedSector.ceilingshade = integer(-128, 127); break;
        case Property::FloorPalette: updatedSector.floorpal = integer(0, 255); break;
        case Property::CeilingPalette: updatedSector.ceilingpal = integer(0, 255); break;
        case Property::FloorXPanning: updatedSector.floorxpanning = integer(0, 255); break;
        case Property::FloorYPanning: updatedSector.floorypanning = integer(0, 255); break;
        case Property::CeilingXPanning: updatedSector.ceilingxpanning = integer(0, 255); break;
        case Property::CeilingYPanning: updatedSector.ceilingypanning = integer(0, 255); break;
        case Property::Visibility: updatedSector.visibility = integer(0, 255); break;
        case Property::Extra: updatedSector.extra = integer(-32768, 32767); break;
        case Property::FirstWall: {
            const auto first = std::find(updatedSector.walls.begin(), updatedSector.walls.end(),
                                         static_cast<MapDocument::WallId>(integer(0, 2147483647)));
            if (first == updatedSector.walls.end()) return;
            const auto offset = first - updatedSector.walls.begin();
            const auto outerEnd = updatedSector.loopStarts.size() > 1
                ? updatedSector.loopStarts[1] : updatedSector.walls.size();
            if (static_cast<std::size_t>(offset) >= outerEnd) return;
            std::rotate(updatedSector.walls.begin(), first, updatedSector.walls.begin() + outerEnd);
            std::rotate(updatedSector.vertices.begin(), updatedSector.vertices.begin() + offset,
                        updatedSector.vertices.begin() + outerEnd);
            break;
        }
        default: changedAdditional = false; break;
        }
        if (changedAdditional) {
            m_document.setSector(sectorId, updatedSector);
            updateProperties();
            return;
        }
        switch (property) {
        case Property::Hitag:
            m_document.setSectorHitag(sectorId,
                static_cast<int>(std::round(std::clamp(value, -32768.0, 32767.0))));
            break;
        case Property::SectorLotag: {
            int tag = static_cast<int>(std::round(std::clamp(value, -32768.0, 65535.0)));
            if (tag < 0) {
                tag += 65536;
            }
            m_document.setSectorLotag(sectorId, tag);
            break;
        }
        case Property::FloorZ:
            m_document.setSectorFloorZ(sectorId, value);
            break;
        case Property::CeilingZ:
            m_document.setSectorCeilingZ(sectorId, value);
            break;
        case Property::FloorTexture:
        case Property::CeilingTexture: {
            const int texture = static_cast<int>(std::round(std::clamp(value, 0.0, 32767.0)));
            if (property == Property::FloorTexture) {
                m_document.setSectorFloorTexture(sectorId, texture);
            } else {
                m_document.setSectorCeilingTexture(sectorId, texture);
            }
            break;
        }
        default:
            return;
        }
        if (property == Property::FloorTexture || property == Property::CeilingTexture) {
            updateSectorTextures();
        }
        updateProperties();
        return;
    }
    const bool isPlayerStart = dynamic_cast<PlayerStartItem *>(selectedItem) != nullptr;
    auto *spriteItem = dynamic_cast<SpriteItem *>(selectedItem);
    if (!isPlayerStart && !spriteItem) {
        return;
    }

    MapDocument::SpriteId spriteId = 0;
    if (spriteItem) {
        spriteId = static_cast<MapDocument::SpriteId>(
            spriteItem->data(spriteIdRole).toULongLong());
        if (spriteId >= m_document.sprites().size()) {
            return;
        }
    }

    const auto boundedCoordinate = [](qreal coordinate) {
        return std::clamp(coordinate, -sceneExtent, sceneExtent);
    };
    if (isPlayerStart) {
        const MapDocument::PlayerStart &playerStart = m_document.playerStart();
        switch (property) {
        default: return;
        case Property::X:
            m_document.setPlayerStartPosition(
                {boundedCoordinate(value), playerStart.position.y()});
            break;
        case Property::Y:
            m_document.setPlayerStartPosition(
                {playerStart.position.x(), boundedCoordinate(value)});
            break;
        case Property::Z:
            m_document.setPlayerStartZ(value);
            break;
        case Property::Angle:
            m_document.setPlayerStartAngle(std::clamp(value, 0.0, 360.0));
            break;
        case Property::Texture:
        case Property::Hitag:
        case Property::Lotag:
        case Property::SectorLotag:
        case Property::FloorZ:
        case Property::CeilingZ:
        case Property::FloorTexture:
        case Property::CeilingTexture:
            return;
        }
    } else {
        const MapDocument::Sprite &sprite = m_document.sprites()[spriteId];
        auto updatedSprite = sprite;
        bool changedAdditional = true;
        switch (property) {
        case Property::Cstat: updatedSprite.cstat = integer(0, 65535); break;
        case Property::Shade: updatedSprite.shade = integer(-128, 127); break;
        case Property::Palette: updatedSprite.palette = integer(0, 255); break;
        case Property::Clipdist: updatedSprite.clipdist = integer(0, 255); break;
        case Property::XRepeat: updatedSprite.xrepeat = integer(0, 255); break;
        case Property::YRepeat: updatedSprite.yrepeat = integer(0, 255); break;
        case Property::XOffset: updatedSprite.xoffset = integer(-128, 127); break;
        case Property::YOffset: updatedSprite.yoffset = integer(-128, 127); break;
        case Property::Status: updatedSprite.statnum = integer(0, 1023); break;
        case Property::Owner: updatedSprite.owner = integer(-32768, 32767); break;
        case Property::XVelocity: updatedSprite.xvel = integer(-32768, 32767); break;
        case Property::YVelocity: updatedSprite.yvel = integer(-32768, 32767); break;
        case Property::ZVelocity: updatedSprite.zvel = integer(-32768, 32767); break;
        case Property::Extra: updatedSprite.extra = integer(-32768, 32767); break;
        case Property::Alignment:
            updatedSprite.cstat = (updatedSprite.cstat & ~48) | (integer(0, 2) << 4);
            break;
        default: changedAdditional = false; break;
        }
        if (changedAdditional) {
            m_document.setSprite(spriteId, updatedSprite);
            if (property == Property::Palette && m_textureResolver) {
                const int key = updatedSprite.texture * 256 + updatedSprite.palette;
                const QImage image = m_textureResolver(updatedSprite.texture,
                                                        updatedSprite.palette);
                if (!image.isNull()) m_spriteTextures.insert(key, image);
                for (QGraphicsItem *item : m_scene->items()) {
                    auto *spriteItem = dynamic_cast<SpriteItem *>(item);
                    if (spriteItem && item->data(spriteIdRole).toULongLong() == spriteId) {
                        spriteItem->setTexture(image);
                        break;
                    }
                }
            }
            updateProperties();
            return;
        }
        switch (property) {
        default: return;
        case Property::SectorLotag:
        case Property::FloorZ:
        case Property::CeilingZ:
        case Property::FloorTexture:
        case Property::CeilingTexture:
            return;
        case Property::X:
            m_document.setSpritePositions(
                {{spriteId, {boundedCoordinate(value), sprite.position.y()}}});
            break;
        case Property::Y:
            m_document.setSpritePositions(
                {{spriteId, {sprite.position.x(), boundedCoordinate(value)}}});
            break;
        case Property::Z:
            m_document.setSpriteZ(spriteId, value);
            break;
        case Property::Angle:
            m_document.setSpriteAngle(spriteId, std::clamp(value, 0.0, 360.0));
            break;
        case Property::Hitag:
        case Property::Lotag: {
            const int tag = static_cast<int>(std::round(std::clamp(
                value, -32768.0, 32767.0)));
            if (property == Property::Hitag) {
                m_document.setSpriteHitag(spriteId, tag);
            } else {
                m_document.setSpriteLotag(spriteId, tag);
            }
            // Tags do not affect sprite geometry. Keep the selected item and
            // let Qt finish committing the combo editor before rebuilding rows.
            QTimer::singleShot(0, this, [this] { updateProperties(); });
            return;
        }
        case Property::Texture: {
            const int texture = static_cast<int>(std::clamp(
                std::llround(value), 0LL,
                static_cast<long long>(std::numeric_limits<short>::max())));
            m_document.setSpriteTexture(spriteId, texture);
            if (m_textureResolver) {
                const int key = texture * 256 + sprite.palette;
                const QImage image = m_textureResolver(texture, sprite.palette);
                if (!image.isNull()) {
                    m_spriteTextures.insert(key, image);
                }
            }
            break;
        }
        }
    }

    rebuildScene();
    for (QGraphicsItem *item : m_scene->items()) {
        if ((isPlayerStart && dynamic_cast<PlayerStartItem *>(item))
            || (!isPlayerStart && dynamic_cast<SpriteItem *>(item)
                && item->data(spriteIdRole).toULongLong() == spriteId)) {
            item->setSelected(true);
            break;
        }
    }
}

void MapEditor::setSpriteValues(std::size_t sprite, const MapDocument::Sprite &values)
{
    Edit edit(this, "Change sprite", continuousEditKey);
    m_document.setSprite(sprite, values);
    rebuildScene();
    updateProperties();
}

void MapEditor::setSurfaceValues(const MapDocument &values, const QString &label)
{
    if (values.vertices() != m_document.vertices()
        || values.sectors().size() != m_document.sectors().size()
        || values.walls().size() != m_document.walls().size()
        || values.sprites().size() != m_document.sprites().size()) return;
    for (std::size_t i = 0; i < values.sectors().size(); ++i) {
        if (values.sectors()[i].walls != m_document.sectors()[i].walls
            || values.sectors()[i].vertices != m_document.sectors()[i].vertices) return;
    }
    Edit edit(this, label, continuousEditKey);
    // Commit object properties together, retaining the real player start and topology.
    for (std::size_t i = 0; i < values.sectors().size(); ++i) m_document.setSector(i, values.sectors()[i]);
    for (std::size_t i = 0; i < values.walls().size(); ++i) {
        m_document.setWallSide(i, false, values.walls()[i].forwardSide);
        m_document.setWallSide(i, true, values.walls()[i].reverseSide);
    }
    for (std::size_t i = 0; i < values.sprites().size(); ++i) m_document.setSprite(i, values.sprites()[i]);
    rebuildScene();
    updateProperties();
}

void MapEditor::setShadeValues(const MapDocument &values)
{
    if (values.sectors().size() != m_document.sectors().size()
        || values.walls().size() != m_document.walls().size()
        || values.sprites().size() != m_document.sprites().size()) return;
    Edit edit(this, "Change shade", continuousEditKey);
    // Copy only shade values: a preview snapshot has its own camera start.
    for (std::size_t i = 0; i < values.sectors().size(); ++i) {
        auto sector = m_document.sectors()[i];
        sector.floorshade = values.sectors()[i].floorshade;
        sector.ceilingshade = values.sectors()[i].ceilingshade;
        m_document.setSector(i, sector);
    }
    for (std::size_t i = 0; i < values.walls().size(); ++i) {
        for (bool reversed : {false, true}) {
            auto side = reversed ? m_document.walls()[i].reverseSide : m_document.walls()[i].forwardSide;
            side.shade = reversed ? values.walls()[i].reverseSide.shade : values.walls()[i].forwardSide.shade;
            m_document.setWallSide(i, reversed, side);
        }
    }
    for (std::size_t i = 0; i < values.sprites().size(); ++i) {
        auto sprite = m_document.sprites()[i];
        sprite.shade = values.sprites()[i].shade;
        m_document.setSprite(i, sprite);
    }
    rebuildScene();
    updateProperties();
}

void MapEditor::setSectorValues(std::size_t sector, const MapDocument::Sector &values)
{
    Edit edit(this, "Change sector", continuousEditKey);
    m_document.setSector(sector, values);
    rebuildScene();
    updateProperties();
}
void MapEditor::setWallSideValues(std::size_t wall, bool reversed, const MapDocument::WallSide &values)
{
    Edit edit(this, "Change wall", continuousEditKey);
    m_document.setWallSide(wall, reversed, values);
    rebuildScene();
    updateProperties();
}

void MapEditor::setSectorHeight(std::size_t sector, bool floor, qreal height)
{
    Edit edit(this, "Change sector height", continuousEditKey);
    if (sector >= m_document.sectors().size()) { return; }
    if (floor) { m_document.setSectorFloorZ(sector, height); }
    else { m_document.setSectorCeilingZ(sector, height); }
    rebuildScene();
    updateProperties();
}

bool MapEditor::hasUnsavedChanges() const
{
    return m_recoveredDirty || !m_drawingPoints.empty() || !(m_document == m_savedDocument);
}

bool MapEditor::saveMap(const QString &filename, QString &error)
{
    finishPendingEdit();
    if (!m_drawingPoints.empty()) {
        error = "Finish or cancel the current drawing before saving.";
        return false;
    }
    if (!saveBuildMap(m_document, filename, error)) return false;
    m_savedDocument = m_document;
    m_recoveredDirty = false;
    m_undoStack.setClean();
    return true;
}

bool MapEditor::openMap(const QString &filename, QString &error, bool asUnsavedCopy)
{
    MapDocument loaded;
    if (!loaded.openMap(filename, error)) return false;
    cancelDrawing();
    m_draggingVertices = m_draggingSprites = m_draggingPlayerStart = false;
    m_spriteDragMoved = m_clickedPlayerStart = m_panning = false;
    m_draggedVertices.clear();
    m_draggedWalls.clear();
    m_draggedSectors.clear();
    m_draggedSprites.clear();
    m_wallSideReversed = false;
    finishPendingEdit();
    m_undoStack.clear();
    m_document = std::move(loaded);
    m_savedDocument = m_document;
    m_recoveredDirty = asUnsavedCopy;
    m_spriteTextures.clear();
    for (const auto &sprite : m_document.sprites()) {
        const int key = sprite.texture * 256 + sprite.palette;
        if (m_textureResolver && sprite.texture >= 0 && !m_spriteTextures.contains(key))
            m_spriteTextures.insert(key, m_textureResolver(sprite.texture, sprite.palette));
    }
    rebuildScene();
    QRectF bounds(-sceneExtent, -sceneExtent, sceneExtent * 2, sceneExtent * 2);
    bounds = bounds.united(m_scene->itemsBoundingRect().adjusted(-1024, -1024, 1024, 1024));
    m_scene->setSceneRect(bounds);
    centerOn(m_document.playerStart().position);
    setCursor(Qt::CrossCursor);
    updateProperties();
    reportStatus("Map opened");
    return true;
}

void MapEditor::recoverDocument(const MapDocument &document, const std::vector<QPointF> &points)
{
    finishPendingEdit();
    m_undoStack.clear();
    setMode(Mode::Draw);
    restore(Snapshot{document, Selection{{}, {}, Mode::Draw, false}});
    m_recoveredDirty = true;
    m_drawingPoints = points;
    if (!points.empty()) updatePreview(points.back());
    centerOn(document.playerStart().position);
}

void MapEditor::showMapIssue(const MapCheckResult &issue)
{
    using Target = MapCheckResult::Target;
    if (issue.valid || issue.target == Target::Map) return;
    if (issue.target == Target::Drawing) {
        if (!m_drawingPoints.empty()) centerOn(m_drawingPoints.back());
        setFocus();
        return;
    }
    const auto mode = issue.target == Target::Sector ? Mode::Sectors
        : issue.target == Target::Wall ? Mode::Lines : Mode::Sprites;
    setMode(mode);
    m_scene->clearSelection();
    for (auto *item : m_scene->items()) {
        const int role = issue.target == Target::Sector ? sectorIdRole
            : issue.target == Target::Wall ? wallIdRole : spriteIdRole;
        const bool matches = issue.target == Target::PlayerStart
            ? dynamic_cast<PlayerStartItem *>(item) != nullptr
            : item->data(role).isValid() && item->data(role).toULongLong() == issue.id;
        if (matches) {
            item->setSelected(true);
            if (issue.target == Target::Wall) m_wallSideReversed = issue.reversed;
            centerOn(item->sceneBoundingRect().center());
            updateProperties();
            break;
        }
    }
    setFocus();
}

void MapEditor::newMap()
{
    finishPendingEdit();
    m_undoStack.clear();
    cancelDrawing();
    m_document.clear();
    m_savedDocument = m_document;
    m_recoveredDirty = false;
    rebuildScene();
    reportStatus("New map");
}

void MapEditor::setMode(Mode mode)
{
    finishPendingEdit();
    ++m_historyGeneration;
    if (m_mode == mode) {
        return;
    }

    cancelDrawing();
    m_mode = mode;
    clearSplitPreview();
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
            sprite->setVisible(m_spritesVisible || mode == Mode::Sprites);
        } else if (auto *playerStart = dynamic_cast<PlayerStartItem *>(item)) {
            playerStart->setInteractive(mode == Mode::Sprites);
        }
    }
    updateProperties();
}

void MapEditor::setSpritesVisible(bool visible)
{
    m_spritesVisible = visible;
    for (QGraphicsItem *item : m_scene->items()) {
        if (auto *sprite = dynamic_cast<SpriteItem *>(item)) {
            sprite->setVisible(visible || m_mode == Mode::Sprites);
        }
    }
}

void MapEditor::setGridSize(qreal size)
{
    clearSplitPreview();
    m_scene->setGridSize(size);
}

void MapEditor::reorientGridToSelectedLine()
{
    if (m_mode != Mode::Lines || m_scene->selectedItems().size() != 1) return;
    auto *wall = dynamic_cast<WallItem *>(m_scene->selectedItems().front());
    if (!wall || wall->line().isNull()) return;
    const qreal angle = std::atan2(wall->line().dy(), wall->line().dx()) * 180.0 / std::acos(-1.0);
    // Equivalent axes repeat every 90 degrees. Choose the smallest change
    // from the current orientation, including when aligning a second line.
    m_scene->setGridAngle(m_scene->gridAngle() + std::remainder(angle - m_scene->gridAngle(), 90.0));
    clearSplitPreview();
    viewport()->update();
}

void MapEditor::resetGridOrientation()
{
    m_scene->setGridAngle(0.0);
    clearSplitPreview();
    viewport()->update();
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
    clearSplitPreview();
    const qreal currentScale = std::abs(transform().m11());
    const qreal targetScale = std::clamp(baseZoomScale * percent / 100.0,
                                       minimumZoomScale, maximumZoomScale);
    const QPointF center = mapToScene(viewport()->rect().center());
    scale(targetScale / currentScale, targetScale / currentScale);
    centerOn(center);
    if (m_zoomCallback) {
        m_zoomCallback(targetScale / baseZoomScale * 100.0);
    }
}

void MapEditor::setZoomCallback(std::function<void(qreal)> callback)
{
    m_zoomCallback = std::move(callback);
    if (m_zoomCallback) {
        m_zoomCallback(std::abs(transform().m11()) / baseZoomScale * 100.0);
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

    return m_scene->snapToGrid(scenePosition);
}

void MapEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        finishPendingEdit();
        beginEdit(m_mode == Mode::Draw ? "Draw geometry" : "Move selection");
        m_mouseEdit = true;
    }

    if (event->button() != Qt::LeftButton) clearSplitPreview();
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
                m_editLabel = "Create sprite";
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
            m_vertexDragDocument = m_document;
            m_draggingVertices = true;
            m_vertexDragStart = mapToScene(event->position().toPoint());
            m_vertexDragAnchor = vertex->pos();
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
            m_vertexDragDocument = m_document;
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
            m_vertexDragDocument = m_document;
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

void MapEditor::clearSplitPreview()
{
    m_splitWall.reset();
    if (m_splitPreviewItem) m_splitPreviewItem->hide();
}

void MapEditor::updateSplitPreview(const QPoint &position, bool disableSnapping)
{
    clearSplitPreview();
    if (m_mode != Mode::Vertices) return;
    const QPointF cursor = mapToScene(position);
    const qreal tolerance = snapRadiusPixels / std::abs(transform().m11());
    // Existing vertices take precedence over creating another nearby vertex.
    for (const auto &vertex : m_document.vertices()) {
        if (QLineF(cursor, vertex.position).length() <= tolerance) return;
    }
    qreal nearest = tolerance;
    for (MapDocument::WallId id = 0; id < m_document.walls().size(); ++id) {
        const auto &wall = m_document.walls()[id];
        const QPointF start = m_document.vertices()[wall.start].position;
        const QPointF delta = m_document.vertices()[wall.end].position - start;
        const qreal squaredLength = QPointF::dotProduct(delta, delta);
        if (squaredLength == 0.0) continue;
        qreal t = QPointF::dotProduct(cursor - start, delta) / squaredLength;
        const qreal distance = QLineF(cursor, start + std::clamp(t, 0.0, 1.0) * delta).length();
        if (distance > nearest) continue;
        if (!disableSnapping) {
            // Snap along the dominant axis to a grid crossing, keeping diagonal
            // and off-grid walls straight rather than bending them onto the grid.
            const QPointF gridStart = m_scene->toGrid(start);
            const QPointF gridDelta = m_scene->toGrid(delta);
            const bool useX = std::abs(gridDelta.x()) >= std::abs(gridDelta.y());
            const qreal origin = useX ? gridStart.x() : gridStart.y();
            const qreal extent = useX ? gridDelta.x() : gridDelta.y();
            const qreal grid = m_scene->gridSize();
            t = (std::round((origin + t * extent) / grid) * grid - origin) / extent;
        }
        if (t <= 0.0 || t >= 1.0) continue;
        const QPointF candidate = start + t * delta;
        if (QLineF(candidate, start).length() < 0.001
            || QLineF(candidate, start + delta).length() < 0.001) continue;
        nearest = distance;
        m_splitWall = id;
        m_splitPosition = candidate;
    }
    if (!m_splitWall) return;
    if (!m_splitPreviewItem) {
        m_splitPreviewItem = m_scene->addEllipse(-4.5, -4.5, 9.0, 9.0,
            cosmeticPen(hoverColor, 1.5), QColor(80, 210, 255, 90));
        m_splitPreviewItem->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        m_splitPreviewItem->setAcceptedMouseButtons(Qt::NoButton);
        m_splitPreviewItem->setZValue(25.0);
    }
    m_splitPreviewItem->setPos(m_splitPosition);
    m_splitPreviewItem->show();
}

void MapEditor::mouseDoubleClickEvent(QMouseEvent *event)
{
    Edit edit(this, "Split line");
    if (m_mode == Mode::Vertices && event->button() == Qt::LeftButton) {
        updateSplitPreview(event->position().toPoint(), event->modifiers().testFlag(Qt::AltModifier));
        if (m_splitWall) {
            const auto vertexId = m_document.splitWall(*m_splitWall, m_splitPosition);
            if (vertexId) {
                rebuildScene();
                for (auto *item : m_scene->items()) {
                    if (dynamic_cast<VertexItem *>(item)
                        && item->data(vertexIdRole).toULongLong() == *vertexId) {
                        item->setSelected(true);
                        break;
                    }
                }
                reportStatus("Vertex created | Line split");
            }
            event->accept();
            return;
        }
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void MapEditor::leaveEvent(QEvent *event)
{
    clearSplitPreview();
    QGraphicsView::leaveEvent(event);
}

void MapEditor::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() == Qt::NoButton) {
        updateSplitPreview(event->position().toPoint(), event->modifiers().testFlag(Qt::AltModifier));
    } else {
        clearSplitPreview();
    }
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
        if (m_mode == Mode::Vertices && !event->modifiers().testFlag(Qt::AltModifier)) {
            // Snap the grabbed vertex, preserving the click offset and the
            // relative positions of all other vertices in the selection.
            delta = m_scene->snapToGrid(m_vertexDragAnchor + delta) - m_vertexDragAnchor;
        }
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
        m_document.setVertexPositions(positions, &m_vertexDragDocument);
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

    if (m_cursorStatusCallback && !m_drawingPoints.empty()) {
        const qreal length = QLineF(m_drawingPoints.back(), position).length();
        const qreal angle = QLineF(m_drawingPoints.back(), position).angle();
        m_cursorStatusCallback(QString("X %1  Y %2 | Length %3 | Angle %4°")
                         .arg(position.x(), 0, 'f', 0)
                         .arg(position.y(), 0, 'f', 0)
                         .arg(length, 0, 'f', 1)
                         .arg(angle, 0, 'f', 1));
    } else if (m_cursorStatusCallback) {
        m_cursorStatusCallback(QString("X %1  Y %2")
                         .arg(position.x(), 0, 'f', 0)
                         .arg(position.y(), 0, 'f', 0));
    }

    QGraphicsView::mouseMoveEvent(event);
}

void MapEditor::mouseReleaseEvent(QMouseEvent *event)
{
    const auto finish = [this, event](void *) {
        if (event->button() == Qt::RightButton) finishPendingEdit();
    };
    std::unique_ptr<void, decltype(finish)> transaction(this, finish);

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
                m_editLabel = "Change sprite texture";
                m_document.setSpriteTexture(spriteId, selection->tile);
            m_spriteTextures.insert(selection->tile * 256, selection->image);
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
    clearSplitPreview();
    const QPointF before = mapToScene(event->position().toPoint());
    const qreal factor = std::pow(1.0015, event->angleDelta().y());
    const qreal currentScale = std::abs(transform().m11());
    const qreal targetScale = std::clamp(currentScale * factor,
                                       minimumZoomScale, maximumZoomScale);
    scale(targetScale / currentScale, targetScale / currentScale);
    const QPointF after = mapToScene(event->position().toPoint());
    translate(after.x() - before.x(), after.y() - before.y());
    if (m_zoomCallback) {
        m_zoomCallback(targetScale / baseZoomScale * 100.0);
    }
    event->accept();
}

void MapEditor::drawBackground(QPainter *painter, const QRectF &rect)
{
    m_scene->paintBackground(painter, rect);
}

bool MapEditor::canJoinSelectedSectors() const
{
    return m_mode == Mode::Sectors && m_sectorSelectionOrder.size() >= 2;
}

void MapEditor::joinSelectedSectors()
{
    Edit edit(this, "Join sectors");
    if (!isVisible() || !canJoinSelectedSectors()) { return; }
    const auto source = m_sectorSelectionOrder.front();
    QString error;
    const auto joined = m_document.joinSectors(m_sectorSelectionOrder, error);
    if (!joined) { reportStatus(error); return; }
    rebuildScene();
    for (auto *item : m_scene->items()) {
        if (dynamic_cast<SectorItem *>(item) && item->data(sectorIdRole).toULongLong() == *joined) {
            item->setSelected(true);
            break;
        }
    }
    updateProperties();
    reportStatus(QString("Joined sectors using properties from sector %1.").arg(source));
}

void MapEditor::stickSelectedSpriteToWall()
{
    Edit edit(this, "Stick sprite to wall");
    if (!isVisible() || m_mode != Mode::Sprites || m_scene->selectedItems().size() != 1) {
        reportStatus("Select one sprite in 2D sprite mode first."); return;
    }
    auto *item = dynamic_cast<SpriteItem *>(m_scene->selectedItems().front());
    if (!item) { reportStatus("Select a sprite, not the player start."); return; }
    const auto id = static_cast<MapDocument::SpriteId>(item->data(spriteIdRole).toULongLong());
    QString error;
    if (!m_document.stickSpriteToWall(id, error)) { reportStatus(error); return; }
    rebuildScene();
    for (auto *candidate : m_scene->items()) {
        if (dynamic_cast<SpriteItem *>(candidate) && candidate->data(spriteIdRole).toULongLong() == id) {
            candidate->setSelected(true); break;
        }
    }
    reportStatus("Sprite placed against the nearest wall.");
}

void MapEditor::keyPressEvent(QKeyEvent *event)
{
    std::unique_ptr<Edit> edit;
    if (event->key() == Qt::Key_Delete) edit = std::make_unique<Edit>(this, "Delete selection");
    if (event->key() == Qt::Key_Delete && m_mode == Mode::Vertices) {
        std::vector<MapDocument::VertexId> ids;
        for (auto *item : m_scene->selectedItems()) {
            if (dynamic_cast<VertexItem *>(item)) { ids.push_back(item->data(vertexIdRole).toULongLong()); }
        }
        if (!ids.empty()) {
            QString error;
            if (m_document.removeVertices(ids,error)) {
                m_draggingVertices = false;
                m_draggedVertices.clear();
                rebuildScene();
                updateProperties();
                reportStatus(QString("%1 vertex/vertices deleted").arg(ids.size()));
            } else { reportStatus(error); }
        }
        event->accept(); return;
    }

    if (event->key() == Qt::Key_Delete && m_mode == Mode::Sectors) {
        if (!m_sectorSelectionOrder.empty()) {
            const auto count = m_sectorSelectionOrder.size();
            QString error;
            if (m_document.removeSectors(m_sectorSelectionOrder, error)) {
                m_draggingVertices = false;
                m_draggedVertices.clear();
                m_draggedWalls.clear();
                m_draggedSectors.clear();
                setCursor(Qt::CrossCursor);
                rebuildScene();
                updateProperties();
                reportStatus(QString("%1 sector(s) and their sprites deleted. Shared boundaries remain solid walls; move the player start if it was inside.").arg(count));
            } else { reportStatus(error); }
        }
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Delete && m_mode == Mode::Lines) {
        if (!m_document.supportsLineDeletion()) {
            reportStatus("Line deletion is not yet supported for this imported map's complex effect geometry.");
            event->accept();
            return;
        }
        std::vector<MapDocument::WallId> wallIds;
        for (QGraphicsItem *item : m_scene->selectedItems()) {
            if (auto *wall = dynamic_cast<WallItem *>(item)) {
                wallIds.push_back(static_cast<MapDocument::WallId>(
                    wall->data(wallIdRole).toULongLong()));
            }
        }
        if (!wallIds.empty()) {
            m_draggingVertices = false;
            m_draggedVertices.clear();
            m_draggedWalls.clear();
            m_draggedSectors.clear();
            setCursor(Qt::CrossCursor);
            m_document.removeWalls(wallIds);
            rebuildScene();
            updateProperties();
            reportStatus(QString("%1 line(s) deleted").arg(wallIds.size()));
        }
        event->accept();
        return;
    }
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
    if (m_drawingPoints.size() >= 2) {
        const bool onWall = std::any_of(m_document.walls().begin(), m_document.walls().end(),
            [&](const MapDocument::Wall &wall) {
                const auto a = m_document.vertices()[wall.start].position;
                const auto b = m_document.vertices()[wall.end].position;
                const auto delta = b - a, offset = position - a;
                const qreal length = std::hypot(delta.x(), delta.y());
                return length > 0 && QPointF::dotProduct(position - a, position - b) <= 0
                    && std::abs(offset.x() * delta.y() - offset.y() * delta.x()) / length <= 0.001;
            });
        if (onWall && finishDrawing(false, false)) return;
    }
    updatePreview(position);
}

bool MapEditor::finishDrawing(bool close, bool discardOnFailure)
{
    Edit edit(this, "Draw geometry");
    QString error;
    const bool sectorCreated = m_document.addPolyline(m_drawingPoints, close, &error);
    if (!sectorCreated && !discardOnFailure) return false;
    m_drawingPoints.clear();
    rebuildScene();
    reportStatus(sectorCreated ? "Sector created" : error.isEmpty() ? "Drawing discarded" : error);
    return sectorCreated;
}

void MapEditor::cancelDrawing()
{
    m_drawingPoints.clear();
    if (m_previewItem) {
        m_scene->removeItem(m_previewItem);
        delete m_previewItem;
        m_previewItem = nullptr;
        m_previewLengthItem = nullptr;
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
        m_previewLengthItem = new LineLengthItem(m_previewItem);
    } else {
        m_previewItem->setPath(path);
    }

    const QLineF segment(m_drawingPoints.back(), cursorPosition);
    const qreal length = segment.length();
    m_previewLengthItem->setVisible(length > 0.0);
    m_previewLengthItem->setText(QString::number(length, 'f', 1));
    m_previewLengthItem->setPos(segment.center());
    const QRectF labelBounds = m_previewLengthItem->boundingRect();
    // Keep the label horizontal and its gap fixed in pixels at every zoom.
    const QPointF offset = std::abs(segment.dx()) >= std::abs(segment.dy())
        ? QPointF(-labelBounds.width() / 2.0, -labelBounds.height() - 8.0)
        : QPointF(8.0, -labelBounds.height() / 2.0);
    m_previewLengthItem->setTransform(QTransform::fromTranslate(offset.x(), offset.y()));
}

void MapEditor::setSectorFill(SectorFill fill)
{
    if (m_sectorFill == fill) return;
    m_sectorFill = fill;
    updateSectorTextures();
}

void MapEditor::updateSectorTextures()
{
    QMap<int, QImage> textures;
    for (QGraphicsItem *item : m_scene->items()) {
        auto *sectorItem = dynamic_cast<SectorItem *>(item);
        if (!sectorItem) continue;
        const auto sectorId = static_cast<std::size_t>(item->data(sectorIdRole).toULongLong());
        QImage image;
        if (m_sectorFill != SectorFill::Plain && m_textureResolver && sectorId < m_document.sectors().size()) {
            const auto &sector = m_document.sectors()[sectorId];
            const int tile = m_sectorFill == SectorFill::Floor ? sector.floorTexture : sector.ceilingTexture;
            if (!textures.contains(tile)) textures.insert(tile, m_textureResolver(tile, 0));
            image = textures.value(tile);
        }
        sectorItem->setTexture(image);
    }
}

void MapEditor::rebuildScene()
{
    m_splitPreviewItem = nullptr;
    m_splitWall.reset();
    m_scene->clear();
    m_previewItem = nullptr;
    m_previewLengthItem = nullptr;

    for (std::size_t sectorId = 0; sectorId < m_document.sectors().size(); ++sectorId) {
        const MapDocument::Sector &sector = m_document.sectors()[sectorId];
        QPainterPath path;
        path.setFillRule(Qt::OddEvenFill);
        for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
            const auto position = m_document.vertices()[sector.vertices[i]].position;
            if (i == 0 || std::find(sector.loopStarts.begin(), sector.loopStarts.end(), i) != sector.loopStarts.end())
                path.moveTo(position);
            else
                path.lineTo(position);
            if (sector.nextWallIndex(i) <= i) path.closeSubpath();
        }
        auto *item = new SectorItem(path);
        item->setInteractive(m_mode == Mode::Sectors);
        item->setData(sectorIdRole, static_cast<qulonglong>(sectorId));
        m_scene->addItem(item);
        item->setZValue(-10.0);
    }

    for (MapDocument::WallId wallId = 0; wallId < m_document.walls().size(); ++wallId) {
        const MapDocument::Wall &wall = m_document.walls()[wallId];
        const QPointF start = m_document.vertices()[wall.start].position;
        const QPointF end = m_document.vertices()[wall.end].position;
        auto *item = new WallItem(QLineF(start, end), wall.isTwoSided());
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
        const int key = sprite.texture * 256 + sprite.palette;
        auto *item = new SpriteItem(m_spriteTextures.value(key), sprite.angle);
        item->setInteractive(m_mode == Mode::Sprites);
        item->setVisible(m_spritesVisible || m_mode == Mode::Sprites);
        item->setData(spriteIdRole, static_cast<qulonglong>(spriteId));
        m_scene->addItem(item);
        item->setPos(sprite.position);
        item->setZValue(12.0);
    }

    auto *playerStart = new PlayerStartItem();
    playerStart->setInteractive(m_mode == Mode::Sprites);
    m_scene->addItem(playerStart);
    playerStart->setPos(m_document.playerStart().position);
    // Build angle zero faces +X; angles increase clockwise in the Y-down view.
    // The artwork points up, so rotate it 90 degrees to face right at angle zero.
    playerStart->setRotation(m_document.playerStart().angle + 90.0);
    playerStart->setZValue(13.0);
    updateSectorTextures();
}

void MapEditor::updateProperties() const
{
    std::optional<MapDocument::WallId> firstWall;
    const auto selected = m_scene->selectedItems();
    if (m_propertiesCallback && m_mode == Mode::Sectors && selected.size() == 1
        && dynamic_cast<SectorItem *>(selected.front())) {
        const auto sectorId = static_cast<std::size_t>(selected.front()->data(sectorIdRole).toULongLong());
        if (sectorId < m_document.sectors().size() && !m_document.sectors()[sectorId].walls.empty()) {
            firstWall = m_document.sectors()[sectorId].walls.front();
        }
    }
    for (auto *item : m_scene->items()) {
        if (auto *wall = dynamic_cast<WallItem *>(item)) {
            wall->setFirstWallHighlighted(firstWall && item->data(wallIdRole).toULongLong() == *firstWall);
        }
    }
    if (!m_propertiesCallback) {
        return;
    }
    if ((m_mode != Mode::Sprites && m_mode != Mode::Sectors && m_mode != Mode::Lines)
        || m_scene->selectedItems().size() != 1) {
        m_propertiesCallback(std::nullopt);
        return;
    }

    QGraphicsItem *item = m_scene->selectedItems().front();
    if (m_mode == Mode::Lines) {
        if (auto *wallItem = dynamic_cast<WallItem *>(item)) {
            const auto wallId = static_cast<MapDocument::WallId>(item->data(wallIdRole).toULongLong());
            if (wallId < m_document.walls().size()) {
                const auto &wall = m_document.walls()[wallId];
                const bool reversed = wall.isTwoSided() ? m_wallSideReversed : wall.reverseSector.has_value();
                wallItem->setEditingSide(reversed);
                SelectionProperties properties{};
                properties.wall = WallProperties{reversed ? wall.reverseSide : wall.forwardSide,
                                                wall.forwardSector, wall.reverseSector, reversed,
                                                QLineF(m_document.vertices()[wall.start].position,
                                                       m_document.vertices()[wall.end].position).length()};
                if (wall.isTwoSided()) {
                    properties.wall->oppositeTexture =
                        reversed ? wall.forwardSide.texture : wall.reverseSide.texture;
                }
                m_propertiesCallback(properties);
                return;
            }
        }
        m_propertiesCallback(std::nullopt);
        return;
    }
    if (m_mode == Mode::Sectors) {
        if (dynamic_cast<SectorItem *>(item)) {
            const auto sectorId = static_cast<std::size_t>(
                item->data(sectorIdRole).toULongLong());
            if (sectorId < m_document.sectors().size()) {
                const auto &sector = m_document.sectors()[sectorId];
                SelectionProperties properties{};
                properties.sector = sector;
                properties.sectorId = sectorId;
                m_propertiesCallback(properties);
                return;
            }
        }
        m_propertiesCallback(std::nullopt);
        return;
    }
    if (dynamic_cast<PlayerStartItem *>(item)) {
        const MapDocument::PlayerStart &playerStart = m_document.playerStart();
        m_propertiesCallback(SelectionProperties{
            playerStart.position.x(), playerStart.position.y(), playerStart.z,
            playerStart.angle, std::nullopt, std::nullopt, std::nullopt});
        return;
    }

    auto *spriteItem = dynamic_cast<SpriteItem *>(item);
    if (spriteItem) {
        const auto spriteId = static_cast<MapDocument::SpriteId>(
            spriteItem->data(spriteIdRole).toULongLong());
        if (spriteId < m_document.sprites().size()) {
            const MapDocument::Sprite &sprite = m_document.sprites()[spriteId];
            m_propertiesCallback(SelectionProperties{
                sprite.position.x(), sprite.position.y(), sprite.z, sprite.angle,
                sprite.texture, sprite.hitag, sprite.lotag, std::nullopt, std::nullopt, sprite});
            return;
        }
    }
    m_propertiesCallback(std::nullopt);
}

void MapEditor::reportStatus(const QString &message) const
{
    if (m_statusCallback) {
        m_statusCallback(message);
    }
}
