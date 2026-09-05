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
        const auto property = static_cast<MapEditor::SpriteProperty>(
            index.siblingAtColumn(0).data(Qt::UserRole).toInt());
        if (property != MapEditor::SpriteProperty::Texture) {
            return QStyledItemDelegate::createEditor(parent, option, index);
        }

        auto *editor = new TexturePropertyEditor(parent);
        connect(editor->value, &QLineEdit::editingFinished, editor,
                [this, editor] {
                    emit const_cast<PropertyValueDelegate *>(this)->commitData(editor);
                });
        connect(editor->browse, &QPushButton::clicked, editor,
                [this, editor] {
                    bool valid = false;
                    const int currentTexture = editor->value->text().toInt(&valid);
                    if (!m_textureChooser) {
                        return;
                    }
                    const std::optional<int> texture = m_textureChooser(
                        valid ? std::optional<int>(currentTexture) : std::nullopt);
                    if (texture) {
                        editor->value->setText(QString::number(*texture));
                        emit const_cast<PropertyValueDelegate *>(this)->commitData(editor);
                    }
                });
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (auto *textureEditor = dynamic_cast<TexturePropertyEditor *>(editor)) {
            textureEditor->value->setText(index.data(Qt::EditRole).toString());
            return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
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
                    editor->setSelectedSpriteProperty(
                        static_cast<MapEditor::SpriteProperty>(
                            item->data(0, Qt::UserRole).toInt()),
                        std::numeric_limits<qreal>::quiet_NaN());
                    return;
                }
                editor->setSelectedSpriteProperty(
                    static_cast<MapEditor::SpriteProperty>(
                        item->data(0, Qt::UserRole).toInt()),
                    value);
            });

    editor->setSpritePropertiesCallback(
        [propertiesControl](std::optional<MapEditor::SpriteProperties> properties) {
            const QSignalBlocker blocker(propertiesControl);
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
                                         MapEditor::SpriteProperty property) {
                auto *item = new QTreeWidgetItem(propertiesControl, {name, value});
                item->setFlags(item->flags() | Qt::ItemIsEditable);
                item->setData(0, Qt::UserRole, static_cast<int>(property));
            };
            addProperty("X", number(properties->x), MapEditor::SpriteProperty::X);
            addProperty("Y", number(properties->y), MapEditor::SpriteProperty::Y);
            addProperty("Z", number(properties->z), MapEditor::SpriteProperty::Z);
            addProperty("Angle", number(std::clamp(properties->angle, 0.0, 360.0)),
                        MapEditor::SpriteProperty::Angle);
            if (properties->texture) {
                addProperty("Texture", QString::number(*properties->texture),
                            MapEditor::SpriteProperty::Texture);
                propertiesControl->openPersistentEditor(
                    propertiesControl->topLevelItem(
                        propertiesControl->topLevelItemCount() - 1),
                    1);
            }
            if (properties->hitag) {
                addProperty("Hitag", QString::number(*properties->hitag),
                            MapEditor::SpriteProperty::Hitag);
            }
            if (properties->lotag) {
                addProperty("Lotag", QString::number(*properties->lotag),
                            MapEditor::SpriteProperty::Lotag);
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
