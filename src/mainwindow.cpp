#include "mainwindow.h"
#include "shortcuthelp.h"
#include <QDialog>
#include "recovery.h"
#include "mapsave.h"
#include <QTimer>
#include "mapeditor.h"
#include "mapview3d.h"
#include "eduke32launch.h"
#include <QStackedWidget>
#include <QCursor>
#include "settingsdialog.h"
#include "texturebrowserwindow.h"
#include "grpfilemanagerwindow.h"
#include <QTemporaryFile>

#include "info.h"
#include "tags.h"
#include "spritelotags.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDir>
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
#include <QMimeData>
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
#include <QWindow>

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

class TextureValueEdit final : public QLineEdit
{
public:
    using QLineEdit::QLineEdit;
    std::function<void(int)> tileDropped;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasFormat("application/x-dukebuilder-tile")) {
            event->acceptProposedAction();
            return;
        }
        QLineEdit::dragEnterEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        bool valid = false;
        const int tile = QString::fromUtf8(
            event->mimeData()->data("application/x-dukebuilder-tile")).toInt(&valid);
        if (valid) {
            setText(QString::number(tile));
            if (tileDropped) tileDropped(tile);
            event->acceptProposedAction();
            return;
        }
        QLineEdit::dropEvent(event);
    }
};

class TexturePropertyEditor final : public QWidget
{
public:
    explicit TexturePropertyEditor(QWidget *parent)
        : QWidget(parent)
        , value(new TextureValueEdit(this))
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
            // Descriptive preset labels must not autocomplete over numeric input.
            editor->setCompleter(nullptr);
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
                const auto tile = index.siblingAtColumn(0).data(Qt::UserRole + 2);
                const auto tags = spriteLotags(tile.isValid() ? tile.toInt() : -1);
                editor->setToolTip(QString::fromUtf8(tags.description)
                    + " Standard Duke 3D / Atomic meanings; mods may redefine them. "
                      "Enter -32768 to 32767, or 65535 as an alias for -1.");
                editor->lineEdit()->setPlaceholderText("Enter lotag");
                for (const auto &entry : tags.presets) {
                    editor->addItem(QString::number(entry.tag) + " - " + entry.meaning, entry.tag);
                }
            }
            const auto commit = [this, editor, index = QPersistentModelIndex(index)] {
                if (index.isValid()) {
                    emit const_cast<PropertyValueDelegate *>(this)->commitData(editor);
                }
            };
            connect(editor, &QComboBox::activated, editor, commit, Qt::QueuedConnection);
            connect(editor->lineEdit(), &QLineEdit::editingFinished, editor, commit);
            return editor;
        }
        if (property != MapEditor::Property::Texture
            && property != MapEditor::Property::FloorTexture
            && property != MapEditor::Property::CeilingTexture
            && property != MapEditor::Property::OverlayTexture) {
            return QStyledItemDelegate::createEditor(parent, option, index);
        }

        auto *editor = new TexturePropertyEditor(parent);
        static_cast<TextureValueEdit *>(editor->value)->tileDropped =
            [this, editor, index = QPersistentModelIndex(index)](int) {
                if (index.isValid()) {
                    emit const_cast<PropertyValueDelegate *>(this)->commitData(editor);
                }
            };
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
            if ((property == MapEditor::Property::WallLotag
                 || property == MapEditor::Property::Lotag) && tag == 65535) {
                tag = -1;
            }
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
    m_recovery = std::make_unique<RecoveryFile>(RecoveryFile::newPath(RecoveryFile::directory()));
    setWindowTitle("Duke Builder");
    resize(1280, 800);

    auto *editor = new MapEditor(this);
    editor->setStatusCallback([this](const QString &message) {
        statusBar()->showMessage(message);
    });
    auto *views = new QStackedWidget(this);
    auto *view3D = new MapView3D(views);
    views->addWidget(editor);
    views->addWidget(view3D);
    setCentralWidget(views);


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
    auto *wallPreviewLayout = new QHBoxLayout(wallTexturePreviews);
    wallPreviewLayout->setContentsMargins(4, 0, 4, 4);
    auto *wallTextureLabel = new QLabel("Wall texture", wallTexturePreviews);
    wallTextureLabel->setAlignment(Qt::AlignCenter);
    auto *wallTextureColumn = new QVBoxLayout;
    wallTextureColumn->addWidget(wallTextureLabel);
    wallTextureColumn->addWidget(wallTexturePreview, 0, Qt::AlignHCenter);
    wallPreviewLayout->addLayout(wallTextureColumn);
    auto *oppositeTexturePanel = new QWidget(wallTexturePreviews);
    auto *oppositeTextureColumn = new QVBoxLayout(oppositeTexturePanel);
    oppositeTextureColumn->setContentsMargins(0, 0, 0, 0);
    auto *oppositeTextureLabel = new QLabel(oppositeTexturePanel);
    oppositeTextureLabel->setAlignment(Qt::AlignCenter);
    auto *oppositeTexturePreview = createPreview(oppositeTexturePanel, "OppositeWallTexturePreview");
    oppositeTextureColumn->addWidget(oppositeTextureLabel);
    oppositeTextureColumn->addWidget(oppositeTexturePreview, 0, Qt::AlignHCenter);
    wallPreviewLayout->addWidget(oppositeTexturePanel);
    oppositeTexturePanel->hide();
    wallTexturePreviews->hide();
    propertiesLayout->addWidget(wallTexturePreviews);
    auto *spriteTexturePreviews = new QWidget(propertiesPanel);
    spriteTexturePreviews->setObjectName("SpriteTexturePreviews");
    auto *spritePreviewLayout = new QVBoxLayout(spriteTexturePreviews);
    spritePreviewLayout->setContentsMargins(4, 0, 4, 4);
    auto *spriteTextureLabel = new QLabel("Sprite texture", spriteTexturePreviews);
    spriteTextureLabel->setAlignment(Qt::AlignCenter);
    spritePreviewLayout->addWidget(spriteTextureLabel);
    auto *spriteTexturePreview = createPreview(spriteTexturePreviews, "SpriteTexturePreview");
    spritePreviewLayout->addWidget(spriteTexturePreview, 0, Qt::AlignHCenter);
    spriteTexturePreviews->hide();
    propertiesLayout->addWidget(spriteTexturePreviews);
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
    connectPreview(oppositeTexturePreview, MapEditor::Property::OppositeTexture);
    connectPreview(spriteTexturePreview, MapEditor::Property::Texture);
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

    auto *reorientGridAction = new QAction("Reorient Grid to Line", this);
    reorientGridAction->setShortcut(QKeySequence(Qt::Key_F11));
    reorientGridAction->setEnabled(false);
    connect(reorientGridAction, &QAction::triggered, editor, &MapEditor::reorientGridToSelectedLine);
    auto *resetGridAction = new QAction("Reset Grid Orientation", this);
    resetGridAction->setShortcut(QKeySequence(Qt::Key_F12));
    connect(resetGridAction, &QAction::triggered, editor, &MapEditor::resetGridOrientation);

    // Keep expansion state outside the items: property edits rebuild the tree,
    // sometimes through an intermediate empty selection. Rebuild signals are
    // blocked below, so only user expansion/collapse changes this state.
    const auto rememberExpansion = [propertiesControl](QTreeWidgetItem *item, bool expanded) {
        QStringList groups = propertiesControl->property("expandedGroups").toStringList();
        groups.removeAll(item->text(0));
        if (expanded) { groups.append(item->text(0)); }
        propertiesControl->setProperty("expandedGroups", groups);
    };
    connect(propertiesControl, &QTreeWidget::itemExpanded, propertiesControl,
            [rememberExpansion](QTreeWidgetItem *item) { rememberExpansion(item, true); });
    connect(propertiesControl, &QTreeWidget::itemCollapsed, propertiesControl,
            [rememberExpansion](QTreeWidgetItem *item) { rememberExpansion(item, false); });

    editor->setPropertiesCallback(
        [propertiesControl, propertyDelegate, editor, wallTexturePreview, wallTexturePreviews, sectorTexturePreviews,
         spriteTexturePreview, spriteTexturePreviews, reorientGridAction,
         wallTextureLabel, oppositeTexturePanel, oppositeTextureLabel, oppositeTexturePreview,
         ceilingTexturePreview, floorTexturePreview, updateTexturePreview, textureBrowserWindow](std::optional<MapEditor::SelectionProperties> properties) {
            const QSignalBlocker blocker(propertiesControl);
            reorientGridAction->setEnabled(properties && properties->wall.has_value());
            // Clearing the rows can finish an edit while its widget is being retired.
            const QSignalBlocker delegateBlocker(propertyDelegate);
            propertiesControl->clear();
            wallTexturePreviews->hide();
            spriteTexturePreviews->hide();
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
                group->setExpanded(propertiesControl->property("expandedGroups")
                                       .toStringList().contains(group->text(0)));
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
                group->setExpanded(propertiesControl->property("expandedGroups")
                                       .toStringList().contains(group->text(0)));
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
                auto *lengthRow = new QTreeWidgetItem(propertiesControl,
                    {"Length", number(wall.length)});
                lengthRow->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                lengthRow->setToolTip(1, "Line length in map units");
                const QString selectedSide = wall.reversed ? "Back wall" : "Front wall";
                wallTextureLabel->setText(wall.oppositeTexture ? selectedSide + " texture" : "Wall texture");
                updateTexturePreview(wallTexturePreview, values.texture, selectedSide);
                oppositeTexturePanel->setVisible(wall.oppositeTexture.has_value());
                if (wall.oppositeTexture) {
                    const QString oppositeSide = wall.reversed ? "Front wall" : "Back wall";
                    oppositeTextureLabel->setText(oppositeSide + " texture");
                    updateTexturePreview(oppositeTexturePreview, *wall.oppositeTexture, oppositeSide);
                }
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
                updateTexturePreview(spriteTexturePreview, *properties->texture, "Sprite");
                spriteTexturePreviews->show();
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
                propertiesControl->openPersistentEditor(row, 1);
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
                auto paletteNumbers = textureBrowserWindow->paletteNumbers();
                paletteNumbers.insert(sprite.palette);
                std::vector<std::pair<int, QString>> paletteChoices;
                for (int palette : paletteNumbers) {
                    paletteChoices.emplace_back(palette, QString::number(palette));
                }
                addChoice("Palette", sprite.palette, MapEditor::Property::Palette, paletteChoices);
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

    const auto rememberMap = [](const QString &filename) {
        const QFileInfo file(filename);
        const QString path = file.canonicalFilePath().isEmpty()
            ? file.absoluteFilePath() : file.canonicalFilePath();
        QSettings settings;
        settings.setValue("files/lastMapDirectory", file.absolutePath());
        QStringList recent = settings.value("files/recentMaps").toStringList();
        recent.removeAll(path);
        recent.prepend(path);
        while (recent.size() > 10) recent.removeLast();
        settings.setValue("files/recentMaps", recent);
    };

    const auto mapDirectory = [this] {
        const QSettings settings;
        const QString saved = settings.value("files/lastMapDirectory").toString();
        if (!saved.isEmpty() && QDir(saved).exists()) return saved;
        if (!m_mapFilename.isEmpty() && QFileInfo(m_mapFilename).dir().exists()) {
            return QFileInfo(m_mapFilename).absolutePath();
        }
        return QDir::homePath();
    };

    const auto saveMap = [this, editor, rememberMap, mapDirectory](bool saveAs) -> bool {
        // Commit any active property edit before taking the export snapshot.
        editor->setFocus();
        QString filename = m_mapFilename;
        if (saveAs || filename.isEmpty()) {
            QFileDialog dialog(this, "Save Build Map", mapDirectory(), "Build maps (*.map *.MAP)");
            if (!filename.isEmpty()) dialog.selectFile(QFileInfo(filename).fileName());
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
        m_recovery->remove();
        m_recoveryOrigin.clear();
        m_mapFilename = filename;
        rememberMap(filename);
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
    const auto confirmMapReplacement = [this, view3D] {
        if (!m_confirmUnsavedChanges()) { return false; }
        if (view3D->leave3D) { view3D->leave3D(); }
        return true;
    };

    auto *newMapAction = fileMenu->addAction("&New Map");
    newMapAction->setShortcut(QKeySequence::New);
    connect(newMapAction, &QAction::triggered, this, [this, editor, confirmMapReplacement] {
        if (!confirmMapReplacement()) return;
        m_recovery->remove();
        m_recoveryOrigin.clear();
        editor->newMap();
        m_mapFilename.clear();
        setWindowFilePath({});
        setWindowTitle("Duke Builder");
    });

    auto *openMapAction = fileMenu->addAction("&Open Map");
    openMapAction->setShortcut(QKeySequence::Open);
    const auto openMap = [this, editor, rememberMap](const QString &filename) {
        QString error;
        if (!editor->openMap(filename, error)) {
            QMessageBox::warning(this, "Unable to open map", error);
            return;
        }
        m_recovery->remove();
        m_recoveryOrigin.clear();
        m_mapFilename = filename;
        rememberMap(filename);
        setWindowFilePath(filename);
        setWindowTitle(QFileInfo(filename).fileName() + " - Duke Builder");
        statusBar()->showMessage("Opened " + QFileInfo(filename).fileName(), 5000);
    };
    connect(openMapAction, &QAction::triggered, this, [this, openMap, confirmMapReplacement, mapDirectory] {
        if (!confirmMapReplacement()) return;
        const QString filename = QFileDialog::getOpenFileName(this, "Open Build Map", mapDirectory(),
                                                             "Build maps (*.map *.MAP);;All files (*)");
        if (!filename.isEmpty()) openMap(filename);
    });

    auto *recentMenu = fileMenu->addMenu("&Recent Files");
    recentMenu->setToolTipsVisible(true);
    connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu, openMap, confirmMapReplacement] {
        recentMenu->clear();
        const QStringList recent = QSettings().value("files/recentMaps").toStringList();
        if (recent.isEmpty()) {
            recentMenu->addAction("No recent files")->setEnabled(false);
            return;
        }
        for (int index = 0; index < recent.size(); ++index) {
            const QString filename = recent[index];
            // Full paths distinguish maps with identical filenames.
            auto *action = recentMenu->addAction(
                QString("%1. %2").arg(index + 1).arg(QString(filename).replace('&', "&&")));
            action->setToolTip(filename);
            connect(action, &QAction::triggered, this, [openMap, confirmMapReplacement, filename] {
                if (confirmMapReplacement()) openMap(filename);
            });
        }
        recentMenu->addSeparator();
        auto *clearAction = recentMenu->addAction("&Clear Recent Files");
        connect(clearAction, &QAction::triggered, this, [] {
            QSettings().remove("files/recentMaps");
        });
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
    auto *undoAction = editMenu->addAction("Undo");
    auto *redoAction = editMenu->addAction("Redo");
    undoAction->setShortcuts(QKeySequence::Undo);
    redoAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y),
                              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    const auto updateHistory = [=] {
        const auto *stack = editor->undoStack();
        undoAction->setEnabled(stack->canUndo());
        redoAction->setEnabled(stack->canRedo());
        undoAction->setText(stack->canUndo() ? "Undo " + stack->undoText() : "Undo");
        redoAction->setText(stack->canRedo() ? "Redo " + stack->redoText() : "Redo");
    };
    connect(editor->undoStack(), &QUndoStack::indexChanged, this, updateHistory);
    connect(editor->undoStack(), &QUndoStack::undoTextChanged, this, updateHistory);
    connect(editor->undoStack(), &QUndoStack::redoTextChanged, this, updateHistory);
    connect(undoAction, &QAction::triggered, this, [editor] { if (editor->isVisible()) editor->setFocus(); editor->undo(); });
    connect(redoAction, &QAction::triggered, this, [editor] { if (editor->isVisible()) editor->setFocus(); editor->redo(); });
    updateHistory();
    editMenu->addSeparator();
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
    gridSizeCombo->setToolTip("Grid and snap spacing in map units ([ / ] to change)");
    gridSizeCombo->setFixedWidth(90);
    for (int size : {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096}) {
        gridSizeCombo->addItem(gridIcon, QString::number(size), size);
    }
    gridSizeCombo->setCurrentText("256");
    editor->setGridSize(256);
    connect(gridSizeCombo, &QComboBox::currentIndexChanged, editor,
            [gridSizeCombo, editor](int index) {
                editor->setGridSize(gridSizeCombo->itemData(index).toReal());
            });

    auto *cursorStatusLabel = new QLabel(this);
    cursorStatusLabel->setObjectName("cursorStatusLabel");
    cursorStatusLabel->setContentsMargins(8, 0, 8, 0);
    cursorStatusLabel->setToolTip("2D cursor coordinates and current drawing measurements");
    cursorStatusLabel->setMinimumWidth(cursorStatusLabel->fontMetrics().horizontalAdvance("X -131072  Y -131072") + 16);
    editor->setCursorStatusCallback([cursorStatusLabel](const QString &message) {
        cursorStatusLabel->setText(message);
    });
    statusBar()->addPermanentWidget(cursorStatusLabel);
    connect(views, &QStackedWidget::currentChanged, cursorStatusLabel, [=](int) {
        cursorStatusLabel->setVisible(views->currentWidget() == editor);
    });

    editorToolBar->addSeparator();
    auto *difficultyLabel = new QLabel("Testing:", editorToolBar);
    difficultyLabel->setContentsMargins(6, 0, 0, 0);
    editorToolBar->addWidget(difficultyLabel);
    auto *difficultyCombo = new QComboBox(editorToolBar);
    difficultyCombo->setObjectName("gameStartDifficultyCombo");
    difficultyCombo->setToolTip("Difficulty used when starting the map in eDuke32");
    difficultyCombo->setFixedWidth(155);
    difficultyCombo->addItem("Piece of Cake", 0);
    difficultyCombo->addItem("Let's Rock", 1);
    difficultyCombo->addItem("Come Get Some", 2);
    difficultyCombo->addItem("Damn I'm Good", 3);
    editorToolBar->addWidget(difficultyCombo);
    connect(difficultyCombo, &QComboBox::currentIndexChanged, editor,
            [difficultyCombo, editor](int index) {
                editor->setGameStartDifficulty(difficultyCombo->itemData(index).toInt());
            });

    auto *enemiesCombo = new QComboBox(editorToolBar);
    enemiesCombo->setObjectName("gameStartEnemiesCombo");
    enemiesCombo->setToolTip("Choose whether enemies are enabled when starting the map in eDuke32");
    enemiesCombo->setFixedWidth(122);
    enemiesCombo->addItem("Enemies: On", true);
    enemiesCombo->addItem("Enemies: Off", false);
    editorToolBar->addWidget(enemiesCombo);
    connect(enemiesCombo, &QComboBox::currentIndexChanged, editor,
            [enemiesCombo, editor](int index) {
                editor->setGameStartEnemiesEnabled(enemiesCombo->itemData(index).toBool());
            });
    statusBar()->addPermanentWidget(gridSizeCombo);

    const auto addGridShortcut = [this, gridSizeCombo](const QString &name, int key, int step) {
        auto *action = new QAction(name, this);
        action->setShortcut(QKeySequence(key));
        addAction(action);
        connect(action, &QAction::triggered, gridSizeCombo, [gridSizeCombo, step] {
            gridSizeCombo->setCurrentIndex(std::clamp(
                gridSizeCombo->currentIndex() + step, 0, gridSizeCombo->count() - 1));
        });
    };
    addGridShortcut("Decrease grid spacing", Qt::Key_BracketLeft, -1);
    addGridShortcut("Increase grid spacing", Qt::Key_BracketRight, 1);

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

    auto *help3DLabel = new QLabel(
        "WASD move · Mouse look · Shift faster · "
        "Wheel height · Ctrl+wheel shade · Alt+wheel slope · "
        "Arrows pan · Shift+Arrows scale · A align selection · H highlight · Esc clear/release · Q 2D", this);
    help3DLabel->setObjectName("help3DStatusLabel");
    help3DLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    help3DLabel->setToolTip(help3DLabel->text());
    help3DLabel->setContentsMargins(8, 0, 8, 0);
    auto *surfaceStatusLabel = new QLabel("Shade: —", this);
    surfaceStatusLabel->setObjectName("surfaceStatusLabel");
    surfaceStatusLabel->setContentsMargins(8, 0, 8, 0);
    // Keep hover/selection text from resizing the viewport during mouse-look.
    surfaceStatusLabel->setFixedWidth(
        surfaceStatusLabel->fontMetrics().horizontalAdvance("Selected Ceiling shade: -128") + 16);
    surfaceStatusLabel->setToolTip("Shade of the selected surface, or the highlighted surface when nothing is selected.");
    view3D->surfaceStatusChanged = [surfaceStatusLabel](const QString &text) {
        surfaceStatusLabel->setText(text);
    };
    statusBar()->addPermanentWidget(help3DLabel, 1);
    statusBar()->addPermanentWidget(surfaceStatusLabel);
    help3DLabel->hide();
    surfaceStatusLabel->hide();
    // Retain the normal single-row height when the 2D combo boxes are hidden.
    statusBar()->setMinimumHeight(statusBar()->sizeHint().height());
    connect(views, &QStackedWidget::currentChanged, help3DLabel, [=](int) {
        const bool in3D = views->currentWidget() == view3D;
        help3DLabel->setVisible(in3D);
        surfaceStatusLabel->setVisible(in3D);
        gridSizeCombo->setVisible(!in3D);
        zoomCombo->setVisible(!in3D);
        modeLabel->setVisible(!in3D);
    });

    const auto addModeAction = [modeMenu, modeGroup, modeLabel, editor](
                                   const QString &name,
                                   const QKeySequence &shortcut,
                                   MapEditor::Mode mode,
                                   bool checked = false) {
        auto *action = modeMenu->addAction(name);
        action->setActionGroup(modeGroup);
        action->setData(static_cast<int>(mode));
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
    auto *grpFileManagerAction = toolsMenu->addAction("GRP File Manager");
    connect(grpFileManagerAction, &QAction::triggered, this, [this, editor, confirmMapReplacement] {
        if (!m_grpFileManager) {
            m_grpFileManager = std::make_unique<GrpFileManagerWindow>(this);
            m_grpFileManager->openMapRequested = [this, editor, confirmMapReplacement](const QString &name, const QByteArray &data) {
                QTemporaryFile file;
                if (!file.open() || file.write(data) != data.size() || !file.flush()) {
                    QMessageBox::warning(this, "Open archive map", "Unable to prepare the map for opening.");
                    return;
                }
                QString error;
                MapDocument check;
                if (!check.openMap(file.fileName(), error)) {
                    QMessageBox::warning(this, "Open archive map", error);
                    return;
                }
                if (!confirmMapReplacement()) {
                    return;
                }
                if (!editor->openMap(file.fileName(), error, true)) {
                    QMessageBox::warning(this, "Open archive map", error);
                    return;
                }
                m_recovery->remove();
                m_recoveryOrigin.clear();
                m_mapFilename.clear();
                setWindowFilePath({});
                setWindowTitle(name + " (archive copy) - Duke Builder");
                statusBar()->showMessage("Opened archive copy; use Save As to save it separately.", 5000);
                raise();
                activateWindow();
                editor->setFocus();
            };
        }
        m_grpFileManager->show();
        m_grpFileManager->raise();
        m_grpFileManager->activateWindow();
    });
    auto *joinSectorsAction = toolsMenu->addAction("Join Sectors");
    joinSectorsAction->setShortcut(QKeySequence(Qt::Key_J));
    joinSectorsAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    joinSectorsAction->setAutoRepeat(false);
    joinSectorsAction->setEnabled(false);
    editor->addAction(joinSectorsAction);
    editor->joinAvailabilityChanged = [joinSectorsAction](bool enabled) {
        joinSectorsAction->setEnabled(enabled);
    };
    connect(joinSectorsAction, &QAction::triggered, editor, &MapEditor::joinSelectedSectors);

    auto *stickSpriteAction = toolsMenu->addAction("Stick Sprite to Wall");
    stickSpriteAction->setShortcut(QKeySequence(Qt::Key_O));
    stickSpriteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    stickSpriteAction->setAutoRepeat(false);
    editor->addAction(stickSpriteAction);
    connect(stickSpriteAction, &QAction::triggered, this, [editor, view3D] {
        if (view3D->isVisible()) { view3D->stickSpriteToWall(); }
        else { editor->stickSelectedSpriteToWall(); }
    });
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
    editor->setTextureResolver([textureBrowserWindow](int tile, int palette) {
        return textureBrowserWindow->textureImage(tile, palette);
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
#ifdef Q_OS_WIN
        // Windows resolves relative programs against the parent's directory,
        // not QProcess's working directory. Let Qt quote the absolute executable
        // and individual arguments for CreateProcess, including paths with spaces.
        process.setProgram(QDir::toNativeSeparators(binaryFile.absoluteFilePath()));
#else
        process.setProgram("./" + binaryFile.fileName());
#endif
        process.setArguments(eduke32MapArguments(
            mapFile.absolutePath(), mapFile.fileName(), editor->gameStartDifficulty(),
            editor->gameStartEnemiesEnabled()));
#ifdef Q_OS_WIN
        qInfo() << "Launching eDuke32:" << process.program()
                << "arguments:" << process.arguments()
                << "working directory:" << process.workingDirectory();
#else
        const auto shellQuote = [](QString value) {
            value.replace("'", "'\\''");
            return "'" + value + "'";
        };
        QStringList command{shellQuote(process.program())};
        for (const auto &argument : process.arguments()) command.append(shellQuote(argument));
        qInfo().noquote() << "Launching eDuke32: cd"
                          << shellQuote(process.workingDirectory()) << "&&" << command.join(' ');
#endif
        if (!process.startDetached()) {
            QMessageBox::warning(this, "Unable to run eDuke32", process.errorString());
            return;
        }
        statusBar()->showMessage("Started eDuke32 with " + mapFile.fileName(), 5000);
    });

    auto *viewMenu = menuBar()->addMenu("&View");
    auto *spritesAction = viewMenu->addAction("&Sprites");
    spritesAction->setCheckable(true);
    spritesAction->setChecked(true);
    spritesAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_T));
    spritesAction->setAutoRepeat(false);
    spritesAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    spritesAction->setToolTip("Show sprites in 2D; sprites are always visible in Sprites mode");
    editor->addAction(spritesAction);
    connect(spritesAction, &QAction::toggled, editor, &MapEditor::setSpritesVisible);
    auto *toggle3D = viewMenu->addAction("3D Mode");
    toggle3D->setCheckable(true);
    toggle3D->setShortcut(QKeySequence(Qt::Key_Q));
    toggle3D->setAutoRepeat(false);
    toggle3D->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    views->addAction(toggle3D);
    const auto leave3D = [=] {
        view3D->stop();
        views->setCurrentWidget(editor);
        editor->setFocus();
        toggle3D->setChecked(false);
        propertiesDock->setEnabled(true);
        editorToolBar->setEnabled(true);
        modeGroup->setEnabled(true);
        gridSizeCombo->setEnabled(true);
        zoomCombo->setEnabled(true);
        statusBar()->showMessage("2D mode");
    };
    view3D->leave3D = leave3D;
    auto *checkMapAction = toolsMenu->addAction("Check Map");
    checkMapAction->setObjectName("checkMapAction");
    checkMapAction->setShortcut(QKeySequence(Qt::Key_F4));
    checkMapAction->setAutoRepeat(false);
    connect(checkMapAction, &QAction::triggered, this, [=] {
        if (editor->isVisible()) editor->setFocus(); // Commit property cell edits.
        MapCheckResult result;
        if (!editor->canAutosave()) {
            result.message = "Finish the current drag before checking the map.";
        } else if (!editor->drawingPoints().empty()) {
            result.target = MapCheckResult::Target::Drawing;
            result.message = "Finish or cancel the current drawing before checking the map.";
        } else result = checkMap(editor->document());
        QMessageBox dialog(result.valid ? QMessageBox::Information : QMessageBox::Warning,
            "Check Map", result.message, QMessageBox::Close, this);
        dialog.setTextFormat(Qt::PlainText);
        QPushButton *show = nullptr;
        if (!result.valid) {
            dialog.setInformativeText("Checking stops at the first error. Fix it, then run Check Map again (F4).");
            if (result.target != MapCheckResult::Target::Map)
                show = dialog.addButton("Show in Map", QMessageBox::ActionRole);
        }
        view3D->runModal([&] { dialog.exec(); });
        if (show && dialog.clickedButton() == show) {
            if (view3D->isVisible()) leave3D();
            using Target = MapCheckResult::Target;
            const auto mode = result.target == Target::Drawing ? MapEditor::Mode::Draw
                : result.target == Target::Sector ? MapEditor::Mode::Sectors
                : result.target == Target::Wall ? MapEditor::Mode::Lines : MapEditor::Mode::Sprites;
            if (result.target != Target::Drawing) {
                for (auto *action : modeGroup->actions())
                    if (action->data().toInt() == static_cast<int>(mode)) { action->trigger(); break; }
            }
            editor->showMapIssue(result);
        }
    });
    view3D->continuousEditChanged = [editor](const QString &key) { editor->continuousEditKey = key; };
    editor->documentRestored = [=] {
        if (view3D->isVisible() && !view3D->refreshDocument(editor->document())) {
            leave3D();
            statusBar()->showMessage("Map restored; returned to 2D because the 3D preview could not be rebuilt.", 5000);
        }
    };
    view3D->chooseTexture = [textureBrowserWindow](int current) -> std::optional<int> {
        const auto selection = textureBrowserWindow->chooseTexture(current);
        return selection ? std::optional<int>(selection->tile) : std::nullopt;
    };
    view3D->surfaceHeightChanged = [editor](std::size_t sector, bool floor, qreal height) {
        editor->setSectorHeight(sector, floor, height);
    };
    view3D->sectorChanged = [editor](std::size_t sector, const MapDocument::Sector &values) {
        editor->setSectorValues(sector, values);
    };
    view3D->shadesChanged = [editor](const MapDocument &values) {
        editor->setShadeValues(values);
    };
    view3D->surfacesChanged = [editor](const MapDocument &values, const QString &label) {
        editor->setSurfaceValues(values, label);
    };
    view3D->spriteChanged = [editor](std::size_t sprite, const MapDocument::Sprite &values) {
        editor->setSpriteValues(sprite, values);
    };
    view3D->wallSideChanged = [editor](std::size_t wall, bool reversed, const MapDocument::WallSide &values) {
        editor->setWallSideValues(wall, reversed, values);
    };
    view3D->statusMessage = [this](const QString &message) { statusBar()->showMessage(message, 5000); };
    connect(toggle3D, &QAction::triggered, this, [=](bool enabled) {
        if (!enabled) { leave3D(); return; }
        // Read the pointer before swapping widgets or capturing the mouse.
        QPoint cursor = editor->viewport()->mapFromGlobal(QCursor::pos());
        if (!editor->viewport()->rect().contains(cursor)) {
            cursor = editor->viewport()->rect().center();
        }
        const QPointF start = editor->mapToScene(cursor);
        editor->setFocus(); // Commit any property edit before snapshotting.
        views->setCurrentWidget(view3D);
        QString error;
        if (!view3D->start(editor->document(), start, error)) {
            leave3D();
            QMessageBox::warning(this, "Unable to enter 3D mode", error);
            return;
        }
        propertiesDock->setEnabled(false);
        editorToolBar->setEnabled(false);
        modeGroup->setEnabled(false); // In particular, S must reach navigation.
        gridSizeCombo->setEnabled(false);
        zoomCombo->setEnabled(false);
        statusBar()->showMessage("3D mode");
    });
    viewMenu->addSeparator();
    viewMenu->addAction(reorientGridAction);
    viewMenu->addAction(resetGridAction);
    auto *resetTextureScale = viewMenu->addAction("Reset Texture Scale");
    resetTextureScale->setToolTip("Reset the selected wall side to the default texture scale (R in 3D)");
    connect(resetTextureScale, &QAction::triggered, this, [editor, view3D] {
        if (view3D->isVisible()) { view3D->resetTextureScale(); }
        else { editor->resetSelectedWallTextureScale(); }
    });
    viewMenu->addSeparator();
    auto *propertyEditorAction = viewMenu->addAction("&Property Editor");
    propertyEditorAction->setCheckable(true);
    propertyEditorAction->setChecked(!propertiesDock->isHidden());
    connect(propertyEditorAction, &QAction::toggled,
            propertiesDock, &QDockWidget::setVisible);
    connect(propertiesDock, &QDockWidget::visibilityChanged,
            propertyEditorAction, &QAction::setChecked);

    // Keep the property editor with the primary view controls, and put 3D mode
    // after the view configuration actions.
    viewMenu->insertAction(toggle3D, propertyEditorAction);
    viewMenu->removeAction(toggle3D);
    viewMenu->addAction(toggle3D);

    auto *helpMenu = menuBar()->addMenu("&Help");
    auto *help2D = createShortcutHelp(this, false);
    auto *help3D = createShortcutHelp(this, true);
    const auto showShortcuts = [view3D](QDialog *dialog) {
        view3D->releaseMouseLook();
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    };
    auto *currentHelpAction = helpMenu->addAction("Current Mode Shortcuts");
    currentHelpAction->setShortcut(QKeySequence(Qt::Key_F1));
    currentHelpAction->setAutoRepeat(false);
    connect(currentHelpAction, &QAction::triggered, this, [=] {
        showShortcuts(view3D->isVisible() ? help3D : help2D);
    });
    auto *help2DAction = helpMenu->addAction("2D Mode Shortcuts");
    auto *help3DAction = helpMenu->addAction("3D Mode Shortcuts");
    connect(help2DAction, &QAction::triggered, this, [=] { showShortcuts(help2D); });
    connect(help3DAction, &QAction::triggered, this, [=] { showShortcuts(help3D); });
    helpMenu->addSeparator();
    auto *aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this,
            "About " PROGRAM_NAME,
            "<h3>" PROGRAM_NAME "</h3>"
            "<p>" PROGRAM_DESCRIPTION "<br>" "Version: " PROGRAM_VERSION "</p>"
            "<p>" PROGRAM_COPYRIGHT "<br/>" PROGRAM_TERMS "</p>");
    });

    auto *autosaveTimer = m_autosaveTimer = new QTimer(this);
    autosaveTimer->setObjectName("autosaveTimer");
    autosaveTimer->setInterval(60000);
    connect(autosaveTimer, &QTimer::timeout, this, [this, editor] {
        if (!editor->canAutosave()) return;
        if (!editor->hasUnsavedChanges()) { m_recovery->remove(); return; }
        RecoverySnapshot snapshot{editor->document(), editor->drawingPoints(),
            m_mapFilename.isEmpty() ? m_recoveryOrigin : m_mapFilename, QDateTime::currentDateTimeUtc()};
        QString error;
        if (!m_recovery->write(snapshot, error))
            statusBar()->showMessage("Autosave failed: " + error, 10000);
    });
    autosaveTimer->start();
    // Wait for the native window to be mapped before creating a modal child.
    // A zero-delay callback can run before exposure, leaving the recovery
    // prompt behind the newly shown editor while it blocks all editor input.
    auto *recoveryTimer = new QTimer(this);
    recoveryTimer->setInterval(50);
    connect(recoveryTimer, &QTimer::timeout, this, [this, editor, recoveryTimer] {
        if (!isVisible() || !windowHandle() || !windowHandle()->isExposed()) return;
        recoveryTimer->stop();
        recoveryTimer->deleteLater();
        for (const auto &path : RecoveryFile::candidates(RecoveryFile::directory())) {
            RecoveryFile previous(path);
            if (!previous.locked()) continue; // Another editor instance is using it.
            RecoverySnapshot snapshot;
            QString error;
            if (!previous.read(snapshot, error)) {
                QMessageBox::warning(this, "Unable to read recovery", error + "\n\nSnapshot kept at:\n" + path);
                continue;
            }
            const QString name = snapshot.sourceFilename.isEmpty() ? "Untitled map" : snapshot.sourceFilename;
            QMessageBox prompt(QMessageBox::Question, "Recover interrupted work",
                "An earlier session left unsaved work.\n\n" + name + "\n" +
                snapshot.timestamp.toLocalTime().toString("yyyy-MM-dd HH:mm:ss"),
                QMessageBox::Yes | QMessageBox::Discard | QMessageBox::Cancel, this);
            prompt.button(QMessageBox::Yes)->setText("Recover");
            prompt.button(QMessageBox::Cancel)->setText("Later");
            prompt.setDefaultButton(QMessageBox::Yes);
            prompt.show();
            prompt.raise();
            prompt.activateWindow();
            const auto answer = prompt.exec();
            if (answer == QMessageBox::Discard) { previous.remove(); continue; }
            if (answer != QMessageBox::Yes) break;
            // Transfer durably before retiring the old session's only copy.
            if (!m_recovery->write(snapshot, error)) {
                QMessageBox::warning(this, "Unable to recover work", error);
                break;
            }
            editor->recoverDocument(snapshot.document, snapshot.drawingPoints);
            m_recoveryOrigin = snapshot.sourceFilename;
            m_mapFilename.clear(); // Save As protects the original map on disk.
            setWindowFilePath({});
            setWindowTitle("Recovered map - Duke Builder");
            previous.remove();
            statusBar()->showMessage("Recovered unsaved work. Use Save As to save it as a map.", 10000);
            break;
        }
    });
    recoveryTimer->start();

    statusBar()->showMessage("Ready");
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_confirmUnsavedChanges && !m_confirmUnsavedChanges()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) {
        m_autosaveTimer->stop();
        m_recovery->remove();
    }
}
