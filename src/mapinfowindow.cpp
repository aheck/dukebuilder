#include "mapinfowindow.h"
#include "mapdocument.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLocale>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QtEndian>
#include <libduke/map.h>

#include <algorithm>
#include <set>

MapInfoWindow::MapInfoWindow(const MapDocument &document, const QString &filename,
                             bool unsavedChanges, QWidget *parent)
    : QDialog(parent)
{
    std::size_t wallSides = 0;
    std::set<int> bunches;
    for (const auto &sector : document.sectors()) {
        wallSides += sector.walls.size();
        if (sector.ceilingBunch) bunches.insert(*sector.ceilingBunch);
        if (sector.floorBunch) bunches.insert(*sector.floorBunch);
    }
    const auto portals = std::count_if(document.walls().begin(), document.walls().end(),
        [](const MapDocument::Wall &wall) { return wall.isTwoSided(); });
    QString savedFormat = "Not saved";
    if (!filename.isEmpty()) {
        savedFormat = "Unavailable";
        QFile file(filename);
        if (file.open(QIODevice::ReadOnly)) {
            const auto header = file.read(4);
            if (header.size() == 4) {
                const auto version = qFromLittleEndian<qint32>(header.constData());
                if (version >= 7 && version <= 9)
                    savedFormat = QString("Build version %1").arg(version);
                else savedFormat = "Unrecognized file header";
            }
        }
    }
    const int nextVersion = document.hasTror() ? 9
        : document.sectors().size() <= MAPV7_MAXSECTORS && wallSides <= MAPV7_MAXWALLS
            && document.sprites().size() <= MAPV7_MAXSPRITES ? 7 : 8;
    const bool exceedsLimits = document.sectors().size() > MAPV8_MAXSECTORS
        || wallSides > MAPV8_MAXWALLS || document.sprites().size() > MAPV8_MAXSPRITES;

    setWindowTitle("Map Info");
    setMinimumWidth(520);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(16);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    const auto makeLabel = [this](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setTextFormat(Qt::PlainText);
        // Protect descenders and wrapped text from vertical compression.
        label->setContentsMargins(0, 3, 0, 3);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        return label;
    };
    const auto mute = [](QLabel *label) {
        auto colors = label->palette();
        const QColor foreground = colors.color(QPalette::WindowText);
        const QColor background = colors.color(QPalette::Window);
        colors.setColor(QPalette::WindowText, QColor(
            (foreground.red() * 3 + background.red()) / 4,
            (foreground.green() * 3 + background.green()) / 4,
            (foreground.blue() * 3 + background.blue()) / 4));
        label->setPalette(colors);
    };
    const auto enlarge = [](QLabel *label, qreal scale) {
        auto font = label->font();
        if (font.pointSizeF() > 0) font.setPointSizeF(font.pointSizeF() * scale);
        else font.setPixelSize(qRound(font.pixelSize() * scale));
        font.setBold(true);
        label->setFont(font);
    };
    auto *header = new QVBoxLayout;
    header->setSpacing(2);
    auto *title = makeLabel(filename.isEmpty() ? "Untitled map" : QFileInfo(filename).fileName());
    enlarge(title, 1.5);
    title->setWordWrap(true);
    header->addWidget(title);
    auto *path = makeLabel(filename.isEmpty() ? "This map has not been saved yet."
        : QFileInfo(filename).absoluteFilePath());
    mute(path);
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    header->addWidget(path);
    layout->addLayout(header);

    auto *badges = new QHBoxLayout;
    badges->setSpacing(8);
    const auto addBadge = [&](const QString &text, const QString &tooltip) {
        auto *badge = makeLabel(text);
        badge->setToolTip(tooltip);
        badge->setContentsMargins(10, 5, 10, 5);
        badge->setAutoFillBackground(true);
        badge->setBackgroundRole(QPalette::AlternateBase);
        badges->addWidget(badge);
    };
    addBadge(savedFormat, "Format of the file currently on disk.");
    addBadge(unsavedChanges ? "Unsaved changes" : filename.isEmpty() ? "New map" : "Saved",
        "Whether the current map contains unsaved edits.");
    badges->addStretch();
    layout->addLayout(badges);

    auto *counts = new QGridLayout;
    counts->setSpacing(12);
    const auto number = [](std::size_t count) {
        return QLocale().toString(static_cast<qulonglong>(count));
    };
    const auto addCard = [&](int row, int column, const QString &name,
                             std::size_t count, const QString &tooltip) {
        auto *card = new QFrame(this);
        card->setAutoFillBackground(true);
        card->setBackgroundRole(QPalette::AlternateBase);
        card->setToolTip(tooltip);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 10, 16, 10);
        cardLayout->setSpacing(0);
        auto *value = makeLabel(number(count));
        enlarge(value, 1.8);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        cardLayout->addWidget(value);
        auto *caption = makeLabel(name);
        mute(caption);
        cardLayout->addWidget(caption);
        counts->addWidget(card, row, column);
    };
    addCard(0, 0, "Sectors", document.sectors().size(), "Sectors in the current map.");
    addCard(0, 1, "Walls", wallSides,
        "Build wall sides. Shared sector boundaries count twice; unfinished editor lines are excluded.");
    addCard(1, 0, "Sprites", document.sprites().size(), "Sprites in the current map.");
    addCard(1, 1, "Textures", document.usedTextureTiles().size(),
        "Unique texture tiles used by floors, ceilings, walls, active overlays and sprites.");
    counts->setColumnStretch(0, 1);
    counts->setColumnStretch(1, 1);
    layout->addLayout(counts);

    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);
    auto *detailsTitle = makeLabel("Map details");
    auto detailsFont = detailsTitle->font();
    detailsFont.setBold(true);
    detailsTitle->setFont(detailsFont);
    layout->addWidget(detailsTitle);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(4);
    const auto addRow = [&](const QString &name, const QString &value) {
        auto *label = makeLabel(value);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
        auto *caption = makeLabel(name);
        mute(caption);
        form->addRow(caption, label);
    };
    addRow("Format on next save", exceedsLimits ? "Exceeds supported map limits"
        : QString("Build version %1 (if valid)").arg(nextVersion));
    addRow("Editor lines", number(document.walls().size()));
    addRow("Shared boundaries", number(portals));
    addRow("Vertices", number(document.vertices().size()));
    addRow("TROR bunches", number(bunches.size()));
    layout->addLayout(form);
    auto *note = makeLabel("Counts include unsaved edits.");
    mute(note);
    note->setToolTip("Use Tools → Check Map to validate the map before saving.");
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
