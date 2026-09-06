#pragma once
#include <QString>
class MapDocument;

// Export a validated version-7 Build map. Failure leaves the destination intact
// and returns a diagnostic in error. Editor coordinates are Build coordinates;
// fractional coordinates are rounded, and angles are expressed in degrees.
bool saveBuildMap(const MapDocument &document, const QString &filename, QString &error);
