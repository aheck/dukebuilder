#include "mapdocument.h"

#include <QtGlobal>

#include <cmath>
#include <algorithm>
#include <functional>
#include <tuple>

namespace {
constexpr qreal coordinateEpsilon = 0.001;
}

void MapDocument::setWallSide(WallId wallId, bool reversed, const WallSide &side)
{
    if (wallId < m_walls.size()) {
        (reversed ? m_walls[wallId].reverseSide : m_walls[wallId].forwardSide) = side;
    }
}

void MapDocument::setSector(SectorId sectorId, const Sector &sector)
{
    if (sectorId < m_sectors.size()) m_sectors[sectorId] = sector;
}

void MapDocument::setSprite(SpriteId spriteId, const Sprite &sprite)
{
    if (spriteId < m_sprites.size()) m_sprites[spriteId] = sprite;
}

void MapDocument::clear()
{
    m_complexTopology = false;
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
    if (m_complexTopology) return false;
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

void MapDocument::removeWalls(const std::vector<WallId> &wallIds)
{
    if (m_complexTopology) return;
    const WallId removed = m_walls.size();
    std::vector<bool> selected(m_walls.size(), false);
    bool changed = false;
    for (const WallId id : wallIds) {
        if (id < selected.size()) {
            selected[id] = true;
            changed = true;
        }
    }
    if (!changed) return;

    // Collapse connected selections onto their lowest-numbered endpoint.
    // Choosing an existing endpoint keeps the result on the original grid and
    // makes multi-selection independent of selection order.
    std::vector<VertexId> roots(m_vertices.size());
    for (VertexId id = 0; id < roots.size(); ++id) roots[id] = id;
    const auto root = [&](VertexId id) {
        while (roots[id] != id) id = roots[id];
        return id;
    };
    for (WallId id = 0; id < m_walls.size(); ++id) {
        if (!selected[id]) continue;
        const VertexId a = root(m_walls[id].start);
        const VertexId b = root(m_walls[id].end);
        roots[std::max(a, b)] = std::min(a, b);
    }
    for (VertexId id = 0; id < roots.size(); ++id) roots[id] = root(id);

    std::vector<WallId> wallMapping(m_walls.size(), removed);
    std::vector<Wall> remainingWalls;
    for (WallId id = 0; id < m_walls.size(); ++id) {
        Wall wall = m_walls[id];
        wall.start = roots[wall.start];
        wall.end = roots[wall.end];
        if (selected[id] || wall.start == wall.end) continue;
        const auto duplicate = std::find_if(remainingWalls.begin(), remainingWalls.end(),
            [&](const Wall &other) {
                return (other.start == wall.start && other.end == wall.end)
                    || (other.start == wall.end && other.end == wall.start);
            });
        if (duplicate != remainingWalls.end()) {
            wallMapping[id] = duplicate - remainingWalls.begin();
        } else {
            wallMapping[id] = remainingWalls.size();
            remainingWalls.push_back(std::move(wall));
        }
    }

    // Shorten each old boundary before matching rebuilt faces, preserving
    // properties and sector order even when one of its edges was collapsed.
    for (Sector &sector : m_sectors) {
        std::vector<WallId> walls;
        std::vector<VertexId> vertices;
        for (std::size_t i = 0; i < sector.walls.size(); ++i) {
            const WallId mapped = wallMapping[sector.walls[i]];
            if (mapped == removed) continue;
            walls.push_back(mapped);
            vertices.push_back(roots[sector.vertices[i]]);
        }
        sector.walls = std::move(walls);
        sector.vertices = std::move(vertices);
    }
    std::vector<bool> collapsedBoundary(remainingWalls.size(), false);
    std::vector<bool> survivingBoundary(remainingWalls.size(), false);
    m_sectors.erase(std::remove_if(m_sectors.begin(), m_sectors.end(),
        [&](const Sector &sector) {
            auto walls = sector.walls;
            std::sort(walls.begin(), walls.end());
            const bool collapsed = walls.size() < 3
                || std::adjacent_find(walls.begin(), walls.end()) != walls.end();
            for (WallId id : walls) {
                (collapsed ? collapsedBoundary : survivingBoundary)[id] = true;
            }
            return collapsed;
        }), m_sectors.end());

    // A collapsed triangle leaves two coincident edges, deduplicated above
    // into one line. Remove that remnant unless another sector still uses it.
    m_walls.clear();
    std::vector<WallId> compactedWalls(remainingWalls.size());
    for (WallId id = 0; id < remainingWalls.size(); ++id) {
        if (collapsedBoundary[id] && !survivingBoundary[id]) continue;
        compactedWalls[id] = m_walls.size();
        m_walls.push_back(std::move(remainingWalls[id]));
    }
    for (Sector &sector : m_sectors) {
        for (WallId &id : sector.walls) id = compactedWalls[id];
    }

    // Remove orphaned vertices, retaining endpoints still used by open lines.
    std::vector<bool> used(m_vertices.size(), false);
    for (const Wall &wall : m_walls) used[wall.start] = used[wall.end] = true;
    std::vector<VertexId> vertexMapping(m_vertices.size());
    std::vector<Vertex> remainingVertices;
    for (VertexId id = 0; id < m_vertices.size(); ++id) {
        if (used[id]) {
            vertexMapping[id] = remainingVertices.size();
            remainingVertices.push_back(m_vertices[id]);
        }
    }
    for (Wall &wall : m_walls) {
        wall.start = vertexMapping[wall.start];
        wall.end = vertexMapping[wall.end];
    }
    for (Sector &sector : m_sectors) {
        for (VertexId &id : sector.vertices) id = vertexMapping[id];
    }
    m_vertices = std::move(remainingVertices);
    rebuildSectors();
}

void MapDocument::setVertexPositions(
    const std::vector<std::pair<VertexId, QPointF>> &positions)
{
    for (const auto &[vertexId, position] : positions) {
        if (vertexId < m_vertices.size()) {
            m_vertices[vertexId].position = position;
        }
    }
    if (!m_complexTopology) rebuildSectors();
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
    // Rebuilt faces can change indices. Imported memberships are hints only.
    m_playerStart.sectorId.reset();
    for (auto &sprite : m_sprites) sprite.sectorId.reset();
    struct OutgoingEdge {
        WallId wall;
        VertexId destination;
        qreal angle;
        bool reversed;
    };

    const auto previousSectors = std::move(m_sectors);
    m_sectors.clear();
    std::vector<std::optional<Sector>> survivingSectors(previousSectors.size());
    std::vector<Sector> newSectors;
    for (Wall &wall : m_walls) {
        wall.forwardSector.reset();
        wall.reverseSector.reset();
    }
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
                            auto walls = std::move(sector.walls);
                            auto vertices = std::move(sector.vertices);
                            const auto first = std::find(walls.begin(), walls.end(), previous->walls.front());
                            const auto offset = first - walls.begin();
                            std::rotate(walls.begin(), first, walls.end());
                            std::rotate(vertices.begin(), vertices.begin() + offset, vertices.end());
                            sector = *previous;
                            sector.walls = std::move(walls);
                            sector.vertices = std::move(vertices);
                            survivingSectors[previous - previousSectors.begin()] = std::move(sector);
                        } else {
                            newSectors.push_back(std::move(sector));
                        }
                    }
                    break;
                }
            }
        }
    }

    // Keep surviving sectors in their previous order, then append new faces.
    // Removed faces leave no gaps in the Build sector indices.
    for (auto &sector : survivingSectors) {
        if (sector) m_sectors.push_back(std::move(*sector));
    }
    for (auto &sector : newSectors) m_sectors.push_back(std::move(sector));

    // Side references must use the final ordering, not face discovery order.
    for (SectorId sectorId = 0; sectorId < m_sectors.size(); ++sectorId) {
        const Sector &sector = m_sectors[sectorId];
        for (std::size_t index = 0; index < sector.walls.size(); ++index) {
            Wall &boundary = m_walls[sector.walls[index]];
            auto &side = boundary.start == sector.vertices[index]
                ? boundary.forwardSector : boundary.reverseSector;
            side = sectorId;
        }
    }
}

std::size_t MapDocument::Sector::nextWallIndex(std::size_t index) const
{
    std::size_t start = 0;
    for (const auto next : loopStarts) {
        if (next > index) return index + 1 < next ? index + 1 : start;
        start = next;
    }
    return index + 1 < walls.size() ? index + 1 : start;
}

bool MapDocument::Vertex::operator==(const Vertex &other) const
{
    return position == other.position;
}

bool MapDocument::WallSide::operator==(const WallSide &other) const
{
    return std::tie(
        texture, overlayTexture, shade, palette, xrepeat, yrepeat, xpanning, ypanning, cstat,
        hitag, lotag, extra)
        == std::tie(
        other.texture, other.overlayTexture, other.shade, other.palette, other.xrepeat,
        other.yrepeat, other.xpanning, other.ypanning, other.cstat, other.hitag, other.lotag,
        other.extra);
}

bool MapDocument::Wall::operator==(const Wall &other) const
{
    return std::tie(
        start, end, forwardSide, reverseSide, forwardSector, reverseSector)
        == std::tie(
        other.start, other.end, other.forwardSide, other.reverseSide, other.forwardSector,
        other.reverseSector);
}

bool MapDocument::Sector::operator==(const Sector &other) const
{
    return std::tie(
        walls, vertices, loopStarts, floorz, ceilingz, floorTexture, ceilingTexture, hitag,
        lotag, floorstat, ceilingstat, floorheinum, ceilingheinum, floorshade, ceilingshade,
        floorpal, ceilingpal, floorxpanning, floorypanning, ceilingxpanning, ceilingypanning,
        visibility, extra, filler)
        == std::tie(
        other.walls, other.vertices, other.loopStarts, other.floorz, other.ceilingz,
        other.floorTexture, other.ceilingTexture, other.hitag, other.lotag, other.floorstat,
        other.ceilingstat, other.floorheinum, other.ceilingheinum, other.floorshade,
        other.ceilingshade, other.floorpal, other.ceilingpal, other.floorxpanning,
        other.floorypanning, other.ceilingxpanning, other.ceilingypanning, other.visibility,
        other.extra, other.filler);
}

bool MapDocument::Sprite::operator==(const Sprite &other) const
{
    return std::tie(
        position, z, angle, texture, hitag, lotag, cstat, shade, palette, clipdist, xrepeat,
        yrepeat, xoffset, yoffset, statnum, owner, xvel, yvel, zvel, extra, filler, sectorId)
        == std::tie(
        other.position, other.z, other.angle, other.texture, other.hitag, other.lotag,
        other.cstat, other.shade, other.palette, other.clipdist, other.xrepeat, other.yrepeat,
        other.xoffset, other.yoffset, other.statnum, other.owner, other.xvel, other.yvel,
        other.zvel, other.extra, other.filler, other.sectorId);
}

bool MapDocument::PlayerStart::operator==(const PlayerStart &other) const
{
    return std::tie(
        position, z, angle, sectorId)
        == std::tie(
        other.position, other.z, other.angle, other.sectorId);
}

bool MapDocument::operator==(const MapDocument &other) const
{
    return std::tie(m_vertices, m_walls, m_sectors, m_sprites, m_playerStart, m_complexTopology)
        == std::tie(other.m_vertices, other.m_walls, other.m_sectors, other.m_sprites,
                    other.m_playerStart, other.m_complexTopology);
}

std::set<int> MapDocument::usedTextureTiles() const
{
    std::set<int> tiles;
    const auto addSide = [&](const WallSide &side) {
        tiles.insert(side.texture);
        // Build uses the overlay only for masked or one-way walls.
        if (side.cstat & (16 | 32)) tiles.insert(side.overlayTexture);
    };
    for (const auto &wall : m_walls) {
        if (wall.forwardSector) addSide(wall.forwardSide);
        if (wall.reverseSector) addSide(wall.reverseSide);
    }
    for (const auto &sector : m_sectors) {
        tiles.insert(sector.floorTexture);
        tiles.insert(sector.ceilingTexture);
    }
    for (const auto &sprite : m_sprites) tiles.insert(sprite.texture);
    tiles.erase(-1);
    return tiles;
}
