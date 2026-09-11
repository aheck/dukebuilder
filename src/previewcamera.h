#pragma once
#include "mapdocument.h"
// Prepare only a copy's player start for preview; never changes editor data.
bool placePreviewCamera(MapDocument &snapshot, const QPointF &pointer, QString &error);
