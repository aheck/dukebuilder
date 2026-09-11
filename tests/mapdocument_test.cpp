#include "mapdocument.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void checkSideReferences(const MapDocument &document)
{
    for (std::size_t id = 0; id < document.sectors().size(); ++id) {
        const auto &sector = document.sectors()[id];
        for (std::size_t i = 0; i < sector.walls.size(); ++i) {
            const auto &wall = document.walls()[sector.walls[i]];
            const auto next = sector.vertices[sector.nextWallIndex(i)];
            require((wall.start == sector.vertices[i] && wall.end == next)
                    || (wall.end == sector.vertices[i] && wall.start == next),
                    "Sector boundary must remain connected and closed");
            const auto owner = wall.start == sector.vertices[i]
                ? wall.forwardSector : wall.reverseSector;
            require(owner && *owner == id, "Wall side has incorrect sector number");
        }
    }
}
}

int main()
{
    {
        MapDocument split;
        require(split.addPolyline({{0,0}, {1024,0}, {1024,1024}, {0,1024}}, true), "Split outer room");
        require(split.addPolyline({{256,256}, {768,256}, {768,768}, {256,768}}, true), "Split inner room");
        split.setSectorLotag(0, 17);
        split.setSectorFloorZ(1, 4096);
        const auto before = split;
        require(!split.splitWall(0, {0,0}), "Do not split at an endpoint");
        require(!split.splitWall(0, {512,1}), "Do not bend a wall to an off-line point");
        require(!split.splitWall(999, {512,0}), "Reject unknown wall");
        require(split == before, "Rejected splits leave document unchanged");
        require(split.splitWall(0, {512,0}).has_value(), "Split outer wall");
        require(split.sectors()[0].loopStarts == std::vector<std::size_t>({0,5}),
                "Splitting outer loop shifts hole start");
        checkSideReferences(split);
        const auto wallId = split.sectors()[1].walls.front();
        const auto wall = split.walls()[wallId];
        auto front = wall.forwardSide;
        auto back = wall.reverseSide;
        front.texture = 123;
        front.xpanning = 37;
        back.texture = 456;
        back.lotag = 42;
        split.setWallSide(wallId, false, front);
        split.setWallSide(wallId, true, back);
        const auto position = (split.vertices()[wall.start].position + split.vertices()[wall.end].position) / 2.0;
        const auto vertexId = split.splitWall(wallId, position);
        require(vertexId.has_value(), "Split shared wall");
        require(split.vertices()[*vertexId].position == position, "Inserted vertex position");
        auto firstFront = front, lastFront = front, firstBack = back, lastBack = back;
        firstFront.xrepeat = lastFront.xrepeat = front.xrepeat / 2;
        firstBack.xrepeat = lastBack.xrepeat = back.xrepeat / 2;
        lastFront.xpanning = (front.xpanning + firstFront.xrepeat*8) % 256;
        firstBack.xpanning = (back.xpanning + lastBack.xrepeat*8) % 256;
        require(split.walls()[wallId].forwardSide == firstFront && split.walls().back().forwardSide == lastFront
                && split.walls()[wallId].reverseSide == firstBack && split.walls().back().reverseSide == lastBack,
                "Split preserves density and continues both sides' texture coordinates");
        require(split.sectors()[0].walls.size() == 10 && split.sectors()[1].walls.size() == 5,
                "Shared wall split updates both sectors");
        require(split.sectors()[0].lotag == 17 && split.sectors()[1].floorz == 4096,
                "Sector properties survive splits");
        checkSideReferences(split);
        MapDocument diagonal;
        require(diagonal.addPolyline({{0,0}, {1024,1024}, {0,1024}}, true), "Diagonal room");
        require(diagonal.splitWall(0, {256,256}).has_value(), "Split diagonal wall");
        checkSideReferences(diagonal);
    }
    MapDocument islands;
    require(islands.addPolyline({{0,0}, {1000,0}, {1000,1000}, {0,1000}}, true), "Outer room");
    islands.setSectorLotag(0, 17);
    require(islands.addPolyline({{100,100}, {400,100}, {400,400}, {100,400}}, true), "Inner box");
    islands.setSectorFloorZ(1, -1024);
    require(islands.sectors()[0].loopStarts == std::vector<std::size_t>({0,4}), "Room gets a hole");
    for (auto wall : islands.sectors()[1].walls)
        require(islands.walls()[wall].isTwoSided(), "Box connects to surrounding room");
    require(islands.addPolyline({{600,600}, {800,600}, {800,800}, {600,800}}, true), "Second box");
    require(islands.sectors()[0].loopStarts.size() == 3, "Multiple boxes produce separate holes");
    require(islands.sectors()[0].lotag == 17 && islands.sectors()[1].floorz == -1024,
            "Rebuilding holes retains sector properties");
    require(islands.addPolyline({{150,150}, {250,150}, {250,250}, {150,250}}, true), "Box on box");
    require(islands.sectors()[1].loopStarts.size() == 2 && islands.sectors()[0].loopStarts.size() == 3,
            "Nested box attaches only to its immediate parent");
    islands.setVertexPositions({{4, {90,100}}});
    checkSideReferences(islands);
    islands.removeWalls({12});
    checkSideReferences(islands);
    require(islands.sectors()[0].lotag == 17 && islands.sectors()[1].floorz == -1024,
            "Deleting an island edge retains properties");

    MapDocument edited;
    require(edited == MapDocument{}, "New documents have no changes");
    require(edited.addPolyline({{0, 0}, {100, 0}, {100, 100}, {0, 100}}, true),
            "Create document for saved-state comparison");
    const auto saved = edited;
    require(edited == saved, "Saved snapshot matches document");
    edited.setSectorFloorTexture(0, 42);
    require(!(edited == saved), "Texture changes differ from saved snapshot");
    edited.setSectorFloorTexture(0, saved.sectors()[0].floorTexture);
    require(edited == saved, "Restoring a property removes unsaved changes");
    auto changedSide = edited.walls()[0].reverseSide;
    changedSide.shade = 12;
    edited.setWallSide(0, true, changedSide);
    require(!(edited == saved), "Wall back-side changes are tracked");
    edited = saved;
    edited.setVertexPositions({{0, {-10, 0}}});
    require(!(edited == saved), "Vertex movement is tracked");
    edited = saved;
    const auto sprite = edited.addSprite({50, 50});
    require(!(edited == saved), "Sprite insertion is tracked");
    const auto withSprite = edited;
    edited.setSpriteAngle(sprite, 90);
    require(!(edited == withSprite), "Sprite properties are tracked");
    edited.removeSprites({sprite});
    require(edited == saved, "Removing an added sprite restores saved state");
    edited.setPlayerStartAngle(90);
    require(!(edited == saved), "Player start changes are tracked");

    // Every edge (including the closing edge) must remove only its own wall.
    for (MapDocument::WallId deleted = 0; deleted < 4; ++deleted) {
        MapDocument rectangle;
        require(rectangle.addPolyline({{0, 0}, {100, 0}, {100, 100}, {0, 100}}, true),
                "Rectangle for individual edge deletion");
        for (MapDocument::WallId id = 0; id < 4; ++id) {
            auto side = rectangle.walls()[id].forwardSide;
            side.texture = 100 + id;
            rectangle.setWallSide(id, false, side);
        }
        rectangle.removeWalls({deleted});
        require(rectangle.walls().size() == 3 && rectangle.sectors().size() == 1,
                "Single rectangle edge deletion must preserve the other three walls");
        for (MapDocument::WallId id = 0; id < 4; ++id) {
            const bool retained = std::any_of(rectangle.walls().begin(), rectangle.walls().end(),
                [&](const auto &wall) { return wall.forwardSide.texture == static_cast<int>(100 + id); });
            require(retained == (id != deleted), "Only the selected wall's properties should disappear");
        }
        checkSideReferences(rectangle);
    }

    MapDocument document;
    require(document.addPolyline({{0, 0}, {100, 0}, {100, 100}, {0, 100}}, true), "First sector");
    require(document.addPolyline({{300, 0}, {400, 0}, {400, 100}, {300, 100}}, true), "Second sector");
    auto first = document.sectors()[0];
    std::rotate(first.walls.begin(), first.walls.begin() + 1, first.walls.end());
    std::rotate(first.vertices.begin(), first.vertices.begin() + 1, first.vertices.end());
    first.lotag = 42;
    first.floorheinum = 256;
    document.setSector(0, first);
    const auto secondWalls = document.sectors()[1].walls;

    // This neighbor is discovered through sector 0's first geometric edge,
    // before the disconnected sector 1 in the wall traversal.
    require(document.addPolyline({{100, 0}, {0, 0}, {0, -100}, {100, -100}}, true), "Neighbor sector");
    require(document.sectors().size() == 3, "Expected three sectors");
    require(document.sectors()[0].walls == first.walls, "First sector and first wall must stay stable");
    require(document.sectors()[1].walls == secondWalls, "Neighbor must not renumber second sector");
    require(document.sectors()[0].lotag == 42 && document.sectors()[0].floorheinum == 256,
            "Sector properties must survive rebuilding");
    const auto neighborWalls = document.sectors()[2].walls;
    checkSideReferences(document);

    document.setVertexPositions({{first.vertices[0], {120, 0}}});
    require(document.sectors()[0].walls == first.walls
            && document.sectors()[1].walls == secondWalls
            && document.sectors()[2].walls == neighborWalls, "Moving a vertex must preserve numbers");
    checkSideReferences(document);

    require(!document.addPolyline({{600, 0}, {700, 0}}, false), "Open isolated line must be rejected");
    require(document.sectors()[1].walls == secondWalls
            && document.sectors()[2].walls == neighborWalls, "Rejected geometry must preserve numbers");
    checkSideReferences(document);

    // Removing a face through degenerate geometry compacts surviving indices.
    std::vector<std::pair<MapDocument::VertexId, QPointF>> collapsed;
    for (const auto vertex : document.sectors()[1].vertices) collapsed.push_back({vertex, {300, 0}});
    document.setVertexPositions(collapsed);
    require(document.sectors().size() == 2, "Collapsed sector should disappear");
    require(document.sectors()[0].walls == first.walls
            && document.sectors()[1].walls == neighborWalls, "Surviving sectors must compact in order");
    checkSideReferences(document);

    MapDocument deletion;
    require(deletion.addPolyline({{0, 0}, {100, 0}, {100, 100}, {0, 100}}, true), "Deletion sector");
    require(deletion.addPolyline({{300, 0}, {400, 0}, {400, 100}, {300, 100}}, true), "Unaffected sector");
    deletion.setSectorLotag(0, 42);
    deletion.setSectorLotag(1, 77);
    auto side = deletion.walls()[4].forwardSide;
    side.texture = 123;
    deletion.setWallSide(4, false, side);
    deletion.removeWalls({0});
    require(deletion.walls().size() == 7 && deletion.sectors().size() == 2
            && deletion.sectors()[0].walls.size() == 3,
            "Deleting an outer line must leave a closed triangle");
    require(deletion.sectors()[0].lotag == 42 && deletion.sectors()[1].lotag == 77 && deletion.walls()[3].forwardSide.texture == 123,
            "Wall compaction must preserve unaffected sector and side properties");
    checkSideReferences(deletion);
    deletion.removeWalls({2, 0, 2, 999});
    require(deletion.walls().size() == 4 && deletion.vertices().size() == 4,
            "Multiple deletion must deduplicate IDs and remove orphaned vertices");
    require(deletion.sectors()[0].lotag == 77, "Unaffected sector survives repeated deletion");
    checkSideReferences(deletion);
    deletion.removeWalls({});
    deletion.removeWalls({999});
    require(deletion.walls().size() == 4, "Empty and invalid selections must be harmless");
    deletion.removeWalls({0, 1, 2, 3, 4});
    require(deletion.walls().empty() && deletion.vertices().empty() && deletion.sectors().empty(),
            "Deleting every line must clear geometry");

    MapDocument shared;
    require(shared.addPolyline({{0, 0}, {100, 0}, {100, 100}, {0, 100}}, true), "Shared first sector");
    require(shared.addPolyline({{100, 0}, {200, 0}, {200, 100}, {100, 100}}, true), "Shared second sector");
    require(shared.walls()[1].isTwoSided(), "Expected shared line");
    shared.removeWalls({1});
    require(shared.walls().size() == 6 && shared.sectors().size() == 2
            && shared.sectors()[0].walls.size() == 3
            && shared.sectors()[1].walls.size() == 3, "Deleting shared line must keep both sectors closed");
    require(std::none_of(shared.walls().begin(), shared.walls().end(),
                        [](const auto &wall) { return wall.isTwoSided(); }),
            "Deleted portal must leave no stale two-sided references");
    checkSideReferences(shared);

    MapDocument multiple;
    require(multiple.addPolyline({{0, 0}, {100, 0}, {200, 100}, {100, 200}, {0, 200}}, true),
            "Multi-edge sector");
    multiple.setSectorLotag(0, 91);
    multiple.removeWalls({1, 0, 1});
    require(multiple.sectors().size() == 1 && multiple.walls().size() == 3
            && multiple.sectors()[0].lotag == 91, "Adjacent selected edges collapse to a closed triangle");
    require(multiple.vertices()[0].position == QPointF(0, 0), "Collapse keeps the lowest-numbered endpoint");
    checkSideReferences(multiple);

    multiple.removeWalls({0});
    require(multiple.sectors().empty() && multiple.walls().empty() && multiple.vertices().empty(),
            "Deleting one edge of a triangle must leave no stray line or vertices");

    MapDocument adjacentTriangle;
    require(adjacentTriangle.addPolyline({{0, 0}, {100, 0}, {0, 100}}, true), "Triangle to collapse");
    require(adjacentTriangle.addPolyline({{100, 0}, {100, 100}, {0, 100}}, true), "Neighbor to preserve");
    adjacentTriangle.setSectorLotag(1, 73);
    adjacentTriangle.removeWalls({0});
    require(adjacentTriangle.sectors().size() == 1 && adjacentTriangle.walls().size() == 3
            && adjacentTriangle.vertices().size() == 3 && adjacentTriangle.sectors()[0].lotag == 73,
            "Collapsed triangle cleanup must preserve the neighbor's boundary");
    checkSideReferences(adjacentTriangle);

    MapDocument nestedHeights;
    require(nestedHeights.addPolyline({{0,0},{1000,0},{1000,1000},{0,1000}}, true), "Parent room");
    nestedHeights.setSectorFloorZ(0, 4096);
    nestedHeights.setSectorCeilingZ(0, -16384);
    require(nestedHeights.addPolyline({{100,100},{900,100},{900,900},{100,900}}, true), "Child room");
    require(nestedHeights.sectors()[1].floorz == 4096
            && nestedHeights.sectors()[1].ceilingz == -16384, "Child inherits parent heights");
    nestedHeights.setSectorFloorZ(1, 2048);
    nestedHeights.setSectorCeilingZ(1, -12288);
    require(nestedHeights.addPolyline({{200,200},{800,200},{800,800},{200,800}}, true), "Grandchild room");
    require(nestedHeights.sectors()[2].floorz == 2048
            && nestedHeights.sectors()[2].ceilingz == -12288, "Nearest parent supplies heights");
    require(nestedHeights.addPolyline({{2000,0},{3000,0},{3000,1000},{2000,1000}}, true), "Outside room");
    require(nestedHeights.sectors()[3].floorz == 0
            && nestedHeights.sectors()[3].ceilingz == -8192, "Outside room uses defaults");
    require(nestedHeights.sectors()[0].floorz == 4096
            && nestedHeights.sectors()[1].floorz == 2048
            && nestedHeights.sectors()[2].ceilingz == -12288, "Rebuild preserves existing heights");
    checkSideReferences(nestedHeights);

    MapDocument density;
    require(density.addPolyline({{0,0},{2048,0},{2048,1024},{0,1024}}, true), "Density room");
    require(density.walls()[0].forwardSide.xrepeat == 16
            && density.walls()[1].forwardSide.xrepeat == 8, "New walls have length-based density");
    auto custom = density.walls()[0].forwardSide;
    custom.xrepeat = 32; custom.yrepeat = 21;
    density.setWallSide(0, false, custom);
    const auto dragStart = density;
    density.setVertexPositions({{1,{2050,0}}}, &dragStart);
    density.setVertexPositions({{1,{4096,0}}}, &dragStart);
    require(density.walls()[0].forwardSide.xrepeat == 64
            && density.walls()[0].reverseSide.xrepeat == 32
            && density.walls()[0].forwardSide.yrepeat == 21, "Resize preserves custom density on both sides");
    density.setVertexPositions({{1,{2048,0}}}, &dragStart);
    require(density.walls()[0].forwardSide.xrepeat == 32, "Drag back restores exact repeat");
    require(density.defaultWallXRepeat(0) == 16, "Reset computes default independent of custom density");

    MapDocument ornament;
    require(ornament.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}}, true), "Ornament room");
    const auto decoration = ornament.addSprite({900,512});
    auto decorationValues = ornament.sprites()[decoration];
    decorationValues.z=-2048; decorationValues.angle=0; decorationValues.texture=123; decorationValues.lotag=42;
    decorationValues.cstat=32|1|64; decorationValues.xrepeat=37;
    ornament.setSprite(decoration,decorationValues);
    QString error;
    require(ornament.stickSpriteToWall(decoration,error), "Ornament to nearest wall");
    const auto attached=ornament.sprites()[decoration];
    require(attached.position == QPointF(1023,512) && attached.angle == 180,
            "Hits nearest wall regardless of sprite angle");
    require(attached.z == decorationValues.z && attached.texture == decorationValues.texture
            && attached.lotag == 42 && attached.xrepeat == 37
            && attached.cstat == (16|1|64) && attached.sectorId == 0,
            "Wall alignment preserves other sprite properties and sets sector");
    require(ornament.stickSpriteToWall(decoration,error)
            && ornament.sprites()[decoration] == attached, "Repeated ornament is stable");
    ornament.setSpritePositions({{decoration,{2000,2000}}});
    const auto outside=ornament;
    require(!ornament.stickSpriteToWall(decoration,error) && ornament == outside,
            "Outside sprite fails without changes");

    MapDocument holes;
    require(holes.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}},true), "Outer ornament room");
    require(holes.addPolyline({{256,256},{768,256},{768,768},{256,768}},true), "Inner ornament room");
    const auto holeSprite=holes.addSprite({200,512});
    holes.setSpriteAngle(holeSprite,180);
    require(holes.stickSpriteToWall(holeSprite,error), "Ornament onto shared hole boundary");
    require(holes.sprites()[holeSprite].position == QPointF(255,512)
            && holes.sprites()[holeSprite].angle == 180 && holes.sprites()[holeSprite].sectorId == 0,
            "Stops at two-sided wall and faces back into owning sector");

}
