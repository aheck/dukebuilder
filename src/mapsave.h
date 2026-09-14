#pragma once
#include <QString>
class MapDocument;
#include <cstddef>

struct MapCheckResult {
    enum class Target { Map, Sector, Wall, Sprite, PlayerStart, Drawing };
    bool valid = false;
    QString message;
    Target target = Target::Map;
    std::size_t id = 0;
    bool reversed = false;
};
// Runs exactly the export validation, without writing a file. Reports the first
// blocker; callers can navigate to its editor object and check again after fixing.
MapCheckResult checkMap(const MapDocument &document);

// Export a validated version-7 Build map. Failure leaves the destination intact
// and returns a diagnostic in error. Editor coordinates are Build coordinates;
// fractional coordinates are rounded, and angles are expressed in degrees.
bool saveBuildMap(const MapDocument &document, const QString &filename, QString &error);

#include <functional>
#include <libduke/map.h>
// Supply a validated, borrowed in-memory snapshot for the duration of consume.
// The consumer must not retain map pointers; it can report failures via error.
bool withBuildMap(const MapDocument &document, QString &error,
                  const std::function<bool(DukeMapFile &, QString &)> &consume);
