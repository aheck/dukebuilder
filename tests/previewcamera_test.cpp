#include "previewcamera.h"
#include "mapsave.h"
#include <QCoreApplication>
#include <cstdlib>
#include <iostream>
#include <cmath>
static void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    MapDocument document;
    require(document.addPolyline({{0,0},{2048,0},{2048,2048},{0,2048}}, true), "room");
    const auto original = document;
    QString error;
    for (const auto point : {QPointF(1024,1024), QPointF(-500,1024), QPointF(-500,-500)}) {
        auto snapshot = original;
        require(placePreviewCamera(snapshot, point, error), "place camera");
        require(snapshot.playerStart().z < 0 && snapshot.playerStart().z > -8192, "vertical placement");
        bool valid = withBuildMap(snapshot, error, [&](DukeMapFile &map, QString &) {
            return duke_map_sector_classify_point(&map, 0, map.posx, map.posy) == DUKE_MAP_POINT_INSIDE;
        });
        if (!valid) { std::cerr << error.toStdString() << " at " << snapshot.playerStart().position.x() << "," << snapshot.playerStart().position.y() << "\n"; }
        require(valid, "preview start lies inside exported sector");
        if (point == QPointF(1024,1024)) {
            require(snapshot.playerStart().position == point, "preserve point inside sector");
        }
        require(document == original, "do not modify editor data");
    }
    auto sector = document.sectors()[0];
    sector.floorstat = 2; sector.floorheinum = 256;
    document.setSector(0,sector);
    require(placePreviewCamera(document, {1024,1024}, error), "sloped start");
    require(std::abs(document.playerStart().z - (-3584)) < 1, "slope height");
    MapDocument empty;
    require(!placePreviewCamera(empty, {}, error), "empty map rejected");
    return 0;
}
