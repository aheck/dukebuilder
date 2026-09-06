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
}
