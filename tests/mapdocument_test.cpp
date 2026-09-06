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
            const auto next = sector.vertices[(i + 1) % sector.vertices.size()];
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

}
