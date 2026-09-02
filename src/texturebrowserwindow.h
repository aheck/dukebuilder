#pragma once

#include <QDialog>
#include <QImage>

#include <optional>

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
    [[nodiscard]] std::optional<Selection> chooseTexture(
        std::optional<int> currentTexture = std::nullopt);

private:
    TextureBrowserWidget *m_browser = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};
