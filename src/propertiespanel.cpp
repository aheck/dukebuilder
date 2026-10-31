#include "propertiespanel.h"
#include "angledial.h"
#include "propertyvaluedelegate.h"
#include "spritelotags.h"
#include "texturebrowserwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

PropertiesPanel::PropertiesPanel(MapEditor *editor,
    TextureBrowserWindow *textureBrowser, QWidget *parent)
    : QWidget(parent), m_editor(editor), m_textureBrowserWindow(textureBrowser)
{
    m_propertiesControl = new QTreeWidget(this);
    m_propertiesControl->setObjectName("PropertiesControl");
    m_propertiesControl->setColumnCount(2);
    m_propertiesControl->setHeaderLabels({"Property", "Value"});
    m_propertiesControl->setRootIsDecorated(true);
    m_propertiesControl->setAlternatingRowColors(true);
    m_propertyDelegate = new PropertyValueDelegate(m_propertiesControl);
    m_propertiesControl->setItemDelegate(m_propertyDelegate);
    m_propertiesControl->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_propertiesControl->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    auto *propertiesLayout = new QVBoxLayout(this);
    propertiesLayout->setContentsMargins(0, 0, 0, 0);
    m_trorLayersPanel = new QGroupBox("TROR Layers", this);
    m_trorLayersPanel->setObjectName("TrorLayersPanel");
    auto *layersLayout = new QVBoxLayout(m_trorLayersPanel);
    m_layersStatus = new QLabel(m_trorLayersPanel);
    m_layersStatus->setWordWrap(true);
    layersLayout->addWidget(m_layersStatus);
    m_layersList = new QTreeWidget(m_trorLayersPanel);
    m_layersList->setObjectName("TrorLayersList");
    m_layersList->setAccessibleName("TROR layers, top to bottom");
    m_layersList->setColumnCount(2);
    m_layersList->setHeaderLabels({"Layer", "Height range (Z)"});
    m_layersList->setRootIsDecorated(false);
    m_layersList->setAlternatingRowColors(true);
    m_layersList->setMinimumHeight(90);
    m_layersList->setMaximumHeight(200);
    m_layersList->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_layersList->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    layersLayout->addWidget(m_layersList);
    m_showAllLayers = new QPushButton("Show all layers", m_trorLayersPanel);
    m_showAllLayers->setObjectName("ShowAllTrorLayers");
    m_showAllLayers->setToolTip("Clear the editing filter and show all map geometry.");
    layersLayout->addWidget(m_showAllLayers);
    propertiesLayout->addWidget(m_trorLayersPanel);
    m_trorLayersPanel->hide();
    // Selection is temporarily empty during scene rebuilds. Refresh after the
    // current edit/selection operation finishes, preserving the stack context.
    m_layersRefresh = new QTimer(m_trorLayersPanel);
    m_layersRefresh->setSingleShot(true);
    m_layersRefresh->setInterval(0);
    m_editor->trorLayersChanged = [timer = QPointer<QTimer>(m_layersRefresh)] {
        if (timer) timer->start();
    };
    connect(m_layersRefresh, &QTimer::timeout, this, &PropertiesPanel::refreshLayers);
    connect(m_layersList, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *row, QTreeWidgetItem *) {
        if (!row) return;
        m_editor->activateTrorLayer(row->data(0, Qt::UserRole).toULongLong());
        m_layersRefresh->start();
    });
    connect(m_showAllLayers, &QPushButton::clicked, this, [this] { m_editor->clearEditingScope(); m_layersRefresh->start(); });
    propertiesLayout->addWidget(m_propertiesControl, 1);
    const auto createPreview = [](QWidget *parent, const QString &name) {
        auto *preview = new QPushButton(parent);
        preview->setObjectName(name);
        preview->setFixedSize(160, 160);
        preview->setIconSize(QSize(156, 156));
        preview->setCursor(Qt::PointingHandCursor);
        preview->setStyleSheet("background-color: #181a1f; color: #e2e7f0;");
        return preview;
    };
    m_wallTexturePreview = createPreview(this, "WallTexturePreview");
    m_wallTexturePreviews = new QWidget(this);
    auto *wallPreviewLayout = new QHBoxLayout(m_wallTexturePreviews);
    wallPreviewLayout->setContentsMargins(4, 0, 4, 4);
    m_wallTextureLabel = new QLabel("Wall texture", m_wallTexturePreviews);
    m_wallTextureLabel->setAlignment(Qt::AlignCenter);
    auto *wallTextureColumn = new QVBoxLayout;
    wallTextureColumn->addWidget(m_wallTextureLabel);
    wallTextureColumn->addWidget(m_wallTexturePreview, 0, Qt::AlignHCenter);
    wallPreviewLayout->addLayout(wallTextureColumn);
    m_oppositeTexturePanel = new QWidget(m_wallTexturePreviews);
    auto *oppositeTextureColumn = new QVBoxLayout(m_oppositeTexturePanel);
    oppositeTextureColumn->setContentsMargins(0, 0, 0, 0);
    m_oppositeTextureLabel = new QLabel(m_oppositeTexturePanel);
    m_oppositeTextureLabel->setAlignment(Qt::AlignCenter);
    m_oppositeTexturePreview = createPreview(m_oppositeTexturePanel, "OppositeWallTexturePreview");
    oppositeTextureColumn->addWidget(m_oppositeTextureLabel);
    oppositeTextureColumn->addWidget(m_oppositeTexturePreview, 0, Qt::AlignHCenter);
    wallPreviewLayout->addWidget(m_oppositeTexturePanel);
    m_oppositeTexturePanel->hide();
    m_wallTexturePreviews->hide();
    propertiesLayout->addWidget(m_wallTexturePreviews);
    m_spriteTexturePreviews = new QWidget(this);
    m_spriteTexturePreviews->setObjectName("SpriteTexturePreviews");
    auto *spritePreviewLayout = new QVBoxLayout(m_spriteTexturePreviews);
    spritePreviewLayout->setContentsMargins(4, 0, 4, 4);
    auto *spriteTextureLabel = new QLabel("Sprite texture", m_spriteTexturePreviews);
    spriteTextureLabel->setAlignment(Qt::AlignCenter);
    spritePreviewLayout->addWidget(spriteTextureLabel);
    m_spriteTexturePreview = createPreview(m_spriteTexturePreviews, "SpriteTexturePreview");
    spritePreviewLayout->addWidget(m_spriteTexturePreview, 0, Qt::AlignHCenter);
    m_spriteTexturePreviews->hide();
    propertiesLayout->addWidget(m_spriteTexturePreviews);
    m_sectorTexturePreviews = new QWidget(this);
    m_sectorTexturePreviews->setObjectName("SectorTexturePreviews");
    auto *sectorPreviewLayout = new QHBoxLayout(m_sectorTexturePreviews);
    sectorPreviewLayout->setContentsMargins(4, 0, 4, 4);
    const auto addSectorPreview = [&](const QString &title, const QString &name) {
        auto *column = new QVBoxLayout;
        auto *label = new QLabel(title, m_sectorTexturePreviews);
        label->setAlignment(Qt::AlignCenter);
        column->addWidget(label);
        auto *preview = createPreview(m_sectorTexturePreviews, name);
        column->addWidget(preview, 0, Qt::AlignHCenter);
        sectorPreviewLayout->addLayout(column);
        return preview;
    };
    m_floorTexturePreview = addSectorPreview("Floor texture", "FloorTexturePreview");
    m_ceilingTexturePreview = addSectorPreview("Ceiling texture", "CeilingTexturePreview");
    m_sectorTexturePreviews->hide();
    propertiesLayout->addWidget(m_sectorTexturePreviews);
    const auto connectPreview = [this](QPushButton *preview,
                                            MapEditor::Property property) {
        connect(preview, &QPushButton::clicked, this, [this, preview, property] {
            const auto selection = m_textureBrowserWindow->chooseTexture(preview->property("tile").toInt());
            if (selection) m_editor->setSelectedProperty(property, selection->tile);
        });
    };
    connectPreview(m_wallTexturePreview, MapEditor::Property::Texture);
    connectPreview(m_oppositeTexturePreview, MapEditor::Property::OppositeTexture);
    connectPreview(m_spriteTexturePreview, MapEditor::Property::Texture);
    connectPreview(m_floorTexturePreview, MapEditor::Property::FloorTexture);
    connectPreview(m_ceilingTexturePreview, MapEditor::Property::CeilingTexture);

    connect(m_propertiesControl, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem *item, int column) {
                if (column != 1) {
                    return;
                }
                const int mask = item->data(0, Qt::UserRole + 1).toInt();
                qreal value;
                if (mask && item->parent()) {
                    int flags = item->parent()->text(1).toInt();
                    value = item->checkState(1) == Qt::Checked ? flags | mask : flags & ~mask;
                } else {
                    bool valid = false;
                    value = item->text(1).toDouble(&valid);
                    if (!valid) value = std::numeric_limits<qreal>::quiet_NaN();
                }
                const auto property = static_cast<MapEditor::Property>(
                    item->data(0, Qt::UserRole).toInt());
                const QPersistentModelIndex index = m_propertiesControl->indexFromItem(item, column);
                // Committing rebuilds the tree. Qt still accesses the item after
                // emitting itemChanged (notably for check states), so let that
                // call finish before retiring the row. Ignore edits whose row
                // or panel was removed before the queued commit runs.
                QTimer::singleShot(0, this, [this, index, property, value] {
                    if (index.isValid()) m_editor->setSelectedProperty(property, value);
                });
            });

    // Keep expansion state outside the items: property edits rebuild the tree,
    // sometimes through an intermediate empty selection. Rebuild signals are
    // blocked below, so only user expansion/collapse changes this state.
    const auto rememberExpansion = [this](QTreeWidgetItem *item, bool expanded) {
        QStringList groups = m_propertiesControl->property("expandedGroups").toStringList();
        groups.removeAll(item->text(0));
        if (expanded) { groups.append(item->text(0)); }
        m_propertiesControl->setProperty("expandedGroups", groups);
    };
    connect(m_propertiesControl, &QTreeWidget::itemExpanded, m_propertiesControl,
            [rememberExpansion](QTreeWidgetItem *item) { rememberExpansion(item, true); });
    connect(m_propertiesControl, &QTreeWidget::itemCollapsed, m_propertiesControl,
            [rememberExpansion](QTreeWidgetItem *item) { rememberExpansion(item, false); });

    m_propertyDelegate->setTextureChooser(
        [this](std::optional<int> currentTexture) -> std::optional<int> {
            const auto selection = m_textureBrowserWindow->chooseTexture(currentTexture);
            return selection ? std::optional<int>(selection->tile) : std::nullopt;
        });
}

void PropertiesPanel::setProperties(std::optional<MapEditor::SelectionProperties> properties)
{
    const QSignalBlocker blocker(m_propertiesControl);
    // Clearing the rows can finish an edit while its widget is being retired.
    const QSignalBlocker delegateBlocker(m_propertyDelegate);
    m_propertiesControl->clear();
    m_wallTexturePreviews->hide();
    m_spriteTexturePreviews->hide();
    m_sectorTexturePreviews->hide();
    if (!properties) {
        return;
    }
    const auto number = [](qreal value) {
        return qFuzzyCompare(value, std::round(value))
            ? QString::number(value, 'f', 0)
            : QString::number(value, 'f', 2);
    };
    const auto addProperty = [this](
                                 const QString &name, const QString &value,
                                 MapEditor::Property property, QTreeWidgetItem *parent = nullptr) {
        auto *item = parent ? new QTreeWidgetItem(parent, {name, value})
                            : new QTreeWidgetItem(m_propertiesControl, {name, value});
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setData(0, Qt::UserRole, static_cast<int>(property));
        return item;
    };
    const auto addTextureProperty = [&](const QString &name, int texture,
                                        MapEditor::Property property) {
        addProperty(name, QString::number(texture), property);
        m_propertiesControl->openPersistentEditor(
            m_propertiesControl->topLevelItem(
                m_propertiesControl->topLevelItemCount() - 1), 1);
    };
    const auto addFlags = [&](const QString &name, int flags, MapEditor::Property property,
                              std::initializer_list<std::pair<int, const char *>> meanings) {
        auto *group = addProperty(name, QString::number(flags), property);
        for (const auto &[mask, meaning] : meanings) {
            auto *flag = new QTreeWidgetItem(group, {QString::fromUtf8(meaning), ""});
            flag->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            flag->setData(0, Qt::UserRole, static_cast<int>(property));
            flag->setData(0, Qt::UserRole + 1, mask);
            flag->setCheckState(1, flags & mask ? Qt::Checked : Qt::Unchecked);
        }
        group->setExpanded(m_propertiesControl->property("expandedGroups")
                               .toStringList().contains(group->text(0)));
    };
    const auto addChoice = [&](const QString &name, int value, MapEditor::Property property,
                               const std::vector<std::pair<int, QString>> &choices) {
        auto *row = new QTreeWidgetItem(m_propertiesControl, {name, ""});
        auto *combo = new QComboBox(m_propertiesControl);
        for (const auto &[id, label] : choices) combo->addItem(label, id);
        combo->setCurrentIndex(combo->findData(value));
        m_propertiesControl->setItemWidget(row, 1, combo);
        const QPersistentModelIndex index = m_propertiesControl->model()->index(
            m_propertiesControl->topLevelItemCount() - 1, 1);
        connect(combo, &QComboBox::activated, combo,
                [this, combo, property, index](int selected) {
                    if (index.isValid()) m_editor->setSelectedProperty(property, combo->itemData(selected).toInt());
                }, Qt::QueuedConnection);
    };
    const auto advancedGroup = [&]() {
        auto *group = new QTreeWidgetItem(m_propertiesControl, {"Advanced", ""});
        group->setFlags(Qt::ItemIsEnabled);
        group->setExpanded(m_propertiesControl->property("expandedGroups")
                               .toStringList().contains(group->text(0)));
        return group;
    };
    if (properties->wall) {
        const auto &wall = *properties->wall;
        auto *sideRow = new QTreeWidgetItem(m_propertiesControl, {"Side", ""});
        auto *sideChooser = new QComboBox(m_propertiesControl);
        const auto addSide = [&](const QString &name,
                                 std::optional<MapDocument::SectorId> sector, bool reversed) {
            if (sector) sideChooser->addItem(name + " - Sector " + QString::number(*sector), reversed);
        };
        addSide("Front", wall.forwardSector, false);
        addSide("Back", wall.reverseSector, true);
        if (sideChooser->count() == 0) sideChooser->addItem("Front", false);
        sideChooser->setCurrentIndex(sideChooser->findData(wall.reversed));
        sideChooser->setEnabled(sideChooser->count() > 1);
        m_propertiesControl->setItemWidget(sideRow, 1, sideChooser);
        const QPersistentModelIndex sideIndex = m_propertiesControl->model()->index(0, 1);
        connect(sideChooser, &QComboBox::activated, sideChooser,
                [this, sideChooser, sideIndex](int index) {
                    if (sideIndex.isValid()) m_editor->setSelectedWallSide(sideChooser->itemData(index).toBool());
                }, Qt::QueuedConnection);
        const auto &values = wall.values;
        auto *lengthRow = new QTreeWidgetItem(m_propertiesControl,
            {"Length", number(wall.length)});
        lengthRow->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        lengthRow->setToolTip(1, "Line length in map units");
        const QString selectedSide = wall.reversed ? "Back wall" : "Front wall";
        m_wallTextureLabel->setText(wall.oppositeTexture ? selectedSide + " texture" : "Wall texture");
        updateTexturePreview(m_wallTexturePreview, values.texture, selectedSide);
        m_oppositeTexturePanel->setVisible(wall.oppositeTexture.has_value());
        if (wall.oppositeTexture) {
            const QString oppositeSide = wall.reversed ? "Front wall" : "Back wall";
            m_oppositeTextureLabel->setText(oppositeSide + " texture");
            updateTexturePreview(m_oppositeTexturePreview, *wall.oppositeTexture, oppositeSide);
        }
        m_wallTexturePreviews->show();
        addTextureProperty("Texture", values.texture, MapEditor::Property::Texture);
        addTextureProperty("Overlay texture", values.overlayTexture, MapEditor::Property::OverlayTexture);
        addProperty("Shade", QString::number(values.shade), MapEditor::Property::Shade);
        addProperty("Palette", QString::number(values.palette), MapEditor::Property::Palette);
        addProperty("X repeat", QString::number(values.xrepeat), MapEditor::Property::XRepeat);
        addProperty("Y repeat", QString::number(values.yrepeat), MapEditor::Property::YRepeat);
        addProperty("X panning", QString::number(values.xpanning), MapEditor::Property::XPanning);
        addProperty("Y panning", QString::number(values.ypanning), MapEditor::Property::YPanning);
        addFlags("Flags (cstat)", values.cstat, MapEditor::Property::Cstat,
                 {{1,"Blocking"},{2,"Swap bottom texture"},{4,"Align to bottom"},
                  {8,"Flip X"},{16,"Masked"},{32,"One-way"},{64,"Block hitscan"},
                  {128,"Translucent"},{256,"Flip Y"},{512,"Reverse translucency"}});
        addProperty("Hitag", QString::number(values.hitag), MapEditor::Property::Hitag);
        if (values.upLink) {
            new QTreeWidgetItem(m_propertiesControl, {"TROR upper wall", QString("%1 (%2)").arg(values.upLink->wall).arg(values.upLink->reversed ? "back" : "front")});
        } else {
            addProperty("Lotag", QString::number(values.lotag), MapEditor::Property::WallLotag);
            m_propertiesControl->openPersistentEditor(
                m_propertiesControl->topLevelItem(m_propertiesControl->topLevelItemCount() - 1), 1);
        }
        if (values.downLink) {
            new QTreeWidgetItem(m_propertiesControl, {"TROR lower wall", QString("%1 (%2)").arg(values.downLink->wall).arg(values.downLink->reversed ? "back" : "front")});
        } else { addProperty("Extra", QString::number(values.extra), MapEditor::Property::Extra, advancedGroup()); }
        return;
    }
    if (properties->sector) {
        const auto &sector = *properties->sector;
        auto *sectorNumber = new QTreeWidgetItem(m_propertiesControl,
            {"Sector number", QString::number(*properties->sectorId)});
        sectorNumber->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        updateTexturePreview(m_ceilingTexturePreview, sector.ceilingTexture, "Ceiling");
        updateTexturePreview(m_floorTexturePreview, sector.floorTexture, "Floor");
        m_sectorTexturePreviews->show();
        addProperty("Floor Z", number(sector.floorz), MapEditor::Property::FloorZ);
        addProperty("Ceiling Z", number(sector.ceilingz), MapEditor::Property::CeilingZ);
        addTextureProperty("Floor texture", sector.floorTexture,
                           MapEditor::Property::FloorTexture);
        addTextureProperty("Ceiling texture", sector.ceilingTexture,
                           MapEditor::Property::CeilingTexture);
        addProperty("Hitag", QString::number(sector.hitag), MapEditor::Property::Hitag);
        addProperty("Lotag", QString::number(sector.lotag), MapEditor::Property::SectorLotag);
        m_propertiesControl->openPersistentEditor(
            m_propertiesControl->topLevelItem(
                m_propertiesControl->topLevelItemCount() - 1), 1);
        addFlags("Ceiling flags", sector.ceilingstat, MapEditor::Property::CeilingStat,
                 {{1,"Parallax sky"},{2,"Sloped"},{4,"Swap texture axes"},{8,"Double texture scale"},
                  {16,"Flip X"},{32,"Flip Y"},{64,"Align to first wall"}});
        addProperty("Ceiling slope", QString::number(sector.ceilingheinum), MapEditor::Property::CeilingSlope);
        addProperty("Ceiling shade", QString::number(sector.ceilingshade), MapEditor::Property::CeilingShade);
        addProperty("Ceiling palette", QString::number(sector.ceilingpal), MapEditor::Property::CeilingPalette);
        if (sector.ceilingBunch) {
            new QTreeWidgetItem(m_propertiesControl, {"Ceiling TROR bunch", QString::number(*sector.ceilingBunch)});
        } else { addProperty("Ceiling X panning", QString::number(sector.ceilingxpanning), MapEditor::Property::CeilingXPanning); }
        addProperty("Ceiling Y panning", QString::number(sector.ceilingypanning), MapEditor::Property::CeilingYPanning);
        addFlags("Floor flags", sector.floorstat, MapEditor::Property::FloorStat,
                 {{1,"Parallax sky"},{2,"Sloped"},{4,"Swap texture axes"},{8,"Double texture scale"},
                  {16,"Flip X"},{32,"Flip Y"},{64,"Align to first wall"}});
        addProperty("Floor slope", QString::number(sector.floorheinum), MapEditor::Property::FloorSlope);
        addProperty("Floor shade", QString::number(sector.floorshade), MapEditor::Property::FloorShade);
        addProperty("Floor palette", QString::number(sector.floorpal), MapEditor::Property::FloorPalette);
        if (sector.floorBunch) {
            new QTreeWidgetItem(m_propertiesControl, {"Floor TROR bunch", QString::number(*sector.floorBunch)});
        } else { addProperty("Floor X panning", QString::number(sector.floorxpanning), MapEditor::Property::FloorXPanning); }
        addProperty("Floor Y panning", QString::number(sector.floorypanning), MapEditor::Property::FloorYPanning);
        std::vector<std::pair<int, QString>> wallChoices;
        const auto outerEnd = sector.loopStarts.size() > 1 ? sector.loopStarts[1] : sector.walls.size();
        for (std::size_t i = 0; i < outerEnd; ++i) {
            const auto wallId = sector.walls[i];
            wallChoices.emplace_back(static_cast<int>(wallId), QString("Wall %1").arg(wallId));
        }
        if (!sector.walls.empty()) addChoice("First wall (slope reference)", static_cast<int>(sector.walls.front()),
                                             MapEditor::Property::FirstWall, wallChoices);
        addProperty("Visibility", QString::number(sector.visibility), MapEditor::Property::Visibility);
        addProperty("Extra", QString::number(sector.extra), MapEditor::Property::Extra, advancedGroup());
        return;
    }
    addProperty("X", number(properties->x), MapEditor::Property::X);
    addProperty("Y", number(properties->y), MapEditor::Property::Y);
    addProperty("Z", number(properties->z), MapEditor::Property::Z);
    auto *angleRow = addProperty("Angle", number(std::clamp(properties->angle, 0.0, 360.0)),
                                 MapEditor::Property::Angle);
    angleRow->setFlags(angleRow->flags() & ~Qt::ItemIsEditable);
    auto *angleControl = new QWidget(m_propertiesControl);
    auto *angleLayout = new QHBoxLayout(angleControl);
    angleLayout->setContentsMargins(0, 0, 0, 0);
    auto *angleText = new QLineEdit(angleRow->text(1), angleControl);
    angleText->setAccessibleName("Angle in degrees");
    auto *angleDial = new AngleDial(angleControl);
    angleDial->setAccessibleName("Angle");
    // QDial maps the entire min/max interval to a full circle.
    angleDial->setRange(0, 360);
    angleDial->setWrapping(true);
    angleDial->setTracking(false);
    // Single-step controls keyboard increments and notch spacing, but
    // leaves mouse dragging free to select individual degrees.
    angleDial->setSingleStep(15);
    angleDial->setPageStep(15);
    angleDial->setNotchTarget(1);
    angleDial->setNotchesVisible(true);
    angleDial->setFixedSize(48, 48);
    angleDial->setToolTip("Angle in degrees. Hold Shift to snap to 15°");
    const auto syncDial = [angleDial](const QString &text) {
        bool valid = false;
        const double angle = text.toDouble(&valid);
        if (valid && std::isfinite(angle) && angle >= 0 && angle <= 360) {
            const QSignalBlocker blocker(angleDial);
            angleDial->setValue(AngleDial::positionForAngle(qRound(angle)));
        }
    };
    syncDial(angleText->text());
    angleLayout->addWidget(angleText, 1);
    angleLayout->addWidget(angleDial);
    angleRow->setSizeHint(1, angleControl->sizeHint());
    m_propertiesControl->setItemWidget(angleRow, 1, angleControl);
    const QPersistentModelIndex angleIndex = m_propertiesControl->model()->index(
        m_propertiesControl->topLevelItemCount() - 1, 1);
    const auto commitAngle = [this, angleText, angleIndex] {
        if (!angleIndex.isValid() || angleText->text() == angleIndex.data().toString()) return;
        bool valid = false;
        const double angle = angleText->text().toDouble(&valid);
        m_editor->setSelectedProperty(MapEditor::Property::Angle,
            valid ? angle : std::numeric_limits<qreal>::quiet_NaN());
    };
    connect(angleText, &QLineEdit::textEdited, angleControl, syncDial);
    connect(angleText, &QLineEdit::editingFinished, angleControl, commitAngle, Qt::QueuedConnection);
    connect(angleDial, &QDial::sliderMoved, angleControl, [angleText, angleDial](int angle) {
        if (QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier)) {
            angle = (qRound(angle / 15.0) * 15) % 360;
            const QSignalBlocker blocker(angleDial);
            angleDial->setSliderPosition(angle);
        }
        angleText->setText(QString::number(AngleDial::angleForPosition(angle)));
    });
    connect(angleDial, &QDial::valueChanged, angleControl, [angleText](int angle) {
        angleText->setText(QString::number(AngleDial::angleForPosition(angle)));
    });
    // Queue commits because applying a property rebuilds this entire tree.
    connect(angleDial, &QDial::valueChanged, angleControl, commitAngle, Qt::QueuedConnection);
    if (properties->texture) {
        updateTexturePreview(m_spriteTexturePreview, *properties->texture, "Sprite");
        m_spriteTexturePreviews->show();
        addTextureProperty("Texture", *properties->texture, MapEditor::Property::Texture);
    }
    if (properties->hitag) {
        addProperty("Hitag", QString::number(*properties->hitag),
                    MapEditor::Property::Hitag);
    }
    if (properties->lotag) {
        auto *row = addProperty("Lotag", QString::number(*properties->lotag),
                                MapEditor::Property::Lotag);
        const int tile = properties->texture.value_or(-1);
        row->setData(0, Qt::UserRole + 2, tile);
        row->setToolTip(0, QString::fromUtf8(spriteLotags(tile).description));
        m_propertiesControl->openPersistentEditor(row, 1);
    }
    if (properties->sprite) {
        const auto &sprite = *properties->sprite;
        addFlags("Flags (cstat)", sprite.cstat, MapEditor::Property::Cstat,
                 {{1,"Blocking"},{2,"Translucent"},{4,"Flip X"},{8,"Flip Y"},
                  {64,"One-sided"},{128,"Centered on Z"},{256,"Block hitscan"},
                  {512,"Reverse translucency"},{32768,"Invisible"}});
        addChoice("Alignment", (sprite.cstat >> 4) & 3, MapEditor::Property::Alignment,
                  {{0,"Face camera"},{1,"Wall aligned"},{2,"Floor aligned"},{3,"Sloped floor"}});
        addProperty("Shade", QString::number(sprite.shade), MapEditor::Property::Shade);
        auto paletteNumbers = m_textureBrowserWindow->paletteNumbers();
        paletteNumbers.insert(sprite.palette);
        std::vector<std::pair<int, QString>> paletteChoices;
        for (int palette : paletteNumbers) {
            paletteChoices.emplace_back(palette, QString::number(palette));
        }
        addChoice("Palette", sprite.palette, MapEditor::Property::Palette, paletteChoices);
        addProperty("Collision size", QString::number(sprite.clipdist), MapEditor::Property::Clipdist);
        addProperty("X repeat", QString::number(sprite.xrepeat), MapEditor::Property::XRepeat);
        addProperty("Y repeat", QString::number(sprite.yrepeat), MapEditor::Property::YRepeat);
        if (sprite.floorAligned()) {
            addProperty("Sprite slope", QString::number(sprite.slope()), MapEditor::Property::SpriteSlope);
        }
        if ((sprite.cstat & 48) != 48) {
            addProperty("X offset", QString::number(sprite.xoffset), MapEditor::Property::XOffset);
            addProperty("Y offset", QString::number(sprite.yoffset), MapEditor::Property::YOffset);
        }
        auto *advanced = advancedGroup();
        addProperty("Status", QString::number(sprite.statnum), MapEditor::Property::Status, advanced);
        addProperty("Owner", QString::number(sprite.owner), MapEditor::Property::Owner, advanced);
        addProperty("X velocity", QString::number(sprite.xvel), MapEditor::Property::XVelocity, advanced);
        addProperty("Y velocity", QString::number(sprite.yvel), MapEditor::Property::YVelocity, advanced);
        addProperty("Z velocity", QString::number(sprite.zvel), MapEditor::Property::ZVelocity, advanced);
        addProperty("Extra", QString::number(sprite.extra), MapEditor::Property::Extra, advanced);
    }
}

void PropertiesPanel::refreshLayers()
{
    const auto layers = m_editor->trorLayers();
    const QSignalBlocker blocker(m_layersList);
    m_layersList->clear();
    m_trorLayersPanel->setVisible(!layers.empty() && m_mapViewActive);
    int active = -1;
    const auto relatedLayers = [&](const auto &sectors) {
        QStringList names;
        for (std::size_t i = 0; i < layers.size(); ++i) {
            if (std::any_of(sectors.begin(), sectors.end(), [&](auto id) { return layers[i].sectors.count(id); }))
                names.append(QString("Layer %1").arg(i + 1));
        }
        return names.isEmpty() ? QString("None") : names.join(", ");
    };
    for (std::size_t index = 0; index < layers.size(); ++index) {
        const auto &layer = layers[index];
        const auto title = QString("Layer %1 · %2 sector%3%4").arg(index + 1).arg(layer.sectors.size())
            .arg(layer.sectors.size() == 1 ? "" : "s").arg(layer.active ? " (editing)" : "");
        auto *row = new QTreeWidgetItem(m_layersList, {title,
            QString("%1 … %2").arg(layer.ceilingZ, 0, 'f', 0).arg(layer.floorZ, 0, 'f', 0)});
        row->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(*layer.sectors.begin()));
        QStringList ids, bunches;
        for (auto id : layer.sectors) {
            ids.append(QString::number(id));
            const auto &sector = m_editor->document().sectors()[id];
            for (auto bunch : {sector.ceilingBunch, sector.floorBunch})
                if (bunch && !bunches.contains(QString::number(*bunch))) bunches.append(QString::number(*bunch));
        }
        const auto tooltip = QString("Sectors: %1\nAbove: %2\nBelow: %3\nTROR bunches: %4\nHeight range includes slopes; smaller Build Z is higher.")
            .arg(ids.join(", "), relatedLayers(layer.above), relatedLayers(layer.below), bunches.join(", "));
        row->setToolTip(0, tooltip); row->setToolTip(1, tooltip);
        if (layer.active) {
            active = static_cast<int>(index);
            auto font = row->font(0); font.setBold(true); row->setFont(0, font);
            m_layersList->setCurrentItem(row);
        }
    }
    m_layersStatus->setText(active >= 0 ? QString("Editing Layer %1. Other layers are dimmed.").arg(active + 1)
        : m_editor->editingScope() ? "Custom editing filter. Choose a layer to edit."
                                 : "All layers editable. Choose a layer to isolate it.");
    m_showAllLayers->setEnabled(m_editor->editingScope().has_value());
}

void PropertiesPanel::updateTexturePreview(QPushButton *preview, int tile, const QString &surface)
{
    preview->setText({});
    preview->setIcon({});
    preview->setProperty("tile", tile);
    preview->setAccessibleName(surface + " texture");
    preview->setToolTip(QString("%1 texture %2 — Click to change").arg(surface).arg(tile));
    const QImage image = m_textureBrowserWindow->textureImage(tile);
    if (image.isNull()) {
        preview->setText(QString("Texture %1\nUnavailable").arg(tile));
    } else {
        preview->setIcon(QIcon(QPixmap::fromImage(image).scaled(
            156, 156, Qt::KeepAspectRatio, Qt::FastTransformation)));
    }
}

void PropertiesPanel::setMapViewActive(bool active)
{
    m_mapViewActive = active;
    m_layersRefresh->start();
}
