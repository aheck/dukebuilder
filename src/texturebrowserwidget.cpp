#include "texturebrowserwidget.h"

#include <libduke/art.h>
#include <libduke/grp.h>
#include <libduke/palette.h>

#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <cstdint>
#include <memory>

namespace {
constexpr auto grpFilesSettingsKey = "gameData/grpFiles";
constexpr int thumbnailSize = 96;

struct GrpDeleter {
    void operator()(DukeGrpFile *file) const { duke_grp_free(file); }
};

struct ArtDeleter {
    void operator()(DukeArtFile *file) const { duke_art_free(file); }
};

struct PaletteDeleter {
    void operator()(DukePaletteFile *file) const { duke_palette_free(file); }
};

using GrpPointer = std::unique_ptr<DukeGrpFile, GrpDeleter>;
using ArtPointer = std::unique_ptr<DukeArtFile, ArtDeleter>;
using PalettePointer = std::unique_ptr<DukePaletteFile, PaletteDeleter>;

struct Texture {
    QImage image;
    int width = 0;
    int height = 0;
};

uint8_t expandVgaChannel(uint8_t channel)
{
    return static_cast<uint8_t>((channel << 2) | (channel >> 4));
}

QImage tileImage(const DukeArtTile &tile, const uint8_t *pixels,
                 const DukePaletteFile &palette)
{
    QImage image(tile.width, tile.height, QImage::Format_ARGB32);
    for (int y = 0; y < tile.height; ++y) {
        auto *scanLine = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < tile.width; ++x) {
            const uint8_t paletteIndex = pixels[x * tile.height + y];
            const DukePaletteColor &color = palette.colors[paletteIndex];
            const int alpha = paletteIndex == 255 ? 0 : 255;
            scanLine[x] = qRgba(expandVgaChannel(color.red),
                                expandVgaChannel(color.green),
                                expandVgaChannel(color.blue), alpha);
        }
    }
    return image;
}

bool isEntryNamed(const DukeGrpFileEntry &entry, const QString &name)
{
    return QString::fromLatin1(entry.filename).compare(name, Qt::CaseInsensitive) == 0;
}
}

TextureBrowserWidget::TextureBrowserWidget(QWidget *parent)
    : QWidget(parent)
{
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText("Filter by tile number...");
    auto *reloadButton = new QPushButton("Reload", this);

    auto *controls = new QHBoxLayout();
    controls->addWidget(m_filter, 1);
    controls->addWidget(reloadButton);

    m_textureList = new QListWidget(this);
    m_textureList->setViewMode(QListView::IconMode);
    m_textureList->setResizeMode(QListView::Adjust);
    m_textureList->setMovement(QListView::Static);
    m_textureList->setIconSize(QSize(thumbnailSize, thumbnailSize));
    m_textureList->setGridSize(QSize(128, 132));
    m_textureList->setSpacing(4);
    m_textureList->setUniformItemSizes(true);

    m_statusLabel = new QLabel(this);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(controls);
    layout->addWidget(m_textureList, 1);
    layout->addWidget(m_statusLabel);

    connect(m_filter, &QLineEdit::textChanged,
            this, &TextureBrowserWidget::updateFilter);
    connect(reloadButton, &QPushButton::clicked,
            this, &TextureBrowserWidget::reload);

    reload();
}

void TextureBrowserWidget::reload()
{
    m_textureList->clear();
    m_images.clear();
    const QStringList grpPaths = QSettings().value(grpFilesSettingsKey).toStringList();
    if (grpPaths.isEmpty()) {
        m_statusLabel->setText("No GRP files configured in Settings → Game Data.");
        return;
    }

    std::vector<GrpPointer> archives;
    PalettePointer palette(duke_palette_new());
    bool paletteLoaded = false;
    int failedArchives = 0;
    int failedArtFiles = 0;

    for (const QString &path : grpPaths) {
        GrpPointer archive(duke_grp_new());
        const QByteArray encodedPath = QFileInfo(path).absoluteFilePath().toUtf8();
        if (!archive || !duke_grp_open_filename(archive.get(), encodedPath.constData())
                || !duke_grp_read_entries_sparse(archive.get())) {
            ++failedArchives;
            continue;
        }

        for (uint32_t index = 0; index < archive->header.entry_count; ++index) {
            DukeGrpFileEntry *entry = duke_grp_get_entry_by_index(archive.get(), index);
            if (!entry || !isEntryNamed(*entry, "PALETTE.DAT")) {
                continue;
            }
            void *data = nullptr;
            const size_t size = duke_grp_get_file_data_by_index(
                archive.get(), index, &data);
            if (size != static_cast<size_t>(-1) && palette
                    && duke_palette_read_from_memory(palette.get(), data, size)) {
                paletteLoaded = true;
            }
        }
        archives.push_back(std::move(archive));
    }

    if (!paletteLoaded || !palette || !duke_palette_validate(palette.get())) {
        m_statusLabel->setText("No valid PALETTE.DAT found in the configured GRP files.");
        return;
    }

    QMap<int, Texture> textures;
    for (const GrpPointer &archive : archives) {
        for (uint32_t entryIndex = 0; entryIndex < archive->header.entry_count;
             ++entryIndex) {
            DukeGrpFileEntry *entry = duke_grp_get_entry_by_index(
                archive.get(), entryIndex);
            if (!entry
                    || !QString::fromLatin1(entry->filename).endsWith(
                        ".ART", Qt::CaseInsensitive)) {
                continue;
            }

            void *data = nullptr;
            const size_t size = duke_grp_get_file_data_by_index(
                archive.get(), entryIndex, &data);
            ArtPointer art(duke_art_new());
            if (size == static_cast<size_t>(-1) || !art
                    || !duke_art_open_memory(art.get(), data, size)
                    || !duke_art_read_tiles_sparse(art.get())) {
                ++failedArtFiles;
                continue;
            }

            const int64_t tileCount = static_cast<int64_t>(art->header.localtileend)
                - art->header.localtilestart + 1;
            for (int64_t tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
                DukeArtTile *tile = duke_art_get_tile_by_index(
                    art.get(), static_cast<uint32_t>(tileIndex));
                if (!tile || tile->width <= 0 || tile->height <= 0) {
                    continue;
                }
                void *pixels = nullptr;
                if (duke_art_get_tile_data_by_index(
                        art.get(), static_cast<uint32_t>(tileIndex), &pixels)
                        == static_cast<size_t>(-1)
                    || !pixels) {
                    continue;
                }
                textures[tile->tile_number] = {
                    tileImage(*tile, static_cast<const uint8_t *>(pixels), *palette),
                    tile->width,
                    tile->height,
                };
            }
        }
    }

    for (auto iterator = textures.cbegin(); iterator != textures.cend(); ++iterator) {
        const Texture &texture = iterator.value();
        const QPixmap thumbnail = QPixmap::fromImage(texture.image).scaled(
            thumbnailSize, thumbnailSize, Qt::KeepAspectRatio,
            Qt::FastTransformation);
        auto *item = new QListWidgetItem(QIcon(thumbnail), QString::number(iterator.key()));
        item->setData(Qt::UserRole, iterator.key());
        item->setToolTip(QString("Tile %1\n%2 × %3")
                             .arg(iterator.key())
                             .arg(texture.width)
                             .arg(texture.height));
        m_textureList->addItem(item);
        m_images.insert(iterator.key(), texture.image);
    }

    updateFilter(m_filter->text());
    QString status = QString("%1 textures from %2 GRP file(s)")
                         .arg(textures.size())
                         .arg(archives.size());
    if (failedArchives > 0 || failedArtFiles > 0) {
        status += QString(" — %1 GRP and %2 ART file(s) failed")
                      .arg(failedArchives)
                      .arg(failedArtFiles);
    }
    m_statusLabel->setText(status);
}

std::optional<int> TextureBrowserWidget::selectedTile() const
{
    const QListWidgetItem *item = m_textureList->currentItem();
    if (!item) {
        return std::nullopt;
    }
    return item->data(Qt::UserRole).toInt();
}

QImage TextureBrowserWidget::textureImage(int tile) const
{
    return m_images.value(tile);
}

void TextureBrowserWidget::selectTile(int tile)
{
    for (int index = 0; index < m_textureList->count(); ++index) {
        QListWidgetItem *item = m_textureList->item(index);
        if (item->data(Qt::UserRole).toInt() == tile) {
            m_textureList->setCurrentItem(item);
            m_textureList->scrollToItem(item);
            return;
        }
    }
    m_textureList->setCurrentItem(nullptr);
}

void TextureBrowserWidget::updateFilter(const QString &text)
{
    const QString filter = text.trimmed();
    for (int index = 0; index < m_textureList->count(); ++index) {
        QListWidgetItem *item = m_textureList->item(index);
        item->setHidden(!filter.isEmpty() && !item->text().contains(filter));
    }
}
