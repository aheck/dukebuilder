#include "previewcamera.h"
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <limits>

bool placePreviewCamera(MapDocument &snapshot, const QPointF &pointer, QString &error)
{
    double best = std::numeric_limits<double>::infinity();
    QPointF position;
    const MapDocument::Sector *selected = nullptr;
    for (const auto &sector : snapshot.sectors()) {
        if (sector.vertices.size() < 3) { continue; }
        QPainterPath path;
        path.setFillRule(Qt::OddEvenFill);
        for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
            const auto p = snapshot.vertices()[sector.vertices[i]].position;
            if (i == 0 || std::find(sector.loopStarts.begin(), sector.loopStarts.end(), i) != sector.loopStarts.end()) {
                if (i != 0) { path.closeSubpath(); }
                path.moveTo(p);
            } else { path.lineTo(p); }
        }
        path.closeSubpath();
        auto consider = [&](QPointF p) {
            // Match the integer coordinates used in the Build snapshot.
            p = QPointF(std::round(p.x()), std::round(p.y()));
            if (!path.contains(p)) { return; }
            // QPainterPath can include some boundary points. Keep the camera
            // strictly inside, including after rounding to Build coordinates.
            for (std::size_t j = 0; j < sector.vertices.size(); ++j) {
                const auto a = snapshot.vertices()[sector.vertices[j]].position;
                const auto b = snapshot.vertices()[sector.vertices[sector.nextWallIndex(j)]].position;
                const auto edge = b-a;
                const double length2 = QPointF::dotProduct(edge, edge);
                if (length2 == 0) { continue; }
                const auto nearest = a + std::clamp(QPointF::dotProduct(p-a, edge)/length2, 0.0, 1.0)*edge;
                const auto delta = p-nearest;
                if (QPointF::dotProduct(delta,delta) < 1.0) { return; }
            }
            const auto delta = p - pointer;
            double distance = QPointF::dotProduct(delta, delta);
            if (distance < best) { best = distance; position = p; selected = &sector; }
        };
        consider(pointer);
        if (best == 0) { break; }
        for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
            const auto a = snapshot.vertices()[sector.vertices[i]].position;
            const auto b = snapshot.vertices()[sector.vertices[sector.nextWallIndex(i)]].position;
            const auto edge = b - a;
            const auto length2 = QPointF::dotProduct(edge, edge);
            if (length2 == 0) { continue; }
            auto p = a + std::clamp(QPointF::dotProduct(pointer-a, edge)/length2, 0.0, 1.0)*edge;
            // Test both sides so winding, hole loops and concave corners work.
            const QPointF normal(-edge.y()/std::sqrt(length2), edge.x()/std::sqrt(length2));
            for (double offset : {2.0, 8.0, 32.0}) {
                consider(p + normal*offset); consider(p - normal*offset);
                const double inset = std::min(0.5, offset/std::sqrt(length2));
                const double t = std::clamp(QPointF::dotProduct(pointer-a, edge)/length2,
                                            inset, 1.0-inset);
                const auto cornerInset = a + t*edge;
                consider(cornerInset + normal*offset); consider(cornerInset - normal*offset);
            }
        }
    }
    if (!selected) { error = "Create a closed sector with room for a camera before entering 3D mode."; return false; }
    const auto surface = [&](bool floor) {
        double z = floor ? selected->floorz : selected->ceilingz;
        if ((floor ? selected->floorstat : selected->ceilingstat) & 2) {
            const auto a = snapshot.vertices()[selected->vertices[0]].position;
            const auto b = snapshot.vertices()[selected->vertices[selected->nextWallIndex(0)]].position;
            const auto d = b-a;
            double length = std::hypot(d.x(), d.y());
            if (length > 0) {
                z += (floor ? selected->floorheinum : selected->ceilingheinum)
                    * (d.x()*(position.y()-a.y()) - d.y()*(position.x()-a.x())) / (length*256.0);
            }
        }
        return z;
    };
    double floor = surface(true), ceiling = surface(false);
    if (floor <= ceiling) { error = "The nearest sector has no vertical space for a camera."; return false; }
    snapshot.setPlayerStartPosition(position);
    snapshot.setPlayerStartZ(floor - std::min(6144.0, (floor-ceiling)*0.5));
    return true;
}
