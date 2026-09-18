#pragma once

#include <QStringList>

enum class TextureCategory {
    WallsAndArchitecture,
    FloorsAndCeilings,
    DoorsSwitchesAndControls,
    SignsAndScreens,
    PropsAndDecorations,
    SkiesAndBackgrounds,
    WeaponsAndAmmo,
    HealthArmorAndPickups,
    Enemies,
    Characters,
    EffectsAndProjectiles,
    HudAndFonts,
    EditorAndSpecialTiles,
    Others,
};

struct TextureMetadata {
    QString name;
    QStringList categories;
    QString keywords;
};

QString textureCategoryName(TextureCategory category);
QStringList textureCategories();
TextureMetadata textureMetadata(int tile);
bool textureMatchesSearch(int tile, const TextureMetadata &metadata, const QString &query);
