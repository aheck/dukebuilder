#pragma once

#include "tags.h"
#include <iterator>
#include <vector>

struct SpriteLotags {
    const char *description;
    std::vector<Tag> presets;
};

// Standard Duke 3D / Atomic tile identities, not artwork categories: rotation
// frames and dead enemies must not inherit the live actor's tag semantics.
// Sources: https://github.com/jonof/jfduke3d/blob/master/src/game.c (spawn),
// src/sector.c (checkhitswitch), src/actors.c (movefx), src/premap.c, src/names.h.
// Custom CON definitions / remapped tiles can change these meanings, so callers
// must always allow arbitrary numeric input.
inline SpriteLotags spriteLotags(int tile)
{
    if (tile == 1) {
        return {"Sector Effector: effect type.",
                {std::begin(sectorEffectorLotags), std::end(sectorEffectorLotags)}};
    }
    switch (tile) {
    // Actors explicitly filtered by player_skill in spawn().
    case 1267: case 1550: // Rat, shark
    case 1680: case 1681: case 1682: case 1715: case 1725:
    case 1741: case 1742: case 1744: // Trooper variants
    case 1820: case 1821: case 1880: case 1920: case 1921:
    case 1960: case 2000: case 2001: case 2045:
    case 2120: case 2121: case 2150: case 2160: case 2165:
    case 2360: case 2370: case 2420:
    case 2630: case 2631: case 2710: case 2760: case 4740: case 4741:
    // Atomic CON actors use spawn()'s scripted-actor difficulty filter.
    case 1975: case 4610: case 4611: case 4690: case 4670: case 4671:
    // Pickups with the same filter; access cards and placed pipebombs differ.
    case 21: case 22: case 23: case 24: case 25: case 27: case 28: case 29:
    case 37: case 40: case 41: case 42: case 44: case 45: case 46: case 47:
    case 48: case 49: case 51: case 52: case 53: case 54: case 55: case 56:
    case 57: case 59: case 61: case 100: case 1348:
        return {"Minimum difficulty: appears at this skill and all higher skills. 0 and 1 both include all four skills.",
                {{0, "All difficulties (default)"}, {1, "Piece of Cake and above"},
                 {2, "Let's Rock and above"}, {3, "Come Get Some and above"},
                 {4, "Damn I'm Good only"}}};
    case 2: case 3: case 4: case 8: case 9:
        return {"Activation channel: match the lotag of the triggering switch or touchplate. For Respawn, hitag is the tile to spawn.", {}};
    case 5:
        return {"MusicAndSFX: sound ID or 1000 + echo amount (0-255). Hitag and sector type determine sound behavior/range. Standard Duke 3D sounds are suggested; mods may define others.",
                {std::begin(musicAndSfxLotags), std::end(musicAndSfxLotags)}};
    case 6:
        return {"Locator: numbered waypoint in a vehicle path, starting at 0.",
                {{0, "First waypoint"}}};
    case 7:
        return {"Cycler: initial lighting phase; hitag is the activation channel.", {}};
    case 10:
        return {"GPSpeed: speed value for the sector's effect; units depend on the effect.", {}};
    case 1405:
        return {"Player sprite: multiplayer start type (separate from the map's player start).",
                {{0, "DukeMatch start"}, {1, "Cooperative start"}}};
    default:
        break;
    }
    // Include the on/off artwork for switches, plus all four multiswitch states.
    constexpr int switches[] = {130, 132, 134, 136, 138, 140, 162, 164, 166,
                               168, 170, 712, 860, 862, 864, 1111, 1122, 1142, 1155};
    bool isSwitch = tile >= 146 && tile <= 149;
    for (int base : switches) {
        if (tile == base || tile == base + 1) { isSwitch = true; }
    }
    if (isSwitch) {
        return {"Switch: activation channel matching activators/effects. Multiswitch states use consecutive channels. -1 (65535) ends the level.",
                {{0, "Inactive switch"}, {-1, "End level (65535)"}}};
    }
    return {"No known lotag preset for this tile. Meaning depends on the sprite and game scripts.", {}};
}
