#include "mapdocument.h"

#include <QtGlobal>

#include <cmath>

namespace {
constexpr qreal coordinateEpsilon = 0.001;
}

void MapDocument::clear()
{
    m_vertices.clear();
    m_walls.clear();
    m_sectors.clear();
}

MapDocument::VertexId MapDocument::findOrAddVertex(const QPointF &position)
{
    for (VertexId index = 0; index < m_vertices.size(); ++index) {
        const QPointF delta = m_vertices[index].position - position;
        if (std::abs(delta.x()) < coordinateEpsilon
            && std::abs(delta.y()) < coordinateEpsilon) {
            return index;
        }
    }

    m_vertices.push_back({position});
    return m_vertices.size() - 1;
}

void MapDocument::addPolyline(const std::vector<QPointF> &points, bool closed)
{
    if (points.size() < 2) {
        return;
    }

    std::vector<VertexId> vertexIds;
    vertexIds.reserve(points.size());
    for (const QPointF &point : points) {
        vertexIds.push_back(findOrAddVertex(point));
    }

    Sector sector;
    const std::size_t segmentCount = closed ? points.size() : points.size() - 1;
    sector.walls.reserve(segmentCount);

    for (std::size_t index = 0; index < segmentCount; ++index) {
        const VertexId start = vertexIds[index];
        const VertexId end = vertexIds[(index + 1) % vertexIds.size()];
        if (start == end) {
            continue;
        }

        m_walls.push_back({start, end});
        sector.walls.push_back(m_walls.size() - 1);
    }

    if (closed && sector.walls.size() >= 3) {
        m_sectors.push_back(std::move(sector));
    }
}
