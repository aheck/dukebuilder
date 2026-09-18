#include "texturecatalog.h"
#include "mapdocument.h"
#include <cstdlib>
#include <iostream>

void require(bool condition, const char *message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(EXIT_FAILURE); }
}

int main()
{
    require(textureMetadata(21).categories.contains("Weapons & Ammo"), "Pistol pickup category");
    require(textureMatchesSearch(21, textureMetadata(21), "PISTOL"), "Friendly case-insensitive alias");
    require(textureMatchesSearch(2000, textureMetadata(2000), "pig cop"), "Multiword name search");
    require(textureMatchesSearch(2000, textureMetadata(2000), "2000 pig"), "Combined number/name search");
    require(!textureMatchesSearch(2000, textureMetadata(2000), "pig door"), "Every search word must match");
    require(textureMetadata(2005).categories.contains("Enemies"), "Enemy rotation frames stay together");
    require(textureMetadata(6000).categories == QStringList{"Others"}, "Unknown tiles remain in Others");
    require(textureMatchesSearch(6000, textureMetadata(6000), "6000"), "Unknown tile number search");
    require(textureMatchesSearch(6000, textureMetadata(6000), "  "), "Empty search shows all");

    MapDocument map;
    require(map.usedTextureTiles().empty(), "Empty map has no textures");
    require(map.addPolyline({{0,0}, {100,0}, {100,100}, {0,100}}, true), "Create room");
    map.setSectorFloorTexture(0, 80);
    map.setSectorCeilingTexture(0, 81);
    auto side = map.walls()[0].forwardSide;
    side.texture = 150; side.overlayTexture = 663; side.cstat = 16;
    map.setWallSide(0, false, side);
    auto unusedSide = side; unusedSide.texture = 999;
    map.setWallSide(0, true, unusedSide);
    const auto sprite = map.addSprite({50,50});
    map.setSpriteTexture(sprite, 2000);
    const auto used = map.usedTextureTiles();
    for (int tile : {80,81,150,663,2000}) require(used.count(tile), "Collect every active surface and sprite");
    require(!used.count(999), "Ignore wall sides without a sector");
    side.cstat = 0;
    map.setWallSide(0, false, side);
    map.removeSprites({sprite});
    require(!map.usedTextureTiles().count(663) && !map.usedTextureTiles().count(2000),
            "Usage reflects edits and inactive overlays");
}
