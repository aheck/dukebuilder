#include "texturebrowserwindow.h"

#include "texturebrowserwidget.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

TextureBrowserWindow::TextureBrowserWindow(QWidget *parent)
    : QDialog(parent)
    , m_browser(new TextureBrowserWidget(this))
    , m_buttons(new QDialogButtonBox(this))
{
    setWindowTitle("Texture Browser");
    setWindowModality(Qt::NonModal);
    resize(800, 600);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_browser, 1);
    layout->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_browser->setTextureActivationCallback([this] {
        if (m_browser->selectedTile()) accept();
    });
}

void TextureBrowserWindow::browse()
{
    m_browser->setUsedTiles(m_usedTexturesProvider ? m_usedTexturesProvider() : std::set<int>{});
    m_browser->reload();
    m_buttons->setStandardButtons(QDialogButtonBox::Close);
    show();
    raise();
    activateWindow();
}

std::optional<TextureBrowserWindow::Selection> TextureBrowserWindow::chooseTexture(std::optional<int> currentTexture)
{
    m_browser->setUsedTiles(m_usedTexturesProvider ? m_usedTexturesProvider() : std::set<int>{});
    m_browser->reload();
    m_browser->selectTile(currentTexture.value_or(-1));
    m_buttons->setStandardButtons(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttons->button(QDialogButtonBox::Ok)->setText("Select");
    const int result = exec();
    const std::optional<int> tile = m_browser->selectedTile();
    if (result != QDialog::Accepted || !tile) {
        return std::nullopt;
    }
    return Selection{*tile, m_browser->textureImage(*tile)};
}

QImage TextureBrowserWindow::textureImage(int tile)
{
    QImage image = m_browser->textureImage(tile);
    if (image.isNull()) {
        m_browser->reload();
        image = m_browser->textureImage(tile);
    }
    return image;
}
