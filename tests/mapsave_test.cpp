#include "mapdocument.h"
#include "mapsave.h"
#include <libduke/map.h>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>

namespace {
void require(bool value, const QString &message)
{
    if (!value) {
        std::cerr << message.toStdString() << '\n';
        std::exit(EXIT_FAILURE);
    }
}
QByteArray read(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Read exported file");
    return file.readAll();
}
MapDocument room()
{
    MapDocument document;
    require(document.addPolyline({{-1024, -1024}, {1024, -1024}, {1024, 1024}, {-1024, 1024}}, true), "Create room");
    document.setPlayerStartPosition({32.4, -64.6});
    document.setPlayerStartZ(-4096);
    document.setPlayerStartAngle(90);
    return document;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "Temporary directory");
    const QString path = directory.filePath("roundtrip.map");
    QString error;
    MapDocument box = room();
    box.setPlayerStartPosition({700,700});
    require(box.addPolyline({{-256,-256}, {256,-256}, {256,256}, {-256,256}}, true), "Draw raised box");
    box.setSectorFloorZ(1, -2048);
    box.setSectorFloorTexture(1, 899);
    const QString boxPath = directory.filePath("box.map");
    require(saveBuildMap(box, boxPath, error), error);
    std::unique_ptr<DukeMapFile, decltype(&duke_map_file_free)> exportedBox(duke_map_file_new(), duke_map_file_free);
    require(duke_map_file_read_from_filename(exportedBox.get(), QFile::encodeName(boxPath).constData()),
            "Read box export");
    require(exportedBox->numwalls == 12 && exportedBox->sectors[0]->wallnum == 8,
            "Export surrounding room with inner loop");
    for (int w = 8; w < 12; ++w) {
        const auto &wall = *exportedBox->walls[w];
        require(wall.nextsector == 0 && wall.nextwall >= 4 && wall.nextwall < 8
                && exportedBox->walls[wall.nextwall]->nextwall == w,
                "Box has reciprocal portals visible from surrounding room");
    }
    MapDocument reopenedBox;
    require(reopenedBox.openMap(boxPath, error), error);
    require(reopenedBox.sectors()[1].floorz == -2048 && reopenedBox.sectors()[1].floorTexture == 899,
            "Raised box height and texture survive reopening");
    require(saveBuildMap(reopenedBox, directory.filePath("box-copy.map"), error), error);
    require(read(boxPath) == read(directory.filePath("box-copy.map")), "Box portals survive round trip");
    {
        MapDocument splitBox = reopenedBox;
        const auto id = splitBox.sectors()[1].walls.front();
        const auto &wall = splitBox.walls()[id];
        const QPointF midpoint = (splitBox.vertices()[wall.start].position
                                  + splitBox.vertices()[wall.end].position) / 2.0;
        require(splitBox.splitWall(id, midpoint).has_value(), "Split imported shared wall");
        const QString splitPath = directory.filePath("split-box.map");
        require(saveBuildMap(splitBox, splitPath, error), error);
        MapDocument loadedSplit;
        require(loadedSplit.openMap(splitPath, error), error);
        require(loadedSplit.sectors()[0].walls.size() == 9 && loadedSplit.sectors()[1].walls.size() == 5,
                "Split portal is retained on both sides after export/import");
        require(saveBuildMap(loadedSplit, directory.filePath("split-copy.map"), error), error);
        require(read(splitPath) == read(directory.filePath("split-copy.map")), "Split map round trip preserves bytes");
    }
    require(reopenedBox.supportsTopologyEditing(), "Reopened connected box supports line editing");
    const auto boxWall = reopenedBox.sectors()[1].walls.front();
    reopenedBox.removeWalls({boxWall});
    require(reopenedBox.walls().size() == 7 && reopenedBox.sectors()[1].walls.size() == 3,
            "Delete a box edge after reopening");
    require(reopenedBox.sectors()[1].floorz == -2048 && reopenedBox.sectors()[1].floorTexture == 899,
            "Deletion preserves raised box properties");
    require(saveBuildMap(reopenedBox, directory.filePath("box-deleted.map"), error), error);
    require(reopenedBox.openMap(directory.filePath("box-deleted.map"), error), error);
    require(reopenedBox.supportsTopologyEditing(), "Edited box remains editable after another reload");
    // Reproduce the disconnected overlapping square saved by older versions.
    exportedBox->sectors[0]->wallnum = 4;
    exportedBox->sectors[1]->wallptr = 4;
    for (int i = 0; i < 4; ++i) {
        *exportedBox->walls[4 + i] = *exportedBox->walls[8 + i];
        exportedBox->walls[4 + i]->point2 = 4 + (i + 1) % 4;
        exportedBox->walls[4 + i]->nextwall = -1;
        exportedBox->walls[4 + i]->nextsector = -1;
    }
    const auto originalWallCount = exportedBox->numwalls;
    exportedBox->numwalls = 8;
    const QString overlapPath = directory.filePath("legacy-overlap.map");
    require(duke_map_file_write_to_filename(exportedBox.get(), QFile::encodeName(overlapPath).constData()),
            "Write disconnected legacy box fixture");
    exportedBox->numwalls = originalWallCount; // Free every allocated record.
    MapDocument legacy;
    require(legacy.openMap(overlapPath, error), error);
    require(!legacy.supportsTopologyEditing() && legacy.supportsLineDeletion(),
            "Overlapping single-loop sectors support local deletion without rebuilding faces");
    legacy.removeWalls({legacy.sectors()[1].walls.front()});
    require(legacy.sectors().size() == 2 && legacy.sectors()[1].walls.size() == 3,
            "Delete a line from a disconnected legacy box");
    require(legacy.sectors()[0].walls.size() == 4 && legacy.sectors()[1].floorz == -2048,
            "Local deletion preserves independent outer room and box height");
    require(saveBuildMap(legacy, directory.filePath("legacy-deleted.map"), error), error);
    MapDocument document = room();
    require(document.addPolyline({{1024, -1024}, {3072, -1024}, {3072, 1024}, {1024, 1024}}, true), "Neighbor");
    auto sector = document.sectors()[0];
    std::rotate(sector.walls.begin(), sector.walls.begin() + 1, sector.walls.end());
    std::rotate(sector.vertices.begin(), sector.vertices.begin() + 1, sector.vertices.end());
    sector.floorTexture = 80; sector.ceilingTexture = 81;
    sector.floorstat = 66; sector.ceilingstat = 64;
    sector.floorheinum = 16; sector.ceilingheinum = -17;
    sector.floorshade = -128; sector.ceilingshade = 127;
    sector.floorpal = 12; sector.ceilingpal = 13;
    sector.floorxpanning = 255; sector.floorypanning = 23;
    sector.ceilingxpanning = 24; sector.ceilingypanning = 25;
    sector.visibility = 192; sector.lotag = 65535; sector.hitag = -32768; sector.extra = 9;
    document.setSector(0, sector);
    auto side = document.walls()[1].forwardSide;
    side.texture = 123; side.overlayTexture = 124;
    side.cstat = 32769; side.shade = -32; side.palette = 255;
    side.xrepeat = 11; side.yrepeat = 12; side.xpanning = 13; side.ypanning = 14;
    side.lotag = -1; side.hitag = 234; side.extra = 56;
    document.setWallSide(1, false, side);
    side.texture = 321;
    document.setWallSide(1, true, side);
    const auto spriteId = document.addSprite({2000, 100});
    auto sprite = document.sprites()[spriteId];
    sprite.texture = 1405; sprite.z = -512; sprite.angle = 270;
    sprite.cstat = 32784; sprite.shade = -127; sprite.palette = 15;
    sprite.clipdist = 17; sprite.xrepeat = 18; sprite.yrepeat = 19;
    sprite.xoffset = -128; sprite.yoffset = 127; sprite.statnum = 3;
    sprite.owner = -1; sprite.xvel = -20; sprite.yvel = 21; sprite.zvel = -22;
    sprite.lotag = -32768; sprite.hitag = 32767; sprite.extra = -123;
    document.setSprite(spriteId, sprite);
    require(saveBuildMap(document, path, error), error);
    const QByteArray bytes = read(path);
    require(bytes.size() == 26 + 2 * 40 + 8 * 32 + 44, "Exact v7 record sizes");
    require(bytes.left(4) == QByteArray::fromHex("07000000"), "Version is little endian 7");
    std::unique_ptr<DukeMapFile, decltype(&duke_map_file_free)> map(duke_map_file_new(), duke_map_file_free);
    require(map != nullptr, "Allocate reader");
    const auto nativePath = QFile::encodeName(path);
    require(duke_map_file_read_from_filename(map.get(), nativePath.constData()), "Read saved Build map");
    require(duke_map_file_validate(map.get()), "Full validation after roundtrip");
    require(map->posx == 32 && map->posy == -65 && map->posz == -4096 && map->ang == 512
            && map->cursectnum == 0, "Player start coordinates, angle and sector");
    const auto &s = *map->sectors[0];
    require(s.wallptr == 0 && s.wallnum == 4 && map->walls[0]->x == 1024 && map->walls[0]->y == -1024,
            "Preserve first wall and sector ordering");
    require(s.floorpicnum == 80 && s.ceilingpicnum == 81 && s.floorstat == 66 && s.ceilingstat == 64
            && s.floorheinum == 16 && s.ceilingheinum == -17 && s.floorshade == -128 && s.ceilingshade == 127
            && s.floorpal == 12 && s.ceilingpal == 13 && s.floorxpanning == 255 && s.floorypanning == 23
            && s.ceilingxpanning == 24 && s.ceilingypanning == 25 && s.visibility == 192
            && s.lotag == -1 && s.hitag == -32768 && s.extra == 9 && s.filler == 0, "Sector properties roundtrip");
    const auto &w = *map->walls[0];
    require(w.nextwall >= 4 && w.nextsector == 1 && map->walls[w.nextwall]->nextwall == 0
            && map->walls[w.nextwall]->nextsector == 0 && w.picnum == 123
            && map->walls[w.nextwall]->picnum == 321, "Reciprocal portal with independent side textures");
    require(w.overpicnum == 124 && static_cast<uint16_t>(w.cstat) == 32769 && w.shade == -32 && w.pal == 255
            && w.xrepeat == 11 && w.yrepeat == 12 && w.xpanning == 13 && w.ypanning == 14
            && w.lotag == -1 && w.hitag == 234 && w.extra == 56, "Wall properties roundtrip");
    const auto &p = *map->sprites[0];
    require(p.x == 2000 && p.y == 100 && p.z == -512 && p.sectnum == 1 && p.ang == 1536 && p.picnum == 1405
            && static_cast<uint16_t>(p.cstat) == 32784 && p.shade == -127 && p.pal == 15 && p.clipdist == 17
            && p.xrepeat == 18 && p.yrepeat == 19 && p.xoffset == -128 && p.yoffset == 127
            && p.statnum == 3 && p.owner == -1 && p.xvel == -20 && p.yvel == 21 && p.zvel == -22
            && p.lotag == -32768 && p.hitag == 32767 && p.extra == -123 && p.filler == 0, "Sprite properties roundtrip");

    const auto rejected = [&](const MapDocument &invalid, const QString &diagnostic) {
        require(!saveBuildMap(invalid, path, error), "Invalid map must be rejected: " + diagnostic);
        require(error.contains(diagnostic, Qt::CaseInsensitive), "Expected diagnostic " + diagnostic + ", got: " + error);
        require(read(path) == bytes, "Failed save must preserve existing destination");
    };
    rejected(MapDocument{}, "no closed sectors");
    auto invalid = room();
    invalid.setPlayerStartPosition({5000, 5000});
    rejected(invalid, "Player start");
    invalid = room(); invalid.setPlayerStartZ(100);
    rejected(invalid, "Starting Z");
    invalid = room(); invalid.addSprite({0, 0});
    rejected(invalid, "texture");
    invalid.setSpriteTexture(0, 1405); invalid.setSpritePositions({{0, {5000, 5000}}});
    rejected(invalid, "Sprite 0");
    invalid = room(); invalid.setSectorCeilingZ(0, 100);
    rejected(invalid, "ceiling");
    invalid = room(); invalid.setPlayerStartZ(std::numeric_limits<double>::infinity());
    rejected(invalid, "Player start Z");
    invalid = room(); invalid.setSectorFloorTexture(0, 6144);
    rejected(invalid, "texture");
    invalid = room(); invalid.setVertexPositions({{1, {-1023.8, -1024}}});
    rejected(invalid, "zero length");
    invalid = room();
    for (int i = 0; i <= MAPV7_MAXSPRITES; ++i) invalid.addSprite({0, 0});
    rejected(invalid, "4096 sprites");
    require(!saveBuildMap(document, directory.filePath("missing/map.map"), error) && !error.isEmpty(), "Report I/O failure");
    require(saveBuildMap(document, path, error) && error.isEmpty() && read(path) == bytes, "Repeated saves are deterministic");

    MapDocument reversed;
    require(reversed.addPolyline({{-1024, -1024}, {-1024, 1024}, {1024, 1024}, {1024, -1024}}, true),
            "Draw a room in the opposite direction");
    reversed.setPlayerStartAngle(360);
    const QString reversedPath = directory.filePath("reverse.map");
    require(saveBuildMap(reversed, reversedPath, error), error);
    require(read(reversedPath).size() == 26 + 40 + 4 * 32, "Player start must not create an ordinary sprite");
    require(duke_map_file_read_from_filename(map.get(), QFile::encodeName(reversedPath).constData()), "Read reversed room");
    require(duke_map_file_validate(map.get()) && map->ang == 0 && map->numsprites == 0,
            "Opposite drawing direction produces valid winding and 360 degrees wraps to zero");

    MapDocument opened;
    require(opened.openMap(path, error), error);
    require(opened.sectors().size() == 2 && opened.walls().size() == 7 && opened.vertices().size() == 6,
            "Import must weld portal endpoints and combine both wall sides");
    require(opened.playerStart().sectorId == 0 && opened.sprites()[0].sectorId == 1,
            "Import preserves explicit sector membership");
    const QString copiedPath = directory.filePath("opened.map");
    require(saveBuildMap(opened, copiedPath, error), error);
    require(read(copiedPath) == bytes, "Open/save preserves every v7 record field");
    const QString brokenPath = directory.filePath("broken.map");
    QFile broken(brokenPath);
    require(broken.open(QIODevice::WriteOnly) && broken.write(bytes.left(25)) == 25, "Create truncated fixture");
    broken.close();
    require(!opened.openMap(brokenPath, error) && !error.isEmpty(), "Reject truncated map");
    require(saveBuildMap(opened, copiedPath, error) && read(copiedPath) == bytes,
            "Failed open leaves the current document intact");
    require(!opened.openMap(directory.filePath("missing.map"), error), "Report missing map");
    const auto rejectModifiedMap = [&](QByteArray modified, const QString &message) {
        require(broken.open(QIODevice::WriteOnly | QIODevice::Truncate)
                && broken.write(modified) == modified.size(), "Write invalid fixture");
        broken.close();
        require(!opened.openMap(brokenPath, error) && !error.isEmpty(), message);
        require(saveBuildMap(opened, copiedPath, error) && read(copiedPath) == bytes,
                "Invalid map must not replace the current document");
    };
    auto modified = bytes;
    modified[0] = 6;
    rejectModifiedMap(modified, "Reject unsupported map version");
    // The v7 header is 22 bytes, followed by 40-byte sectors and a wall count.
    const int firstWall = 22 + 40 * 2 + 2;
    modified = bytes;
    modified[firstWall + 8] = char(0xff);
    modified[firstWall + 9] = char(0x7f);
    rejectModifiedMap(modified, "Reject out-of-range next-point link");
    modified = bytes;
    modified[firstWall + 10] = 0;
    modified[firstWall + 11] = 0;
    rejectModifiedMap(modified, "Reject inconsistent portal reference");

    // A sector with an inner loop must remain a single sector with a hole.
    DukeMapSector ring{};
    ring.wallnum = 8; ring.ceilingz = -8192; ring.extra = -1; ring.filler = 17;
    std::vector<DukeMapWall> ringWalls(8);
    const QPoint points[] = {{-1024,-1024},{1024,-1024},{1024,1024},{-1024,1024},
                             {-256,-256},{-256,256},{256,256},{256,-256}};
    std::vector<DukeMapWall *> ringPointers;
    for (int i = 0; i < 8; ++i) {
        auto &wall = ringWalls[i];
        wall.x = points[i].x(); wall.y = points[i].y();
        wall.point2 = i < 4 ? (i + 1) % 4 : 4 + (i - 3) % 4;
        wall.nextwall = wall.nextsector = wall.extra = -1;
        wall.xrepeat = wall.yrepeat = 8;
        ringPointers.push_back(&wall);
    }
    DukeMapSector *ringPointer = &ring;
    DukeMapFile ringMap{};
    ringMap.mapversion = 7; ringMap.numsectors = 1; ringMap.numwalls = 8;
    ringMap.sectors = &ringPointer; ringMap.walls = ringPointers.data();
    ringMap.posx = 512; ringMap.posz = -4096;
    const QString ringPath = directory.filePath("ring.map");
    require(duke_map_file_validate(&ringMap), "Valid ring fixture");
    require(duke_map_file_write_to_filename(&ringMap, QFile::encodeName(ringPath).constData()), "Write ring fixture");
    require(opened.openMap(ringPath, error), error);
    require(opened.sectors().size() == 1 && opened.sectors()[0].loopStarts == std::vector<std::size_t>({0, 4}),
            "Keep outer and inner loops");
    require(opened.supportsTopologyEditing(), "Imported void loops remain editable");
    require(saveBuildMap(opened, copiedPath, error) && read(copiedPath) == read(ringPath),
            "Hole and reserved bytes survive open/save unchanged");
    opened.setVertexPositions({{0, {-1100, -1024}}});
    opened.setSectorFloorTexture(0, 80);
    require(opened.sectors()[0].loopStarts.size() == 2 && saveBuildMap(opened, copiedPath, error),
            "Vertex and property edits preserve imported holes");

    require(opened.addPolyline({{1024,-1024},{2048,-1024},{2048,1024},{1024,1024}},true),
            "Draw neighbor after loading a map with a void");
    require(opened.sectors().size() == 2 && opened.sectors()[0].loopStarts.size() == 2,
            "Imported void remains a hole after drawing");
    require(saveBuildMap(opened, copiedPath, error), error);
    MapDocument reopenedHole;
    require(reopenedHole.openMap(copiedPath, error), error);
    require(reopenedHole.supportsTopologyEditing() && reopenedHole.sectors().size() == 2
            && reopenedHole.sectors()[0].loopStarts.size() == 2,
            "Edited void survives save and reload and remains editable");
    require(reopenedHole.addPolyline({{3000,0},{4000,0},{4000,1000},{3000,1000}},true),
            "Continue drawing after another reload");
    require(saveBuildMap(reopenedHole, copiedPath, error), error);

    // Optional local fixtures permit checking original maps without bundling
    // copyrighted game data in the repository.
    for (int i = 1; i < argc; ++i) {
        require(opened.openMap(QString::fromLocal8Bit(argv[i]), error), error);
        std::cout << "Opened " << argv[i] << ": " << opened.sectors().size()
                  << " sectors, " << opened.sprites().size() << " sprites\n";
    }
}
