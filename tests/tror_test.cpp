#include "mapdocument.h"
#include "mapsave.h"
#include "previewcamera.h"
#include "recovery.h"
#include <libduke/map.h>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <cstdlib>

static void require(bool value, const QString &why)
{
    if (!value) { std::cerr << why.toStdString() << '\n'; std::exit(1); }
}
static QByteArray read(const QString &name)
{
    QFile file(name); require(file.open(QIODevice::ReadOnly), "Read fixture"); return file.readAll();
}
static MapDocument room()
{
    MapDocument d;
    require(d.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}},true), "Create room");
    d.setPlayerStartPosition({512,512});
    return d;
}
static void roundTrip(const MapDocument &d, const QString &path, int version)
{
    QString error;
    require(withBuildMap(d,error,[&](DukeMapFile &m, QString &) {
        require(m.mapversion == version, "Correct automatic version");
        require(duke_map_file_validate_tror(&m), m.last_error);
        return true;
    }),error);
    require(saveBuildMap(d,path,error),error);
    auto bytes = read(path);
    MapDocument loaded;
    require(loaded.openMap(path,error),error);
    require(loaded.validateTror(error),error);
    require(saveBuildMap(loaded,path,error),error);
    require(read(path) == bytes, "Byte-stable TROR round trip");
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir dir;
    require(dir.isValid(), "Temporary directory");
    const auto path = dir.filePath("tror.map");
    QString error;
    {
        auto transformed = room();
        require(transformed.extendTror(0, true, 8192, error) == 1, error);
        const auto original = transformed;
        MapDocument::TransformSelection targets; targets.sectors = {0,1};
        require(transformed.transformSelection(targets, {512,512}, 90, false, error), error);
        require(transformed.validateTror(error), error);
        require(transformed.transformSelection(targets, {512,512}, 0, true, error), error);
        roundTrip(transformed, path, 9);
        require(transformed.transformSelection(targets, {512,512}, 0, true, error), error);
        require(transformed.transformSelection(targets, {512,512}, -90, false, error), error);
        require(transformed == original, "TROR transforms restore explicit side links");
        targets.sectors = {0};
        require(!transformed.transformSelection(targets, {}, 45, false, error) && transformed == original,
                "Incomplete TROR transform rejects atomically");
    }
    {
        auto original = room();
        require(original.extendTror(0, true, 8192, error) == 1, error);
        MapDocument fragment;
        require(original.copySectors({0,1}, fragment, error), error);
        require(fragment.validateTror(error), error);
        require(original.pasteSectors(fragment, {4096,0}, error), error);
        require(original.sectors()[2].floorBunch == original.sectors()[3].ceilingBunch
            && original.sectors()[2].floorBunch != original.sectors()[0].floorBunch,
            "Pasted TROR connections use a fresh bunch");
        roundTrip(original, path, 9);
        require(original.copySectors({0}, fragment, error), error);
        require(!fragment.hasTror(), "A partial TROR copy detaches external connections");
        require(original.pasteSectors(fragment, {8192,0}, error), error);
        roundTrip(original, path, 9);
    }
    // Reproduce the beginner tutorial: two visible rooms, three sectors.
    MapDocument tutorial;
    require(tutorial.addPolyline({{-2048,-2048},{2048,-2048},{2048,2048},{-2048,2048}},true), "Tutorial upper room");
    tutorial.setSectorCeilingZ(0,-16384);
    require(tutorial.addPolyline({{-1024,-1024},{1024,-1024},{1024,1024},{-1024,1024}},true), "Tutorial opening");
    tutorial.setSectorCeilingZ(1,-16384);
    require(tutorial.extendTror(1,true,16384,error) == 2,error);
    tutorial.setPlayerStartPosition({-1536,0});
    tutorial.setPlayerStartZ(-6144);
    require(!tutorial.sectors()[0].floorBunch && tutorial.sectors()[1].floorBunch
        && tutorial.sectors()[2].ceilingz == 0 && tutorial.sectors()[2].floorz == 16384,
        "Tutorial keeps surrounding floor solid above smaller lower room");
    require(tutorial.layerSectors(1) == std::set<MapDocument::SectorId>({0,1})
        && tutorial.layerSectors(2) == std::set<MapDocument::SectorId>({2}), "Tutorial layer navigation");
    roundTrip(tutorial,path,9);
    auto d = room();
    const auto original = d;
    require(d.extendTror(0,true,8192,error) == 1,error);
    require(d.hasTror() && d.sectors()[0].floorBunch == 0 && d.sectors()[1].ceilingBunch == 0, "Explicit bunch ownership");
    require(d.playerStart().sectorId == 0, "Creating overlap retains original player membership");
    require(d.layerSectors(0) == std::set<MapDocument::SectorId>{0}, "Layers do not traverse vertical connections");
    require(d.verticalNeighbors(0,true) == std::set<MapDocument::SectorId>{1}, "Navigate down");
    require(d.verticalNeighbors(1,false) == std::set<MapDocument::SectorId>{0}, "Navigate up");
    roundTrip(d,path,9);
    auto preview = d;
    require(placePreviewCamera(preview,{512,512},error,std::set<MapDocument::SectorId>{1}),error);
    require(preview.playerStart().sectorId == 1 && preview.playerStart().z > 0, "3D starts in isolated lower layer");
    require(d.extendTror(1,true,8192,error) == 2,error);
    auto before = d;
    d.setVertexPositions({{0,{-256,0}}});
    for (const auto &s : d.sectors()) { require(d.vertices()[s.vertices[0]].position == QPointF(-256,0), "Linked movement traverses all levels"); }
    require(d.validateTopologyChange(before,error),error);
    const auto count = d.walls().size();
    require(d.splitWall(0,{384,0}).has_value(), "Split complete vertical wall chain");
    require(d.walls().size() == count+3, "One split per level");
    for (const auto &s : d.sectors()) { require(s.walls.size() == 5, "Each boundary gains a vertex"); }
    require(d.validateTror(error),error);
    roundTrip(d,path,9);
    d.setSectorFloorZ(0,1024);
    require(d.sectors()[1].ceilingz == 1024 && d.sectors()[2].ceilingz == 8192, "Height change affects just its bunch");
    auto values = d.sectors()[0];
    values.floorheinum = 256; values.floorstat |= 2;
    d.setSector(0,values);
    require(d.sectors()[1].ceilingheinum == 256, "Slope follows connected plane");
    require(d.validateTror(error),error);
    values = d.sectors()[0]; values.floorTexture = 123; values.floorshade = 17;
    d.setSector(0,values);
    require(d.sectors()[1].ceilingTexture == 0 && d.sectors()[1].ceilingshade == 0, "Materials do not propagate");
    roundTrip(d,path,9);
    RecoverySnapshot snapshot; snapshot.document = d;
    RecoverySnapshot decoded;
    require(RecoveryCodec::decode(RecoveryCodec::encode(snapshot),decoded,error),error);
    require(decoded.document == d, "Recovery preserves all TROR data");
    before = d;
    require(!d.addPolyline({{384,0},{384,1024}},false,&error,std::set<MapDocument::SectorId>{0}), "Unsupported topology drawing is rejected");
    require(!d.removeSectors({0},error) && !d.removeVertices({0},error), "Unsupported deletion is rejected");
    require(!d.joinSectors({0,1},error) && !d.supportsLineDeletion(), "Unsafe planar merges are blocked");
    require(!d.extendTror(0,true,8192,error) && d == before, "Failed operations leave map unchanged");
    auto unrelated = d;
    require(unrelated.addPolyline({{2048,0},{3072,0},{3072,1024},{2048,1024}},true,&error),error);
    require(unrelated.addPolyline({{3072,256},{3584,256},{3584,768},{3072,768}},false,&error),error);
    for (std::size_t i = 0; i < d.sectors().size(); ++i) { require(unrelated.sectors()[i] == d.sectors()[i], "Unrelated drawing preserves TROR sectors"); }
    for (std::size_t i = 0; i < d.walls().size(); ++i) { require(unrelated.walls()[i] == d.walls()[i], "Unrelated drawing preserves TROR wall links"); }
    roundTrip(unrelated,path,9);
    auto invalid = d;
    values = invalid.sectors()[0]; values.floorxpanning = 4; invalid.setSector(0,values);
    require(!invalid.validateTror(error), "Bunch storage cannot be edited as texture panning");
    invalid = d;
    auto side = invalid.walls()[0].forwardSide; side.extra = 12; invalid.setWallSide(0,false,side);
    require(!invalid.validateTror(error), "Vertical links cannot be overwritten by game tags");
    invalid = d; invalid.setSectorFloorZ(0,20000);
    require(!invalid.validateTopologyChange(d,error), "Inverted linked room rejected");
    require(d.disconnectTror(0,true,error),error);
    require(!d.sectors()[0].floorBunch && !d.sectors()[1].ceilingBunch && d.hasTror(), "Disconnect one whole bunch");
    require(d.disconnectTror(1,true,error) && !d.hasTror(),error);
    roundTrip(d,path,7);

    // Existing compatible sectors can be reconnected without changing geometry.
    require(d.connectTror(0,1,error),error);
    require(d.validateTror(error),error);
    roundTrip(d,path,9);
    auto above = original;
    require(above.extendTror(0,false,16384,error) == 1,error);
    require(above.sectors()[1].floorz == -8192 && above.sectors()[1].ceilingz == -24576, "Extend ceiling upward");
    roundTrip(above,path,9);

    // A reverse side on an ordinary red wall connects to a forward TROR side.
    auto portal = room();
    require(portal.addPolyline({{1024,0},{2048,0},{2048,1024},{1024,1024}},true), "Adjacent room");
    require(portal.extendTror(1,true,8192,error) == 2,error);
    bool reversedSplit = false;
    for (std::size_t w = 0; w < portal.walls().size(); ++w) {
        const auto wall = portal.walls()[w];
        if (wall.reverseSide.downLink && !wall.reverseSide.downLink->reversed) {
            const auto p = (portal.vertices()[wall.start].position+portal.vertices()[wall.end].position)/2;
            require(portal.splitWall(w,p).has_value(), "Split reverse-side TROR link and ordinary portal together");
            reversedSplit = true; break;
        }
    }
    require(reversedSplit, "Reverse-side fixture exercised");
    require(portal.validateTror(error),error);
    require(portal.sectors()[0].walls.size() == 5, "Real red-wall neighbor also gains vertex");
    roundTrip(portal,path,9);
    auto hole = room();
    require(hole.addPolyline({{256,256},{768,256},{768,768},{256,768}},true), "Inner room");
    hole.setPlayerStartPosition({128,128});
    require(hole.extendTror(0,true,8192,error).has_value(),error);
    require(hole.sectors().back().loopStarts.size() == 2, "Extensions retain holes");
    roundTrip(hole,path,9);

    for (int i = 1; i < argc; ++i) {
        MapDocument imported;
        require(imported.openMap(QString::fromLocal8Bit(argv[i]),error),error);
        require(imported.hasTror(), "External TROR sample");
        require(imported.validateTror(error),error);
        require(withBuildMap(imported,error,[](DukeMapFile &,QString &) { return true; },BuildMapValidation::Preview),error);
        roundTrip(imported,path,9);
        std::cout << "External TROR fixture passed: " << argv[i] << '\n';
    }
}
