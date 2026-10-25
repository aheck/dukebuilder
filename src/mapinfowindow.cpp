#include "mapinfowindow.h"
#include "mapdocument.h"

#include <QDialogButtonBox>
#include <QFile>
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
    setMinimumWidth(440);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->addLayout(form);
    const auto makeLabel = [this](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setTextFormat(Qt::PlainText);
        // Leave room for descenders and fractional font scaling. Minimum vertical
        // sizing also keeps the form from compressing wrapped text below its hint.
        label->setContentsMargins(0, 3, 0, 3);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        return label;
    };
    const auto addRow = [&](const QString &name, const QString &value) {
        auto *label = makeLabel(value);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
        form->addRow(makeLabel(name), label);
    };
    const auto addCount = [&](const QString &name, std::size_t count) {
        addRow(name, QString::number(static_cast<qulonglong>(count)));
    };
    addRow("File", filename.isEmpty() ? "Untitled" : filename);
    addRow("Unsaved changes", unsavedChanges ? "Yes" : "No");
    addRow("Format on disk", savedFormat);
    addRow("Format on next save", exceedsLimits ? "Exceeds supported map limits"
        : QString("Build version %1 (if valid)").arg(nextVersion));
    addCount("Sectors", document.sectors().size());
    addCount("Walls (Build wall sides)", wallSides);
    addCount("Editor lines", document.walls().size());
    addCount("Shared sector boundaries", portals);
    addCount("Vertices", document.vertices().size());
    addCount("Sprites", document.sprites().size());
    addCount("Unique texture tiles", document.usedTextureTiles().size());
    addCount("TROR bunches", bunches.size());
    auto *note = makeLabel("Counts reflect the current map, including unsaved edits. "
        "Shared boundaries have two Build wall sides. Texture tiles include floors, "
        "ceilings, walls, active overlays and sprites. Use Check Map to validate before saving.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
