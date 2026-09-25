#pragma once

#include "mapdocument.h"
#include <QSize>
#include <QStringList>

struct WallTextureTarget {
    MapDocument::WallId wall;
    bool reversed = false;
    bool operator==(const WallTextureTarget &other) const {
        return wall == other.wall && reversed == other.reversed;
    }
};

struct WallTextureAlignmentResult {
    int aligned = 0;
    int changed = 0;
    int skipped = 0;
    bool approximate = false;
    bool closingSeam = false;
    QString error;
    QStringList reasons;
};

// Align only the connected, compatible selected sides. The reference, repeats,
// flags and all other properties remain unchanged. No renderer is required.
WallTextureAlignmentResult alignWallTextures(MapDocument &document,
    const std::vector<WallTextureTarget> &selection, WallTextureTarget reference,
    QSize textureSize);
