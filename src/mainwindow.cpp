#include "mainwindow.h"
#include "mapeditor.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QStatusBar>
#include <QToolBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Duke Builder");
    resize(960, 640);

    auto *editor = new MapEditor(this);
    editor->setStatusCallback([this](const QString &message) {
        statusBar()->showMessage(message);
    });
    setCentralWidget(editor);

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
