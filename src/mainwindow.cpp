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
        if (property == MapEditor::Property::Lotag) {
            auto *editor = new QComboBox(parent);
            editor->setEditable(true);
            editor->setInsertPolicy(QComboBox::NoInsert);
            editor->setMinimumContentsLength(12);
            editor->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            editor->setToolTip("Sector Effector (tile 1) meanings. Other sprites use "
                               "lotags differently. Enter any integer from -32768 to 32767.");
            int tag = 0;
            for (const char *meaning : sectorEffectorLotags) {
                editor->addItem(QString::number(tag) + " - " + meaning, tag);
                ++tag;
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
            && property != MapEditor::Property::CeilingTexture) {
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
            const int tag = entry >= 0 ? lotagEditor->itemData(entry).toInt(&valid)
                                       : text.toInt(&valid);
            if (valid && tag >= -32768 && tag <= 32767) {
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
    propertiesDock->setWidget(propertiesControl);
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
        [propertiesControl, propertyDelegate](std::optional<MapEditor::SelectionProperties> properties) {
            const QSignalBlocker blocker(propertiesControl);
            // Clearing the rows can finish an edit while its widget is being retired.
            const QSignalBlocker delegateBlocker(propertyDelegate);
            propertiesControl->clear();
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
            if (properties->sector) {
                const auto &sector = *properties->sector;
                addProperty("Floor Z", number(sector.floorz), MapEditor::Property::FloorZ);
                addProperty("Ceiling Z", number(sector.ceilingz), MapEditor::Property::CeilingZ);
                addTextureProperty("Floor texture", sector.floorTexture,
                                   MapEditor::Property::FloorTexture);
                addTextureProperty("Ceiling texture", sector.ceilingTexture,
                                   MapEditor::Property::CeilingTexture);
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
    auto *textureBrowserWindow = new TextureBrowserWindow(this);
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
    editor->setSpriteTextureResolver([textureBrowserWindow](int tile) {
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
