#pragma once

#include <QDialog>
#include <QImage>

#include <optional>
#include <functional>
#include <set>
#include <utility>

class QDialogButtonBox;
class TextureBrowserWidget;

class TextureBrowserWindow final : public QDialog
{
public:
    struct Selection {
        int tile;
        QImage image;
    };

    explicit TextureBrowserWindow(QWidget *parent = nullptr);

    void browse();

    void setUsedTexturesProvider(std::function<std::set<int>()> provider) {
        m_usedTexturesProvider = std::move(provider);
    }

    [[nodiscard]] std::optional<Selection> chooseTexture(std::optional<int> currentTexture = std::nullopt);
    [[nodiscard]] QImage textureImage(int tile, int palette = 0);
    [[nodiscard]] const std::set<int> &paletteNumbers() const;

private:
    std::function<std::set<int>()> m_usedTexturesProvider;
    TextureBrowserWidget *m_browser = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};
