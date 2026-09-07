#include "texturebrowserwidget.h"
#include "texturecatalog.h"

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

QImage tileImage(const DukeArtTile &tile, const uint8_t *pixels, size_t pixelsSize,
                 const DukePaletteFile &palette)
{
    QImage image(tile.width, tile.height, QImage::Format_RGBA8888);
    if (image.isNull() || !duke_art_tile_to_rgba(&tile, pixels, pixelsSize, &palette,
                                              image.bits(), static_cast<size_t>(image.sizeInBytes()))) {
        return {};
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
    m_filter->setPlaceholderText("Search by tile number or name...");
    m_filter->setClearButtonEnabled(true);
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

    m_categories = new QListWidget(this);
    m_categories->setObjectName("TextureCategories");
    m_categories->setFixedWidth(210);
    m_categories->addItems(QStringList{"All", "Used in this map"} + textureCategories());
    m_categories->setCurrentRow(0);
    m_categories->setToolTip("Categories describe original Duke3D artwork; custom artwork may differ.");
    auto *content = new QHBoxLayout;
    content->addWidget(m_categories);
    content->addWidget(m_textureList, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(controls);
    layout->addLayout(content, 1);
    layout->addWidget(m_statusLabel);

    connect(m_filter, &QLineEdit::textChanged,
            this, &TextureBrowserWidget::updateFilter);
    connect(m_categories, &QListWidget::currentRowChanged, this,
            [this] { updateFilter(m_filter->text()); });
    connect(reloadButton, &QPushButton::clicked,
            this, &TextureBrowserWidget::reload);
    connect(m_textureList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
                m_textureList->setCurrentItem(item);
                if (m_textureActivationCallback) m_textureActivationCallback();
            });

    reload();
}

void TextureBrowserWidget::reload()
{
    m_textureList->clear();
    m_images.clear();
    m_loadStatus.clear();
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
                const size_t pixelsSize = duke_art_get_tile_data_by_index(
                    art.get(), static_cast<uint32_t>(tileIndex), &pixels);
                if (pixelsSize == static_cast<size_t>(-1) || !pixels) {
                    continue;
                }
                const QImage image = tileImage(*tile, static_cast<const uint8_t *>(pixels),
                                               pixelsSize, *palette);
                if (image.isNull()) {
                    continue;
                }
                textures[tile->tile_number] = {image, tile->width, tile->height};
            }
        }
    }

    for (auto iterator = textures.cbegin(); iterator != textures.cend(); ++iterator) {
        const Texture &texture = iterator.value();
        const QPixmap thumbnail = QPixmap::fromImage(texture.image).scaled(
            thumbnailSize, thumbnailSize, Qt::KeepAspectRatio,
            Qt::FastTransformation);
        const auto metadata = textureMetadata(iterator.key());
        auto *item = new QListWidgetItem(QIcon(thumbnail),
                                        QString::number(iterator.key()) + "\n" + metadata.name);
        item->setData(Qt::UserRole, iterator.key());
        item->setToolTip(QString("Tile %1\n%4\n%2 × %3\n%5")
                             .arg(iterator.key())
                             .arg(texture.width)
                             .arg(texture.height)
                             .arg(metadata.name)
                             .arg(metadata.categories.join(", ")));
        m_textureList->addItem(item);
        m_images.insert(iterator.key(), texture.image);
    }

    QString status = QString("%1 textures from %2 GRP file(s)")
                         .arg(textures.size())
                         .arg(archives.size());
    if (failedArchives > 0 || failedArtFiles > 0) {
        status += QString(" — %1 GRP and %2 ART file(s) failed")
                      .arg(failedArchives)
                      .arg(failedArtFiles);
    }
    m_loadStatus = status;
    updateFilter(m_filter->text());
}

std::optional<int> TextureBrowserWidget::selectedTile() const
{
    const QListWidgetItem *item = m_textureList->currentItem();
    if (!item || item->isHidden()) {
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
    // The currently assigned tile must remain visible when opening a chooser.
    m_filter->clear();
    m_categories->setCurrentRow(0);
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
    const QString category = m_categories->currentItem()->text();
    int visible = 0;
    for (int index = 0; index < m_textureList->count(); ++index) {
        QListWidgetItem *item = m_textureList->item(index);
        const int tile = item->data(Qt::UserRole).toInt();
        const auto metadata = textureMetadata(tile);
        const bool matchesCategory = category == "All"
            || (category == "Used in this map" ? m_usedTiles.count(tile) != 0
                                               : metadata.categories.contains(category));
        const bool matches = matchesCategory && textureMatchesSearch(tile, metadata, text);
        item->setHidden(!matches);
        if (matches) ++visible;
    }
    if (m_textureList->currentItem() && m_textureList->currentItem()->isHidden())
        m_textureList->setCurrentItem(nullptr);
    if (!m_loadStatus.isEmpty())
        m_statusLabel->setText(QString("%1 shown — %2").arg(visible).arg(m_loadStatus));
}
