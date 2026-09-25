#include "walltexturealignment.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

static void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

static WallTextureTarget target(const MapDocument &map, std::size_t sector, std::size_t local)
{
    const auto &s = map.sectors()[sector];
    return {s.walls[local], map.walls()[s.walls[local]].start != s.vertices[local]};
}

static MapDocument::WallSide side(const MapDocument &map, WallTextureTarget t)
{
    return t.reversed ? map.walls()[t.wall].reverseSide : map.walls()[t.wall].forwardSide;
}

static MapDocument room()
{
    MapDocument map;
    require(map.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}}, true), "Create room");
    for (int i = 0; i < 4; ++i) {
        const auto t = target(map, 0, i);
        auto s = side(map, t);
        s.texture = 1;
        s.xrepeat = 8;
        s.yrepeat = 8;
        s.xpanning = 13;
        s.ypanning = 17;
        map.setWallSide(t.wall, t.reversed, s);
    }
    return map;
}

static double u(const MapDocument &map, WallTextureTarget t, bool end)
{
    const auto s = side(map, t);
    return s.xpanning + (end != bool(s.cstat & 8) ? s.xrepeat * 8 : 0);
}

static bool congruent(double a, double b, double period)
{
    return std::abs(std::remainder(a - b, period)) < 1e-7;
}

int main()
{
    {
        auto map = room();
        const auto a = target(map, 0, 0), b = target(map, 0, 1), c = target(map, 0, 2);
        const auto before = map;
        auto result = alignWallTextures(map, {c,b,a,b}, a, {128,128});
        require(result.error.isEmpty() && result.aligned == 2 && result.changed == 1 && !result.skipped, "Align connected chain, deduplicating targets");
        require(side(map,a) == side(before,a), "Reference unchanged");
        require(congruent(u(map,a,true), u(map,b,false),128) && congruent(u(map,b,true),u(map,c,false),128), "Horizontal seams match");
        require(side(map,target(map,0,3)) == side(before,target(map,0,3)), "Unselected wall unchanged");
        result = alignWallTextures(map,{c,b,a},a,{128,128});
        require(result.changed == 0, "Alignment is idempotent");
    }
    {
        auto map = room();
        const auto a = target(map,0,0), b = target(map,0,1);
        auto s = side(map,b);
        s.cstat = 8 | 4; // X flip and floor-relative origin.
        s.xrepeat = 11;
        s.shade = -12;
        map.setWallSide(b.wall,b.reversed,s);
        const auto result = alignWallTextures(map,{a,b},a,{128,128});
        const auto aligned = side(map,b);
        require(result.changed == 1 && !result.approximate, "Align flipped, bottom-anchored wall");
        require(congruent(u(map,a,true),u(map,b,false),128), "Horizontal flip accounted for");
        require(aligned.ypanning == 81, "Different ceiling/floor origins compensated vertically");
        require(aligned.cstat == s.cstat && aligned.xrepeat == s.xrepeat && aligned.yrepeat == s.yrepeat && aligned.shade == s.shade,
            "Preserve scale, flags and shade");
        s = side(map,a);
        s.cstat |= 256;
        map.setWallSide(a.wall,a.reversed,s);
        auto before = map;
        require(alignWallTextures(map,{a,b},a,{128,128}).skipped == 1 && map == before, "Opposite vertical flips skipped");
        s = side(map,b);
        s.cstat |= 256;
        map.setWallSide(b.wall,b.reversed,s);
        const auto flippedResult = alignWallTextures(map,{a,b},a,{128,128});
        require(flippedResult.aligned == 1 && side(map,b).ypanning == 81, "Matching vertical flips align");
    }
    {
        auto map = room();
        const auto a = target(map,0,0), b = target(map,0,1), c = target(map,0,2);
        auto s = side(map,b);
        s.texture = 2;
        map.setWallSide(b.wall,b.reversed,s);
        const auto before = map;
        auto result = alignWallTextures(map,{a,b,c},a,{128,128});
        require(result.skipped == 2 && result.aligned == 0 && map == before, "Texture mismatch stops traversal; disconnected target unchanged");
        result = alignWallTextures(map,{b},a,{128,128});
        require(!result.error.isEmpty() && map == before, "Reference must be selected");
        require(!alignWallTextures(map,{a,b},a,{}).error.isEmpty(), "Missing texture is an error");
        s.texture = 1;
        s.yrepeat = 9;
        map.setWallSide(b.wall,b.reversed,s);
        require(alignWallTextures(map,{a,b},a,{128,128}).skipped == 1, "Different vertical scales skipped");
    }
    {
        auto map = room();
        std::vector<WallTextureTarget> all;
        for (int i = 0; i < 4; ++i) { all.push_back(target(map,0,i)); }
        require(!alignWallTextures(map,all,all[0],{128,128}).closingSeam, "Integral wrap has no seam");
        auto s = side(map,all[0]);
        ++s.xrepeat;
        map.setWallSide(all[0].wall,all[0].reversed,s);
        require(alignWallTextures(map,all,all[0],{128,128}).closingSeam, "Nonintegral wrap reports seam without stretching");
    }
    {
        auto map = room();
        const auto a = target(map,0,0), b = target(map,0,1);
        auto s = side(map,a);
        s.xrepeat = 64;
        s.xpanning = 200;
        map.setWallSide(a.wall,a.reversed,s);
        auto result = alignWallTextures(map,{a,b},a,{1024,128});
        require(result.approximate && !result.closingSeam && side(map,b).xpanning >= 0 && side(map,b).xpanning <= 255,
            "Wide texture uses nearest byte pan, without a spurious cycle seam");
        map = room();
        s = side(map,b);
        s.cstat = 4;
        map.setWallSide(b.wall,b.reversed,s);
        require(alignWallTextures(map,{a,b},a,{100,100}).approximate, "Non-power-of-two height rounds vertical pan");
    }
    {
        auto map = room();
        require(map.addPolyline({{1024,0},{2048,0},{2048,1024},{1024,1024}},true), "Attach room");
        map.setSectorCeilingZ(1,-4096);
        const auto a = target(map,0,0), portal = target(map,0,1);
        require(map.walls()[portal.wall].isTwoSided(), "Shared portal fixture");
        auto result = alignWallTextures(map,{a,portal},a,{128,128});
        require(result.aligned == 1 && side(map,portal).ypanning == 49, "Upper portal uses neighboring ceiling origin");
        map.setSectorCeilingZ(1,-8192);
        map.setSectorFloorZ(1,-4096);
        result = alignWallTextures(map,{a,portal},a,{128,128});
        require(result.aligned == 1 && side(map,portal).ypanning == 49, "Lower portal uses neighboring floor origin");
        map.setSectorCeilingZ(1,-6144);
        const auto before = map;
        require(alignWallTextures(map,{a,portal},a,{128,128}).skipped == 1 && map == before, "Ambiguous two-band portal is unchanged");
        auto s = side(map,portal);
        s.cstat |= 4;
        map.setWallSide(portal.wall,portal.reversed,s);
        require(alignWallTextures(map,{a,portal},a,{128,128}).aligned == 1, "Two-band portal with shared origin aligns");
        s.cstat |= 2;
        map.setWallSide(portal.wall,portal.reversed,s);
        require(alignWallTextures(map,{a,portal},a,{128,128}).skipped == 1, "Bottom-swap portal skipped");
        require(!alignWallTextures(map,{a,portal},portal,{128,128}).error.isEmpty(), "Unsupported reference gives diagnostic");
        // The reverse side of the shared edge is owned by the other sector.
        const WallTextureTarget reverse{portal.wall,!portal.reversed};
        map.setSectorCeilingZ(0,-5120);
        s = side(map,reverse);
        s.texture = 1;
        s.yrepeat = 8;
        map.setWallSide(reverse.wall,reverse.reversed,s);
        const auto next = target(map,1,target(map,1,0).wall == portal.wall ? 1 : 0);
        s = side(map,next);
        s.texture = 1;
        s.yrepeat = 8;
        map.setWallSide(next.wall,next.reversed,s);
        const auto reverseBefore = side(map,reverse);
        const auto reverseResult = alignWallTextures(map,{reverse,next},reverse,{128,128});
        if (reverseResult.aligned != 1) {
            std::cerr << reverseResult.error.toStdString() << " / " << reverseResult.reasons.join(", ").toStdString() << '\n';
        }
        require(reverseResult.aligned == 1,
            "Reverse-side reference traverses its connected wall");
        require(side(map,reverse) == reverseBefore, "Reverse reference preserved");
    }
    std::cout << "Wall texture alignment tests passed\n";
}
