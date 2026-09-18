#include "texturecatalog.h"

#include <QMap>
#include <QRegularExpression>

QString textureCategoryName(TextureCategory category)
{
    switch (category) {
        case TextureCategory::WallsAndArchitecture: return "Walls & Architecture";
        case TextureCategory::FloorsAndCeilings: return "Floors & Ceilings";
        case TextureCategory::DoorsSwitchesAndControls: return "Doors, Switches & Controls";
        case TextureCategory::SignsAndScreens: return "Signs & Screens";
        case TextureCategory::PropsAndDecorations: return "Props & Decorations";
        case TextureCategory::SkiesAndBackgrounds: return "Skies & Backgrounds";
        case TextureCategory::WeaponsAndAmmo: return "Weapons & Ammo";
        case TextureCategory::HealthArmorAndPickups: return "Health, Armor & Pickups";
        case TextureCategory::Enemies: return "Enemies";
        case TextureCategory::Characters: return "Characters";
        case TextureCategory::EffectsAndProjectiles: return "Effects & Projectiles";
        case TextureCategory::HudAndFonts: return "HUD & Fonts";
        case TextureCategory::EditorAndSpecialTiles: return "Editor & Special Tiles";
        case TextureCategory::Others: return "Others";
    }

    return {};
}

QStringList textureCategories()
{
    QStringList result;
    for (int value = static_cast<int>(TextureCategory::WallsAndArchitecture);
         value <= static_cast<int>(TextureCategory::Others); ++value) {
        result.append(textureCategoryName(static_cast<TextureCategory>(value)));
    }
    return result;
}

TextureMetadata textureMetadata(int tile)
{
    struct NamedTile { int tile; const char *name; TextureCategory category; };
    static const NamedTile names[] = {
#include "texturecatalog_data.inc"
    };
    static const auto catalog = [] {
        QMap<int, TextureMetadata> result;
        // Animation and rotation sheets have many frames without individual names.
        struct Family { int first; int last; const char *name; TextureCategory category; };
        const Family families[] = {
            {1400, 1517, "Duke player", TextureCategory::Characters}, {1550, 1599, "Shark", TextureCategory::Enemies},
            {1680, 1767, "Assault trooper liztroop", TextureCategory::Enemies},
            {1820, 1859, "Octabrain", TextureCategory::Enemies}, {1880, 1889, "Sentry drone", TextureCategory::Enemies},
            {1920, 1959, "Assault commander", TextureCategory::Enemies}, {1960, 1974, "Recon patrol vehicle", TextureCategory::Enemies},
            {1975, 1999, "Tank", TextureCategory::Enemies}, {2000, 2089, "Pig cop", TextureCategory::Enemies},
            {2120, 2199, "Assault enforcer lizman", TextureCategory::Enemies}, {2370, 2377, "Green slime", TextureCategory::Enemies},
            {2630, 2709, "Battlelord boss", TextureCategory::Enemies}, {2710, 2759, "Overlord boss", TextureCategory::Enemies},
            {2760, 2812, "Cycloid emperor boss", TextureCategory::Enemies},
            {4610, 4739, "Assault beast newbeast", TextureCategory::Enemies},
            {4740, 4859, "Alien queen boss", TextureCategory::Enemies},
            {2822, 2915, "Small font alphabet numbers", TextureCategory::HudAndFonts},
            {2940, 3023, "Large font alphabet numbers punctuation", TextureCategory::HudAndFonts},
            {3072, 3135, "Mini font alphabet numbers", TextureCategory::HudAndFonts},
            {2510, 2520, "Devastator weapon", TextureCategory::WeaponsAndAmmo}, {2524, 2532, "Pistol weapon reload", TextureCategory::WeaponsAndAmmo},
            {2536, 2543, "Chaingun weapon", TextureCategory::WeaponsAndAmmo}, {2544, 2547, "RPG rocket launcher weapon", TextureCategory::WeaponsAndAmmo},
            {2548, 2551, "Freezer weapon", TextureCategory::WeaponsAndAmmo}, {2556, 2562, "Shrinker weapon", TextureCategory::WeaponsAndAmmo},
            {2613, 2629, "Shotgun weapon", TextureCategory::WeaponsAndAmmo},
        };
        for (const auto &family : families) {
            for (int id = family.first; id <= family.last; ++id)
                result[id] = {QString::fromLatin1(family.name) + " frame " + QString::number(id - family.first),
                              {textureCategoryName(family.category)}, QString::fromLatin1(family.name)};
        }
        for (const auto &entry : names) {
            auto &metadata = result[entry.tile];
            metadata.keywords += " " + metadata.name;
            metadata.name = QString::fromLatin1(entry.name).replace('_', ' ');
            metadata.categories = {textureCategoryName(entry.category)};
            if (entry.category == TextureCategory::WallsAndArchitecture)
                metadata.categories.append(textureCategoryName(TextureCategory::FloorsAndCeilings));
            if (entry.category == TextureCategory::WeaponsAndAmmo && entry.tile >= 2510 && entry.tile <= 2629)
                metadata.categories.append(textureCategoryName(TextureCategory::HudAndFonts));
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
                if (!metadata.categories.contains(textureCategoryName(TextureCategory::WallsAndArchitecture)))
                    metadata.categories.append(textureCategoryName(TextureCategory::WallsAndArchitecture));
                metadata.keywords += " wall architecture";
            }
            if (surface.floorOrCeiling) {
                if (!metadata.categories.contains(textureCategoryName(TextureCategory::FloorsAndCeilings)))
                    metadata.categories.append(textureCategoryName(TextureCategory::FloorsAndCeilings));
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
