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
#include <array>

namespace {
constexpr qreal coordinateEpsilon = 0.001;
int repeat(qreal value)
{
    return static_cast<int>(std::round(std::clamp(value, qreal(1), qreal(255))));
}

QPainterPath sectorShape(const MapDocument &document, const MapDocument::Sector &sector)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
        const auto p = document.vertices()[sector.vertices[i]].position;
        if (i == 0 || std::find(sector.loopStarts.begin(), sector.loopStarts.end(), i)
                      != sector.loopStarts.end()) path.moveTo(p);
        else path.lineTo(p);
        if (sector.nextWallIndex(i) <= i) path.closeSubpath();
    }
    return path;
}

bool interiorsOverlap(const QPainterPath &a, const QPainterPath &b)
{
    for (const auto &polygon : a.intersected(b).toFillPolygons()) {
        qreal area = 0;
        for (int i = 0; i < polygon.size(); ++i) {
            const auto p = polygon[i], q = polygon[(i + 1) % polygon.size()];
            area += p.x() * q.y() - q.x() * p.y();
        }
        if (std::abs(area) > coordinateEpsilon) return true;
    }
    return false;
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
    if (sectorId >= m_sectors.size()) { return; }
    const auto before = m_sectors[sectorId];
    m_sectors[sectorId] = sector;
    if (before.floorz != sector.floorz || before.floorheinum != sector.floorheinum
        || ((before.floorstat ^ sector.floorstat) & 2)) { propagateTrorPlane(sectorId, true); }
    if (before.ceilingz != sector.ceilingz || before.ceilingheinum != sector.ceilingheinum
        || ((before.ceilingstat ^ sector.ceilingstat) & 2)) { propagateTrorPlane(sectorId, false); }
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
    m_playerStart = {{0.0, 0.0}, -4096.0, 0.0};
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

std::vector<MapDocument::SectorId> MapDocument::sectorsAt(const QPointF &position) const
{
    std::vector<SectorId> result;
    for (SectorId s = 0; s < m_sectors.size(); ++s) {
        bool contains = sectorShape(*this, m_sectors[s]).contains(position);
        for (const auto id : m_sectors[s].walls) {
            const auto &wall = m_walls[id];
            const auto a = m_vertices[wall.start].position, b = m_vertices[wall.end].position;
            const auto d = b - a, p = position - a;
            contains |= QPointF::dotProduct(position - a, position - b) <= 0
                && std::abs(d.x()*p.y() - d.y()*p.x()) <= coordinateEpsilon;
        }
        if (contains) { result.push_back(s); }
    }
    return result;
}

bool MapDocument::validateTopologyChange(const MapDocument &before, QString &error) const
{
    if (!validateTror(error)) { return false; }
    const auto fail = [&](const QString &why) { error = why; return false; };
    for (SectorId s = 0; s < m_sectors.size(); ++s) {
        const auto &sector = m_sectors[s];
        if (sector.walls.size() != sector.vertices.size() || sector.walls.empty()) {
            return fail("A sector boundary is incomplete.");
        }
        std::vector<QPointF> points;
        for (std::size_t i = 0; i < sector.walls.size(); ++i) {
            const auto w = sector.walls[i], v = sector.vertices[i];
            if (w >= m_walls.size() || v >= m_vertices.size() || sector.nextWallIndex(i) >= sector.vertices.size()) {
                return fail("A sector boundary contains an invalid reference.");
            }
            const auto &wall = m_walls[w];
            const auto end = sector.vertices[sector.nextWallIndex(i)];
            const bool forward = wall.start == v && wall.end == end;
            const bool reverse = wall.end == v && wall.start == end;
            if ((!forward && !reverse) || (forward ? wall.forwardSector : wall.reverseSector) != s) {
                return fail("The edit would break a wall's sector connection.");
            }
            points.push_back(m_vertices[v].position);
        }
        const auto sameBoundary = [&](const Sector &old) {
            if (old.vertices.size() != points.size() || old.loopStarts != sector.loopStarts) { return false; }
            for (std::size_t i = 0; i < points.size(); ++i) {
                if (before.m_vertices[old.vertices[i]].position != points[i]) { return false; }
            }
            return true;
        };
        const bool unchanged = (s < before.m_sectors.size() && sameBoundary(before.m_sectors[s]))
            || std::any_of(before.m_sectors.begin(), before.m_sectors.end(), sameBoundary);
        if (unchanged) { continue; }
        auto loops = sector.loopStarts;
        if (loops.empty()) { loops.push_back(0); }
        if (loops.front() != 0) { return fail("A sector has an invalid first loop."); }
        loops.push_back(points.size());
        for (std::size_t loop = 0; loop + 1 < loops.size(); ++loop) {
            const auto first = loops[loop], last = loops[loop + 1];
            if (last <= first + 2 || last > points.size()) { return fail("An edited loop must have at least three walls."); }
            double area = 0;
            for (auto i = first; i < last; ++i) {
                const auto a = points[i], b = points[sector.nextWallIndex(i)];
                if (!std::isfinite(a.x()) || !std::isfinite(a.y()) || QLineF(a,b).length() < coordinateEpsilon) {
                    return fail("The edit would create an invalid or zero-length wall.");
                }
                area += a.x()*b.y() - b.x()*a.y();
            }
            if ((loop == 0 && area <= coordinateEpsilon) || (loop != 0 && area >= -coordinateEpsilon)) {
                return fail("The edit would collapse or reverse a sector loop.");
            }
        }
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto a = points[i], b = points[sector.nextWallIndex(i)];
            for (std::size_t j = i + 1; j < points.size(); ++j) {
                const auto c = points[j], d = points[sector.nextWallIndex(j)];
                if ((a == c && b == d) || (a == d && b == c)) {
                    return fail("The edit would retrace a sector wall.");
                }
                QPointF intersection;
                const bool adjacent = sector.nextWallIndex(i) == j || sector.nextWallIndex(j) == i;
                if (QLineF(a,b).intersects(QLineF(c,d), &intersection) == QLineF::BoundedIntersection && !adjacent) {
                    return fail("The edit would make a sector boundary cross itself or another of its loops.");
                }
                const auto inside = [](QPointF p, QPointF x, QPointF y) {
                    const auto u = y-x, v = p-x;
                    return std::abs(u.x()*v.y()-u.y()*v.x()) < coordinateEpsilon
                        && QPointF::dotProduct(p-x,p-y) < -coordinateEpsilon;
                };
                if (inside(c,a,b) || inside(d,a,b) || inside(a,c,d) || inside(b,c,d)) {
                    return fail("The edit would make walls within a sector overlap.");
                }
            }
        }
        const auto outerEnd = loops[1];
        QPainterPath outer(points.front());
        for (std::size_t i = 1; i < outerEnd; ++i) { outer.lineTo(points[i]); }
        outer.closeSubpath();
        std::vector<QPainterPath> holes;
        for (std::size_t loop = 1; loop + 1 < loops.size(); ++loop) {
            if (!outer.contains(points[loops[loop]])) { return fail("An inner loop would leave its owning sector."); }
            QPainterPath hole(points[loops[loop]]);
            for (auto i = loops[loop] + 1; i < loops[loop + 1]; ++i) { hole.lineTo(points[i]); }
            hole.closeSubpath();
            for (const auto &other : holes) {
                if (interiorsOverlap(hole, other)) { return fail("Inner loops within a sector would overlap."); }
            }
            holes.push_back(hole);
        }
    }
    return true;
}

bool MapDocument::addPolyline(const std::vector<QPointF> &points, bool closed, QString *error,
                              const std::optional<std::set<SectorId>> &editable)
{
    if (error) error->clear();
    if (editable || m_complexTopology) {
        if (editable && std::any_of(editable->begin(), editable->end(), [this](auto id) { return id >= m_sectors.size(); })) {
            if (error) { *error = "The editing scope is stale. Choose the sectors again."; }
            return false;
        }
        auto candidate = *this;
        const auto resolveMembership = [this](auto &member) {
            if (!member.sectorId) {
                const auto owners = sectorsAt(member.position);
                if (owners.size() == 1) { member.sectorId = owners.front(); }
            }
        };
        resolveMembership(candidate.m_playerStart);
        for (auto &sprite : candidate.m_sprites) { resolveMembership(sprite); }
        auto resolvedPoints = points;
        const auto wallInScope = [&editable](const Wall &wall) {
            return !editable || (wall.forwardSector && editable->count(*wall.forwardSector))
                || (wall.reverseSector && editable->count(*wall.reverseSector));
        };
        if (points.size() >= 2) {
            resolvedPoints.clear();
            const auto segments = closed ? points.size() : points.size() - 1;
            for (std::size_t i = 0; i < segments; ++i) {
                const auto a = points[i], b = points[(i + 1) % points.size()];
                std::vector<std::pair<qreal, QPointF>> intersections{{0, a}};
                for (const auto &wall : m_walls) {
                    if (!wallInScope(wall)) { continue; }
                    QPointF hit;
                    if (QLineF(a,b).intersects(QLineF(m_vertices[wall.start].position,
                        m_vertices[wall.end].position), &hit) == QLineF::BoundedIntersection
                        && QLineF(hit,a).length() > coordinateEpsilon && QLineF(hit,b).length() > coordinateEpsilon) {
                        intersections.emplace_back(QLineF(a,hit).length(), hit);
                    }
                }
                std::sort(intersections.begin(), intersections.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
                for (const auto &[distance, point] : intersections) {
                    Q_UNUSED(distance);
                    if (resolvedPoints.empty() || QLineF(resolvedPoints.back(), point).length() > coordinateEpsilon) {
                        resolvedPoints.push_back(point);
                    }
                }
            }
            if (!closed) { resolvedPoints.push_back(points.back()); }
        }
        // Split eligible boundaries on the transaction copy, including real
        // portal peers. Scoped preflight below still rejects ambiguous overlaps
        // when no explicit filter resolves the intended target.
        {
            for (const auto &point : resolvedPoints) {
                const auto count = candidate.m_walls.size();
                for (WallId w = 0; w < count; ++w) {
                    const auto &wall = candidate.m_walls[w];
                    if (wallInScope(wall)) {
                        (void)candidate.splitWall(w, point);
                    }
                }
            }
        }
        if (!candidate.addScopedPolyline(resolvedPoints, closed, error, editable)) { return false; }
        QString validationError;
        if (!candidate.validateTopologyChange(*this, validationError)) {
            if (error) { *error = validationError; }
            return false;
        }
        candidate.m_complexTopology = true;
        *this = std::move(candidate);
        return true;
    }
    if (points.size() < 2) {
        return false;
    }

    const auto original = *this;
    // Join drawing points to the existing boundary before creating new edges.
    // A coincident vertex alone does not connect a new room to an unsplit wall.
    for (const auto &point : points) {
        const auto wallCount = m_walls.size();
        for (WallId wall = 0; wall < wallCount; ++wall) {
            (void)splitWall(wall, point);
        }
    }
    const auto attachmentSectors = m_sectors;
    auto voidSides = voidWallSides();
    const std::size_t originalSectorCount = m_sectors.size();

    std::vector<VertexId> vertexIds;
    vertexIds.reserve(points.size());
    for (const QPointF &point : points) {
        vertexIds.push_back(findOrAddVertex(point));
    }

    const std::size_t segmentCount = closed ? points.size() : points.size() - 1;
    const std::size_t existingWallCount = m_walls.size();

    for (std::size_t index = 0; index < segmentCount; ++index) {
        const VertexId start = vertexIds[index];
        const VertexId end = vertexIds[(index + 1) % vertexIds.size()];
        if (start == end) {
            continue;
        }

        // A closing segment can follow several existing wall pieces. Reuse
        // each piece instead of laying one long wall over the whole boundary.
        const QPointF a = m_vertices[start].position;
        const QPointF delta = m_vertices[end].position - a;
        const qreal length = std::hypot(delta.x(), delta.y());
        std::vector<std::pair<qreal, VertexId>> along{{0, start}, {length, end}};
        for (VertexId vertex = 0; vertex < m_vertices.size(); ++vertex) {
            if (vertex == start || vertex == end) continue;
            const QPointF offset = m_vertices[vertex].position - a;
            const qreal distance = QPointF::dotProduct(offset, delta) / length;
            if (distance > coordinateEpsilon && distance < length - coordinateEpsilon
                && std::abs(offset.x() * delta.y() - offset.y() * delta.x()) / length <= coordinateEpsilon) {
                along.emplace_back(distance, vertex);
            }
        }
        std::sort(along.begin(), along.end());
        for (std::size_t part = 1; part < along.size(); ++part) {
            const auto from = along[part - 1].second, to = along[part].second;
            const bool wallExists = std::any_of(
                m_walls.begin(), m_walls.end(), [from, to](const Wall &wall) {
                    return (wall.start == from && wall.end == to)
                        || (wall.start == to && wall.end == from);
                });
            if (wallExists) continue;

            m_walls.push_back({from, to});
            const int density = defaultWallXRepeat(m_walls.size() - 1);
            m_walls.back().forwardSide.xrepeat = density;
            m_walls.back().reverseSide.xrepeat = density;
        }
    }

    voidSides.resize(m_walls.size() * 2, false);
    rebuildSectors(std::move(voidSides));
    if (m_sectors.size() > originalSectorCount) {
        // Prefer the attached sector whose interior is being subdivided.
        // A boundary vertex can also belong to its neighbors or a parent's hole.
        // For a new room outside existing interiors, use the first attachment.
        // Use vertex identity, not coordinates, to keep independent rooms apart.
        // Existing faces retain their properties; nested unattached faces have
        // already inherited their containing sector's heights in rebuildSectors.
        for (auto &sector : m_sectors) {
            const bool survived = std::any_of(attachmentSectors.begin(), attachmentSectors.end(),
                [&](const Sector &old) {
                    return std::is_permutation(sector.walls.begin(), sector.walls.end(),
                                               old.walls.begin(), old.walls.end());
                });
            if (survived) continue;
            const auto shape = sectorShape(*this, sector);
            const Sector *source = nullptr;
            bool subdivided = false;
            for (const auto vertex : vertexIds) {
                if (std::find(sector.vertices.begin(), sector.vertices.end(), vertex) == sector.vertices.end())
                    continue;
                for (const auto &candidate : attachmentSectors) {
                    if (std::find(candidate.vertices.begin(), candidate.vertices.end(), vertex) == candidate.vertices.end())
                        continue;
                    if (!source) source = &candidate;
                    if (interiorsOverlap(shape, sectorShape(*this, candidate))) {
                        source = &candidate;
                        subdivided = true;
                        break;
                    }
                }
                if (subdivided) break;
            }
            if (source) {
                sector.floorz = source->floorz;
                sector.ceilingz = source->ceilingz;
                sector.floorTexture = source->floorTexture;
                sector.ceilingTexture = source->ceilingTexture;

                const auto sourceId = static_cast<SectorId>(source - attachmentSectors.data());
                for (const auto wallId : sector.walls) {
                    if (wallId < existingWallCount) continue;
                    auto &newWall = m_walls[wallId];
                    const QPointF a = m_vertices[newWall.start].position;
                    const QPointF b = m_vertices[newWall.end].position;
                    const QPointF midpoint = (a + b) / 2.0;
                    qreal nearestDistance = std::numeric_limits<qreal>::infinity();
                    std::optional<int> texture;
                    for (const auto connectedWallId : source->walls) {
                        const auto &connectedWall = m_walls[connectedWallId];
                        const auto start = m_vertices[connectedWall.start].position;
                        const auto delta = m_vertices[connectedWall.end].position - start;
                        const qreal lengthSquared = QPointF::dotProduct(delta, delta);
                        if (lengthSquared <= 0) continue;
                        const qreal t = std::clamp(QPointF::dotProduct(midpoint - start, delta)
                                                   / lengthSquared, qreal(0), qreal(1));
                        const qreal distance = QLineF(midpoint, start + t * delta).length();
                        if (distance >= nearestDistance) continue;
                        if (connectedWall.forwardSector == sourceId) {
                            texture = connectedWall.forwardSide.texture;
                        } else if (connectedWall.reverseSector == sourceId) {
                            texture = connectedWall.reverseSide.texture;
                        } else {
                            continue;
                        }
                        nearestDistance = distance;
                    }
                    if (texture) {
                        newWall.forwardSide.texture = *texture;
                        newWall.reverseSide.texture = *texture;
                    }
                }
            }
        }
        return true;
    }

    *this = original;
    return false;
}

bool MapDocument::addScopedPolyline(const std::vector<QPointF> &points, bool closed, QString *error,
                                    const std::optional<std::set<SectorId>> &editable)
{
    const auto fail = [&](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (points.size() < 2) return false;
    for (const auto &p : points) {
        if (!std::isfinite(p.x()) || !std::isfinite(p.y())) return fail("Invalid drawing coordinates.");
    }
    const auto onSegment = [](const QPointF &p, const QPointF &a, const QPointF &b) {
        const auto d = b - a, v = p - a;
        return std::abs(d.x() * v.y() - d.y() * v.x()) < coordinateEpsilon
            && p.x() >= std::min(a.x(), b.x()) && p.x() <= std::max(a.x(), b.x())
            && p.y() >= std::min(a.y(), b.y()) && p.y() <= std::max(a.y(), b.y());
    };
    QPainterPath drawing;
    drawing.moveTo(points.front());
    for (std::size_t i = 1; i < points.size(); ++i) drawing.lineTo(points[i]);
    if (closed) drawing.closeSubpath();
    std::vector<QPainterPath> shapes;
    std::vector<bool> affected(m_sectors.size(), false);
    for (SectorId s = 0; s < m_sectors.size(); ++s) {
        shapes.push_back(sectorShape(*this, m_sectors[s]));
        affected[s] = (!editable || editable->count(s)) && shapes.back().intersects(drawing);
    }
    const auto segmentCount = closed ? points.size() : points.size() - 1;
    for (std::size_t i = 0; i < segmentCount; ++i) {
        const auto a = points[i], b = points[(i + 1) % points.size()];
        if (a == b) return fail("Drawing contains a zero-length line.");
        for (std::size_t j = i + 1; j < segmentCount; ++j) {
            const auto p = points[j], q = points[(j + 1) % points.size()];
            QPointF intersection;
            const bool adjacent = j == i + 1 || (closed && i == 0 && j + 1 == segmentCount);
            if ((!adjacent && (QLineF(a, b).intersects(QLineF(p, q), &intersection)
                                  == QLineF::BoundedIntersection
                              || onSegment(p, a, b) || onSegment(q, a, b)))
                || (onSegment(p, a, b) && p != a && p != b)
                || (onSegment(q, a, b) && q != a && q != b)
                || (onSegment(a, p, q) && a != p && a != q)
                || (onSegment(b, p, q) && b != p && b != q)
                || (a == q && b == p))
                return fail("Drawing must not cross or retrace itself.");
        }
    }
    // Include every touched boundary, including collinear edges and endpoints.
    // The face builder requires explicit vertices at intersections.
    for (const auto &wall : m_walls) {
        if (editable && !(wall.forwardSector && editable->count(*wall.forwardSector))
            && !(wall.reverseSector && editable->count(*wall.reverseSector))) { continue; }
        const auto a = m_vertices[wall.start].position, b = m_vertices[wall.end].position;
        for (std::size_t i = 0; i < segmentCount; ++i) {
            const auto p = points[i], q = points[(i + 1) % points.size()];
            QPointF intersection;
            const bool crosses = QLineF(a, b).intersects(QLineF(p, q), &intersection)
                == QLineF::BoundedIntersection;
            const bool touches = crosses || onSegment(p, a, b) || onSegment(q, a, b)
                || onSegment(a, p, q) || onSegment(b, p, q);
            if (!touches) continue;
            if (!wall.forwardSector && !wall.reverseSector)
                return fail("Drawing touches an unsupported loose wall.");
            if (wall.forwardSector && (!editable || editable->count(*wall.forwardSector))) affected[*wall.forwardSector] = true;
            if (wall.reverseSector && (!editable || editable->count(*wall.reverseSector))) affected[*wall.reverseSector] = true;
            if ((crosses && ((intersection != a && intersection != b)
                             || (intersection != p && intersection != q)))
                || (onSegment(p, a, b) && p != a && p != b)
                || (onSegment(q, a, b) && q != a && q != b)
                || (onSegment(a, p, q) && a != p && a != q)
                || (onSegment(b, p, q) && b != p && b != q))
                return fail("Drawing crosses an existing wall. Insert vertices at the intersections first.");
        }
    }
    for (SectorId a = 0; a < m_sectors.size(); ++a) {
        if (!affected[a]) continue;
        for (SectorId b = 0; b < m_sectors.size(); ++b) {
            if (a != b && (!editable || affected[b]) && interiorsOverlap(shapes[a], shapes[b]))
                return fail("Drawing affects overlapping sectors. Choose an editing scope to resolve the target.");
        }
    }

    // Planar reconstruction cannot yet repartition a connected TROR surface.
    // Unrelated areas can still be drawn normally; their IDs remain stable.
    for (SectorId s = 0; s < m_sectors.size(); ++s) {
        if (!affected[s]) { continue; }
        const auto &sector = m_sectors[s];
        bool constrained = sector.floorBunch.has_value() || sector.ceilingBunch.has_value();
        for (auto id : sector.walls) {
            const auto &w = m_walls[id];
            constrained |= w.forwardSide.upLink.has_value() || w.forwardSide.downLink.has_value()
                || w.reverseSide.upLink.has_value() || w.reverseSide.downLink.has_value();
        }
        if (constrained) { return fail("Drawing would rebuild a TROR-connected boundary. Use linked vertex editing or Extend floor/ceiling; disconnect TROR before repartitioning this area."); }
    }

    // Work on a compact copy of just the touched sectors. Global IDs and all
    // external portal sides are retained separately for the transactional merge.
    MapDocument local;
    std::vector<SectorId> sectorIds;
    std::vector<WallId> wallIds;
    std::vector<VertexId> vertexIds;
    std::map<SectorId, SectorId> sectorMap;
    std::map<WallId, WallId> wallMap;
    std::map<VertexId, VertexId> vertexMap;
    std::map<std::pair<qreal, qreal>, VertexId> positions;
    for (SectorId s = 0; s < m_sectors.size(); ++s) {
        if (affected[s]) { sectorMap[s] = sectorIds.size(); sectorIds.push_back(s); }
    }
    const auto localVertex = [&](VertexId id) {
        auto [it, inserted] = vertexMap.emplace(id, vertexIds.size());
        if (inserted) {
            vertexIds.push_back(id);
            local.m_vertices.push_back(m_vertices[id]);
        }
        return it->second;
    };
    for (const auto s : sectorIds) {
        auto sector = m_sectors[s];
        for (auto &id : sector.vertices) id = localVertex(id);
        for (auto &id : sector.walls) {
            auto [it, inserted] = wallMap.emplace(id, wallIds.size());
            if (inserted) {
                wallIds.push_back(id);
                auto wall = m_walls[id];
                wall.start = localVertex(wall.start);
                wall.end = localVertex(wall.end);
                const auto remap = [&](std::optional<SectorId> s) -> std::optional<SectorId> {
                    return s && affected[*s] ? std::optional<SectorId>(sectorMap.at(*s)) : std::nullopt;
                };
                wall.forwardSector = remap(wall.forwardSector);
                wall.reverseSector = remap(wall.reverseSector);
                local.m_walls.push_back(wall);
            }
            id = it->second;
        }
        local.m_sectors.push_back(std::move(sector));
    }
    for (VertexId id = 0; id < local.m_vertices.size(); ++id) {
        const auto p = local.m_vertices[id].position;
        if (!positions.emplace(std::make_pair(p.x(), p.y()), id).second)
            return fail("Drawing affects independent vertices at identical coordinates.");
    }
    auto rebuilt = local;
    rebuilt.rebuildSectors();
    if (rebuilt.m_sectors.size() != local.m_sectors.size())
        return fail("The affected area contains unsupported sector loops.");
    for (WallId w = 0; w < local.m_walls.size(); ++w) {
        if (rebuilt.m_walls[w].forwardSector != local.m_walls[w].forwardSector
            || rebuilt.m_walls[w].reverseSector != local.m_walls[w].reverseSector)
            return fail("The affected area cannot safely be reconstructed.");
    }
    const auto before = local;
    if (!local.addPolyline(points, closed)) return false;

    // Splits inherit their source sector's properties. Do not silently change
    // the first-wall basis of slopes or relative texture alignment.
    for (auto &sector : local.m_sectors) {
        const auto path = sectorShape(local, sector);
        for (SectorId s = 0; s < before.m_sectors.size(); ++s) {
            const auto &source = before.m_sectors[s];
            if (!interiorsOverlap(path, shapes[sectorIds[s]])) continue;
            if ((((source.floorstat & 2) && source.floorheinum)
                 || ((source.ceilingstat & 2) && source.ceilingheinum)
                 || ((source.floorstat | source.ceilingstat) & 64))
                && sector.walls.front() != source.walls.front())
                return fail("This split would change a slope or texture alignment's first wall.");
            auto walls = std::move(sector.walls);
            auto vertices = std::move(sector.vertices);
            auto loops = std::move(sector.loopStarts);
            sector = source;
            sector.walls = std::move(walls);
            sector.vertices = std::move(vertices);
            sector.loopStarts = std::move(loops);
            break;
        }
    }
    // Preserve IDs of surviving sectors, and reuse removed IDs for split faces.
    std::vector<SectorId> resultIds(local.m_sectors.size());
    std::set<SectorId> available(sectorIds.begin(), sectorIds.end());
    for (SectorId s = 0; s < local.m_sectors.size(); ++s) {
        resultIds[s] = m_sectors.size();
        for (SectorId old = 0; old < before.m_sectors.size(); ++old) {
            if (local.m_sectors[s].walls == before.m_sectors[old].walls) {
                resultIds[s] = sectorIds[old];
                available.erase(sectorIds[old]);
                break;
            }
        }
    }
    auto nextSector = m_sectors.size();
    for (auto &id : resultIds) {
        if (id != m_sectors.size()) continue;
        if (available.empty()) id = nextSector++;
        else { id = *available.begin(); available.erase(available.begin()); }
    }
    auto candidate = *this;
    candidate.m_sectors.resize(nextSector);
    for (VertexId v = vertexIds.size(); v < local.m_vertices.size(); ++v) {
        vertexIds.push_back(candidate.m_vertices.size());
        candidate.m_vertices.push_back(local.m_vertices[v]);
    }
    for (WallId w = wallIds.size(); w < local.m_walls.size(); ++w) {
        wallIds.push_back(candidate.m_walls.size());
        candidate.m_walls.push_back(local.m_walls[w]);
    }
    for (WallId w = 0; w < local.m_walls.size(); ++w) {
        auto wall = local.m_walls[w];
        wall.start = vertexIds[wall.start];
        wall.end = vertexIds[wall.end];
        const auto restoreSide = [&](std::optional<SectorId> &side, std::optional<SectorId> old) {
            if (old && !affected[*old]) {
                if (side) return false;
                side = old;
            } else if (side) side = resultIds[*side];
            return true;
        };
        const Wall old = w < before.m_walls.size() ? m_walls[wallIds[w]] : Wall{};
        if (!restoreSide(wall.forwardSector, old.forwardSector)
            || !restoreSide(wall.reverseSector, old.reverseSector))
            return fail("Drawing would replace a neighboring sector's portal.");
        candidate.m_walls[wallIds[w]] = wall;
    }
    for (SectorId s = 0; s < local.m_sectors.size(); ++s) {
        auto sector = local.m_sectors[s];
        for (auto &v : sector.vertices) v = vertexIds[v];
        for (auto &w : sector.walls) w = wallIds[w];
        candidate.m_sectors[resultIds[s]] = std::move(sector);
    }
    const auto remapMembership = [&](std::optional<SectorId> &id, const QPointF &p) {
        if (!id || !affected[*id]) return;
        id.reset();
        for (SectorId s = 0; s < local.m_sectors.size(); ++s) {
            bool contains = sectorShape(local, local.m_sectors[s]).contains(p);
            for (auto w : local.m_sectors[s].walls) {
                const auto &wall = local.m_walls[w];
                contains = contains || onSegment(p, local.m_vertices[wall.start].position,
                                                   local.m_vertices[wall.end].position);
            }
            if (contains) { id = resultIds[s]; break; }
        }
    };
    remapMembership(candidate.m_playerStart.sectorId, candidate.m_playerStart.position);
    for (auto &sprite : candidate.m_sprites) remapMembership(sprite.sectorId, sprite.position);
    *this = std::move(candidate);
    return true;
}

std::optional<MapDocument::VertexId> MapDocument::splitWall(WallId wallId, const QPointF &position)
{
    if (wallId >= m_walls.size()) { return std::nullopt; }
    const auto &edge = m_walls[wallId];
    const auto a = m_vertices[edge.start].position, b = m_vertices[edge.end].position;
    const auto delta = b-a, offset = position-a;
    const double length = QLineF(a,b).length();
    if (!std::isfinite(position.x()) || !std::isfinite(position.y()) || length <= coordinateEpsilon
        || QPointF::dotProduct(offset,delta) <= coordinateEpsilon*length
        || QPointF::dotProduct(position-b,delta) >= -coordinateEpsilon*length
        || std::abs(offset.x()*delta.y()-offset.y()*delta.x()) > coordinateEpsilon*length) { return std::nullopt; }
    if (!hasTror()) { return splitWallSingle(wallId, position); }
    std::set<WallId> linked{wallId};
    std::vector<WallId> queue{wallId};
    for (std::size_t i = 0; i < queue.size(); ++i) {
        const auto &w = m_walls[queue[i]];
        for (const auto &side : {w.forwardSide, w.reverseSide}) {
            for (const auto &link : {side.upLink, side.downLink}) {
                if (link && link->wall < m_walls.size() && linked.insert(link->wall).second) { queue.push_back(link->wall); }
            }
        }
    }
    auto candidate = *this;
    std::map<WallId, WallId> seconds;
    std::optional<VertexId> result;
    for (auto w : linked) {
        seconds[w] = candidate.m_walls.size();
        const auto vertex = candidate.splitWallSingle(w, position);
        if (!vertex) { return std::nullopt; }
        if (w == wallId) { result = vertex; }
    }
    for (auto w : linked) {
        for (bool reversed : {false, true}) {
            const auto &old = reversed ? m_walls[w].reverseSide : m_walls[w].forwardSide;
            for (bool floor : {false, true}) {
                const auto &link = floor ? old.downLink : old.upLink;
                if (!link) { continue; }
                if (!seconds.count(link->wall)) { return std::nullopt; }
                for (bool second : {false, true}) {
                    auto &wall = candidate.m_walls[second ? seconds[w] : w];
                    auto &side = reversed ? wall.reverseSide : wall.forwardSide;
                    const bool peerSecond = second != (reversed != link->reversed);
                    (floor ? side.downLink : side.upLink) = WallSideRef{
                        peerSecond ? seconds[link->wall] : link->wall, link->reversed};
                }
            }
        }
    }
    QString error;
    if (!candidate.validateTopologyChange(*this, error)) { return std::nullopt; }
    *this = std::move(candidate);
    return result;
}

std::optional<MapDocument::VertexId> MapDocument::splitWallSingle(WallId wallId, const QPointF &position)
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
    if (hasTror()) { error = "Joining sector outlines in a TROR map is not supported. Disconnect TROR first."; return std::nullopt; }
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
    if (hasTror()) { error = "Disconnect TROR before deleting sectors."; return false; }
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
    // Check that planar reconstruction preserves the remaining boundaries.
    // Empty inner faces are retained as voids by the face builder.
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
    if (hasTror()) { error = "Deleting vertices in a TROR map is not supported. Disconnect TROR first."; return false; }
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
    if (hasTror()) { return; }
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

    const auto oldVoidSides = voidWallSides();

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

    std::vector<bool> remainingVoidSides(remainingWalls.size() * 2, false);
    for (WallId id = 0; id < m_walls.size(); ++id) {
        if (wallMapping[id] == removed) continue;
        const auto mapped = wallMapping[id];
        const bool flipped = roots[m_walls[id].start] != remainingWalls[mapped].start;
        for (int side = 0; side < 2; ++side)
            if (oldVoidSides[id * 2 + side]) remainingVoidSides[mapped * 2 + (side ^ flipped)] = true;
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
    std::vector<bool> compactedVoidSides;
    for (WallId id = 0; id < remainingWalls.size(); ++id) {
        if (collapsedBoundary[id] && !survivingBoundary[id]) continue;
        compactedWalls[id] = m_walls.size();
        compactedVoidSides.push_back(remainingVoidSides[id * 2]);
        compactedVoidSides.push_back(remainingVoidSides[id * 2 + 1]);
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
        rebuildSectors(std::move(compactedVoidSides));
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
    if (hasTror()) { return false; }
    if (!m_complexTopology) return true;
    return std::all_of(m_sectors.begin(), m_sectors.end(), [](const Sector &sector) {
        return sector.loopStarts.size() <= 1 && sector.walls.size() >= 3;
    });
}

void MapDocument::setVertexPositions(
    const std::vector<std::pair<VertexId, QPointF>> &positions, const MapDocument *scaleReference)
{
    const MapDocument previous = scaleReference ? *scaleReference : *this;
    std::map<VertexId, QPointF> resolved(positions.begin(), positions.end());
    if (hasTror()) {
        // Propagate by explicit links, never by coincident XY coordinates.
        bool progress = true;
        while (progress) {
            progress = false;
            for (const auto &wall : m_walls) {
                for (bool reversed : {false, true}) {
                    const auto &side = reversed ? wall.reverseSide : wall.forwardSide;
                    for (const auto &link : {side.upLink, side.downLink}) {
                        if (!link || link->wall >= m_walls.size()) { continue; }
                        const auto &peer = m_walls[link->wall];
                        for (bool end : {false, true}) {
                            const auto a = (end != reversed) ? wall.end : wall.start;
                            const auto b = (end != link->reversed) ? peer.end : peer.start;
                            const auto source = resolved.find(a);
                            if (source == resolved.end()) { continue; }
                            auto [target, inserted] = resolved.emplace(b, source->second);
                            if (!inserted && target->second != source->second) { return; }
                            progress |= inserted;
                        }
                    }
                }
            }
        }
    }
    for (const auto &[vertexId, position] : resolved) {
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
    // Moving coordinates must not infer new connections or reconstruct faces.
    // Imported loops and independent coincident vertices remain authoritative.
    if (!m_complexTopology) {
        std::set<std::pair<qreal,qreal>> positions;
        for (const auto &vertex : m_vertices) {
            if (!std::isfinite(vertex.position.x()) || !std::isfinite(vertex.position.y())
                || !positions.emplace(vertex.position.x(),vertex.position.y()).second) {
                m_complexTopology = true;
                break;
            }
        }
    }
    if (!m_complexTopology) {
        std::vector<QPainterPath> shapes;
        for (const auto &sector : m_sectors) {
            const auto shape = sectorShape(*this, sector);
            for (const auto &other : shapes) {
                if (interiorsOverlap(shape, other)) { m_complexTopology = true; break; }
            }
            if (m_complexTopology) { break; }
            shapes.push_back(shape);
        }
    }
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
        propagateTrorPlane(sectorId, true);
    }
}

void MapDocument::setSectorCeilingZ(std::size_t sectorId, qreal z)
{
    if (sectorId < m_sectors.size()) {
        m_sectors[sectorId].ceilingz = z;
        propagateTrorPlane(sectorId, false);
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

std::vector<bool> MapDocument::voidWallSides() const
{
    // Walk the existing graph's empty faces. Bounded empty faces are holes;
    // the unbounded exterior remains available for neighboring rooms. Looking
    // at directed faces also handles holes shared by several surrounding sectors.
    std::vector<bool> result(m_walls.size() * 2, false), visited(result.size(), false);
    std::vector<std::vector<std::size_t>> outgoing(m_vertices.size());
    const auto from = [this](std::size_t edge) {
        const auto &wall = m_walls[edge / 2];
        return edge % 2 ? wall.end : wall.start;
    };
    for (std::size_t edge = 0; edge < result.size(); ++edge) outgoing[from(edge)].push_back(edge);
    for (auto &edges : outgoing) {
        std::sort(edges.begin(), edges.end(), [&](auto a, auto b) {
            const auto da = m_vertices[from(a ^ 1)].position - m_vertices[from(a)].position;
            const auto db = m_vertices[from(b ^ 1)].position - m_vertices[from(b)].position;
            return std::atan2(da.y(), da.x()) < std::atan2(db.y(), db.x());
        });
    }
    for (std::size_t first = 0; first < result.size(); ++first) {
        if (visited[first]) continue;
        std::vector<std::size_t> face;
        auto edge = first;
        qreal area = 0;
        bool empty = true;
        do {
            if (visited[edge]) { empty = false; break; }
            visited[edge] = true;
            face.push_back(edge);
            const auto &wall = m_walls[edge / 2];
            if (edge % 2 ? wall.reverseSector.has_value() : wall.forwardSector.has_value()) empty = false;
            const auto a = m_vertices[from(edge)].position;
            const auto b = m_vertices[from(edge ^ 1)].position;
            area += a.x() * b.y() - b.x() * a.y();
            const auto &edges = outgoing[from(edge ^ 1)];
            const auto reverse = std::find(edges.begin(), edges.end(), edge ^ 1) - edges.begin();
            edge = edges[(reverse + edges.size() - 1) % edges.size()];
        } while (edge != first);
        if (empty && area > coordinateEpsilon) {
            for (auto side : face) result[side] = true;
        }
    }
    return result;
}

void MapDocument::rebuildSectors(std::vector<bool> voidSides)
{
    if (voidSides.empty()) voidSides = voidWallSides();
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
    std::vector<bool> voidFaces;
    for (const auto &sector : m_sectors) {
        bool empty = false;
        for (std::size_t i = 0; i < sector.walls.size(); ++i) {
            const auto wall = sector.walls[i];
            const auto side = wall * 2 + (m_walls[wall].start != sector.vertices[i]);
            empty = empty || (side < voidSides.size() && voidSides[side]);
        }
        voidFaces.push_back(empty);
    }
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

    // Keep empty faces during containment analysis: an island inside a void
    // must not punch an additional hole in the surrounding playable room.
    // Only now discard the empty faces and compact the sector indices.
    std::vector<Sector> occupied;
    for (SectorId id = 0; id < m_sectors.size(); ++id) {
        if (!voidFaces[id]) occupied.push_back(std::move(m_sectors[id]));
    }
    m_sectors = std::move(occupied);

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

namespace {
struct TrorPlane {
    double x = 0, y = 0, c = 0;
    double at(const QPointF &p) const { return c + x*p.x() + y*p.y(); }
};
TrorPlane trorPlane(const MapDocument &d, MapDocument::SectorId id, bool floor)
{
    const auto &s = d.sectors()[id];
    TrorPlane plane{0, 0, floor ? s.floorz : s.ceilingz};
    if (((floor ? s.floorstat : s.ceilingstat) & 2) && s.vertices.size() >= 2) {
        const auto a = d.vertices()[s.vertices[0]].position;
        const auto b = d.vertices()[s.vertices[s.nextWallIndex(0)]].position;
        const auto delta = b-a;
        const double length = std::hypot(delta.x(), delta.y());
        if (length > 0) {
            const double scale = (floor ? s.floorheinum : s.ceilingheinum) / (256.0*length);
            plane.x = -delta.y()*scale;
            plane.y = delta.x()*scale;
            plane.c -= plane.x*a.x()+plane.y*a.y();
        }
    }
    return plane;
}
}

bool MapDocument::copySectors(const std::vector<SectorId> &ids, MapDocument &fragment, QString &error) const
{
    error.clear();
    if (ids.empty()) { error = "Select one or more sectors to copy."; return false; }
    MapDocument result;
    std::map<SectorId, SectorId> sectors;
    std::map<WallId, WallId> walls;
    std::map<VertexId, VertexId> vertices;
    for (auto id : ids) {
        if (id >= m_sectors.size()) { error = "Invalid sector selection."; return false; }
        if (sectors.count(id)) continue;
        sectors.emplace(id, result.m_sectors.size());
        result.m_sectors.push_back(m_sectors[id]);
        for (auto v : m_sectors[id].vertices) {
            if (vertices.count(v)) continue;
            vertices.emplace(v, result.m_vertices.size());
            result.m_vertices.push_back(m_vertices[v]);
        }
        for (auto w : m_sectors[id].walls) {
            if (walls.count(w)) continue;
            walls.emplace(w, result.m_walls.size());
            result.m_walls.push_back(m_walls[w]);
        }
    }
    // A partially copied bunch cannot retain its complete vertical boundary.
    std::set<int> detachedBunches;
    for (SectorId s = 0; s < m_sectors.size(); ++s) {
        if (sectors.count(s)) continue;
        for (auto bunch : {m_sectors[s].floorBunch, m_sectors[s].ceilingBunch})
            if (bunch) detachedBunches.insert(*bunch);
    }
    for (auto &sector : result.m_sectors) {
        for (auto &v : sector.vertices) v = vertices.at(v);
        for (auto &w : sector.walls) w = walls.at(w);
        for (auto *bunch : {&sector.floorBunch, &sector.ceilingBunch})
            if (*bunch && detachedBunches.count(**bunch)) bunch->reset();
    }
    for (auto &wall : result.m_walls) {
        wall.start = vertices.at(wall.start);
        wall.end = vertices.at(wall.end);
        for (auto *owner : {&wall.forwardSector, &wall.reverseSector}) {
            if (*owner && sectors.count(**owner)) *owner = sectors.at(**owner);
            else owner->reset();
        }
        for (bool reverse : {false, true}) {
            auto &side = reverse ? wall.reverseSide : wall.forwardSide;
            const auto owner = reverse ? wall.reverseSector : wall.forwardSector;
            if (!owner) { side = WallSide{}; continue; }
            const auto &sector = result.m_sectors[*owner];
            for (bool floor : {false, true}) {
                auto &link = floor ? side.downLink : side.upLink;
                if (!(floor ? sector.floorBunch : sector.ceilingBunch)) link.reset();
                else if (link) link->wall = walls.at(link->wall);
            }
        }
    }
    std::map<SpriteId, SpriteId> sprites;
    for (SpriteId id = 0; id < m_sprites.size(); ++id) {
        const auto &sprite = m_sprites[id];
        auto owner = sprite.sectorId;
        if (!owner) {
            const auto candidates = sectorsAt(sprite.position);
            if (candidates.size() == 1) owner = candidates.front();
            else if (std::any_of(candidates.begin(), candidates.end(), [&](auto s) { return sectors.count(s); })) {
                error = "A sprite overlaps multiple sectors without an assigned sector. Assign its sector before copying.";
                return false;
            }
        }
        if (!owner || !sectors.count(*owner)) continue;
        sprites.emplace(id, result.m_sprites.size());
        result.m_sprites.push_back(sprite);
        result.m_sprites.back().sectorId = sectors.at(*owner);
    }
    for (auto &sprite : result.m_sprites) {
        sprite.owner = sprite.owner >= 0 && sprites.count(static_cast<SpriteId>(sprite.owner))
            ? static_cast<int>(sprites.at(static_cast<SpriteId>(sprite.owner))) : -1;
    }
    result.m_complexTopology = m_complexTopology;
    if (!result.validateTror(error)) return false;
    fragment = std::move(result);
    return true;
}

bool MapDocument::pasteSectors(const MapDocument &fragment, const QPointF &offset, QString &error)
{
    error.clear();
    if (fragment.m_sectors.empty()) { error = "No copied sectors to paste."; return false; }
    MapDocument candidate = *this;
    // Preserve unambiguous original membership if the copy overlaps its source.
    const auto resolveMembership = [this](auto &object) {
        if (object.sectorId) return;
        const auto owners = sectorsAt(object.position);
        if (owners.size() == 1) object.sectorId = owners.front();
    };
    resolveMembership(candidate.m_playerStart);
    for (auto &sprite : candidate.m_sprites) resolveMembership(sprite);
    const auto vertexBase = candidate.m_vertices.size(), wallBase = candidate.m_walls.size();
    const auto sectorBase = candidate.m_sectors.size(), spriteBase = candidate.m_sprites.size();
    std::set<int> usedBunches;
    std::map<int, int> bunches;
    for (const auto &sector : m_sectors)
        for (auto bunch : {sector.floorBunch, sector.ceilingBunch})
            if (bunch) usedBunches.insert(*bunch);
    for (const auto &sector : fragment.m_sectors) {
        for (auto bunch : {sector.floorBunch, sector.ceilingBunch}) {
            if (!bunch || bunches.count(*bunch)) continue;
            int next = 0;
            while (usedBunches.count(next)) ++next;
            if (next >= 256) { error = "Version 9's limit of 256 TROR bunches has been reached."; return false; }
            bunches.emplace(*bunch, next);
            usedBunches.insert(next);
        }
    }
    for (auto vertex : fragment.m_vertices) {
        vertex.position += offset;
        candidate.m_vertices.push_back(vertex);
    }
    for (auto wall : fragment.m_walls) {
        wall.start += vertexBase; wall.end += vertexBase;
        for (auto *owner : {&wall.forwardSector, &wall.reverseSector})
            if (*owner) **owner += sectorBase;
        for (auto *side : {&wall.forwardSide, &wall.reverseSide})
            for (auto *link : {&side->upLink, &side->downLink})
                if (*link) (*link)->wall += wallBase;
        candidate.m_walls.push_back(wall);
    }
    for (auto sector : fragment.m_sectors) {
        for (auto &v : sector.vertices) v += vertexBase;
        for (auto &w : sector.walls) w += wallBase;
        for (auto *bunch : {&sector.floorBunch, &sector.ceilingBunch})
            if (*bunch) *bunch = bunches.at(**bunch);
        candidate.m_sectors.push_back(sector);
    }
    for (auto sprite : fragment.m_sprites) {
        sprite.position += offset;
        if (sprite.sectorId) *sprite.sectorId += sectorBase;
        if (sprite.owner >= 0) sprite.owner += static_cast<int>(spriteBase);
        candidate.m_sprites.push_back(sprite);
    }
    // Independent geometry must never be welded by a later planar rebuild.
    candidate.m_complexTopology = true;
    if (!candidate.validateTopologyChange(*this, error)) return false;
    *this = std::move(candidate);
    return true;
}

bool MapDocument::hasTror() const
{
    for (const auto &s : m_sectors) {
        if (s.ceilingBunch || s.floorBunch) { return true; }
    }
    for (const auto &w : m_walls) {
        if (w.forwardSide.upLink || w.forwardSide.downLink || w.reverseSide.upLink || w.reverseSide.downLink) { return true; }
    }
    return false;
}

bool MapDocument::validateTror(QString &error) const
{
    error.clear();
    if (!hasTror()) { return true; }
    const auto fail = [&](const QString &why) { error = why; return false; };
    std::array<std::array<int,2>,256> members{};
    for (SectorId id = 0; id < m_sectors.size(); ++id) {
        const auto &s = m_sectors[id];
        if ((s.floorstat | s.ceilingstat) & 1024) { return fail("TROR marker bits are managed by the connection tools, not surface flags."); }
        if (s.vertices.size() != s.walls.size() || s.vertices.empty()) { return fail("Invalid TROR sector boundary."); }
        for (auto v : s.vertices) {
            if (v >= m_vertices.size()) { return fail("Invalid TROR vertex reference."); }
        }
        for (auto w : s.walls) {
            if (w >= m_walls.size()) { return fail("Invalid TROR wall reference."); }
        }
        for (std::size_t i = 0; i < s.vertices.size(); ++i) {
            if (s.nextWallIndex(i) >= s.vertices.size()) { return fail("Invalid TROR boundary loop."); }
        }
        if (s.floorBunch && s.floorBunch == s.ceilingBunch) { return fail("A sector cannot belong to both sides of the same TROR bunch."); }
        for (bool floor : {false, true}) {
            const auto &bunch = floor ? s.floorBunch : s.ceilingBunch;
            if (!bunch) { continue; }
            if (*bunch < 0 || *bunch > 255) { return fail("TROR bunch IDs must be between 0 and 255."); }
            if ((floor ? s.floorxpanning : s.ceilingxpanning) != 0) {
                return fail("X panning is unavailable on connected TROR surfaces in version 9.");
            }
            ++members[*bunch][floor ? 1 : 0];
        }
        if (s.floorBunch || s.ceilingBunch) {
            for (auto v : s.vertices) {
                const auto p = m_vertices[v].position;
                const double ceiling = trorPlane(*this,id,false).at(p), floor = trorPlane(*this,id,true).at(p);
                if (!std::isfinite(ceiling) || !std::isfinite(floor) || ceiling > floor) {
                    return fail("A TROR edit would invert a room's floor and ceiling.");
                }
            }
        }
    }
    for (const auto &member : members) {
        if ((member[0] == 0) != (member[1] == 0)) { return fail("TROR bunch is missing its opposite surface."); }
    }
    for (WallId w = 0; w < m_walls.size(); ++w) {
        const auto &wall = m_walls[w];
        if (wall.start >= m_vertices.size() || wall.end >= m_vertices.size()) { return fail("Invalid TROR wall vertices."); }
        for (bool reversed : {false, true}) {
            const auto &side = reversed ? wall.reverseSide : wall.forwardSide;
            const auto owner = reversed ? wall.reverseSector : wall.forwardSector;
            if (side.cstat & 3072) { return fail("TROR wall marker bits are managed by the connection tools, not wall flags."); }
            if ((side.upLink && side.lotag != 0) || (side.downLink && side.extra != -1)) {
                return fail("Upper TROR wall lotag and lower TROR wall extra are reserved for connections in version 9.");
            }
            for (bool floor : {false, true}) {
                const auto &link = floor ? side.downLink : side.upLink;
                if (!link) {
                    if (owner && *owner < m_sectors.size()) {
                        const auto &s = m_sectors[*owner];
                        const auto bunch = floor ? s.floorBunch : s.ceilingBunch;
                        const auto neighbor = reversed ? wall.forwardSector : wall.reverseSector;
                        if (bunch && (!neighbor || *neighbor >= m_sectors.size()
                            || (floor ? m_sectors[*neighbor].floorBunch : m_sectors[*neighbor].ceilingBunch) != bunch)) {
                            return fail("A TROR bunch boundary is missing a vertical wall link.");
                        }
                    }
                    continue;
                }
                if (!owner || *owner >= m_sectors.size() || link->wall >= m_walls.size()) { return fail("TROR wall link has an invalid owner or target."); }
                const auto &peer = m_walls[link->wall];
                if (peer.start >= m_vertices.size() || peer.end >= m_vertices.size()) { return fail("Invalid TROR peer vertices."); }
                const auto peerOwner = link->reversed ? peer.reverseSector : peer.forwardSector;
                const auto &peerSide = link->reversed ? peer.reverseSide : peer.forwardSide;
                const auto &back = floor ? peerSide.upLink : peerSide.downLink;
                if (!peerOwner || *peerOwner >= m_sectors.size() || !back || !(*back == WallSideRef{w,reversed})) {
                    return fail("TROR wall links must be reciprocal.");
                }
                const auto &s = m_sectors[*owner], &t = m_sectors[*peerOwner];
                const auto bunch = floor ? s.floorBunch : s.ceilingBunch;
                if (!bunch || bunch != (floor ? t.ceilingBunch : t.floorBunch)) { return fail("TROR wall links have incompatible bunches."); }
                for (bool end : {false, true}) {
                    const auto a = m_vertices[(end != reversed) ? wall.end : wall.start].position;
                    const auto b = m_vertices[(end != link->reversed) ? peer.end : peer.start].position;
                    if (QLineF(a,b).length() > coordinateEpsilon) { return fail("Linked TROR boundaries must move and split together."); }
                    if (std::abs(trorPlane(*this,*owner,floor).at(a)-trorPlane(*this,*peerOwner,!floor).at(b)) > 0.01) {
                        return fail("Connected TROR planes must have matching heights and slopes. This slope cannot be represented by both first-wall directions.");
                    }
                }
                if (members[*bunch][0] + members[*bunch][1] > 2 &&
                    (((floor ? s.floorstat : s.ceilingstat) & 2) && (floor ? s.floorheinum : s.ceilingheinum))) {
                    return fail("Sloped TROR connections require one sector on each side.");
                }
            }
        }
    }
    return true;
}

void MapDocument::propagateTrorPlane(SectorId id, bool floor)
{
    const auto &source = m_sectors[id];
    const auto bunch = floor ? source.floorBunch : source.ceilingBunch;
    if (!bunch || source.vertices.size() < 2) { return; }
    const auto plane = trorPlane(*this, id, floor);
    for (SectorId s = 0; s < m_sectors.size(); ++s) {
        for (bool f : {false, true}) {
            auto &target = m_sectors[s];
            if ((s == id && f == floor) || (f ? target.floorBunch : target.ceilingBunch) != bunch
                || target.vertices.size() < 2) { continue; }
            const auto a = m_vertices[target.vertices[0]].position;
            const auto delta = m_vertices[target.vertices[target.nextWallIndex(0)]].position-a;
            const auto length = std::hypot(delta.x(), delta.y());
            const auto heinum = length > 0 ? 256*(-plane.x*delta.y()+plane.y*delta.x())/length : 0;
            (f ? target.floorz : target.ceilingz) = plane.at(a);
            (f ? target.floorheinum : target.ceilingheinum) = static_cast<int>(std::round(std::clamp(heinum,-32768.0,32767.0)));
            auto &flags = f ? target.floorstat : target.ceilingstat;
            flags = (flags & ~2) | (std::abs(heinum) > 0.001 ? 2 : 0);
        }
    }
}

std::set<MapDocument::SectorId> MapDocument::layerSectors(SectorId seed) const
{
    if (seed >= m_sectors.size()) { return {}; }
    std::set<SectorId> result{seed};
    std::vector<SectorId> queue{seed};
    for (std::size_t i = 0; i < queue.size(); ++i) {
        for (auto id : m_sectors[queue[i]].walls) {
            const auto &w = m_walls[id];
            for (const auto s : {w.forwardSector, w.reverseSector}) {
                if (s && result.insert(*s).second) { queue.push_back(*s); }
            }
        }
    }
    return result;
}

std::set<MapDocument::SectorId> MapDocument::verticalNeighbors(SectorId id, bool floor) const
{
    std::set<SectorId> result;
    if (id >= m_sectors.size()) { return result; }
    const auto bunch = floor ? m_sectors[id].floorBunch : m_sectors[id].ceilingBunch;
    if (!bunch) { return result; }
    for (SectorId s = 0; s < m_sectors.size(); ++s) {
        if ((floor ? m_sectors[s].ceilingBunch : m_sectors[s].floorBunch) == bunch) { result.insert(s); }
    }
    return result;
}

bool MapDocument::connectTror(SectorId upper, SectorId lower, QString &error)
{
    error.clear();
    const auto fail = [&](const QString &why) { error = why; return false; };
    if (upper >= m_sectors.size() || lower >= m_sectors.size() || upper == lower) { return fail("Choose distinct upper and lower sectors."); }
    const auto &a = m_sectors[upper], &b = m_sectors[lower];
    if (a.floorBunch || b.ceilingBunch) { return fail("Those surfaces are already connected. Disconnect their bunch first."); }
    if (a.walls.size() != b.walls.size()) { return fail("TROR joining requires matching boundaries and vertex counts."); }
    std::vector<std::pair<WallSideRef,WallSideRef>> pairs;
    std::set<WallId> used;
    for (std::size_t i = 0; i < a.walls.size(); ++i) {
        const auto start = m_vertices[a.vertices[i]].position;
        const auto end = m_vertices[a.vertices[a.nextWallIndex(i)]].position;
        bool found = false;
        for (std::size_t j = 0; j < b.walls.size(); ++j) {
            if (used.count(b.walls[j])) { continue; }
            if (QLineF(start,m_vertices[b.vertices[j]].position).length() > coordinateEpsilon
                || QLineF(end,m_vertices[b.vertices[b.nextWallIndex(j)]].position).length() > coordinateEpsilon) { continue; }
            if (std::abs(trorPlane(*this,upper,true).at(start)-trorPlane(*this,lower,false).at(start)) > 0.01
                || std::abs(trorPlane(*this,upper,true).at(end)-trorPlane(*this,lower,false).at(end)) > 0.01) {
                return fail("Move the upper floor and lower ceiling to the same plane before joining.");
            }
            pairs.push_back({{a.walls[i], m_walls[a.walls[i]].start != a.vertices[i]},
                             {b.walls[j], m_walls[b.walls[j]].start != b.vertices[j]}});
            used.insert(b.walls[j]); found = true; break;
        }
        if (!found) { return fail("TROR joining requires identical directed boundary edges, including holes."); }
    }
    std::set<int> bunches;
    for (const auto &s : m_sectors) {
        if (s.ceilingBunch) { bunches.insert(*s.ceilingBunch); }
        if (s.floorBunch) { bunches.insert(*s.floorBunch); }
    }
    int bunch = 0;
    while (bunches.count(bunch)) { ++bunch; }
    if (bunch >= 256) { return fail("Version 9's limit of 256 TROR bunches has been reached."); }
    auto candidate = *this;
    candidate.m_sectors[upper].floorBunch = candidate.m_sectors[lower].ceilingBunch = bunch;
    candidate.m_sectors[upper].floorxpanning = candidate.m_sectors[lower].ceilingxpanning = 0;
    for (const auto &[up,down] : pairs) {
        auto &u = candidate.m_walls[up.wall], &d = candidate.m_walls[down.wall];
        (up.reversed ? u.reverseSide : u.forwardSide).downLink = down;
        (down.reversed ? d.reverseSide : d.forwardSide).upLink = up;
        (up.reversed ? u.reverseSide : u.forwardSide).extra = -1;
        (down.reversed ? d.reverseSide : d.forwardSide).lotag = 0;
    }
    candidate.m_complexTopology = true;
    if (!candidate.validateTopologyChange(*this,error)) { return false; }
    *this = std::move(candidate);
    return true;
}

std::optional<MapDocument::SectorId> MapDocument::extendTror(SectorId id, bool floor, qreal depth, QString &error)
{
    error.clear();
    if (id >= m_sectors.size() || !std::isfinite(depth) || depth <= 0 || depth > 2147483647) {
        error = "Choose a sector and a positive extension height."; return std::nullopt;
    }
    const auto source = m_sectors[id];
    std::size_t wallCount = source.walls.size();
    for (const auto &s : m_sectors) { wallCount += s.walls.size(); }
    if (m_sectors.size() >= 4096 || wallCount > 16384) {
        error = "The extension would exceed version 9's sector or wall limits."; return std::nullopt;
    }
    if (floor ? source.floorBunch.has_value() : source.ceilingBunch.has_value()) {
        error = "This surface already has a TROR connection."; return std::nullopt;
    }
    auto candidate = *this;
    const SectorId created = candidate.m_sectors.size();
    const auto resolveMember = [&](auto &member) {
        if (!member.sectorId) {
            const auto owners = sectorsAt(member.position);
            if (owners.size() == 1) { member.sectorId = owners.front(); }
        }
    };
    resolveMember(candidate.m_playerStart);
    for (auto &sprite : candidate.m_sprites) { resolveMember(sprite); }
    auto sector = source;
    sector.floorBunch.reset(); sector.ceilingBunch.reset();
    sector.vertices.clear(); sector.walls.clear();
    const qreal z = floor ? source.floorz : source.ceilingz;
    sector.ceilingz = floor ? z : z-depth;
    sector.floorz = floor ? z+depth : z;
    const int slope = floor ? source.floorheinum : source.ceilingheinum;
    const int slopeFlag = (floor ? source.floorstat : source.ceilingstat) & 2;
    sector.floorheinum = sector.ceilingheinum = slope;
    sector.floorstat = (sector.floorstat & ~2) | slopeFlag;
    sector.ceilingstat = (sector.ceilingstat & ~2) | slopeFlag;
    for (auto v : source.vertices) {
        sector.vertices.push_back(candidate.m_vertices.size());
        candidate.m_vertices.push_back(m_vertices[v]);
    }
    for (std::size_t i = 0; i < source.walls.size(); ++i) {
        const auto &original = m_walls[source.walls[i]];
        Wall wall{sector.vertices[i], sector.vertices[source.nextWallIndex(i)]};
        wall.forwardSector = created;
        wall.forwardSide = original.start == source.vertices[i] ? original.forwardSide : original.reverseSide;
        wall.forwardSide.upLink.reset(); wall.forwardSide.downLink.reset();
        sector.walls.push_back(candidate.m_walls.size());
        candidate.m_walls.push_back(wall);
    }
    candidate.m_sectors.push_back(sector);
    if (!candidate.connectTror(floor ? id : created, floor ? created : id, error)) { return std::nullopt; }
    if (!candidate.validateTopologyChange(*this,error)) { return std::nullopt; }
    *this = std::move(candidate);
    return created;
}

bool MapDocument::disconnectTror(SectorId id, bool floor, QString &error)
{
    error.clear();
    if (id >= m_sectors.size()) { error = "Choose a sector first."; return false; }
    const auto bunch = floor ? m_sectors[id].floorBunch : m_sectors[id].ceilingBunch;
    if (!bunch) { error = "This surface has no TROR connection."; return false; }
    for (auto &sector : m_sectors) {
        for (bool f : {false, true}) {
            auto &member = f ? sector.floorBunch : sector.ceilingBunch;
            if (member != bunch) { continue; }
            for (std::size_t i = 0; i < sector.walls.size(); ++i) {
                auto &wall = m_walls[sector.walls[i]];
                auto &side = wall.start == sector.vertices[i] ? wall.forwardSide : wall.reverseSide;
                (f ? side.downLink : side.upLink).reset();
            }
            member.reset();
        }
    }
    return true;
}

bool MapDocument::Vertex::operator==(const Vertex &other) const
{
    return position == other.position;
}

bool MapDocument::WallSide::operator==(const WallSide &other) const
{
    return std::tie(
        texture, overlayTexture, shade, palette, xrepeat, yrepeat, xpanning, ypanning, cstat,
        hitag, lotag, extra, upLink, downLink)
        == std::tie(
        other.texture, other.overlayTexture, other.shade, other.palette, other.xrepeat,
        other.yrepeat, other.xpanning, other.ypanning, other.cstat, other.hitag, other.lotag,
        other.extra, other.upLink, other.downLink);
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
        visibility, extra, filler, ceilingBunch, floorBunch)
        == std::tie(
        other.walls, other.vertices, other.loopStarts, other.floorz, other.ceilingz,
        other.floorTexture, other.ceilingTexture, other.hitag, other.lotag, other.floorstat,
        other.ceilingstat, other.floorheinum, other.ceilingheinum, other.floorshade,
        other.ceilingshade, other.floorpal, other.ceilingpal, other.floorxpanning,
        other.floorypanning, other.ceilingxpanning, other.ceilingypanning, other.visibility,
        other.extra, other.filler, other.ceilingBunch, other.floorBunch);
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
        if ((side.cstat & (16 | 32)) && side.overlayTexture >= 0)
            tiles.insert(side.overlayTexture);
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
