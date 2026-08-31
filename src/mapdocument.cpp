#include "mapdocument.h"

#include <QtGlobal>

#include <cmath>
#include <algorithm>

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

    const std::size_t segmentCount = closed ? points.size() : points.size() - 1;

    for (std::size_t index = 0; index < segmentCount; ++index) {
        const VertexId start = vertexIds[index];
        const VertexId end = vertexIds[(index + 1) % vertexIds.size()];
        if (start == end) {
            continue;
        }

        m_walls.push_back({start, end});
    }

    rebuildSectors();
}

void MapDocument::rebuildSectors()
{
    struct OutgoingEdge {
        WallId wall;
        VertexId destination;
        qreal angle;
        bool reversed;
    };

    m_sectors.clear();
    std::vector<std::vector<OutgoingEdge>> outgoing(m_vertices.size());
    for (WallId wallId = 0; wallId < m_walls.size(); ++wallId) {
        const Wall &wall = m_walls[wallId];
        const QPointF forward = m_vertices[wall.end].position - m_vertices[wall.start].position;
        const QPointF reverse = -forward;
        outgoing[wall.start].push_back(
            {wallId, wall.end, std::atan2(forward.y(), forward.x()), false});
        outgoing[wall.end].push_back(
            {wallId, wall.start, std::atan2(reverse.y(), reverse.x()), true});
    }

    for (auto &edges : outgoing) {
        std::sort(edges.begin(), edges.end(), [](const OutgoingEdge &left, const OutgoingEdge &right) {
            return left.angle < right.angle;
        });
    }

    // Each directed wall borders one face. Following the edge immediately
    // clockwise from the reverse edge walks the face on the left.
    std::vector<bool> visited(m_walls.size() * 2, false);
    for (WallId initialWall = 0; initialWall < m_walls.size(); ++initialWall) {
        for (int initialReverse = 0; initialReverse < 2; ++initialReverse) {
            const std::size_t initialHalfEdge = initialWall * 2 + initialReverse;
            if (visited[initialHalfEdge]) {
                continue;
            }

            Sector sector;
            WallId wallId = initialWall;
            bool reversed = initialReverse != 0;
            qreal twiceArea = 0.0;

            for (std::size_t step = 0; step <= m_walls.size() * 2; ++step) {
                const std::size_t halfEdge = wallId * 2 + (reversed ? 1 : 0);
                if (visited[halfEdge]) {
                    break;
                }
                visited[halfEdge] = true;

                const Wall &wall = m_walls[wallId];
                const VertexId from = reversed ? wall.end : wall.start;
                const VertexId to = reversed ? wall.start : wall.end;
                sector.walls.push_back(wallId);
                sector.vertices.push_back(from);

                const QPointF &a = m_vertices[from].position;
                const QPointF &b = m_vertices[to].position;
                twiceArea += a.x() * b.y() - b.x() * a.y();

                const auto &edges = outgoing[to];
                const auto reverseEdge = std::find_if(
                    edges.begin(), edges.end(), [wallId](const OutgoingEdge &edge) {
                        return edge.wall == wallId;
                    });
                if (reverseEdge == edges.end()) {
                    break;
                }

                const std::size_t reverseIndex = static_cast<std::size_t>(reverseEdge - edges.begin());
                const OutgoingEdge &next = edges[(reverseIndex + edges.size() - 1) % edges.size()];
                wallId = next.wall;
                reversed = next.reversed;

                if (wallId * 2 + (reversed ? 1 : 0) == initialHalfEdge) {
                    if (sector.vertices.size() >= 3 && twiceArea > coordinateEpsilon) {
                        m_sectors.push_back(std::move(sector));
                    }
                    break;
                }
            }
        }
    }
}
