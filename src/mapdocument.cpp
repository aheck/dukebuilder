#include "mapdocument.h"

#include <QtGlobal>
#include <QPainterPath>
#include <QLineF>

#include <cmath>
#include <algorithm>
#include <functional>
#include <tuple>
#include <limits>
#include <map>

namespace {
constexpr qreal coordinateEpsilon = 0.001;
int repeat(qreal value)
{
    return static_cast<int>(std::round(std::clamp(value, qreal(1), qreal(255))));
}
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

bool MapDocument::stickSpriteToWall(SpriteId id, QString &error)
{
    error.clear();
    if (id >= m_sprites.size()) { error = "Select a sprite first."; return false; }
    const auto &sprite = m_sprites[id];
    if (!std::isfinite(sprite.angle) || !std::isfinite(sprite.position.x())
        || !std::isfinite(sprite.position.y())) {
        error = "The sprite has invalid coordinates or angle."; return false;
    }
    // Sector loops keep holes and overlapping imported rooms distinct.
    const auto sectorPath = [this](const Sector &sector) {
        QPainterPath path;
        path.setFillRule(Qt::OddEvenFill);
        for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
            const auto point = m_vertices[sector.vertices[i]].position;
            if (i == 0 || std::find(sector.loopStarts.begin(), sector.loopStarts.end(), i) != sector.loopStarts.end()) {
                if (i) { path.closeSubpath(); }
                path.moveTo(point);
            } else { path.lineTo(point); }
        }
        path.closeSubpath();
        return path;
    };
    std::optional<SectorId> owner;
    if (sprite.sectorId && *sprite.sectorId < m_sectors.size()
        && sectorPath(m_sectors[*sprite.sectorId]).contains(sprite.position)) {
        owner = sprite.sectorId;
    } else {
        for (SectorId s = 0; s < m_sectors.size(); ++s) {
            if (sectorPath(m_sectors[s]).contains(sprite.position)) {
                if (owner) { error = "The sprite is in overlapping sectors without a valid sector assignment."; return false; }
                owner = s;
            }
        }
    }
    if (!owner) { error = "Place the sprite inside a sector first."; return false; }
    const auto &sector = m_sectors[*owner];
    const auto cross = [](QPointF a, QPointF b) { return a.x()*b.y() - a.y()*b.x(); };
    double nearest = std::numeric_limits<double>::infinity();
    QPointF hit, normal;
    for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
        const auto a = m_vertices[sector.vertices[i]].position;
        const auto b = m_vertices[sector.vertices[sector.nextWallIndex(i)]].position;
        const auto edge = b-a;
        const double lengthSquared = QPointF::dotProduct(edge, edge);
        if (lengthSquared < coordinateEpsilon*coordinateEpsilon) { continue; }
        const double fraction = std::clamp(QPointF::dotProduct(sprite.position-a, edge)/lengthSquared, 0.0, 1.0);
        const auto projection = a + edge*fraction;
        const auto delta = sprite.position-projection;
        const double distanceSquared = QPointF::dotProduct(delta, delta);
        if (distanceSquared >= nearest) { continue; }
        nearest = distanceSquared;
        hit = projection;
        normal = QPointF(-edge.y(), edge.x()) / std::sqrt(lengthSquared);
        if (QPointF::dotProduct(normal, delta) < 0) { normal = -normal; }
    }
    if (!std::isfinite(nearest)) { error = "No wall was found in the sprite's sector."; return false; }
    // Offset toward the room, avoiding coplanar flicker and integer rounding
    // onto a boundary. At corners also step toward the original sprite position.
    const auto path = sectorPath(sector);
    const auto delta = sprite.position-hit;
    const auto away = nearest > 0 ? delta/std::sqrt(nearest) : normal;
    for (double gap : {1.0, 2.0, 4.0}) {
        for (const auto offset : {normal*gap, (normal+away)*gap}) {
            const auto candidate = hit+offset;
            const QPointF position(std::round(candidate.x()), std::round(candidate.y()));
            if (!path.contains(position)) { continue; }
            bool onBoundary = false;
            for (std::size_t i=0; i<sector.vertices.size(); ++i) {
                const auto a=m_vertices[sector.vertices[i]].position;
                const auto b=m_vertices[sector.vertices[sector.nextWallIndex(i)]].position;
                const auto edge=b-a;
                if (std::abs(cross(edge,position-a)) < 1e-6
                    && QPointF::dotProduct(position-a,position-b) <= 0) { onBoundary=true; break; }
            }
            if (onBoundary) { continue; }
            auto updated = sprite;
            updated.position = position;
            double degrees = std::atan2(normal.y(), normal.x())*180.0/3.14159265358979323846;
            if (degrees < 0) { degrees += 360; }
            updated.angle = (std::lround(degrees*2048.0/360.0) % 2048)*360.0/2048.0;
            updated.cstat = (updated.cstat & ~48) | 16;
            updated.sectorId = owner;
            m_sprites[id] = updated;
            return true;
        }
    }
    error = "Not enough space to place the sprite just inside that wall.";
    return false;
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
        const int density = defaultWallXRepeat(m_walls.size() - 1);
        m_walls.back().forwardSide.xrepeat = density;
        m_walls.back().reverseSide.xrepeat = density;
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

std::optional<MapDocument::VertexId> MapDocument::splitWall(WallId wallId, const QPointF &position)
{
    if (wallId >= m_walls.size() || !std::isfinite(position.x()) || !std::isfinite(position.y())) {
        return std::nullopt;
    }
    const Wall original = m_walls[wallId];
    const QPointF start = m_vertices[original.start].position;
    const QPointF delta = m_vertices[original.end].position - start;
    const qreal length = std::hypot(delta.x(), delta.y());
    if (length <= coordinateEpsilon) return std::nullopt;
    const QPointF offset = position - start;
    const qreal distance = QPointF::dotProduct(offset, delta) / length;
    if (distance <= coordinateEpsilon || distance >= length - coordinateEpsilon
        || std::abs(offset.x() * delta.y() - offset.y() * delta.x()) / length > coordinateEpsilon) {
        return std::nullopt;
    }
    const VertexId vertexId = m_vertices.size();
    const WallId secondId = m_walls.size();
    m_vertices.push_back({position});
    m_walls[wallId].end = vertexId;
    Wall second = original;
    second.start = vertexId;
    m_walls.push_back(second);
    auto &firstHalf = m_walls[wallId];
    auto &secondHalf = m_walls[secondId];
    for (bool reversed : {false, true}) {
        const auto &source = reversed ? original.reverseSide : original.forwardSide;
        auto &first = reversed ? firstHalf.reverseSide : firstHalf.forwardSide;
        auto &last = reversed ? secondHalf.reverseSide : secondHalf.forwardSide;
        first.xrepeat = repeat(source.xrepeat * distance / length);
        last.xrepeat = repeat(source.xrepeat * (length-distance) / length);
        // Continue texture coordinates in each side's traversal direction.
        if (reversed) { first.xpanning = (source.xpanning + last.xrepeat*8) % 256; }
        else { last.xpanning = (source.xpanning + first.xrepeat*8) % 256; }
    }
    // Update existing loops directly: rebuilding planar faces would lose imported
    // overlapping rooms and effect sectors. Reverse sides traverse the new half first.
    for (auto &sector : m_sectors) {
        for (std::size_t i = 0; i < sector.walls.size(); ++i) {
            if (sector.walls[i] != wallId) continue;
            const bool reversed = sector.vertices[i] == original.end;
            sector.walls[i] = reversed ? secondId : wallId;
            sector.walls.insert(sector.walls.begin() + i + 1, reversed ? wallId : secondId);
            sector.vertices.insert(sector.vertices.begin() + i + 1, vertexId);
            for (auto &loopStart : sector.loopStarts) {
                if (loopStart > i) ++loopStart;
            }
            ++i;
        }
    }
    return vertexId;
}

std::optional<MapDocument::SectorId> MapDocument::joinSectors(
    const std::vector<SectorId> &ids, QString &error)
{
    error.clear();
    const auto fail = [&](const QString &message) -> std::optional<SectorId> {
        error = message;
        return std::nullopt;
    };
    const std::set<SectorId> selected(ids.begin(), ids.end());
    if (selected.size() < 2 || *selected.rbegin() >= m_sectors.size()) {
        return fail("Select at least two adjacent sectors to join.");
    }
    const SectorId donor = ids.front();
    std::set<WallId> removed;
    std::set<SectorId> connected{donor};
    bool progress = true;
    while (progress) {
        progress = false;
        for (WallId w = 0; w < m_walls.size(); ++w) {
            const auto &wall = m_walls[w];
            if (!wall.isTwoSided() || !selected.count(*wall.forwardSector)
                || !selected.count(*wall.reverseSector)) { continue; }
            removed.insert(w);
            if (connected.count(*wall.forwardSector) || connected.count(*wall.reverseSector)) {
                progress |= connected.insert(*wall.forwardSector).second;
                progress |= connected.insert(*wall.reverseSector).second;
            }
        }
    }
    if (connected != selected) { return fail("Selected sectors must be connected by shared walls."); }

    struct Edge { WallId wall; VertexId start, end; };
    std::vector<Edge> edges;
    std::map<VertexId, std::size_t> outgoing;
    std::set<VertexId> incoming;
    for (SectorId id : selected) {
        const auto &sector = m_sectors[id];
        for (std::size_t i = 0; i < sector.walls.size(); ++i) {
            if (removed.count(sector.walls[i])) { continue; }
            const auto a = sector.vertices[i], b = sector.vertices[sector.nextWallIndex(i)];
            if (!outgoing.emplace(a, edges.size()).second || !incoming.insert(b).second) {
                return fail("Joining would create a branching or touching boundary.");
            }
            edges.push_back({sector.walls[i], a, b});
        }
    }
    std::vector<std::vector<std::size_t>> loops;
    std::vector<bool> visited(edges.size());
    std::optional<std::size_t> outer;
    for (std::size_t start = 0; start < edges.size(); ++start) {
        if (visited[start]) { continue; }
        std::vector<std::size_t> loop;
        double area = 0;
        auto current = start;
        do {
            if (visited[current]) { return fail("Joining would create an invalid boundary loop."); }
            visited[current] = true;
            loop.push_back(current);
            const auto &edge = edges[current];
            const auto a = m_vertices[edge.start].position, b = m_vertices[edge.end].position;
            area += a.x()*b.y() - a.y()*b.x();
            const auto next = outgoing.find(edge.end);
            if (next == outgoing.end()) { return fail("Joining would leave an open boundary."); }
            current = next->second;
        } while (current != start);
        if (loop.size() < 3 || std::abs(area) < coordinateEpsilon) {
            return fail("Joining would create a degenerate sector.");
        }
        if (area > 0) {
            if (outer) { return fail("Joining must produce one connected outer boundary."); }
            outer = loops.size();
        }
        loops.push_back(std::move(loop));
    }
    if (!outer) { return fail("Joining would remove the entire sector boundary."); }
    std::swap(loops[0], loops[*outer]);
    auto joined = m_sectors[donor];
    const auto first = std::find_if(loops[0].begin(), loops[0].end(), [&](auto e) {
        return edges[e].wall == joined.walls.front()
            && edges[e].start == joined.vertices.front();
    });
    if (first != loops[0].end()) {
        std::rotate(loops[0].begin(), first, loops[0].end());
    } else if (((joined.floorstat & 2) && joined.floorheinum != 0)
               || ((joined.ceilingstat & 2) && joined.ceilingheinum != 0)
               || ((joined.floorstat | joined.ceilingstat) & 64)) {
        return fail("The join would remove the source sector's first wall. Choose a surviving outer first wall before joining slopes or relatively aligned textures.");
    }
    joined.walls.clear();
    joined.vertices.clear();
    joined.loopStarts.clear();
    for (const auto &loop : loops) {
        if (loops.size() > 1) { joined.loopStarts.push_back(joined.walls.size()); }
        for (auto e : loop) {
            joined.walls.push_back(edges[e].wall);
            joined.vertices.push_back(edges[e].start);
        }
    }

    // A source slope may intersect the opposite surface in the enlarged room.
    const auto a = m_vertices[joined.vertices[0]].position;
    const auto d = m_vertices[joined.vertices[1]].position - a;
    const auto surfaceZ = [&](VertexId v, bool floor) {
        double z = floor ? joined.floorz : joined.ceilingz;
        if ((floor ? joined.floorstat : joined.ceilingstat) & 2) {
            const auto p = m_vertices[v].position - a;
            z += (floor ? joined.floorheinum : joined.ceilingheinum)
                * (d.x()*p.y() - d.y()*p.x()) / (std::hypot(d.x(), d.y())*256.0);
        }
        return z;
    };
    for (auto v : joined.vertices) {
        if (surfaceZ(v, true) < surfaceZ(v, false)) {
            return fail("The source sector's slope would put the floor above the ceiling in the joined sector.");
        }
    }

    // Compact only removed wall/sector records. All surviving wall-side values
    // remain intact, including portals to sectors outside the selection.
    auto candidate = *this;
    candidate.m_sectors.clear();
    std::vector<SectorId> sectorMap(m_sectors.size());
    for (SectorId s = 0; s < m_sectors.size(); ++s) {
        if (selected.count(s) && s != donor) { continue; }
        sectorMap[s] = candidate.m_sectors.size();
        candidate.m_sectors.push_back(s == donor ? joined : m_sectors[s]);
    }
    for (SectorId s : selected) { sectorMap[s] = sectorMap[donor]; }
    std::vector<WallId> wallMap(m_walls.size());
    candidate.m_walls.clear();
    for (WallId w = 0; w < m_walls.size(); ++w) {
        if (removed.count(w)) { continue; }
        wallMap[w] = candidate.m_walls.size();
        auto wall = m_walls[w];
        if (wall.forwardSector) { wall.forwardSector = sectorMap[*wall.forwardSector]; }
        if (wall.reverseSector) { wall.reverseSector = sectorMap[*wall.reverseSector]; }
        candidate.m_walls.push_back(wall);
    }
    for (auto &sector : candidate.m_sectors) {
        for (auto &wall : sector.walls) { wall = wallMap[wall]; }
    }
    for (auto &sprite : candidate.m_sprites) {
        if (sprite.sectorId) { sprite.sectorId = sectorMap[*sprite.sectorId]; }
    }
    if (candidate.m_playerStart.sectorId) {
        candidate.m_playerStart.sectorId = sectorMap[*candidate.m_playerStart.sectorId];
    }
    std::vector<bool> used(candidate.m_vertices.size());
    for (const auto &wall : candidate.m_walls) { used[wall.start] = used[wall.end] = true; }
    std::vector<VertexId> vertexMap(candidate.m_vertices.size());
    std::vector<Vertex> vertices;
    for (VertexId v = 0; v < used.size(); ++v) {
        if (used[v]) { vertexMap[v] = vertices.size(); vertices.push_back(candidate.m_vertices[v]); }
    }
    for (auto &wall : candidate.m_walls) {
        wall.start = vertexMap[wall.start]; wall.end = vertexMap[wall.end];
    }
    for (auto &sector : candidate.m_sectors) {
        for (auto &v : sector.vertices) { v = vertexMap[v]; }
    }
    candidate.m_vertices = std::move(vertices);
    *this = std::move(candidate);
    return sectorMap[donor];
}

bool MapDocument::removeSectors(const std::vector<SectorId> &ids, QString &error)
{
    error.clear();
    const std::set<SectorId> removed(ids.begin(), ids.end());
    if (removed.empty() || *removed.rbegin() >= m_sectors.size()) {
        error = "Select sectors to delete.";
        return false;
    }
    auto candidate = *this;
    std::vector<std::optional<SectorId>> sectorMap(m_sectors.size());
    candidate.m_sectors.clear();
    for (SectorId s = 0; s < m_sectors.size(); ++s) {
        if (removed.count(s)) { continue; }
        sectorMap[s] = candidate.m_sectors.size();
        candidate.m_sectors.push_back(m_sectors[s]);
    }
    std::vector<bool> usedWall(m_walls.size());
    for (const auto &sector : candidate.m_sectors) {
        for (auto w : sector.walls) { usedWall[w] = true; }
    }
    std::vector<WallId> wallMap(m_walls.size());
    candidate.m_walls.clear();
    for (WallId w = 0; w < m_walls.size(); ++w) {
        const auto &old = m_walls[w];
        // Retain unrelated unfinished lines, but remove orphaned sector walls.
        if (!usedWall[w] && (old.forwardSector || old.reverseSector)) { continue; }
        wallMap[w] = candidate.m_walls.size();
        auto wall = old;
        if (wall.forwardSector) { wall.forwardSector = sectorMap[*wall.forwardSector]; }
        if (wall.reverseSector) { wall.reverseSector = sectorMap[*wall.reverseSector]; }
        candidate.m_walls.push_back(wall);
    }
    for (auto &sector : candidate.m_sectors) {
        for (auto &w : sector.walls) { w = wallMap[w]; }
    }
    std::vector<QPainterPath> paths;
    for (const auto &sector : m_sectors) {
        QPainterPath path;
        path.setFillRule(Qt::OddEvenFill);
        for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
            const auto p = m_vertices[sector.vertices[i]].position;
            if (i == 0 || std::find(sector.loopStarts.begin(), sector.loopStarts.end(), i) != sector.loopStarts.end()) {
                path.moveTo(p);
            } else { path.lineTo(p); }
            if (sector.nextWallIndex(i) <= i) { path.closeSubpath(); }
        }
        paths.push_back(path);
    }
    const auto ownerAt = [&](QPointF position, std::optional<SectorId> hint) {
        if (hint && *hint < paths.size() && paths[*hint].contains(position)) { return hint; }
        std::optional<SectorId> found;
        for (SectorId s = 0; s < paths.size(); ++s) {
            if (!paths[s].contains(position)) { continue; }
            if (found) { return std::optional<SectorId>{}; }
            found = s;
        }
        return found;
    };
    std::vector<int> spriteMap(m_sprites.size(), -1);
    candidate.m_sprites.clear();
    for (SpriteId s = 0; s < m_sprites.size(); ++s) {
        auto sprite = m_sprites[s];
        auto owner = ownerAt(sprite.position, sprite.sectorId);
        if (owner && removed.count(*owner)) { continue; }
        sprite.sectorId = owner ? sectorMap[*owner] : std::nullopt;
        spriteMap[s] = static_cast<int>(candidate.m_sprites.size());
        candidate.m_sprites.push_back(sprite);
    }
    for (auto &sprite : candidate.m_sprites) {
        if (sprite.owner >= 0 && std::size_t(sprite.owner) < spriteMap.size()) {
            sprite.owner = spriteMap[sprite.owner];
        }
    }
    const auto playerOwner = ownerAt(m_playerStart.position, m_playerStart.sectorId);
    candidate.m_playerStart.sectorId = playerOwner ? sectorMap[*playerOwner] : std::nullopt;
    std::vector<bool> usedVertex(m_vertices.size());
    for (const auto &wall : candidate.m_walls) { usedVertex[wall.start] = usedVertex[wall.end] = true; }
    std::vector<VertexId> vertexMap(m_vertices.size());
    candidate.m_vertices.clear();
    for (VertexId v = 0; v < m_vertices.size(); ++v) {
        if (usedVertex[v]) { vertexMap[v] = candidate.m_vertices.size(); candidate.m_vertices.push_back(m_vertices[v]); }
    }
    for (auto &wall : candidate.m_walls) {
        wall.start = vertexMap[wall.start]; wall.end = vertexMap[wall.end];
    }
    for (auto &sector : candidate.m_sectors) {
        for (auto &v : sector.vertices) { v = vertexMap[v]; }
    }
    // The general face builder would fill an empty hole back in. Detect that
    // situation without replacing the deliberately preserved sector loops.
    if (candidate.m_sectors.empty() && candidate.m_walls.empty()) { candidate.m_complexTopology = false; }
    if (!candidate.m_complexTopology) {
        auto rebuilt = candidate;
        rebuilt.rebuildSectors();
        candidate.m_complexTopology = rebuilt.m_sectors != candidate.m_sectors;
    }
    *this = std::move(candidate);
    return true;
}

bool MapDocument::removeVertices(const std::vector<VertexId> &ids, QString &error)
{
    error.clear();
    const std::set<VertexId> selected(ids.begin(), ids.end());
    if (selected.empty() || *selected.rbegin() >= m_vertices.size()) {
        error = "Select vertices to delete."; return false;
    }
    auto candidate = *this;
    std::set<WallId> removedWalls;
    for (auto v : selected) {
        std::vector<WallId> incident;
        for (WallId w = 0; w < candidate.m_walls.size(); ++w) {
            const auto &wall = candidate.m_walls[w];
            if (!removedWalls.count(w) && (wall.start == v || wall.end == v)) { incident.push_back(w); }
        }
        if (incident.size() != 2) {
            error = "Only vertices connecting exactly two walls can be deleted. Edit junction walls or sectors first."; return false;
        }
        const auto keep = incident[0], drop = incident[1];
        auto &wall = candidate.m_walls[keep];
        const auto &other = candidate.m_walls[drop];
        const auto a = wall.start == v ? wall.end : wall.start;
        const auto b = other.start == v ? other.end : other.start;
        if (a == b) { error = "Deleting these vertices would collapse a boundary."; return false; }
        Wall merged = wall;
        merged.start = a; merged.end = b;
        merged.forwardSector.reset(); merged.reverseSector.reset();
        for (SectorId id = 0; id < candidate.m_sectors.size(); ++id) {
            auto &sector = candidate.m_sectors[id];
            const auto found = std::find(sector.vertices.begin(),sector.vertices.end(),v);
            if (found == sector.vertices.end()) { continue; }
            const std::size_t index = found - sector.vertices.begin();
            std::size_t prev = index;
            for (std::size_t j = 0; j < sector.vertices.size(); ++j) {
                if (sector.nextWallIndex(j) == index) { prev = j; break; }
            }
            const auto next = sector.nextWallIndex(index);
            std::size_t count = 1;
            for (auto j = next; j != index; j = sector.nextWallIndex(j)) { ++count; }
            if (count <= 3) { error = "A boundary needs at least three vertices. Delete the sector instead."; return false; }
            if ((index == 0 || prev == 0) &&
                ((((sector.floorstat & 2) && sector.floorheinum) || ((sector.ceilingstat & 2) && sector.ceilingheinum))
                 || ((sector.floorstat | sector.ceilingstat) & 64))) {
                error = "This would change the first wall used by a slope or relative texture alignment."; return false;
            }
            const auto &incoming = candidate.m_walls[sector.walls[prev]];
            const bool incomingReverse = incoming.end == sector.vertices[prev];
            const auto side = incomingReverse ? incoming.reverseSide : incoming.forwardSide;
            const bool reversed = sector.vertices[prev] == b;
            (reversed ? merged.reverseSide : merged.forwardSide) = side;
            (reversed ? merged.reverseSector : merged.forwardSector) = id;
            sector.walls[prev] = keep;
            sector.walls.erase(sector.walls.begin()+index);
            sector.vertices.erase(sector.vertices.begin()+index);
            for (auto &start : sector.loopStarts) { if (start > index) { --start; } }
        }
        wall = merged;
        removedWalls.insert(drop);
    }
    std::vector<WallId> wallMap(candidate.m_walls.size());
    std::vector<Wall> walls;
    for (WallId w = 0; w < candidate.m_walls.size(); ++w) {
        if (!removedWalls.count(w)) { wallMap[w] = walls.size(); walls.push_back(candidate.m_walls[w]); }
    }
    std::vector<VertexId> vertexMap(m_vertices.size());
    candidate.m_vertices.clear();
    for (VertexId v = 0; v < m_vertices.size(); ++v) {
        if (!selected.count(v)) { vertexMap[v] = candidate.m_vertices.size(); candidate.m_vertices.push_back(m_vertices[v]); }
    }
    for (auto &wall : walls) { wall.start = vertexMap[wall.start]; wall.end = vertexMap[wall.end]; }
    candidate.m_walls = std::move(walls);
    for (auto &sector : candidate.m_sectors) {
        for (auto &v : sector.vertices) { v = vertexMap[v]; }
        for (auto &w : sector.walls) { w = wallMap[w]; }
    }
    // Check resulting loops before committing a cut across a concave boundary.
    for (const auto &sector : candidate.m_sectors) {
        double area = 0;
        std::size_t loopStart = 0;
        for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
            const auto next = sector.nextWallIndex(i);
            const auto a = candidate.m_vertices[sector.vertices[i]].position;
            const auto b = candidate.m_vertices[sector.vertices[next]].position;
            area += a.x()*b.y()-a.y()*b.x();
            if (next <= i) {
                if ((loopStart == 0 && area <= coordinateEpsilon) || (loopStart != 0 && area >= -coordinateEpsilon)) {
                    error = "Deleting these vertices would collapse or invert a boundary."; return false;
                }
                loopStart = i+1; area = 0;
            }
            for (std::size_t j = i+1; j < sector.vertices.size(); ++j) {
                if (next == j || sector.nextWallIndex(j) == i) { continue; }
                const auto c = candidate.m_vertices[sector.vertices[j]].position;
                const auto d = candidate.m_vertices[sector.vertices[sector.nextWallIndex(j)]].position;
                if (QLineF(a,b).intersects(QLineF(c,d),nullptr) == QLineF::BoundedIntersection) {
                    error = "Deleting these vertices would intersect another boundary."; return false;
                }
            }
        }
    }
    *this = std::move(candidate);
    return true;
}

void MapDocument::removeWalls(const std::vector<WallId> &wallIds)
{
    if (!supportsLineDeletion()) return;
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

    // Hole loops are derived from the surrounding wall graph during rebuilding.
    // Match surviving properties against each sector's outer boundary only.
    for (auto &sector : m_sectors) {
        if (sector.loopStarts.size() > 1) {
            sector.walls.resize(sector.loopStarts[1]);
            sector.vertices.resize(sector.loopStarts[1]);
        }
        sector.loopStarts.clear();
    }

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
    std::vector<std::optional<SectorId>> sectorMapping(m_sectors.size());
    SectorId oldSector = 0, nextSector = 0;
    m_sectors.erase(std::remove_if(m_sectors.begin(), m_sectors.end(),
        [&](const Sector &sector) {
            auto walls = sector.walls;
            std::sort(walls.begin(), walls.end());
            const bool collapsed = walls.size() < 3
                || std::adjacent_find(walls.begin(), walls.end()) != walls.end();
            if (!collapsed) sectorMapping[oldSector] = nextSector++;
            ++oldSector;
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
    if (!m_complexTopology) {
        rebuildSectors();
    } else {
        // Imported overlapping rooms retain their existing sector boundaries.
        // Collapsing edges does not require planar face reconstruction.
        for (auto &wall : m_walls) {
            wall.forwardSector.reset();
            wall.reverseSector.reset();
        }
        for (SectorId id = 0; id < m_sectors.size(); ++id) {
            const auto &sector = m_sectors[id];
            for (std::size_t i = 0; i < sector.walls.size(); ++i) {
                auto &wall = m_walls[sector.walls[i]];
                (wall.start == sector.vertices[i] ? wall.forwardSector : wall.reverseSector) = id;
            }
        }
        const auto remap = [&](std::optional<SectorId> &id) {
            id = id && *id < sectorMapping.size() ? sectorMapping[*id] : std::nullopt;
        };
        remap(m_playerStart.sectorId);
        for (auto &sprite : m_sprites) remap(sprite.sectorId);
    }
}

bool MapDocument::supportsLineDeletion() const
{
    if (!m_complexTopology) return true;
    return std::all_of(m_sectors.begin(), m_sectors.end(), [](const Sector &sector) {
        return sector.loopStarts.size() <= 1 && sector.walls.size() >= 3;
    });
}

void MapDocument::setVertexPositions(
    const std::vector<std::pair<VertexId, QPointF>> &positions, const MapDocument *scaleReference)
{
    const MapDocument previous = scaleReference ? *scaleReference : *this;
    for (const auto &[vertexId, position] : positions) {
        if (vertexId < m_vertices.size()) {
            m_vertices[vertexId].position = position;
        }
    }
    for (WallId id = 0; id < m_walls.size() && id < previous.m_walls.size(); ++id) {
        auto &wall = m_walls[id];
        const auto &old = previous.m_walls[id];
        const auto before = previous.m_vertices[old.end].position - previous.m_vertices[old.start].position;
        const auto after = m_vertices[wall.end].position - m_vertices[wall.start].position;
        const qreal oldLength = std::hypot(before.x(), before.y());
        const qreal newLength = std::hypot(after.x(), after.y());
        if (oldLength > coordinateEpsilon && newLength > coordinateEpsilon) {
            const auto resized = [&](int value) {
                if (!value || std::abs(newLength-oldLength) < coordinateEpsilon) { return value; }
                return repeat(value * newLength / oldLength);
            };
            wall.forwardSide.xrepeat = resized(old.forwardSide.xrepeat);
            wall.reverseSide.xrepeat = resized(old.reverseSide.xrepeat);
        }
    }
    if (!m_complexTopology) rebuildSectors();
}

int MapDocument::defaultWallXRepeat(WallId id) const
{
    if (id >= m_walls.size()) { return 8; }
    const auto &wall = m_walls[id];
    const auto delta = m_vertices[wall.end].position - m_vertices[wall.start].position;
    // 16 horizontal Build units per ART pixel, matching ordinary sector textures.
    return repeat(std::hypot(delta.x(), delta.y()) / 128.0);
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

    auto previousSectors = std::move(m_sectors);
    for (auto &sector : previousSectors) {
        if (sector.loopStarts.size() > 1) {
            sector.walls.resize(sector.loopStarts[1]);
            sector.vertices.resize(sector.loopStarts[1]);
        }
        sector.loopStarts.clear();
    }
    m_sectors.clear();
    std::vector<std::optional<Sector>> survivingSectors(previousSectors.size());
    std::vector<Sector> newSectors;
    std::vector<Sector> exteriorLoops;
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
                    } else if (sector.vertices.size() >= 3 && twiceArea < -coordinateEpsilon) {
                        exteriorLoops.push_back(std::move(sector));
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
    const auto firstNewSector = m_sectors.size();
    for (auto &sector : newSectors) m_sectors.push_back(std::move(sector));

    // A disconnected component inside a face contributes a clockwise hole to
    // that face. Its opposite wall sides already bound the inner sector(s).
    // Attach it to the smallest enclosing face, supporting nested islands.
    const auto outline = [this](const Sector &sector) {
        QPainterPath path;
        path.moveTo(m_vertices[sector.vertices.front()].position);
        for (std::size_t i = 1; i < sector.vertices.size(); ++i)
            path.lineTo(m_vertices[sector.vertices[i]].position);
        path.closeSubpath();
        return path;
    };
    std::vector<QPainterPath> outlines;
    std::vector<qreal> areas;
    for (const auto &sector : m_sectors) {
        outlines.push_back(outline(sector));
        qreal area = 0;
        for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
            const auto a = m_vertices[sector.vertices[i]].position;
            const auto b = m_vertices[sector.vertices[(i + 1) % sector.vertices.size()]].position;
            area += a.x() * b.y() - b.x() * a.y();
        }
        areas.push_back(area);
    }
    // New nested faces inherit only heights; surviving sectors retain their
    // own values. Process outer faces first for multiple new nested loops.
    std::vector<SectorId> newFaces;
    for (SectorId id = firstNewSector; id < m_sectors.size(); ++id) {
        newFaces.push_back(id);
    }
    std::sort(newFaces.begin(), newFaces.end(), [&](SectorId a, SectorId b) {
        return areas[a] > areas[b];
    });
    for (const auto child : newFaces) {
        std::optional<SectorId> parent;
        for (SectorId id = 0; id < m_sectors.size(); ++id) {
            if (areas[id] > areas[child] && outlines[id].contains(outlines[child])
                && (!parent || areas[id] < areas[*parent])) {
                parent = id;
            }
        }
        if (parent) {
            m_sectors[child].floorz = m_sectors[*parent].floorz;
            m_sectors[child].ceilingz = m_sectors[*parent].ceilingz;
        }
    }

    for (const auto &hole : exteriorLoops) {
        const auto holePath = outline(hole);
        std::optional<SectorId> parent;
        for (SectorId id = 0; id < m_sectors.size(); ++id) {
            const auto &sector = m_sectors[id];
            const bool sharesWall = std::any_of(hole.walls.begin(), hole.walls.end(), [&](WallId wall) {
                return std::find(sector.walls.begin(), sector.walls.end(), wall) != sector.walls.end();
            });
            if (!sharesWall && outlines[id].contains(holePath)
                && (!parent || areas[id] < areas[*parent])) parent = id;
        }
        if (!parent) continue;
        auto &sector = m_sectors[*parent];
        if (sector.loopStarts.empty()) sector.loopStarts.push_back(0);
        sector.loopStarts.push_back(sector.walls.size());
        sector.walls.insert(sector.walls.end(), hole.walls.begin(), hole.walls.end());
        sector.vertices.insert(sector.vertices.end(), hole.vertices.begin(), hole.vertices.end());
    }

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
