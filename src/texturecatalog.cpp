#include "texturecatalog.h"

#include <QMap>
#include <QRegularExpression>

QStringList textureCategories()
{
    return {"Walls & Architecture", "Floors & Ceilings", "Doors, Switches & Controls",
            "Signs & Screens", "Props & Decorations", "Skies & Backgrounds", "Weapons & Ammo",
            "Health, Armor & Pickups", "Enemies", "Characters", "Effects & Projectiles",
            "HUD & Fonts", "Editor & Special Tiles", "Others"};
}

TextureMetadata textureMetadata(int tile)
{
    struct NamedTile { int tile; const char *name; int category; };
    static const NamedTile names[] = {
#include "texturecatalog_data.inc"
    };
    static const auto catalog = [] {
        QMap<int, TextureMetadata> result;
        const auto categories = textureCategories();
        // Animation and rotation sheets have many frames without individual names.
        struct Family { int first; int last; const char *name; int category; };
        const Family families[] = {
            {1400, 1517, "Duke player", 9}, {1550, 1599, "Shark", 8},
            {1680, 1767, "Assault trooper liztroop", 8},
            {1820, 1859, "Octabrain", 8}, {1880, 1889, "Sentry drone", 8},
            {1920, 1959, "Assault commander", 8}, {1960, 1974, "Recon patrol vehicle", 8},
            {1975, 1999, "Tank", 8}, {2000, 2089, "Pig cop", 8},
            {2120, 2199, "Assault enforcer lizman", 8}, {2370, 2377, "Green slime", 8},
            {2630, 2709, "Battlelord boss", 8}, {2710, 2759, "Overlord boss", 8},
            {2760, 2812, "Cycloid emperor boss", 8},
            {4610, 4739, "Assault beast newbeast", 8},
            {4740, 4859, "Alien queen boss", 8},
            {2822, 2915, "Small font alphabet numbers", 11},
            {2940, 3023, "Large font alphabet numbers punctuation", 11},
            {3072, 3135, "Mini font alphabet numbers", 11},
            {2510, 2520, "Devastator weapon", 6}, {2524, 2532, "Pistol weapon reload", 6},
            {2536, 2543, "Chaingun weapon", 6}, {2544, 2547, "RPG rocket launcher weapon", 6},
            {2548, 2551, "Freezer weapon", 6}, {2556, 2562, "Shrinker weapon", 6},
            {2613, 2629, "Shotgun weapon", 6},
        };
        for (const auto &family : families) {
            for (int id = family.first; id <= family.last; ++id)
                result[id] = {QString::fromLatin1(family.name) + " frame " + QString::number(id - family.first),
                              {categories[family.category]}, QString::fromLatin1(family.name)};
        }
        for (const auto &entry : names) {
            auto &metadata = result[entry.tile];
            metadata.keywords += " " + metadata.name;
            metadata.name = QString::fromLatin1(entry.name).replace('_', ' ');
            metadata.categories = {categories[entry.category]};
            if (entry.category == 0) metadata.categories.append(categories[1]);
            if (entry.category == 6 && entry.tile >= 2510 && entry.tile <= 2629)
                metadata.categories.append(categories[11]);
            if (metadata.name.contains("FIRSTGUN")) metadata.keywords += " pistol";
            if (metadata.name.contains("DEVISTATOR")) metadata.keywords += " devastator";
            if (metadata.name.contains("PANNEL")) metadata.keywords += " panel metal";
            if (metadata.name.contains("LIZTROOP")) metadata.keywords += " assault trooper";
            if (metadata.name.contains("LIZMAN")) metadata.keywords += " assault enforcer";
            if (metadata.name.contains("PIGCOP")) metadata.keywords += " pig cop";
            if (metadata.name.contains("RPG")) metadata.keywords += " rocket launcher";
        }
        // NAMES.H only names tiles referenced by game code. Most building
        // materials are unnamed, so also classify actual shipped-map usage.
        struct SurfaceUse { int tile; bool wall; bool floorOrCeiling; };
        static const SurfaceUse surfaces[] = {
#include "texturecatalog_surfaces.inc"
        };
        for (const auto &surface : surfaces) {
            auto &metadata = result[surface.tile];
            if (metadata.name.isEmpty()) metadata.name = QString("Surface tile %1").arg(surface.tile);
            if (surface.wall) {
                if (!metadata.categories.contains(categories[0])) metadata.categories.append(categories[0]);
                metadata.keywords += " wall architecture";
            }
            if (surface.floorOrCeiling) {
                if (!metadata.categories.contains(categories[1])) metadata.categories.append(categories[1]);
                metadata.keywords += " floor ceiling";
            }
        }
        return result;
    }();
    return catalog.value(tile, {QString("Tile %1").arg(tile), {"Others"}, {}});
}

bool textureMatchesSearch(int tile, const TextureMetadata &metadata, const QString &query)
{
    const auto haystack = QString::number(tile) + " " + metadata.name + " " + metadata.keywords;
    const auto words = query.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for (const auto &word : words) {
        if (!haystack.contains(word, Qt::CaseInsensitive)) return false;
    }
    return true;
}
