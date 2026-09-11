#pragma once
#include <QString>
class MapDocument;

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
