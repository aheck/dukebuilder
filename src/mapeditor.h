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

    enum class SectorFill { Plain, Floor, Ceiling };
    void setSectorFill(SectorFill fill);
    [[nodiscard]] SectorFill sectorFill() const { return m_sectorFill; }

    explicit MapEditor(QWidget *parent = nullptr);
    ~MapEditor() override;

    struct SpriteTexture {
        int tile;
        QImage image;
    };

    using SectorProperties = MapDocument::Sector;

    struct WallProperties {
        MapDocument::WallSide values;
        std::optional<MapDocument::SectorId> forwardSector;
        std::optional<MapDocument::SectorId> reverseSector;
        bool reversed;
    };

    struct SelectionProperties {
        qreal x;
        qreal y;
        qreal z;
        qreal angle;
        std::optional<int> texture;
        std::optional<int> hitag;
        std::optional<int> lotag;
        std::optional<SectorProperties> sector = std::nullopt;
        std::optional<WallProperties> wall = std::nullopt;
        std::optional<MapDocument::Sprite> sprite = std::nullopt;
        std::optional<MapDocument::SectorId> sectorId = std::nullopt;
    };

    enum class Property {
        X,
        Y,
        Z,
        Angle,
        Texture,
        Hitag,
        Lotag,
        FloorZ,
        CeilingZ,
        FloorTexture,
        CeilingTexture,
        SectorLotag,
        OverlayTexture,
        WallLotag,
        Shade,
        Palette,
        XRepeat,
        YRepeat,
        XPanning,
        YPanning,
        Cstat,
        Extra,
        FloorStat,
        CeilingStat,
        FloorSlope,
        CeilingSlope,
        FloorShade,
        CeilingShade,
        FloorPalette,
        CeilingPalette,
        FloorXPanning,
        FloorYPanning,
        CeilingXPanning,
        CeilingYPanning,
        Visibility,
        FirstWall,
        Clipdist,
        XOffset,
        YOffset,
        Status,
        Owner,
        XVelocity,
        YVelocity,
        ZVelocity,
        Alignment,
    };

    void newMap();
    bool saveMap(const QString &filename, QString &error) const;
    void setMode(Mode mode);
    void setGridSize(qreal size);
    void setGridVisible(bool visible);
    [[nodiscard]] bool isGridVisible() const;
    void setZoomPercent(qreal percent);
    void setZoomCallback(std::function<void(qreal)> callback);
    void setTextureSelector(std::function<std::optional<SpriteTexture>(
                                std::optional<int>)> selector);
    void setTextureResolver(std::function<QImage(int)> resolver);
    void setPropertiesCallback(
        std::function<void(std::optional<SelectionProperties>)> callback);
    void setSelectedProperty(Property property, qreal value);
    void setSelectedWallSide(bool reversed);
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
    void updateSectorTextures();
    void updateProperties() const;
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
    bool m_wallSideReversed = false;
    SectorFill m_sectorFill = SectorFill::Plain;
    std::function<void(const QString &)> m_statusCallback;
    std::function<void(qreal)> m_zoomCallback;
    std::function<std::optional<SpriteTexture>(std::optional<int>)> m_textureSelector;
    std::function<QImage(int)> m_textureResolver;
    std::function<void(std::optional<SelectionProperties>)> m_propertiesCallback;
};
