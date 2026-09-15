#pragma once

struct Tag {
    int tag;
    const char *meaning;
};

// Classic Duke Nukem 3D / Atomic Edition Sector Effector lotags (tile 1).
// https://wiki.eduke32.com/wiki/Sector_Effector_Reference_Guide
constexpr Tag sectorEffectorLotags[] = {
    {0, "Sector rotation"},
    {1, "Rotation pivot"},
    {2, "Earthquake"},
    {3, "Shot-triggered flicker"},
    {4, "Flickering lights"},
    {5, "Boss sector (unfinished)"},
    {6, "Subway engine"},
    {7, "Teleport"},
    {8, "Door lighting: up"},
    {9, "Door lighting: down"},
    {10, "Automatic door closing"},
    {11, "Swinging door"},
    {12, "Switched lighting"},
    {13, "Explosive sector"},
    {14, "Subway carriage"},
    {15, "Sliding door"},
    {16, "Reactor rotation (unfinished)"},
    {17, "Transport elevator"},
    {18, "Incremental vertical movement"},
    {19, "Explosion-triggered ceiling drop"},
    {20, "Stretching bridge"},
    {21, "Dropping floor"},
    {22, "Teeth-door component"},
    {23, "One-way teleport exit"},
    {24, "Conveyor / current"},
    {25, "Piston ceiling"},
    {26, "Escalator (unfinished)"},
    {27, "Demo viewpoint"},
    {28, "Lightning generator"},
    {29, "Waves"},
    {30, "Shuttle train"},
    {31, "Moving floor"},
    {32, "Moving ceiling"},
    {33, "Quake debris"},
    {34, "Alternate conveyor (undocumented)"},
    {35, "Drill (unfinished)"},
    {36, "Projectile emitter"},
    {49, "Point light"},
    {50, "Spot light"},
};

// Sector tags are distinct from Sector Effector sprite tags.
// https://wiki.eduke32.com/wiki/Sector_Tag_Reference_Guide
constexpr Tag sectorLotags[] = {
    {0, "Normal sector"},
    {1, "Water surface"},
    {2, "Underwater"},
    {3, "Cycloid Emperor movement area"},
    {9, "Star Trek sliding doors"},
    {15, "Transport elevator"},
    {16, "Descending platform"},
    {17, "Ascending platform"},
    {18, "Elevator down"},
    {19, "Elevator up"},
    {20, "Ceiling door"},
    {21, "Floor door"},
    {22, "Vertically splitting door"},
    {23, "Hinged door"},
    {25, "Door sliding sideways"},
    {26, "Star Trek split door"},
    {27, "Stretching bridge"},
    {28, "Dropping floor / ceiling"},
    {29, "Teeth-door prong"},
    {30, "Bridge rotation and elevation"},
    {31, "Two-way train"},
    {10000, "Play sound 0 once (10000 + sound ID)"},
    {32767, "Secret area"},
    {65534, "End of level with message"},
    {65535, "Immediate end of exit"},
};
