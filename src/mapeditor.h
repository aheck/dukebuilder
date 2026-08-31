#pragma once

#include "mapdocument.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPoint>

#include <functional>
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
    };

    explicit MapEditor(QWidget *parent = nullptr);

    void newMap();
    void setMode(Mode mode);
    void setGridSize(qreal size);
    void setGridVisible(bool visible);
    [[nodiscard]] bool isGridVisible() const;
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
    void reportStatus(const QString &message) const;

    MapDocument m_document;
    MapScene *m_scene = nullptr;
    QGraphicsPathItem *m_previewItem = nullptr;
    std::vector<QPointF> m_drawingPoints;
    std::vector<std::pair<MapDocument::VertexId, QPointF>> m_draggedVertices;
    std::vector<MapDocument::WallId> m_draggedWalls;
    QPointF m_vertexDragStart;
    QPoint m_lastPanPosition;
    bool m_panning = false;
    bool m_draggingVertices = false;
    Mode m_mode = Mode::Draw;
    std::function<void(const QString &)> m_statusCallback;
};
