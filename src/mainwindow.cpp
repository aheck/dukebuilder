#include "mainwindow.h"
#include "mapeditor.h"
#include "settingsdialog.h"
#include "texturebrowserwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QIconEngine>
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
#include <QProcess>
#include <QSettings>
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
enum class ToolbarSymbol { New, Open, Save, Grid, Plain, Floor, Ceiling };

// Draw at the requested size so toolbar icons remain crisp on high-DPI screens.
class ToolbarIconEngine final : public QIconEngine
{
public:
    explicit ToolbarIconEngine(ToolbarSymbol symbol) : m_symbol(symbol) {}
    QIconEngine *clone() const override { return new ToolbarIconEngine(m_symbol); }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        painter->save();
        painter->translate(rect.center().x() - rect.width() / 2.0,
                           rect.center().y() - rect.height() / 2.0);
        painter->scale(rect.width() / 24.0, rect.height() / 24.0);
        painter->setRenderHint(QPainter::Antialiasing);
        const auto palette = QApplication::palette();
        const auto group = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
        const QColor ink = palette.color(group, QPalette::ButtonText);
        const QColor accent = palette.color(group, QPalette::Highlight);
        painter->setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (m_symbol == ToolbarSymbol::New) {
            // Folded document with a plus sign.
            painter->drawPolygon(QPolygonF{QPointF(5, 3), QPointF(14, 3), QPointF(19, 8),
                                          QPointF(19, 21), QPointF(5, 21)});
            painter->drawPolyline(QPolygonF{QPointF(14, 3), QPointF(14, 8), QPointF(19, 8)});
            painter->setPen(QPen(accent, 2, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(QPointF(8, 14), QPointF(16, 14));
            painter->drawLine(QPointF(12, 10), QPointF(12, 18));
        } else if (m_symbol == ToolbarSymbol::Open) {
            // Open folder with a raised front flap.
            painter->drawPolygon(QPolygonF{QPointF(3, 19), QPointF(3, 5), QPointF(9, 5),
                                          QPointF(11, 8), QPointF(20, 8), QPointF(20, 19)});
            painter->setBrush(accent);
            painter->drawPolygon(QPolygonF{QPointF(3, 19), QPointF(6, 11),
                                          QPointF(22, 11), QPointF(19, 19)});
        } else if (m_symbol == ToolbarSymbol::Save) {
            // Floppy disk with a shutter and label.
            painter->setBrush(accent);
            painter->drawPolygon(QPolygonF{QPointF(4, 3), QPointF(17, 3), QPointF(21, 7),
                                          QPointF(21, 21), QPointF(3, 21), QPointF(3, 4)});
            painter->setBrush(palette.color(group, QPalette::Button));
            painter->drawRect(QRectF(7, 3, 9, 6));
            painter->drawRect(QRectF(7, 13, 10, 8));
            painter->drawLine(QPointF(10, 17), QPointF(14, 17));
        } else if (m_symbol == ToolbarSymbol::Grid) {
            for (int coordinate : {4, 12, 20}) {
                painter->drawLine(QPointF(coordinate, 4), QPointF(coordinate, 20));
                painter->drawLine(QPointF(4, coordinate), QPointF(20, coordinate));
            }
        } else if (m_symbol == ToolbarSymbol::Plain) {
            painter->setBrush(accent);
            painter->drawRoundedRect(QRectF(4, 4, 16, 16), 2, 2);
        } else {
            // Mirror the textured plane to distinguish the ceiling from the floor.
            if (m_symbol == ToolbarSymbol::Ceiling) {
                painter->translate(0, 24);
                painter->scale(1, -1);
            }
            painter->drawLine(QPointF(4, 5), QPointF(4, 20));
            painter->drawLine(QPointF(20, 5), QPointF(20, 20));
            painter->setBrush(accent);
            painter->drawPolygon(QPolygonF{QPointF(4, 20), QPointF(20, 20),
                                          QPointF(17, 12), QPointF(7, 12)});
            painter->drawLine(QPointF(12, 12), QPointF(12, 20));
            painter->drawLine(QPointF(5.5, 16), QPointF(18.5, 16));
        }
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }

private:
    ToolbarSymbol m_symbol;
};

QIcon toolbarIcon(ToolbarSymbol symbol)
{
    return QIcon(new ToolbarIconEngine(symbol));
}

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
    textureBrowserWindow->setUsedTexturesProvider([editor] { return editor->usedTextureTiles(); });

    auto *propertiesDock = new QDockWidget("Properties", this);
    propertiesDock->setObjectName("PropertiesDock");
    propertiesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    propertiesDock->setMinimumWidth(220);

    auto *propertiesControl = new QTreeWidget(propertiesDock);
    propertiesControl->setObjectName("PropertiesControl");
    propertiesControl->setColumnCount(2);
    propertiesControl->setHeaderLabels({"Property", "Value"});
    propertiesControl->setRootIsDecorated(true);
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
        auto *preview = new QPushButton(parent);
        preview->setObjectName(name);
        preview->setFixedSize(160, 160);
        preview->setIconSize(QSize(156, 156));
        preview->setCursor(Qt::PointingHandCursor);
        preview->setStyleSheet("background-color: #181a1f; color: #e2e7f0;");
        return preview;
    };
    auto *wallTexturePreview = createPreview(propertiesPanel, "WallTexturePreview");
    auto *wallTexturePreviews = new QWidget(propertiesPanel);
    auto *wallPreviewLayout = new QVBoxLayout(wallTexturePreviews);
    wallPreviewLayout->setContentsMargins(4, 0, 4, 4);
    auto *wallTextureLabel = new QLabel("Wall texture", wallTexturePreviews);
    wallTextureLabel->setAlignment(Qt::AlignCenter);
    wallPreviewLayout->addWidget(wallTextureLabel);
    wallPreviewLayout->addWidget(wallTexturePreview, 0, Qt::AlignHCenter);
    wallTexturePreviews->hide();
    propertiesLayout->addWidget(wallTexturePreviews);
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
    auto *floorTexturePreview = addSectorPreview("Floor texture", "FloorTexturePreview");
    auto *ceilingTexturePreview = addSectorPreview("Ceiling texture", "CeilingTexturePreview");
    sectorTexturePreviews->hide();
    propertiesLayout->addWidget(sectorTexturePreviews);
    const auto updateTexturePreview = [textureBrowserWindow](QPushButton *preview, int tile,
                                                            const QString &surface) {
        preview->setText({});
        preview->setIcon({});
        preview->setProperty("tile", tile);
        preview->setAccessibleName(surface + " texture");
        preview->setToolTip(QString("%1 texture %2 — Click to change").arg(surface).arg(tile));
        const QImage image = textureBrowserWindow->textureImage(tile);
        if (image.isNull()) {
            preview->setText(QString("Texture %1\nUnavailable").arg(tile));
        } else {
            preview->setIcon(QIcon(QPixmap::fromImage(image).scaled(
                156, 156, Qt::KeepAspectRatio, Qt::FastTransformation)));
        }
    };
    const auto connectPreview = [editor, textureBrowserWindow](QPushButton *preview,
                                                               MapEditor::Property property) {
        connect(preview, &QPushButton::clicked, editor, [editor, textureBrowserWindow, preview, property] {
            const auto selection = textureBrowserWindow->chooseTexture(preview->property("tile").toInt());
            if (selection) editor->setSelectedProperty(property, selection->tile);
        });
    };
    connectPreview(wallTexturePreview, MapEditor::Property::Texture);
    connectPreview(floorTexturePreview, MapEditor::Property::FloorTexture);
    connectPreview(ceilingTexturePreview, MapEditor::Property::CeilingTexture);
    propertiesDock->setWidget(propertiesPanel);
    addDockWidget(Qt::LeftDockWidgetArea, propertiesDock);

    connect(propertiesControl, &QTreeWidget::itemChanged, editor,
            [editor](QTreeWidgetItem *item, int column) {
                if (column != 1) {
                    return;
                }
                const int mask = item->data(0, Qt::UserRole + 1).toInt();
                if (mask && item->parent()) {
                    int flags = item->parent()->text(1).toInt();
                    flags = item->checkState(1) == Qt::Checked ? flags | mask : flags & ~mask;
                    editor->setSelectedProperty(static_cast<MapEditor::Property>(
                        item->data(0, Qt::UserRole).toInt()), flags);
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
        [propertiesControl, propertyDelegate, editor, wallTexturePreview, wallTexturePreviews, sectorTexturePreviews,
         ceilingTexturePreview, floorTexturePreview, updateTexturePreview](std::optional<MapEditor::SelectionProperties> properties) {
            const QSignalBlocker blocker(propertiesControl);
            // Clearing the rows can finish an edit while its widget is being retired.
            const QSignalBlocker delegateBlocker(propertyDelegate);
            propertiesControl->clear();
            wallTexturePreviews->hide();
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
                                         MapEditor::Property property, QTreeWidgetItem *parent = nullptr) {
                auto *item = parent ? new QTreeWidgetItem(parent, {name, value})
                                    : new QTreeWidgetItem(propertiesControl, {name, value});
                item->setFlags(item->flags() | Qt::ItemIsEditable);
                item->setData(0, Qt::UserRole, static_cast<int>(property));
                return item;
            };
            const auto addTextureProperty = [&](const QString &name, int texture,
                                                MapEditor::Property property) {
                addProperty(name, QString::number(texture), property);
                propertiesControl->openPersistentEditor(
                    propertiesControl->topLevelItem(
                        propertiesControl->topLevelItemCount() - 1), 1);
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
                group->setExpanded(false);
            };
            const auto addChoice = [&](const QString &name, int value, MapEditor::Property property,
                                       const std::vector<std::pair<int, QString>> &choices) {
                auto *row = new QTreeWidgetItem(propertiesControl, {name, ""});
                auto *combo = new QComboBox(propertiesControl);
                for (const auto &[id, label] : choices) combo->addItem(label, id);
                combo->setCurrentIndex(combo->findData(value));
                propertiesControl->setItemWidget(row, 1, combo);
                const QPersistentModelIndex index = propertiesControl->model()->index(
                    propertiesControl->topLevelItemCount() - 1, 1);
                connect(combo, &QComboBox::activated, combo,
                        [editor, combo, property, index](int selected) {
                            if (index.isValid()) editor->setSelectedProperty(property, combo->itemData(selected).toInt());
                        }, Qt::QueuedConnection);
            };
            const auto advancedGroup = [&]() {
                auto *group = new QTreeWidgetItem(propertiesControl, {"Advanced", ""});
                group->setFlags(Qt::ItemIsEnabled);
                group->setExpanded(false);
                return group;
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
                wallTexturePreviews->show();
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
                addProperty("Lotag", QString::number(values.lotag), MapEditor::Property::WallLotag);
                propertiesControl->openPersistentEditor(
                    propertiesControl->topLevelItem(propertiesControl->topLevelItemCount() - 1), 1);
                addProperty("Extra", QString::number(values.extra), MapEditor::Property::Extra, advancedGroup());
                return;
            }
            if (properties->sector) {
                const auto &sector = *properties->sector;
                auto *sectorNumber = new QTreeWidgetItem(propertiesControl,
                    {"Sector number", QString::number(*properties->sectorId)});
                sectorNumber->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
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
                addFlags("Ceiling flags", sector.ceilingstat, MapEditor::Property::CeilingStat,
                         {{1,"Parallax sky"},{2,"Sloped"},{4,"Swap texture axes"},{8,"Double texture scale"},
                          {16,"Flip X"},{32,"Flip Y"},{64,"Align to first wall"}});
                addProperty("Ceiling slope", QString::number(sector.ceilingheinum), MapEditor::Property::CeilingSlope);
                addProperty("Ceiling shade", QString::number(sector.ceilingshade), MapEditor::Property::CeilingShade);
                addProperty("Ceiling palette", QString::number(sector.ceilingpal), MapEditor::Property::CeilingPalette);
                addProperty("Ceiling X panning", QString::number(sector.ceilingxpanning), MapEditor::Property::CeilingXPanning);
                addProperty("Ceiling Y panning", QString::number(sector.ceilingypanning), MapEditor::Property::CeilingYPanning);
                addFlags("Floor flags", sector.floorstat, MapEditor::Property::FloorStat,
                         {{1,"Parallax sky"},{2,"Sloped"},{4,"Swap texture axes"},{8,"Double texture scale"},
                          {16,"Flip X"},{32,"Flip Y"},{64,"Align to first wall"}});
                addProperty("Floor slope", QString::number(sector.floorheinum), MapEditor::Property::FloorSlope);
                addProperty("Floor shade", QString::number(sector.floorshade), MapEditor::Property::FloorShade);
                addProperty("Floor palette", QString::number(sector.floorpal), MapEditor::Property::FloorPalette);
                addProperty("Floor X panning", QString::number(sector.floorxpanning), MapEditor::Property::FloorXPanning);
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
            if (properties->sprite) {
                const auto &sprite = *properties->sprite;
                addFlags("Flags (cstat)", sprite.cstat, MapEditor::Property::Cstat,
                         {{1,"Blocking"},{2,"Translucent"},{4,"Flip X"},{8,"Flip Y"},
                          {64,"One-sided"},{128,"Centered on Z"},{256,"Block hitscan"},
                          {512,"Reverse translucency"},{32768,"Invisible"}});
                addChoice("Alignment", (sprite.cstat >> 4) & 3, MapEditor::Property::Alignment,
                          {{0,"Face camera"},{1,"Wall aligned"},{2,"Floor aligned"}});
                addProperty("Shade", QString::number(sprite.shade), MapEditor::Property::Shade);
                addProperty("Palette", QString::number(sprite.palette), MapEditor::Property::Palette);
                addProperty("Collision size", QString::number(sprite.clipdist), MapEditor::Property::Clipdist);
                addProperty("X repeat", QString::number(sprite.xrepeat), MapEditor::Property::XRepeat);
                addProperty("Y repeat", QString::number(sprite.yrepeat), MapEditor::Property::YRepeat);
                addProperty("X offset", QString::number(sprite.xoffset), MapEditor::Property::XOffset);
                addProperty("Y offset", QString::number(sprite.yoffset), MapEditor::Property::YOffset);
                auto *advanced = advancedGroup();
                addProperty("Status", QString::number(sprite.statnum), MapEditor::Property::Status, advanced);
                addProperty("Owner", QString::number(sprite.owner), MapEditor::Property::Owner, advanced);
                addProperty("X velocity", QString::number(sprite.xvel), MapEditor::Property::XVelocity, advanced);
                addProperty("Y velocity", QString::number(sprite.yvel), MapEditor::Property::YVelocity, advanced);
                addProperty("Z velocity", QString::number(sprite.zvel), MapEditor::Property::ZVelocity, advanced);
                addProperty("Extra", QString::number(sprite.extra), MapEditor::Property::Extra, advanced);
            }
        });

    auto *editorToolBar = addToolBar("Editor");
    editorToolBar->setObjectName("EditorToolBar");
    editorToolBar->setMovable(true);
    editorToolBar->setIconSize(QSize(24, 24));
    editorToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    auto *gridAction = editorToolBar->addAction(toolbarIcon(ToolbarSymbol::Grid), "Grid");
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
        const auto symbol = fill == MapEditor::SectorFill::Floor ? ToolbarSymbol::Floor
            : fill == MapEditor::SectorFill::Ceiling ? ToolbarSymbol::Ceiling : ToolbarSymbol::Plain;
        auto *action = editorToolBar->addAction(toolbarIcon(symbol), label);
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

    const auto saveMap = [this, editor](bool saveAs) -> bool {
        // Commit any active property edit before taking the export snapshot.
        editor->setFocus();
        QString filename = m_mapFilename;
        if (saveAs || filename.isEmpty()) {
            QFileDialog dialog(this, "Save Build Map", filename, "Build maps (*.map *.MAP)");
            dialog.setAcceptMode(QFileDialog::AcceptSave);
            dialog.setFileMode(QFileDialog::AnyFile);
            dialog.setDefaultSuffix("map");
            if (dialog.exec() != QDialog::Accepted) return false;
            filename = dialog.selectedFiles().value(0);
            if (filename.isEmpty()) return false;
        }
        QString error;
        if (!editor->saveMap(filename, error)) {
            QMessageBox::warning(this, "Unable to save map", error);
            return false;
        }
        m_mapFilename = filename;
        setWindowFilePath(filename);
        setWindowTitle(QFileInfo(filename).fileName() + " - Duke Builder");
        statusBar()->showMessage("Saved " + QFileInfo(filename).fileName(), 5000);
        return true;
    };

    m_confirmUnsavedChanges = [this, editor, saveMap] {
        // Finish editing the property cell before checking for changes.
        editor->setFocus();
        if (!editor->hasUnsavedChanges()) return true;
        const auto answer = QMessageBox::warning(
            this, "Unsaved changes", "Do you want to save your changes?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Save) return saveMap(false);
        return answer == QMessageBox::Discard;
    };
    const auto confirmMapReplacement = m_confirmUnsavedChanges;

    auto *newMapAction = fileMenu->addAction("&New Map");
    newMapAction->setShortcut(QKeySequence::New);
    connect(newMapAction, &QAction::triggered, this, [this, editor, confirmMapReplacement] {
        if (!confirmMapReplacement()) return;
        editor->newMap();
        m_mapFilename.clear();
        setWindowFilePath({});
        setWindowTitle("Duke Builder");
    });

    auto *openMapAction = fileMenu->addAction("&Open Map");
    openMapAction->setShortcut(QKeySequence::Open);
    connect(openMapAction, &QAction::triggered, this, [this, editor, confirmMapReplacement] {
        if (!confirmMapReplacement()) return;
        const QString filename = QFileDialog::getOpenFileName(this, "Open Build Map", m_mapFilename,
                                                             "Build maps (*.map *.MAP);;All files (*)");
        if (filename.isEmpty()) return;
        QString error;
        if (!editor->openMap(filename, error)) {
            QMessageBox::warning(this, "Unable to open map", error);
            return;
        }
        m_mapFilename = filename;
        setWindowFilePath(filename);
        setWindowTitle(QFileInfo(filename).fileName() + " - Duke Builder");
        statusBar()->showMessage("Opened " + QFileInfo(filename).fileName(), 5000);
    });

    fileMenu->addSeparator();

    auto *saveAction = fileMenu->addAction("&Save");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [saveMap] { saveMap(false); });

    newMapAction->setIcon(toolbarIcon(ToolbarSymbol::New));
    newMapAction->setToolTip("New map (Ctrl+N)");
    openMapAction->setIcon(toolbarIcon(ToolbarSymbol::Open));
    openMapAction->setToolTip("Open map (Ctrl+O)");
    saveAction->setIcon(toolbarIcon(ToolbarSymbol::Save));
    saveAction->setToolTip("Save map (Ctrl+S)");
    editorToolBar->insertAction(gridAction, newMapAction);
    editorToolBar->insertAction(gridAction, openMapAction);
    editorToolBar->insertAction(gridAction, saveAction);
    editorToolBar->insertSeparator(gridAction);

    auto *saveAsAction = fileMenu->addAction("Save &As...");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [saveMap] { saveMap(true); });

    fileMenu->addSeparator();

    auto *quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

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

    auto *testingMenu = menuBar()->addMenu("T&esting");
    auto *runMapAction = testingMenu->addAction("Run in eDuke32");
    runMapAction->setShortcut(QKeySequence(Qt::Key_F9));
    connect(runMapAction, &QAction::triggered, this, [this, editor, saveMap] {
        const QString binary = QSettings().value("eduke32/binaryPath").toString();
        if (binary.isEmpty()) {
            QMessageBox::warning(this, "EDuke32 is not configured",
                                 "Set the EDuke32 binary path in Settings → EDuke32 first.");
            return;
        }
        // Run the current edits, requesting a filename for a new map.
        editor->setFocus();
        if ((m_mapFilename.isEmpty() || editor->hasUnsavedChanges()) && !saveMap(false)) return;

        const QFileInfo mapFile(m_mapFilename);
        const QFileInfo binaryFile(binary);
        QProcess process;
        process.setWorkingDirectory(binaryFile.absolutePath());
        process.setProgram("./" + binaryFile.fileName());
        process.setArguments({"-usecwd", "-nosetup", "-j", mapFile.absolutePath(), "-map", mapFile.fileName()});
        const auto shellQuote = [](QString value) {
            value.replace("'", "'\\''");
            return "'" + value + "'";
        };
        QStringList command{shellQuote(process.program())};
        for (const auto &argument : process.arguments()) command.append(shellQuote(argument));
        qInfo().noquote() << "Launching eDuke32: cd"
                          << shellQuote(process.workingDirectory()) << "&&" << command.join(' ');
        if (!process.startDetached()) {
            QMessageBox::warning(this, "Unable to run eDuke32", process.errorString());
            return;
        }
        statusBar()->showMessage("Started eDuke32 with " + mapFile.fileName(), 5000);
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_confirmUnsavedChanges && !m_confirmUnsavedChanges()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}
