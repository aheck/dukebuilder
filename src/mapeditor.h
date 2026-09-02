#pragma once

#include "mapdocument.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QMap>
#include <QPoint>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QGraphicsPathItem;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

class MapScene final : public QGraphicsScene
{
public:
    explicit MapScene(QObject *parent = nullptr);

    void setGridSize(qreal size);
    [[nodiscard]] qreal gridSize() const { return m_gridSize; }
    void setGridVisible(bool visible);
    [[nodiscard]] bool isGridVisible() const { return m_gridVisible; }
    void paintBackground(QPainter *painter, const QRectF &rect);

private:
    qreal m_gridSize = 64.0;
    bool m_gridVisible = true;
};

class MapEditor final : public QGraphicsView
{
public:
    enum class Mode {
        Draw,
        Lines,
        Vertices,
        Sectors,
        Sprites,
    };

    explicit MapEditor(QWidget *parent = nullptr);

    struct SpriteTexture {
        int tile;
        QImage image;
    };

    struct SpriteProperties {
        qreal x;
        qreal y;
        qreal z;
        qreal angle;
        std::optional<int> texture;
    };

    enum class SpriteProperty {
        X,
        Y,
        Z,
        Angle,
        Texture,
    };

    void newMap();
    void setMode(Mode mode);
    void setGridSize(qreal size);
    void setGridVisible(bool visible);
    [[nodiscard]] bool isGridVisible() const;
    void setZoomPercent(qreal percent);
    void setZoomCallback(std::function<void(qreal)> callback);
    void setTextureSelector(std::function<std::optional<SpriteTexture>(
                                std::optional<int>)> selector);
    void setSpriteTextureResolver(std::function<QImage(int)> resolver);
    void setSpritePropertiesCallback(
        std::function<void(std::optional<SpriteProperties>)> callback);
    void setSelectedSpriteProperty(SpriteProperty property, qreal value);
    void setStatusCallback(std::function<void(const QString &)> callback);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;

private:
    QPointF snappedPosition(const QPoint &viewportPosition, bool disableSnapping) const;
    void addDrawingPoint(const QPointF &position);
    void finishDrawing(bool close);
    void cancelDrawing();
    void updatePreview(const QPointF &cursorPosition);
    void rebuildScene();
    void updateSpriteProperties() const;
    void reportStatus(const QString &message) const;

    MapDocument m_document;
    MapScene *m_scene = nullptr;
    QGraphicsPathItem *m_previewItem = nullptr;
    std::vector<QPointF> m_drawingPoints;
    std::vector<std::pair<MapDocument::VertexId, QPointF>> m_draggedVertices;
    std::vector<MapDocument::WallId> m_draggedWalls;
    std::vector<std::size_t> m_draggedSectors;
    std::vector<std::pair<MapDocument::SpriteId, QPointF>> m_draggedSprites;
    QMap<int, QImage> m_spriteTextures;
    QPointF m_vertexDragStart;
    QPointF m_draggedPlayerStart;
    QPoint m_lastPanPosition;
    QPoint m_spriteRightPressPosition;
    MapDocument::SpriteId m_clickedSprite = 0;
    bool m_panning = false;
    bool m_draggingVertices = false;
    bool m_draggingSprites = false;
    bool m_spriteDragMoved = false;
    bool m_draggingPlayerStart = false;
    bool m_clickedPlayerStart = false;
    Mode m_mode = Mode::Draw;
    std::function<void(const QString &)> m_statusCallback;
    std::function<void(qreal)> m_zoomCallback;
    std::function<std::optional<SpriteTexture>(std::optional<int>)> m_textureSelector;
    std::function<QImage(int)> m_spriteTextureResolver;
    std::function<void(std::optional<SpriteProperties>)> m_spritePropertiesCallback;
};
