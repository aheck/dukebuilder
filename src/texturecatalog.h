#pragma once

#include <QStringList>

enum class TextureCategory {
    WallsAndArchitecture,
    FloorsAndCeilings,
    Enemies,
    WeaponsAndAmmo,
    HealthArmorAndPickups,
    EditorAndSpecialTiles,
    Lights,
    SkiesAndBackgrounds,
    DoorsSwitchesAndControls,
    EffectsAndProjectiles,
    SignsAndScreens,
    PropsAndDecorations,
    Characters,
    HudAndFonts,
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
