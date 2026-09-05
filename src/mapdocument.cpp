#include "mapdocument.h"

#include <QtGlobal>

#include <cmath>
#include <algorithm>
#include <functional>

namespace {
constexpr qreal coordinateEpsilon = 0.001;
}

void MapDocument::clear()
{
    m_vertices.clear();
    m_walls.clear();
    m_sectors.clear();
    m_sprites.clear();
    m_playerStart = {{0.0, 0.0}, 0.0, 0.0};
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

bool MapDocument::addPolyline(const std::vector<QPointF> &points, bool closed)
{
    if (points.size() < 2) {
        return false;
    }

    const std::size_t originalVertexCount = m_vertices.size();
    const std::size_t originalWallCount = m_walls.size();
    const std::size_t originalSectorCount = m_sectors.size();

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

        const bool wallExists = std::any_of(
            m_walls.begin(), m_walls.end(), [start, end](const Wall &wall) {
                return (wall.start == start && wall.end == end)
                    || (wall.start == end && wall.end == start);
            });
        if (wallExists) {
            continue;
        }

        m_walls.push_back({start, end});
    }

    rebuildSectors();
    if (m_sectors.size() > originalSectorCount) {
        return true;
    }

    m_vertices.resize(originalVertexCount);
    m_walls.resize(originalWallCount);
    rebuildSectors();
    return false;
}

void MapDocument::setVertexPositions(
    const std::vector<std::pair<VertexId, QPointF>> &positions)
{
    for (const auto &[vertexId, position] : positions) {
        if (vertexId < m_vertices.size()) {
            m_vertices[vertexId].position = position;
        }
    }
    rebuildSectors();
}

void MapDocument::setSectorFloorZ(std::size_t sectorId, qreal z)
{
    if (sectorId < m_sectors.size()) {
        m_sectors[sectorId].floorz = z;
    }
}

void MapDocument::setSectorCeilingZ(std::size_t sectorId, qreal z)
{
    if (sectorId < m_sectors.size()) {
        m_sectors[sectorId].ceilingz = z;
    }
}

void MapDocument::setSectorFloorTexture(std::size_t sectorId, int texture)
{
    if (sectorId < m_sectors.size()) {
        m_sectors[sectorId].floorTexture = texture;
    }
}

void MapDocument::setSectorCeilingTexture(std::size_t sectorId, int texture)
{
    if (sectorId < m_sectors.size()) {
        m_sectors[sectorId].ceilingTexture = texture;
    }
}

void MapDocument::setSectorHitag(std::size_t sectorId, int hitag)
{
    if (sectorId < m_sectors.size()) {
        m_sectors[sectorId].hitag = hitag;
    }
}

void MapDocument::setSectorLotag(std::size_t sectorId, int lotag)
{
    if (sectorId < m_sectors.size()) {
        m_sectors[sectorId].lotag = lotag;
    }
}

MapDocument::SpriteId MapDocument::addSprite(const QPointF &position)
{
    m_sprites.push_back({position, 0.0, 0.0, -1});
    return m_sprites.size() - 1;
}

void MapDocument::removeSprites(const std::vector<SpriteId> &spriteIds)
{
    std::vector<SpriteId> sortedIds = spriteIds;
    std::sort(sortedIds.begin(), sortedIds.end(), std::greater<SpriteId>());
    sortedIds.erase(std::unique(sortedIds.begin(), sortedIds.end()), sortedIds.end());
    for (const SpriteId spriteId : sortedIds) {
        if (spriteId < m_sprites.size()) {
            m_sprites.erase(m_sprites.begin() + static_cast<std::ptrdiff_t>(spriteId));
        }
    }
}

void MapDocument::setSpritePositions(
    const std::vector<std::pair<SpriteId, QPointF>> &positions)
{
    for (const auto &[spriteId, position] : positions) {
        if (spriteId < m_sprites.size()) {
            m_sprites[spriteId].position = position;
        }
    }
}

void MapDocument::setSpriteTexture(SpriteId spriteId, int texture)
{
    if (spriteId < m_sprites.size()) {
        m_sprites[spriteId].texture = texture;
    }
}

void MapDocument::setSpriteHitag(SpriteId spriteId, int hitag)
{
    if (spriteId < m_sprites.size()) {
        m_sprites[spriteId].hitag = hitag;
    }
}

void MapDocument::setSpriteLotag(SpriteId spriteId, int lotag)
{
    if (spriteId < m_sprites.size()) {
        m_sprites[spriteId].lotag = lotag;
    }
}

void MapDocument::setSpriteZ(SpriteId spriteId, qreal z)
{
    if (spriteId < m_sprites.size()) {
        m_sprites[spriteId].z = z;
    }
}

void MapDocument::setSpriteAngle(SpriteId spriteId, qreal angle)
{
    if (spriteId < m_sprites.size()) {
        m_sprites[spriteId].angle = angle;
    }
}

void MapDocument::setPlayerStartPosition(const QPointF &position)
{
    m_playerStart.position = position;
}

void MapDocument::setPlayerStartZ(qreal z)
{
    m_playerStart.z = z;
}

void MapDocument::setPlayerStartAngle(qreal angle)
{
    m_playerStart.angle = angle;
}

void MapDocument::rebuildSectors()
{
    struct OutgoingEdge {
        WallId wall;
        VertexId destination;
        qreal angle;
        bool reversed;
    };

    const auto previousSectors = std::move(m_sectors);
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
                        // Geometry edits rebuild faces; retain properties of the same boundary.
                        const auto previous = std::find_if(
                            previousSectors.begin(), previousSectors.end(),
                            [&sector](const Sector &candidate) {
                                return std::is_permutation(
                                    sector.walls.begin(), sector.walls.end(),
                                    candidate.walls.begin(), candidate.walls.end());
                            });
                        if (previous != previousSectors.end()) {
                            sector.floorz = previous->floorz;
                            sector.ceilingz = previous->ceilingz;
                            sector.floorTexture = previous->floorTexture;
                            sector.ceilingTexture = previous->ceilingTexture;
                            sector.hitag = previous->hitag;
                            sector.lotag = previous->lotag;
                        }
                        m_sectors.push_back(std::move(sector));
                    }
                    break;
                }
            }
        }
    }
}
