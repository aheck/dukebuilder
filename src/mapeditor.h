#pragma once

#include "mapdocument.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QMap>
#include <QPoint>
#include <QUndoStack>
#include <memory>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QGraphicsPathItem;
class QGraphicsEllipseItem;
class QGraphicsSimpleTextItem;
struct MapCheckResult;
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
    void setGridAngle(qreal angle);
    [[nodiscard]] qreal gridAngle() const { return m_gridAngle; }
    [[nodiscard]] QPointF toGrid(const QPointF &point) const;
    [[nodiscard]] QPointF fromGrid(const QPointF &point) const;
    [[nodiscard]] QPointF snapToGrid(const QPointF &point) const;

private:
    qreal m_gridSize = 256.0;
    qreal m_gridAngle = 0.0;
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
        qreal length;
        std::optional<int> oppositeTexture = std::nullopt;
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
        OppositeTexture,
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

    [[nodiscard]] const MapDocument &document() const { return m_document; }
    void setSectorValues(std::size_t sector, const MapDocument::Sector &values);
    void setShadeValues(const MapDocument &values);
    void setSurfaceValues(const MapDocument &values, const QString &label);
    void setWallSideValues(std::size_t wall, bool reversed, const MapDocument::WallSide &values);
    void setSectorHeight(std::size_t sector, bool floor, qreal height);
    void resetSelectedWallTextureScale();
    void stickSelectedSpriteToWall();
    bool canJoinSelectedSectors() const;
    void joinSelectedSectors();
    std::function<void(bool)> joinAvailabilityChanged;
    void setSpriteValues(std::size_t sprite, const MapDocument::Sprite &values);
    QUndoStack *undoStack() { return &m_undoStack; }
    void undo();
    void redo();
    std::function<void()> documentRestored;
    // Set only for the duration of a continuous 3D edit callback.
    QString continuousEditKey;
    bool canAutosave() const { return !m_mouseEdit && m_editDepth == 0; }
    const std::vector<QPointF> &drawingPoints() const { return m_drawingPoints; }
    void recoverDocument(const MapDocument &document, const std::vector<QPointF> &points);
    void showMapIssue(const MapCheckResult &issue);
    void newMap();
    [[nodiscard]] std::set<int> usedTextureTiles() const { return m_document.usedTextureTiles(); }
    bool saveMap(const QString &filename, QString &error);
    [[nodiscard]] bool hasUnsavedChanges() const;
    bool openMap(const QString &filename, QString &error, bool asUnsavedCopy = false);
    void setMode(Mode mode);
    void setSpritesVisible(bool visible);
    void setGameStartDifficulty(int difficulty);
    void setGameStartEnemiesEnabled(bool enabled);
    [[nodiscard]] int gameStartDifficulty() const { return m_gameStartDifficulty; }
    [[nodiscard]] bool gameStartEnemiesEnabled() const { return m_gameStartEnemiesEnabled; }
    void setGridSize(qreal size);
    void reorientGridToSelectedLine();
    void resetGridOrientation();
    void setGridVisible(bool visible);
    [[nodiscard]] bool isGridVisible() const;
    void setZoomPercent(qreal percent);
    void setZoomCallback(std::function<void(qreal)> callback);
    void setTextureSelector(std::function<std::optional<SpriteTexture>(
                                std::optional<int>)> selector);
    void setTextureResolver(std::function<QImage(int, int)> resolver);
    void setPropertiesCallback(
        std::function<void(std::optional<SelectionProperties>)> callback);
    void setSelectedProperty(Property property, qreal value);
    void setSelectedWallSide(bool reversed);
    void setStatusCallback(std::function<void(const QString &)> callback);
    void setCursorStatusCallback(std::function<void(const QString &)> callback);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;

private:
    struct Selection {
        std::vector<std::pair<int, qulonglong>> items;
        std::vector<MapDocument::SectorId> sectorOrder;
        Mode mode;
        bool reversed;
    };
    struct Snapshot { MapDocument document; Selection selection; };
    class Edit;
    class SnapshotCommand;
    Selection selection() const;
    void restore(const Snapshot &snapshot);
    void beginEdit(const QString &label, const QString &mergeKey = {});
    void endEdit();
    void finishPendingEdit();
    QUndoStack m_undoStack;
    std::optional<Snapshot> m_beforeEdit;
    QString m_editLabel, m_editKey;
    int m_editDepth = 0;
    unsigned m_historyGeneration = 0;
    bool m_mouseEdit = false;
    QPointF snappedPosition(const QPoint &viewportPosition, bool disableSnapping) const;
    void addDrawingPoint(const QPointF &position);
    bool finishDrawing(bool close, bool discardOnFailure = true);
    void cancelDrawing();
    void updatePreview(const QPointF &cursorPosition);
    void updateSplitPreview(const QPoint &position, bool disableSnapping);
    void clearSplitPreview();
    void rebuildScene();
    void updateSectorTextures();
    void updateProperties() const;
    void reportStatus(const QString &message) const;

    MapDocument m_document;
    MapDocument m_savedDocument;
    bool m_recoveredDirty = false;
    MapDocument m_vertexDragDocument;
    MapScene *m_scene = nullptr;
    std::vector<MapDocument::SectorId> m_sectorSelectionOrder;
    QGraphicsPathItem *m_previewItem = nullptr;
    QGraphicsEllipseItem *m_splitPreviewItem = nullptr;
    std::optional<MapDocument::WallId> m_splitWall;
    QPointF m_splitPosition;
    QGraphicsSimpleTextItem *m_previewLengthItem = nullptr;
    std::vector<QPointF> m_drawingPoints;
    std::vector<std::pair<MapDocument::VertexId, QPointF>> m_draggedVertices;
    std::vector<MapDocument::WallId> m_draggedWalls;
    std::vector<std::size_t> m_draggedSectors;
    std::vector<std::pair<MapDocument::SpriteId, QPointF>> m_draggedSprites;
    std::vector<MapDocument::Sprite> m_spriteClipboard;
    unsigned m_spritePasteCount = 0;
    QMap<int, QImage> m_spriteTextures;
    QPointF m_vertexDragStart;
    QPointF m_vertexDragAnchor;
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
    bool m_spritesVisible = true;
    int m_gameStartDifficulty = 0;
    bool m_gameStartEnemiesEnabled = true;
    bool m_wallSideReversed = false;
    SectorFill m_sectorFill = SectorFill::Plain;
    std::function<void(const QString &)> m_statusCallback;
    std::function<void(const QString &)> m_cursorStatusCallback;
    std::function<void(qreal)> m_zoomCallback;
    std::function<std::optional<SpriteTexture>(std::optional<int>)> m_textureSelector;
    std::function<QImage(int, int)> m_textureResolver;
    std::function<void(std::optional<SelectionProperties>)> m_propertiesCallback;
};
