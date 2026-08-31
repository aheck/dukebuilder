#pragma once

#include <QPointF>

#include <cstddef>
#include <utility>
#include <vector>

class MapDocument
{
public:
    using VertexId = std::size_t;
    using WallId = std::size_t;

    struct Vertex {
        QPointF position;
    };

    struct Wall {
        VertexId start;
        VertexId end;
    };

    struct Sector {
        std::vector<WallId> walls;
        std::vector<VertexId> vertices;
    };

    void clear();
    void addPolyline(const std::vector<QPointF> &points, bool closed);
    void setVertexPositions(const std::vector<std::pair<VertexId, QPointF>> &positions);

    [[nodiscard]] const std::vector<Vertex> &vertices() const { return m_vertices; }
    [[nodiscard]] const std::vector<Wall> &walls() const { return m_walls; }
    [[nodiscard]] const std::vector<Sector> &sectors() const { return m_sectors; }

private:
    VertexId findOrAddVertex(const QPointF &position);
    void rebuildSectors();

    std::vector<Vertex> m_vertices;
    std::vector<Wall> m_walls;
    std::vector<Sector> m_sectors;
};
