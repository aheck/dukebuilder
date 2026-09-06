#include "mapdocument.h"
#include <libduke/map.h>
#include <QFile>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>

bool MapDocument::openMap(const QString &filename, QString &error)
{
    error.clear();
    try {
        std::unique_ptr<DukeMapFile, decltype(&duke_map_file_free)> map(duke_map_file_new(), duke_map_file_free);
        if (!map) throw std::bad_alloc();
        const auto path = QFile::encodeName(filename);
        if (!duke_map_file_read_from_filename(map.get(), path.constData())) throw std::runtime_error(map->last_error);
        if (map->mapversion != 7) throw std::runtime_error("Only classic version-7 Build maps are supported.");
        if (map->numsectors == 0) throw std::runtime_error("The map contains no sectors.");

        // Validate references without imposing the exporter's geometric rules:
        // shipped Duke maps include intentional two-wall effect sectors.
        std::vector<int> owners(map->numwalls, -1), incoming(map->numwalls, 0);
        for (int s = 0; s < map->numsectors; ++s) {
            const auto &sector = *map->sectors[s];
            if (sector.wallptr < 0 || sector.wallnum < 1 || sector.wallptr + sector.wallnum > map->numwalls)
                throw std::runtime_error("A sector has an invalid wall range.");
            for (int w = sector.wallptr; w < sector.wallptr + sector.wallnum; ++w) {
                if (owners[w] != -1) throw std::runtime_error("Sector wall ranges overlap.");
                owners[w] = s;
            }
        }
        for (int w = 0; w < map->numwalls; ++w) {
            const auto &wall = *map->walls[w];
            if (owners[w] < 0 || wall.point2 < 0 || wall.point2 >= map->numwalls
                || owners[wall.point2] != owners[w] || ++incoming[wall.point2] != 1)
                throw std::runtime_error("A wall has an invalid next-point link.");
            if (wall.nextwall == -1 && wall.nextsector == -1) continue;
            if (wall.nextwall < 0 || wall.nextwall >= map->numwalls || wall.nextsector < 0
                || wall.nextsector >= map->numsectors || owners[wall.nextwall] != wall.nextsector)
                throw std::runtime_error("A wall has an invalid portal reference.");
            const auto &other = *map->walls[wall.nextwall];
            if (other.nextwall != w || other.nextsector != owners[w] || other.point2 < 0
                || other.point2 >= map->numwalls || wall.x != map->walls[other.point2]->x
                || wall.y != map->walls[other.point2]->y || other.x != map->walls[wall.point2]->x
                || other.y != map->walls[wall.point2]->y)
                throw std::runtime_error("A portal's two wall sides do not match.");
        }

        MapDocument loaded;
        // A Build wall is also the vertex at its start. Only portal links weld
        // vertices across sectors; equal coordinates alone do not imply a link
        // (overlapping rooms must remain independent).
        std::vector<int> roots(map->numwalls);
        for (int i = 0; i < map->numwalls; ++i) roots[i] = i;
        const auto root = [&](int id) {
            while (roots[id] != id) id = roots[id];
            return id;
        };
        const auto unite = [&](int a, int b) {
            a = root(a); b = root(b);
            roots[std::max(a, b)] = std::min(a, b);
        };
        for (int i = 0; i < map->numwalls; ++i) {
            const auto &wall = *map->walls[i];
            if (wall.nextwall >= 0) {
                unite(i, map->walls[wall.nextwall]->point2);
                unite(wall.point2, wall.nextwall);
            }
        }
        std::map<int, VertexId> vertexIds;
        std::map<std::pair<int32_t, int32_t>, VertexId> positions;
        std::vector<VertexId> starts(map->numwalls);
        for (int i = 0; i < map->numwalls; ++i) {
            const int representative = root(i);
            auto [entry, inserted] = vertexIds.emplace(representative, loaded.m_vertices.size());
            starts[i] = entry->second;
            if (inserted) {
                const auto &wall = *map->walls[i];
                loaded.m_vertices.push_back({QPointF(wall.x, wall.y)});
                // The planar face builder cannot preserve independent vertices
                // at identical coordinates during structural edits.
                if (!positions.emplace(std::make_pair(wall.x, wall.y), entry->second).second)
                    loaded.m_complexTopology = true;
            }
        }
        std::vector<WallId> wallIds(map->numwalls);
        std::vector<bool> reversed(map->numwalls, false);
        for (int i = 0; i < map->numwalls; ++i) {
            const auto &source = *map->walls[i];
            WallSide side;
            side.texture = source.picnum;
            side.overlayTexture = source.overpicnum;
            side.shade = source.shade;
            side.palette = source.pal;
            side.xrepeat = source.xrepeat;
            side.yrepeat = source.yrepeat;
            side.xpanning = source.xpanning;
            side.ypanning = source.ypanning;
            side.hitag = source.hitag;
            side.lotag = source.lotag;
            side.extra = source.extra;
            side.cstat = static_cast<uint16_t>(source.cstat);
            if (source.nextwall >= 0 && source.nextwall < i) {
                wallIds[i] = wallIds[source.nextwall];
                reversed[i] = true;
                loaded.m_walls[wallIds[i]].reverseSide = side;
            } else {
                wallIds[i] = loaded.m_walls.size();
                Wall wall{starts[i], starts[source.point2]};
                wall.forwardSide = side;
                loaded.m_walls.push_back(wall);
            }
        }
        for (int id = 0; id < map->numsectors; ++id) {
            const auto &source = *map->sectors[id];
            Sector sector;
            sector.ceilingz = source.ceilingz;
            sector.floorz = source.floorz;
            sector.ceilingheinum = source.ceilingheinum;
            sector.floorheinum = source.floorheinum;
            sector.ceilingshade = source.ceilingshade;
            sector.floorshade = source.floorshade;
            sector.ceilingpal = source.ceilingpal;
            sector.floorpal = source.floorpal;
            sector.ceilingxpanning = source.ceilingxpanning;
            sector.ceilingypanning = source.ceilingypanning;
            sector.floorxpanning = source.floorxpanning;
            sector.floorypanning = source.floorypanning;
            sector.visibility = source.visibility;
            sector.extra = source.extra;
            sector.filler = source.filler;
            sector.hitag = source.hitag;
            sector.lotag = static_cast<uint16_t>(source.lotag);
            sector.ceilingstat = static_cast<uint16_t>(source.ceilingstat);
            sector.floorstat = static_cast<uint16_t>(source.floorstat);
            sector.floorTexture = source.floorpicnum;
            sector.ceilingTexture = source.ceilingpicnum;
            std::vector<bool> visited(source.wallnum, false);
            for (int initial = source.wallptr; initial < source.wallptr + source.wallnum; ++initial) {
                if (visited[initial - source.wallptr]) continue;
                sector.loopStarts.push_back(sector.walls.size());
                int current = initial;
                do {
                    visited[current - source.wallptr] = true;
                    sector.walls.push_back(wallIds[current]);
                    sector.vertices.push_back(starts[current]);
                    auto &wall = loaded.m_walls[wallIds[current]];
                    (reversed[current] ? wall.reverseSector : wall.forwardSector) = id;
                    current = map->walls[current]->point2;
                } while (current != initial);
            }
            if (sector.walls.size() < 3) loaded.m_complexTopology = true;
            loaded.m_sectors.push_back(std::move(sector));
        }
        if (!loaded.m_complexTopology) {
            std::vector<QPainterPath> shapes;
            for (const auto &sector : loaded.m_sectors) {
                QPainterPath shape;
                shape.setFillRule(Qt::OddEvenFill);
                for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
                    const auto position = loaded.m_vertices[sector.vertices[i]].position;
                    if (i == 0 || std::find(sector.loopStarts.begin(), sector.loopStarts.end(), i)
                                      != sector.loopStarts.end()) shape.moveTo(position);
                    else shape.lineTo(position);
                    if (sector.nextWallIndex(i) <= i) shape.closeSubpath();
                }
                for (const auto &other : shapes) {
                    // Intersections can contain zero-area shared portal edges.
                    for (const auto &polygon : shape.intersected(other).toFillPolygons()) {
                        qreal twiceArea = 0;
                        for (int i = 0; i < polygon.size(); ++i) {
                            const auto a = polygon[i];
                            const auto b = polygon[(i + 1) % polygon.size()];
                            twiceArea += a.x() * b.y() - b.x() * a.y();
                        }
                        if (std::abs(twiceArea) > 0.001) loaded.m_complexTopology = true;
                    }
                }
                shapes.push_back(shape);
            }
        }
        if (!loaded.m_complexTopology) {
            // A hole enclosing a connected sector is now supported. Other
            // imported loops (e.g. empty voids) must not gain sectors on edit.
            auto rebuilt = loaded;
            rebuilt.rebuildSectors();
            if (rebuilt.m_sectors.size() != loaded.m_sectors.size()) {
                loaded.m_complexTopology = true;
            } else {
                for (std::size_t i = 0; i < loaded.m_walls.size(); ++i) {
                    const auto &before = loaded.m_walls[i];
                    const auto &after = rebuilt.m_walls[i];
                    if (before.forwardSector != after.forwardSector
                        || before.reverseSector != after.reverseSector) {
                        loaded.m_complexTopology = true;
                        break;
                    }
                }
            }
        }
        for (int id = 0; id < map->numsprites; ++id) {
            const auto &source = *map->sprites[id];
            Sprite sprite;
            sprite.position = QPointF(source.x, source.y);
            sprite.angle = source.ang * 360.0 / 2048.0;
            sprite.sectorId = source.sectnum >= 0 ? std::optional<SectorId>(source.sectnum) : std::nullopt;
            sprite.z = source.z;
            sprite.texture = source.picnum;
            sprite.shade = source.shade;
            sprite.palette = source.pal;
            sprite.hitag = source.hitag;
            sprite.lotag = source.lotag;
            sprite.clipdist = source.clipdist;
            sprite.xrepeat = source.xrepeat;
            sprite.yrepeat = source.yrepeat;
            sprite.xoffset = source.xoffset;
            sprite.yoffset = source.yoffset;
            sprite.statnum = source.statnum;
            sprite.owner = source.owner;
            sprite.xvel = source.xvel;
            sprite.yvel = source.yvel;
            sprite.zvel = source.zvel;
            sprite.extra = source.extra;
            sprite.filler = source.filler;
            sprite.cstat = static_cast<uint16_t>(source.cstat);
            loaded.m_sprites.push_back(sprite);
        }
        loaded.m_playerStart.position = QPointF(map->posx, map->posy);
        loaded.m_playerStart.z = map->posz;
        loaded.m_playerStart.angle = map->ang * 360.0 / 2048.0;
        if (map->cursectnum >= 0) loaded.m_playerStart.sectorId = map->cursectnum;
        *this = std::move(loaded);
        return true;
    } catch (const std::exception &exception) {
        error = QString::fromUtf8(exception.what());
        return false;
    }
}
