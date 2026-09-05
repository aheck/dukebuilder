#include "mainwindow.h"
#include "mapeditor.h"
#include "settingsdialog.h"
#include "texturebrowserwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QPushButton>
#include <QStatusBar>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
// Classic Duke Nukem 3D / Atomic Edition Sector Effector lotags (tile 1).
// https://wiki.eduke32.com/wiki/Sector_Effector_Reference_Guide
constexpr const char *sectorEffectorLotags[] = {
    "Sector rotation",
    "Rotation pivot",
    "Earthquake",
    "Shot-triggered flicker",
    "Flickering lights",
    "Boss sector (unfinished)",
    "Subway engine",
    "Teleport",
    "Door lighting: up",
    "Door lighting: down",
    "Automatic door closing",
    "Swinging door",
    "Switched lighting",
    "Explosive sector",
    "Subway carriage",
    "Sliding door",
    "Reactor rotation (unfinished)",
    "Transport elevator",
    "Incremental vertical movement",
    "Explosion-triggered ceiling drop",
    "Stretching bridge",
    "Dropping floor",
    "Teeth-door component",
    "One-way teleport exit",
    "Conveyor / current",
    "Piston ceiling",
    "Escalator (unfinished)",
    "Demo viewpoint",
    "Lightning generator",
    "Waves",
    "Shuttle train",
    "Moving floor",
    "Moving ceiling",
    "Quake debris",
    "Alternate conveyor (undocumented)",
    "Drill (unfinished)",
    "Projectile emitter",
};

// Sector tags are distinct from Sector Effector sprite tags.
// https://wiki.eduke32.com/wiki/Sector_Tag_Reference_Guide
constexpr struct {
    int tag;
    const char *meaning;
} sectorLotags[] = {
    {0, "Normal sector"},
    {1, "Water surface"},
    {2, "Submerged area"},
    {3, "Cycloid Emperor movement area"},
    {9, "Star Trek sliding doors"},
    {15, "Transport elevator"},
    {16, "Descending platform"},
    {17, "Ascending platform"},
    {18, "Descending elevator"},
    {19, "Ascending elevator"},
    {20, "Door in ceiling"},
    {21, "Door in floor"},
    {22, "Vertically splitting door"},
    {23, "Hinged door"},
    {25, "Door sliding sideways"},
    {26, "Star Trek split door"},
    {27, "Stretching bridge"},
    {28, "Dropping floor / ceiling"},
    {29, "Teeth-door component"},
    {30, "Bridge rotation and elevation"},
    {31, "Shuttle train"},
    {10000, "Play sound 0 once (10000 + sound ID)"},
    {32767, "Secret area"},
    {65534, "Level exit with message"},
    {65535, "Immediate level exit"},
};

class TexturePropertyEditor final : public QWidget
{
public:
    explicit TexturePropertyEditor(QWidget *parent)
        : QWidget(parent)
        , value(new QLineEdit(this))
        , browse(new QPushButton("...", this))
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        browse->setFixedWidth(28);
        browse->setToolTip("Choose texture");
        layout->addWidget(value, 1);
        layout->addWidget(browse);
    }

    QLineEdit *value;
    QPushButton *browse;
};

class PropertyValueDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void setTextureChooser(
        std::function<std::optional<int>(std::optional<int>)> chooser)
    {
        m_textureChooser = std::move(chooser);
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        if (index.column() != 1) {
            return nullptr;
        }
        const auto property = static_cast<MapEditor::Property>(
            index.siblingAtColumn(0).data(Qt::UserRole).toInt());
        if (property == MapEditor::Property::Lotag
            || property == MapEditor::Property::SectorLotag
            || property == MapEditor::Property::WallLotag) {
            auto *editor = new QComboBox(parent);
            editor->setEditable(true);
            editor->setInsertPolicy(QComboBox::NoInsert);
            editor->setMinimumContentsLength(12);
            editor->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            if (property == MapEditor::Property::WallLotag) {
                // Wall tags are texture-dependent channels or sound IDs, not sector effects.
                // https://github.com/jonof/jfduke3d/blob/master/src/sector.c
                editor->setToolTip("Wall lotags depend on the texture: switches, doors and "
                                   "forcefields use matching channel numbers; mirrors use sound IDs. "
                                   "Enter -32768 to 32767, or 65535 for an exit switch.");
                editor->addItem("0 - No tag", 0);
                editor->addItem("1 - Channel 1 (switch / door / forcefield example)", 1);
                editor->addItem("252 - Mirror: play sound 252 on use", 252);
                editor->addItem("-1 (65535) - Switch / door: end level", -1);
            } else if (property == MapEditor::Property::SectorLotag) {
                editor->setToolTip("Duke 3D sector lotags. Enter an integer from 0 to 65535. "
                                   "For a one-time sound, enter 10000 + sound ID (10xxx).");
                for (const auto &entry : sectorLotags) {
                    editor->addItem(QString::number(entry.tag) + " - " + entry.meaning,
                                    entry.tag);
                }
            } else {
                editor->setToolTip("Sector Effector (tile 1) meanings. Other sprites use "
                               "lotags differently. Enter any integer from -32768 to 32767.");
                int tag = 0;
                for (const char *meaning : sectorEffectorLotags) {
                    editor->addItem(QString::number(tag) + " - " + meaning, tag);
                    ++tag;
                }
            }
            const auto commit = [this, editor, index = QPersistentModelIndex(index)] {
                if (index.isValid()) {
                    emit const_cast<PropertyValueDelegate *>(this)->commitData(editor);
                }
            };
            connect(editor, &QComboBox::activated, editor, commit, Qt::QueuedConnection);
            connect(editor->lineEdit(), &QLineEdit::editingFinished,
                    editor, commit, Qt::QueuedConnection);
            return editor;
        }
        if (property != MapEditor::Property::Texture
            && property != MapEditor::Property::FloorTexture
            && property != MapEditor::Property::CeilingTexture
            && property != MapEditor::Property::OverlayTexture) {
            return QStyledItemDelegate::createEditor(parent, option, index);
        }

        auto *editor = new TexturePropertyEditor(parent);
        connect(editor->value, &QLineEdit::editingFinished, editor,
                [this, editor, index = QPersistentModelIndex(index)] {
                    if (index.isValid()) {
                        emit const_cast<PropertyValueDelegate *>(this)->commitData(editor);
                    }
                });
        connect(editor->browse, &QPushButton::clicked, editor,
                [this, editor, index = QPersistentModelIndex(index)] {
                    bool valid = false;
                    const int currentTexture = editor->value->text().toInt(&valid);
                    if (!m_textureChooser) {
                        return;
                    }
                    const std::optional<int> texture = m_textureChooser(
                        valid ? std::optional<int>(currentTexture) : std::nullopt);
                    if (texture && index.isValid()) {
                        editor->value->setText(QString::number(*texture));
                        emit const_cast<PropertyValueDelegate *>(this)->commitData(editor);
                    }
                });
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (auto *lotagEditor = dynamic_cast<QComboBox *>(editor)) {
            const QSignalBlocker blocker(lotagEditor);
            const int tag = index.data(Qt::EditRole).toInt();
            const int entry = lotagEditor->findData(tag);
            lotagEditor->setCurrentIndex(entry);
            if (entry < 0) {
                lotagEditor->setEditText(QString::number(tag));
            }
            return;
        }
        if (auto *textureEditor = dynamic_cast<TexturePropertyEditor *>(editor)) {
            textureEditor->value->setText(index.data(Qt::EditRole).toString());
            return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (auto *lotagEditor = dynamic_cast<QComboBox *>(editor)) {
            const QString text = lotagEditor->currentText().trimmed();
            const int entry = lotagEditor->findText(text, Qt::MatchExactly);
            bool valid = false;
            int tag = entry >= 0 ? lotagEditor->itemData(entry).toInt(&valid)
                                       : text.toInt(&valid);
            const auto property = static_cast<MapEditor::Property>(
                index.siblingAtColumn(0).data(Qt::UserRole).toInt());
            if (property == MapEditor::Property::WallLotag && tag == 65535) tag = -1;
            const int maximum = property == MapEditor::Property::SectorLotag ? 65535 : 32767;
            if (valid && tag >= -32768 && tag <= maximum) {
                model->setData(index, QString::number(tag), Qt::EditRole);
            } else {
                setEditorData(editor, index);
            }
            return;
        }
        if (auto *textureEditor = dynamic_cast<TexturePropertyEditor *>(editor)) {
            model->setData(index, textureEditor->value->text(), Qt::EditRole);
            return;
        }
        QStyledItemDelegate::setModelData(editor, model, index);
    }

private:
    std::function<std::optional<int>(std::optional<int>)> m_textureChooser;
};
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Duke Builder");
    resize(1280, 800);

    auto *editor = new MapEditor(this);
    editor->setStatusCallback([this](const QString &message) {
        statusBar()->showMessage(message);
    });
    setCentralWidget(editor);

    auto *textureBrowserWindow = new TextureBrowserWindow(this);

    auto *propertiesDock = new QDockWidget("Properties", this);
    propertiesDock->setObjectName("PropertiesDock");
    propertiesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    propertiesDock->setMinimumWidth(220);

    auto *propertiesControl = new QTreeWidget(propertiesDock);
    propertiesControl->setObjectName("PropertiesControl");
    propertiesControl->setColumnCount(2);
    propertiesControl->setHeaderLabels({"Property", "Value"});
    propertiesControl->setRootIsDecorated(false);
    propertiesControl->setAlternatingRowColors(true);
    auto *propertyDelegate = new PropertyValueDelegate(propertiesControl);
    propertiesControl->setItemDelegate(propertyDelegate);
    propertiesControl->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    propertiesControl->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    auto *propertiesPanel = new QWidget(propertiesDock);
    auto *propertiesLayout = new QVBoxLayout(propertiesPanel);
    propertiesLayout->setContentsMargins(0, 0, 0, 0);
    propertiesLayout->addWidget(propertiesControl, 1);
    const auto createPreview = [](QWidget *parent, const QString &name) {
        auto *preview = new QLabel(parent);
        preview->setObjectName(name);
        preview->setFixedSize(160, 160);
        preview->setAlignment(Qt::AlignCenter);
        preview->setFrameShape(QFrame::StyledPanel);
        preview->setStyleSheet("background-color: #181a1f; color: #e2e7f0;");
        return preview;
    };
    auto *wallTexturePreview = createPreview(propertiesPanel, "WallTexturePreview");
    wallTexturePreview->hide();
    propertiesLayout->addWidget(wallTexturePreview, 0, Qt::AlignHCenter);
    auto *sectorTexturePreviews = new QWidget(propertiesPanel);
    sectorTexturePreviews->setObjectName("SectorTexturePreviews");
    auto *sectorPreviewLayout = new QHBoxLayout(sectorTexturePreviews);
    sectorPreviewLayout->setContentsMargins(4, 0, 4, 4);
    const auto addSectorPreview = [&](const QString &title, const QString &name) {
        auto *column = new QVBoxLayout;
        auto *label = new QLabel(title, sectorTexturePreviews);
        label->setAlignment(Qt::AlignCenter);
        column->addWidget(label);
        auto *preview = createPreview(sectorTexturePreviews, name);
        column->addWidget(preview, 0, Qt::AlignHCenter);
        sectorPreviewLayout->addLayout(column);
        return preview;
    };
    auto *ceilingTexturePreview = addSectorPreview("Ceiling texture", "CeilingTexturePreview");
    auto *floorTexturePreview = addSectorPreview("Floor texture", "FloorTexturePreview");
    sectorTexturePreviews->hide();
    propertiesLayout->addWidget(sectorTexturePreviews);
    const auto updateTexturePreview = [textureBrowserWindow](QLabel *preview, int tile,
                                                            const QString &surface) {
        preview->clear();
        preview->setToolTip(QString("%1 texture %2").arg(surface).arg(tile));
        const QImage image = textureBrowserWindow->textureImage(tile);
        if (image.isNull()) {
            preview->setText(QString("Texture %1\nUnavailable").arg(tile));
        } else {
            preview->setPixmap(QPixmap::fromImage(image).scaled(
                156, 156, Qt::KeepAspectRatio, Qt::FastTransformation));
        }
    };
    propertiesDock->setWidget(propertiesPanel);
    addDockWidget(Qt::LeftDockWidgetArea, propertiesDock);

    connect(propertiesControl, &QTreeWidget::itemChanged, editor,
            [editor](QTreeWidgetItem *item, int column) {
                if (column != 1) {
                    return;
                }
                bool valid = false;
                const qreal value = item->text(1).toDouble(&valid);
                if (!valid) {
                    editor->setSelectedProperty(
                        static_cast<MapEditor::Property>(
                            item->data(0, Qt::UserRole).toInt()),
                        std::numeric_limits<qreal>::quiet_NaN());
                    return;
                }
                editor->setSelectedProperty(
                    static_cast<MapEditor::Property>(
                        item->data(0, Qt::UserRole).toInt()),
                    value);
            });

    editor->setPropertiesCallback(
        [propertiesControl, propertyDelegate, editor, wallTexturePreview, sectorTexturePreviews,
         ceilingTexturePreview, floorTexturePreview, updateTexturePreview](std::optional<MapEditor::SelectionProperties> properties) {
            const QSignalBlocker blocker(propertiesControl);
            // Clearing the rows can finish an edit while its widget is being retired.
            const QSignalBlocker delegateBlocker(propertyDelegate);
            propertiesControl->clear();
            wallTexturePreview->clear();
            wallTexturePreview->hide();
            sectorTexturePreviews->hide();
            if (!properties) {
                return;
            }
            const auto number = [](qreal value) {
                return qFuzzyCompare(value, std::round(value))
                    ? QString::number(value, 'f', 0)
                    : QString::number(value, 'f', 2);
            };
            const auto addProperty = [propertiesControl](
                                         const QString &name, const QString &value,
                                         MapEditor::Property property) {
                auto *item = new QTreeWidgetItem(propertiesControl, {name, value});
                item->setFlags(item->flags() | Qt::ItemIsEditable);
                item->setData(0, Qt::UserRole, static_cast<int>(property));
            };
            const auto addTextureProperty = [&](const QString &name, int texture,
                                                MapEditor::Property property) {
                addProperty(name, QString::number(texture), property);
                propertiesControl->openPersistentEditor(
                    propertiesControl->topLevelItem(
                        propertiesControl->topLevelItemCount() - 1), 1);
            };
            if (properties->wall) {
                const auto &wall = *properties->wall;
                auto *sideRow = new QTreeWidgetItem(propertiesControl, {"Side", ""});
                auto *sideChooser = new QComboBox(propertiesControl);
                const auto addSide = [&](const QString &name,
                                         std::optional<MapDocument::SectorId> sector, bool reversed) {
                    if (sector) sideChooser->addItem(name + " - Sector " + QString::number(*sector), reversed);
                };
                addSide("Front", wall.forwardSector, false);
                addSide("Back", wall.reverseSector, true);
                if (sideChooser->count() == 0) sideChooser->addItem("Front", false);
                sideChooser->setCurrentIndex(sideChooser->findData(wall.reversed));
                sideChooser->setEnabled(sideChooser->count() > 1);
                propertiesControl->setItemWidget(sideRow, 1, sideChooser);
                const QPersistentModelIndex sideIndex = propertiesControl->model()->index(0, 1);
                connect(sideChooser, &QComboBox::activated, sideChooser,
                        [editor, sideChooser, sideIndex](int index) {
                            if (sideIndex.isValid()) editor->setSelectedWallSide(sideChooser->itemData(index).toBool());
                        }, Qt::QueuedConnection);
                const auto &values = wall.values;
                updateTexturePreview(wallTexturePreview, values.texture, "Wall");
                wallTexturePreview->show();
                addTextureProperty("Texture", values.texture, MapEditor::Property::Texture);
                addTextureProperty("Overlay texture", values.overlayTexture, MapEditor::Property::OverlayTexture);
                addProperty("Shade", QString::number(values.shade), MapEditor::Property::Shade);
                addProperty("Palette", QString::number(values.palette), MapEditor::Property::Palette);
                addProperty("X repeat", QString::number(values.xrepeat), MapEditor::Property::XRepeat);
                addProperty("Y repeat", QString::number(values.yrepeat), MapEditor::Property::YRepeat);
                addProperty("X panning", QString::number(values.xpanning), MapEditor::Property::XPanning);
                addProperty("Y panning", QString::number(values.ypanning), MapEditor::Property::YPanning);
                addProperty("Flags (cstat)", QString::number(values.cstat), MapEditor::Property::Cstat);
                addProperty("Hitag", QString::number(values.hitag), MapEditor::Property::Hitag);
                addProperty("Lotag", QString::number(values.lotag), MapEditor::Property::WallLotag);
                propertiesControl->openPersistentEditor(
                    propertiesControl->topLevelItem(propertiesControl->topLevelItemCount() - 1), 1);
                return;
            }
            if (properties->sector) {
                const auto &sector = *properties->sector;
                updateTexturePreview(ceilingTexturePreview, sector.ceilingTexture, "Ceiling");
                updateTexturePreview(floorTexturePreview, sector.floorTexture, "Floor");
                sectorTexturePreviews->show();
                addProperty("Floor Z", number(sector.floorz), MapEditor::Property::FloorZ);
                addProperty("Ceiling Z", number(sector.ceilingz), MapEditor::Property::CeilingZ);
                addTextureProperty("Floor texture", sector.floorTexture,
                                   MapEditor::Property::FloorTexture);
                addTextureProperty("Ceiling texture", sector.ceilingTexture,
                                   MapEditor::Property::CeilingTexture);
                addProperty("Hitag", QString::number(sector.hitag), MapEditor::Property::Hitag);
                addProperty("Lotag", QString::number(sector.lotag), MapEditor::Property::SectorLotag);
                propertiesControl->openPersistentEditor(
                    propertiesControl->topLevelItem(
                        propertiesControl->topLevelItemCount() - 1), 1);
                return;
            }
            addProperty("X", number(properties->x), MapEditor::Property::X);
            addProperty("Y", number(properties->y), MapEditor::Property::Y);
            addProperty("Z", number(properties->z), MapEditor::Property::Z);
            addProperty("Angle", number(std::clamp(properties->angle, 0.0, 360.0)),
                        MapEditor::Property::Angle);
            if (properties->texture) {
                addTextureProperty("Texture", *properties->texture, MapEditor::Property::Texture);
            }
            if (properties->hitag) {
                addProperty("Hitag", QString::number(*properties->hitag),
                            MapEditor::Property::Hitag);
            }
            if (properties->lotag) {
                addProperty("Lotag", QString::number(*properties->lotag),
                            MapEditor::Property::Lotag);
                propertiesControl->openPersistentEditor(
                    propertiesControl->topLevelItem(
                        propertiesControl->topLevelItemCount() - 1),
                    1);
            }
        });

    auto *editorToolBar = addToolBar("Editor");
    editorToolBar->setObjectName("EditorToolBar");
    editorToolBar->setMovable(true);

    auto *gridAction = editorToolBar->addAction("Grid");
    gridAction->setCheckable(true);
    gridAction->setChecked(editor->isGridVisible());
    gridAction->setShortcut(QKeySequence(Qt::Key_G));
    gridAction->setToolTip("Show or hide the grid (G)");
    connect(gridAction, &QAction::toggled, editor, &MapEditor::setGridVisible);

    editorToolBar->addSeparator();
    auto *sectorFillGroup = new QActionGroup(this);
    sectorFillGroup->setExclusive(true);
    const auto addSectorFillAction = [&](const QString &label, const QString &tooltip,
                                         MapEditor::SectorFill fill) {
        auto *action = editorToolBar->addAction(label);
        action->setCheckable(true);
        action->setToolTip(tooltip);
        sectorFillGroup->addAction(action);
        action->setChecked(editor->sectorFill() == fill);
        connect(action, &QAction::triggered, editor, [editor, fill] { editor->setSectorFill(fill); });
    };
    addSectorFillAction("Plain fill", "Disable sector textures", MapEditor::SectorFill::Plain);
    addSectorFillAction("Floor textures", "Fill sectors with their floor textures", MapEditor::SectorFill::Floor);
    addSectorFillAction("Ceiling textures", "Fill sectors with their ceiling textures", MapEditor::SectorFill::Ceiling);

    auto *fileMenu = menuBar()->addMenu("&File");

    auto *newMapAction = fileMenu->addAction("&New Map");
    newMapAction->setShortcut(QKeySequence::New);
    connect(newMapAction, &QAction::triggered, this, [editor] {
        editor->newMap();
    });

    auto *openMapAction = fileMenu->addAction("&Open Map");
    openMapAction->setShortcut(QKeySequence::Open);
    connect(openMapAction, &QAction::triggered, this, [this] {
        statusBar()->showMessage("Open Map is not implemented yet", 3000);
    });

    fileMenu->addSeparator();

    auto *saveAction = fileMenu->addAction("&Save");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this] {
        statusBar()->showMessage("Save is not implemented yet", 3000);
    });

    auto *saveAsAction = fileMenu->addAction("Save &As...");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this] {
        statusBar()->showMessage("Save As is not implemented yet", 3000);
    });

    fileMenu->addSeparator();

    auto *quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    auto *editMenu = menuBar()->addMenu("&Edit");
    auto *settingsAction = editMenu->addAction("&Settings");
    connect(settingsAction, &QAction::triggered, this, [this] {
        SettingsDialog dialog(this);
        dialog.exec();
    });

    auto *modeMenu = menuBar()->addMenu("&Mode");
    auto *modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);

    auto *modeLabel = new QLabel(this);
    modeLabel->setText("Mode: Draw");
    modeLabel->setContentsMargins(8, 0, 8, 0);

    QPixmap gridPixmap(16, 16);
    gridPixmap.fill(Qt::transparent);
    {
        QPainter painter(&gridPixmap);
        painter.setPen(QColor(130, 140, 155));
        for (int coordinate : {2, 7, 12}) {
            painter.drawLine(coordinate, 1, coordinate, 14);
            painter.drawLine(1, coordinate, 14, coordinate);
        }
    }
    const QIcon gridIcon(gridPixmap);

    auto *gridSizeCombo = new QComboBox(this);
    gridSizeCombo->setToolTip("Grid size");
    gridSizeCombo->setFixedWidth(76);
    for (int size : {1, 2, 4, 8, 16, 32}) {
        gridSizeCombo->addItem(gridIcon, QString::number(size), size);
    }
    gridSizeCombo->setCurrentText("16");
    editor->setGridSize(16);
    connect(gridSizeCombo, &QComboBox::currentIndexChanged, editor,
            [gridSizeCombo, editor](int index) {
                editor->setGridSize(gridSizeCombo->itemData(index).toReal());
            });

    statusBar()->addPermanentWidget(gridSizeCombo);

    auto *zoomCombo = new QComboBox(this);
    zoomCombo->setToolTip("Zoom level");
    zoomCombo->setEditable(true);
    zoomCombo->lineEdit()->setReadOnly(true);
    zoomCombo->setFixedWidth(76);
    for (int percent : {10, 25, 50, 75, 100, 200, 400, 800}) {
        zoomCombo->addItem(QString::number(percent) + "%", percent);
    }
    connect(zoomCombo, &QComboBox::textActivated, editor,
            [editor](const QString &text) {
                QString value = text;
                value.remove('%');
                editor->setZoomPercent(value.toDouble());
            });
    editor->setZoomCallback([zoomCombo](qreal percent) {
        const QSignalBlocker blocker(zoomCombo);
        const QString value = qFuzzyCompare(percent, std::round(percent))
            ? QString::number(std::round(percent), 'f', 0)
            : QString::number(percent, 'f', 1);
        zoomCombo->setEditText(value + "%");
    });
    statusBar()->addPermanentWidget(zoomCombo);
    statusBar()->addPermanentWidget(modeLabel);

    const auto addModeAction = [modeMenu, modeGroup, modeLabel, editor](
                                   const QString &name,
                                   const QKeySequence &shortcut,
                                   MapEditor::Mode mode,
                                   bool checked = false) {
        auto *action = modeMenu->addAction(name);
        action->setActionGroup(modeGroup);
        action->setCheckable(true);
        action->setChecked(checked);
        action->setShortcut(shortcut);
        QObject::connect(action, &QAction::triggered, modeLabel, [modeLabel, editor, name, mode] {
            editor->setMode(mode);
            modeLabel->setText("Mode: " + name);
        });
        return action;
    };

    addModeAction("Draw", QKeySequence(Qt::CTRL | Qt::Key_D), MapEditor::Mode::Draw, true);
    addModeAction("Lines", QKeySequence(Qt::Key_L), MapEditor::Mode::Lines);
    addModeAction("Vertices", QKeySequence(Qt::Key_V), MapEditor::Mode::Vertices);
    addModeAction("Sectors", QKeySequence(Qt::Key_S), MapEditor::Mode::Sectors);
    addModeAction("Sprites", QKeySequence(Qt::Key_T), MapEditor::Mode::Sprites);

    auto *toolsMenu = menuBar()->addMenu("&Tools");
    auto *textureBrowserAction = toolsMenu->addAction("&Texture Browser");
    propertyDelegate->setTextureChooser(
        [textureBrowserWindow](std::optional<int> currentTexture) -> std::optional<int> {
            const auto selection = textureBrowserWindow->chooseTexture(currentTexture);
            return selection ? std::optional<int>(selection->tile) : std::nullopt;
        });
    editor->setTextureSelector([textureBrowserWindow](std::optional<int> currentTexture)
                                   -> std::optional<MapEditor::SpriteTexture> {
        const std::optional<TextureBrowserWindow::Selection> selection
            = textureBrowserWindow->chooseTexture(currentTexture);
        if (!selection) {
            return std::nullopt;
        }
        return MapEditor::SpriteTexture{selection->tile, selection->image};
    });
    editor->setTextureResolver([textureBrowserWindow](int tile) {
        return textureBrowserWindow->textureImage(tile);
    });
    connect(textureBrowserAction, &QAction::triggered, this,
            [textureBrowserWindow] {
                textureBrowserWindow->browse();
            });

    auto *viewMenu = menuBar()->addMenu("&View");
    auto *propertyEditorAction = viewMenu->addAction("&Property Editor");
    propertyEditorAction->setCheckable(true);
    propertyEditorAction->setChecked(!propertiesDock->isHidden());
    connect(propertyEditorAction, &QAction::toggled,
            propertiesDock, &QDockWidget::setVisible);
    connect(propertiesDock, &QDockWidget::visibilityChanged,
            propertyEditorAction, &QAction::setChecked);

    auto *helpMenu = menuBar()->addMenu("&Help");
    auto *aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this,
            "About Duke Builder",
            "<h3>Duke Builder</h3>"
            "<p>A map-building application powered by Qt 6.</p>"
            "<p>Version 0.1.0</p>");
    });

    statusBar()->showMessage("Ready");
}
