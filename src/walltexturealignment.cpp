#include "walltexturealignment.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {
struct Side {
    WallTextureTarget target;
    MapDocument::WallSide material;
    MapDocument::VertexId start, end;
    double origin = 0;
};

std::optional<Side> describe(const MapDocument &document, WallTextureTarget target, QString &reason)
{
    if (target.wall >= document.walls().size()) {
        reason = "invalid wall sides";
        return {};
    }
    const auto &wall = document.walls()[target.wall];
    const auto owner = target.reversed ? wall.reverseSector : wall.forwardSector;
    const auto neighbor = target.reversed ? wall.forwardSector : wall.reverseSector;
    if (!owner || *owner >= document.sectors().size()) {
        reason = "unowned wall sides";
        return {};
    }
    Side result{target, target.reversed ? wall.reverseSide : wall.forwardSide,
        target.reversed ? wall.end : wall.start, target.reversed ? wall.start : wall.end};
    const auto &sector = document.sectors()[*owner];
    const bool bottom = result.material.cstat & 4;
    result.origin = bottom ? sector.floorz : sector.ceilingz;
    if (wall.isTwoSided()) {
        // Picking identifies a side, not its upper/lower/overlay band. Do not
        // guess when editing that side could change another visible material.
        if (!neighbor || *neighbor >= document.sectors().size() || (result.material.cstat & (2 | 16 | 32))) {
            reason = "swapped, masked or one-way portal walls";
            return {};
        }
        const auto &other = document.sectors()[*neighbor];
        if ((sector.floorstat | sector.ceilingstat | other.floorstat | other.ceilingstat) & 2) {
            reason = "sloped portal walls";
            return {};
        }
        const bool upper = std::min(sector.floorz, other.ceilingz) > sector.ceilingz;
        const bool lower = sector.floorz > std::max(sector.ceilingz, other.floorz);
        if ((!upper && !lower) || (upper && (sector.ceilingstat & other.ceilingstat & 1))
            || (lower && (sector.floorstat & other.floorstat & 1))) {
            reason = "invisible or sky portal walls";
            return {};
        }
        if (upper && lower && !bottom && other.ceilingz != other.floorz) {
            reason = "portal sides with different upper/lower texture origins";
            return {};
        }
        result.origin = bottom ? sector.ceilingz : upper ? other.ceilingz : other.floorz;
    }
    return result;
}

double horizontal(const Side &side, MapDocument::VertexId vertex)
{
    const bool end = vertex == side.end;
    return side.material.xpanning + (end != bool(side.material.cstat & 8) ? side.material.xrepeat * 8.0 : 0.0);
}

double vertical(const Side &side, int height)
{
    const double value = -side.origin * side.material.yrepeat / (2048.0 * height)
        + side.material.ypanning / 256.0;
    return side.material.cstat & 256 ? -value : value;
}

double distance(double value, double period)
{
    return std::abs(std::remainder(value, period));
}

int nearestPan(double desired, double period, int old, bool &approximate)
{
    // The byte-sized horizontal pan cannot represent every phase of a tile
    // wider than 256 pixels. Search legal values, preserving the old pan on ties.
    int best = old;
    double error = distance(best - desired, period);
    for (int pan = 0; pan < 256; ++pan) {
        const double candidate = distance(pan - desired, period);
        if (candidate < error - 1e-8) {
            best = pan;
            error = candidate;
        }
    }
    approximate |= error > 1e-7;
    return best;
}
}

WallTextureAlignmentResult alignWallTextures(MapDocument &document,
    const std::vector<WallTextureTarget> &selection, WallTextureTarget reference, QSize textureSize)
{
    WallTextureAlignmentResult result;
    if (std::find(selection.begin(), selection.end(), reference) == selection.end()) {
        result.error = "Point at a selected wall to use it as the alignment reference.";
        return result;
    }
    if (textureSize.width() <= 0 || textureSize.height() <= 0) {
        result.error = "Cannot align: the reference texture is unavailable.";
        return result;
    }
    QString reason;
    const auto anchor = describe(document, reference, reason);
    if (!anchor) {
        result.error = "Cannot use this reference: " + reason + ".";
        return result;
    }
    std::vector<Side> sides{*anchor};
    auto sorted = selection;
    std::sort(sorted.begin(), sorted.end(), [](auto a, auto b) {
        return std::make_pair(a.wall, a.reversed) < std::make_pair(b.wall, b.reversed);
    });
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    const auto skip = [&](const QString &why) {
        ++result.skipped;
        if (!result.reasons.contains(why)) {
            result.reasons.append(why);
        }
    };
    for (const auto target : sorted) {
        if (target == reference) {
            continue;
        }
        const auto side = describe(document, target, reason);
        if (!side) {
            skip(reason);
        } else if (side->material.texture != anchor->material.texture) {
            skip("different textures");
        } else if (side->material.yrepeat != anchor->material.yrepeat
                   || ((side->material.cstat ^ anchor->material.cstat) & 256)) {
            skip("incompatible vertical scale or flip");
        } else {
            sides.push_back(*side);
        }
    }
    // Use topological vertices, never coincident XY positions: overlapping
    // rooms must not be joined just because their outlines coincide.
    std::map<MapDocument::VertexId, std::vector<std::size_t>> connected;
    for (std::size_t i = 0; i < sides.size(); ++i) {
        connected[sides[i].start].push_back(i);
        connected[sides[i].end].push_back(i);
    }
    std::vector<bool> visited(sides.size(), false);
    std::vector<std::optional<std::pair<std::size_t, MapDocument::VertexId>>> parents(sides.size());
    visited[0] = true;
    std::vector<std::size_t> queue{0};
    for (std::size_t next = 0; next < queue.size(); ++next) {
        const auto index = queue[next];
        for (const auto vertex : {sides[index].end, sides[index].start}) {
            for (const auto neighbor : connected[vertex]) {
                if (neighbor == index || sides[index].target.wall == sides[neighbor].target.wall) {
                    continue;
                }
                auto &target = sides[neighbor];
                if (visited[neighbor]) {
                    const bool treeEdge = (parents[index] && *parents[index] == std::make_pair(neighbor, vertex))
                        || (parents[neighbor] && *parents[neighbor] == std::make_pair(index, vertex));
                    if (!treeEdge && distance(horizontal(sides[index], vertex) - horizontal(target, vertex), textureSize.width()) > 1e-7) {
                        result.closingSeam = true;
                    }
                    continue;
                }
                visited[neighbor] = true;
                parents[neighbor] = std::make_pair(index, vertex);
                queue.push_back(neighbor);
                const auto old = target.material;
                const double span = horizontal(target, vertex) - target.material.xpanning;
                target.material.xpanning = nearestPan(horizontal(sides[index], vertex) - span,
                    textureSize.width(), old.xpanning, result.approximate);
                const double sign = target.material.cstat & 256 ? -1.0 : 1.0;
                const double desired = (sign * vertical(*anchor, textureSize.height())
                    + target.origin * target.material.yrepeat / (2048.0 * textureSize.height())) * 256.0;
                target.material.ypanning = nearestPan(desired, 256.0, old.ypanning, result.approximate);
                ++result.aligned;
                if (!(target.material == old)) {
                    document.setWallSide(target.target.wall, target.target.reversed, target.material);
                    ++result.changed;
                }
            }
        }
    }
    for (const bool reached : visited) {
        if (!reached) {
            skip("disconnected walls");
        }
    }
    return result;
}
