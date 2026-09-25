#include "mapsave.h"
#include "mapdocument.h"
#include <libduke/map.h>

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const QString &message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

template<typename T> T number(qreal value, const QString &label)
{
    const double rounded = std::round(value);
    require(std::isfinite(rounded) && rounded >= std::numeric_limits<T>::lowest()
            && rounded <= std::numeric_limits<T>::max(), label + ": value is outside the Build field range.");
    return static_cast<T>(rounded);
}

int16_t bits(int value, const QString &label)
{
    require(value >= -32768 && value <= 65535, label + ": expected a 16-bit value.");
    return static_cast<int16_t>(value > 32767 ? value - 65536 : value);
}

int16_t tile(int value, const QString &label)
{
    // Original Duke 3D BUILD.H defines MAXTILES as 6144.
    require(value >= 0 && value < 6144, label + ": choose a Duke 3D texture (0–6143).");
    return static_cast<int16_t>(value);
}

int16_t angle(qreal degrees, const QString &label)
{
    require(std::isfinite(degrees), label + ": angle must be finite.");
    double wrapped = std::fmod(degrees, 360.0);
    if (wrapped < 0) wrapped += 360;
    return static_cast<int16_t>(std::lround(wrapped * 2048.0 / 360.0) % 2048);
}

// Use the rounded, exported geometry, including boundary points. An ambiguous
// interior requires explicit geometry repair; a shared boundary uses its first
// adjoining sector deterministically.
int16_t containingSector(const DukeMapFile &map, int32_t x, int32_t y, const QString &label,
                         std::optional<MapDocument::SectorId> preferred)
{
    int interior = -1, boundary = -1, interiorCount = 0;
    for (int s = 0; s < map.numsectors; ++s) {
        const auto location = duke_map_sector_classify_point(&map, s, x, y);
        require(location != DUKE_MAP_POINT_INVALID, label + ": invalid sector geometry.");
        const bool inside = location == DUKE_MAP_POINT_INSIDE;
        const bool onBoundary = location == DUKE_MAP_POINT_BOUNDARY;
        if ((onBoundary || inside) && preferred && *preferred == static_cast<std::size_t>(s))
            return static_cast<int16_t>(s);
        if (onBoundary) {
            if (boundary < 0) boundary = s;
        } else if (inside) {
            ++interiorCount;
            interior = s;
        }
    }
    require(interiorCount <= 1, label + ": lies in overlapping sectors; sector membership is ambiguous.");
    const int result = interior >= 0 ? interior : boundary;
    require(result >= 0, label + ": place it inside a closed sector.");
    return static_cast<int16_t>(result);
}
}

static bool buildMap(const MapDocument &document, QString &error,
                  const std::function<bool(DukeMapFile &, QString &)> &consume, MapCheckResult *issue,
                  BuildMapValidation validation)
{
    error.clear();
    using Target = MapCheckResult::Target;
    const auto target = [issue](Target kind, std::size_t id = 0, bool reversed = false) {
        if (issue) { issue->target = kind; issue->id = id; issue->reversed = reversed; }
    };
    try {
        const auto &sectors = document.sectors();
        require(!sectors.empty(), "The map has no closed sectors.");
        require(sectors.size() <= MAPV8_MAXSECTORS, "Version 8 supports at most 4096 sectors.");
        require(document.sprites().size() <= MAPV8_MAXSPRITES, "Version 8 supports at most 16384 sprites.");
        std::size_t wallCount = 0;
        for (std::size_t id = 0; id < sectors.size(); ++id) {
            target(Target::Sector, id);
            const auto &sector = sectors[id];
            require(sector.walls.size() >= 3 && sector.walls.size() == sector.vertices.size(),
                    "A sector has an incomplete boundary.");
            wallCount += sector.walls.size();
        }
        target(Target::Map);
        require(wallCount <= MAPV8_MAXWALLS, "Version 8 supports at most 16384 wall sides (shared lines count twice).");

        // Storage owns the records; libduke borrows the pointer arrays below.
        std::vector<DukeMapSector> sectorRecords(sectors.size());
        std::vector<DukeMapWall> wallRecords(wallCount);
        std::vector<DukeMapSprite> spriteRecords(document.sprites().size());
        std::vector<DukeMapSector *> sectorPointers;
        std::vector<DukeMapWall *> wallPointers;
        std::vector<DukeMapSprite *> spritePointers;
        for (auto &record : sectorRecords) sectorPointers.push_back(&record);
        for (auto &record : wallRecords) wallPointers.push_back(&record);
        for (auto &record : spriteRecords) spritePointers.push_back(&record);
        DukeMapFile map{};
        map.mapversion = sectors.size() <= MAPV7_MAXSECTORS && wallCount <= MAPV7_MAXWALLS
            && spriteRecords.size() <= MAPV7_MAXSPRITES ? 7 : 8;
        map.numsectors = static_cast<int16_t>(sectors.size());
        map.numwalls = static_cast<uint16_t>(wallCount);
        map.numsprites = static_cast<uint16_t>(spriteRecords.size());
        map.sectors = sectorPointers.data();
        map.walls = wallPointers.data();
        map.sprites = spritePointers.data();

        std::vector<std::array<int, 2>> sideIndices(document.walls().size(), {-1, -1});
        std::vector<int> wallOwners(wallCount, -1);
        int nextWall = 0;
        for (std::size_t id = 0; id < sectors.size(); ++id) {
            target(Target::Sector, id);
            const auto &source = sectors[id];
            auto &out = sectorRecords[id];
            const QString label = QString("Sector %1").arg(id);
            out.wallptr = static_cast<int16_t>(nextWall);
            out.wallnum = static_cast<int16_t>(source.walls.size());
            out.ceilingz = number<decltype(out.ceilingz)>(source.ceilingz, label + " ceilingz");
            out.floorz = number<decltype(out.floorz)>(source.floorz, label + " floorz");
            out.ceilingheinum = number<decltype(out.ceilingheinum)>(source.ceilingheinum, label + " ceilingheinum");
            out.floorheinum = number<decltype(out.floorheinum)>(source.floorheinum, label + " floorheinum");
            out.ceilingshade = number<decltype(out.ceilingshade)>(source.ceilingshade, label + " ceilingshade");
            out.floorshade = number<decltype(out.floorshade)>(source.floorshade, label + " floorshade");
            out.ceilingpal = number<decltype(out.ceilingpal)>(source.ceilingpal, label + " ceilingpal");
            out.floorpal = number<decltype(out.floorpal)>(source.floorpal, label + " floorpal");
            out.ceilingxpanning = number<decltype(out.ceilingxpanning)>(source.ceilingxpanning, label + " ceilingxpanning");
            out.ceilingypanning = number<decltype(out.ceilingypanning)>(source.ceilingypanning, label + " ceilingypanning");
            out.floorxpanning = number<decltype(out.floorxpanning)>(source.floorxpanning, label + " floorxpanning");
            out.floorypanning = number<decltype(out.floorypanning)>(source.floorypanning, label + " floorypanning");
            out.visibility = number<decltype(out.visibility)>(source.visibility, label + " visibility");
            out.extra = number<decltype(out.extra)>(source.extra, label + " extra");
            out.filler = number<uint8_t>(source.filler, label + " filler");
            out.ceilingstat = bits(source.ceilingstat, label + " ceilingstat");
            out.floorstat = bits(source.floorstat, label + " floorstat");
            out.lotag = bits(source.lotag, label + " lotag");
            out.hitag = bits(source.hitag, label + " hitag");
            out.ceilingpicnum = tile(source.ceilingTexture, label + " ceiling texture");
            out.floorpicnum = tile(source.floorTexture, label + " floor texture");
            for (std::size_t j = 0; j < source.walls.size(); ++j, ++nextWall) {
                target(Target::Sector, id);
                const auto wallId = source.walls[j];
                const auto vertexId = source.vertices[j];
                const auto endId = source.vertices[source.nextWallIndex(j)];
                require(wallId < document.walls().size() && vertexId < document.vertices().size()
                        && endId < document.vertices().size(), label + ": invalid boundary reference.");
                const auto &wall = document.walls()[wallId];
                const bool reversed = wall.end == vertexId && wall.start == endId;
                require(reversed || (wall.start == vertexId && wall.end == endId), label + ": disconnected boundary.");
                auto &index = sideIndices[wallId][reversed ? 1 : 0];
                require(index < 0, label + ": a wall side belongs to multiple sectors.");
                index = nextWall;
                wallOwners[nextWall] = static_cast<int>(id);
                target(Target::Wall, wallId, reversed);
                const auto &side = reversed ? wall.reverseSide : wall.forwardSide;
                auto &record = wallRecords[nextWall];
                const QString wallLabel = QString("Line %1, sector %2").arg(wallId).arg(id);
                const auto &position = document.vertices()[vertexId].position;
                record.x = number<int32_t>(position.x(), wallLabel + " X");
                record.y = number<int32_t>(position.y(), wallLabel + " Y");
                record.point2 = static_cast<int16_t>(out.wallptr + source.nextWallIndex(j));
                record.nextwall = record.nextsector = -1;
                record.picnum = tile(side.texture, wallLabel + " texture");
                require(side.overlayTexture == -1 || (side.overlayTexture >= 0 && side.overlayTexture < 6144),
                        wallLabel + ": overlay texture must be -1 (none) or a Duke 3D texture (0–6143).");
                record.overpicnum = static_cast<int16_t>(side.overlayTexture);
                record.shade = number<decltype(record.shade)>(side.shade, wallLabel + " shade");
                record.pal = number<decltype(record.pal)>(side.palette, wallLabel + " palette");
                record.xrepeat = number<decltype(record.xrepeat)>(side.xrepeat, wallLabel + " xrepeat");
                record.yrepeat = number<decltype(record.yrepeat)>(side.yrepeat, wallLabel + " yrepeat");
                record.xpanning = number<decltype(record.xpanning)>(side.xpanning, wallLabel + " xpanning");
                record.ypanning = number<decltype(record.ypanning)>(side.ypanning, wallLabel + " ypanning");
                record.extra = number<decltype(record.extra)>(side.extra, wallLabel + " extra");
                record.cstat = bits(side.cstat, wallLabel + " cstat");
                record.lotag = bits(side.lotag, wallLabel + " lotag");
                record.hitag = bits(side.hitag, wallLabel + " hitag");
            }
        }
        for (std::size_t id = 0; id < sideIndices.size(); ++id) {
            target(Target::Wall, id);
            const auto &indices = sideIndices[id];
            require(indices[0] >= 0 || indices[1] >= 0,
                    QString("Line %1 is not part of a closed sector.").arg(id));
            if (indices[0] < 0 || indices[1] < 0) continue;
            for (int side = 0; side < 2; ++side) {
                auto &wall = wallRecords[indices[side]];
                wall.nextwall = static_cast<int16_t>(indices[1 - side]);
                wall.nextsector = static_cast<int16_t>(wallOwners[indices[1 - side]]);
            }
        }
        // libduke exposes textual diagnostics. Translate exported wall numbers
        // back to editor lines/sides here; never treat them as editor IDs.
        const auto libraryFailure = [&] {
            target(Target::Map);
            const QString diagnostic = QString::fromUtf8(map.last_error);
            if (diagnostic.startsWith("Starting ") || diagnostic.startsWith("Invalid starting ")) {
                target(Target::PlayerStart);
            } else {
                const auto match = QRegularExpression("\\b(Sector|Wall(?:s| loop beginning at)?|Sprite|wall) ([0-9]+)\\b").match(diagnostic);
                if (match.hasMatch()) {
                    const auto id = match.captured(2).toULongLong();
                    if (match.captured(1) == "Sector" && id < sectors.size()) target(Target::Sector, id);
                    else if (match.captured(1) == "Sprite" && id < document.sprites().size()) target(Target::Sprite, id);
                    else if ((match.captured(1).startsWith("Wall") || match.captured(1) == "wall") && id < wallCount) {
                        for (std::size_t line = 0; line < sideIndices.size(); ++line)
                            for (int side = 0; side < 2; ++side)
                                if (sideIndices[line][side] == static_cast<int>(id)) target(Target::Wall, line, side != 0);
                    }
                }
            }
            throw std::runtime_error(map.last_error);
        };
        if (validation == BuildMapValidation::Strict) {
            if (!duke_map_file_validate_vertical_sectors(&map)) libraryFailure();
            if (!duke_map_file_validate_portals(&map)) libraryFailure();
        }
        // Imported effect sprites may lie outside their sector's polygon. The
        // renderer needs a valid sector index, not strict geometric membership.
        const auto membership = [&](int32_t x, int32_t y, const QString &label,
                                    std::optional<MapDocument::SectorId> preferred) {
            if (validation == BuildMapValidation::Preview && preferred && *preferred < sectors.size()) {
                return static_cast<int16_t>(*preferred);
            }
            return containingSector(map, x, y, label, preferred);
        };

        for (std::size_t id = 0; id < spriteRecords.size(); ++id) {
            target(Target::Sprite, id);
            const auto &source = document.sprites()[id];
            auto &out = spriteRecords[id];
            const QString label = QString("Sprite %1").arg(id);
            out.x = number<int32_t>(source.position.x(), label + " X");
            out.y = number<int32_t>(source.position.y(), label + " Y");
            out.z = number<int32_t>(source.z, label + " Z");
            out.ang = angle(source.angle, label);
            out.picnum = tile(source.texture, label + " texture");
            out.sectnum = membership(out.x, out.y, label, source.sectorId);
            out.shade = number<decltype(out.shade)>(source.shade, label + " shade");
            out.pal = number<decltype(out.pal)>(source.palette, label + " palette");
            out.clipdist = number<decltype(out.clipdist)>(source.clipdist, label + " clipdist");
            out.xrepeat = number<decltype(out.xrepeat)>(source.xrepeat, label + " xrepeat");
            out.yrepeat = number<decltype(out.yrepeat)>(source.yrepeat, label + " yrepeat");
            out.xoffset = number<decltype(out.xoffset)>(source.xoffset, label + " xoffset");
            out.yoffset = number<decltype(out.yoffset)>(source.yoffset, label + " yoffset");
            out.statnum = number<decltype(out.statnum)>(source.statnum, label + " statnum");
            out.owner = number<decltype(out.owner)>(source.owner, label + " owner");
            out.xvel = number<decltype(out.xvel)>(source.xvel, label + " xvel");
            out.yvel = number<decltype(out.yvel)>(source.yvel, label + " yvel");
            out.zvel = number<decltype(out.zvel)>(source.zvel, label + " zvel");
            out.extra = number<decltype(out.extra)>(source.extra, label + " extra");
            out.filler = number<uint8_t>(source.filler, label + " filler");
            out.cstat = bits(source.cstat, label + " cstat");
            out.lotag = bits(source.lotag, label + " lotag");
            out.hitag = bits(source.hitag, label + " hitag");
            require((source.cstat & 48) != 48, label + ": invalid sprite alignment.");
        }
        target(Target::PlayerStart);
        const auto &start = document.playerStart();
        map.posx = number<int32_t>(start.position.x(), "Player start X");
        map.posy = number<int32_t>(start.position.y(), "Player start Y");
        map.posz = number<int32_t>(start.z, "Player start Z");
        map.ang = angle(start.angle, "Player start");
        map.cursectnum = membership(map.posx, map.posy, "Player start", start.sectorId);
        if (validation == BuildMapValidation::Strict) {
            if (!duke_map_file_validate(&map)) libraryFailure();
        } else if (!duke_map_file_validate_references(&map)) {
            libraryFailure();
        }

        return consume(map, error);
    } catch (const std::exception &exception) {
        error = QString::fromUtf8(exception.what());
        return false;
    }
}

bool withBuildMap(const MapDocument &document, QString &error,
                  const std::function<bool(DukeMapFile &, QString &)> &consume, BuildMapValidation validation)
{
    return buildMap(document, error, consume, nullptr, validation);
}

MapCheckResult checkMap(const MapDocument &document)
{
    MapCheckResult result;
    result.valid = buildMap(document, result.message, [](DukeMapFile &, QString &) { return true; }, &result, BuildMapValidation::Strict);
    if (result.valid) {
        result.target = MapCheckResult::Target::Map;
        result.message = "No errors found by Build map save validation.";
    }
    return result;
}

bool saveBuildMap(const MapDocument &document, const QString &filename, QString &error)
{
    return withBuildMap(document, error, [&](DukeMapFile &map, QString &) {
        // libduke writes a filename, so stage its output before atomically
        // replacing the user's destination through QSaveFile.
        QTemporaryFile temporary;
        if (!temporary.open()) throw std::runtime_error(temporary.errorString().toStdString());
        const QString temporaryName = temporary.fileName();
        temporary.close();
        const QByteArray nativeName = QFile::encodeName(temporaryName);
        if (!duke_map_file_write_to_filename(&map, nativeName.constData())) throw std::runtime_error(map.last_error);
        QFile input(temporaryName);
        if (!input.open(QIODevice::ReadOnly)) throw std::runtime_error(input.errorString().toStdString());
        const QByteArray bytes = input.readAll();
        require(input.error() == QFileDevice::NoError, input.errorString());
        QSaveFile output(filename);
        if (!output.open(QIODevice::WriteOnly)) throw std::runtime_error(output.errorString().toStdString());
        if (output.write(bytes) != bytes.size()) throw std::runtime_error(output.errorString().toStdString());
        if (!output.commit()) throw std::runtime_error(output.errorString().toStdString());
        return true;
    });
}
