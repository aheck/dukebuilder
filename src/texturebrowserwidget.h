#pragma once

#include <QImage>
#include <QMap>
#include <QWidget>
#include <QByteArray>

#include <optional>
#include <functional>
#include <utility>
#include <set>

class QLabel;
class QLineEdit;
class QListWidget;

class TextureBrowserWidget final : public QWidget
{
public:
    explicit TextureBrowserWidget(QWidget *parent = nullptr);
    ~TextureBrowserWidget() override;

    void reload();
    [[nodiscard]] std::optional<int> selectedTile() const;
    [[nodiscard]] QImage textureImage(int tile, int palette = 0) const;
    [[nodiscard]] const std::set<int> &paletteNumbers() const { return m_paletteNumbers; }
    void selectTile(int tile);
    void setUsedTiles(const std::set<int> &tiles) { m_usedTiles = tiles; }
    void setTextureActivationCallback(std::function<void()> callback)
    {
        m_textureActivationCallback = std::move(callback);
    }

private:
    void updateFilter(const QString &text);

    QLineEdit *m_filter = nullptr;
    QListWidget *m_categories = nullptr;
    QListWidget *m_textureList = nullptr;
    QLabel *m_statusLabel = nullptr;
    QMap<int, QImage> m_images;
    struct RawTexture { QByteArray pixels; int width = 0; int height = 0; };
    QMap<int, RawTexture> m_rawTextures;
    struct DukePaletteFile *m_palette = nullptr;
    struct DukePaletteLookupFile *m_lookup = nullptr;
    std::function<void()> m_textureActivationCallback;
    std::set<int> m_usedTiles;
    std::set<int> m_paletteNumbers{0};
    QString m_loadStatus;
};
