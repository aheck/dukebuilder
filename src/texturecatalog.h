#pragma once

#include <QStringList>

struct TextureMetadata {
    QString name;
    QStringList categories;
    QString keywords;
};

QStringList textureCategories();
TextureMetadata textureMetadata(int tile);
bool textureMatchesSearch(int tile, const TextureMetadata &metadata, const QString &query);
