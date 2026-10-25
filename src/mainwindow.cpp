#include "mainwindow.h"
#include "propertiespanel.h"
#include "mapinfowindow.h"
#include "toolbariconengine.h"
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
#include "libduke_version.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDir>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QProcess>
#include <QSettings>
#include <QStatusBar>
#include <QSignalBlocker>
#include <QToolBar>
#include <QSpinBox>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWindow>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace {
#ifdef Q_OS_MACOS
// Locate EDuke32 inside the bundle without loading platform frameworks.
QString macBundleExecutable(const QString &path)
{
    const QDir executables(QDir(path).absoluteFilePath("Contents/MacOS"));
    if (!executables.exists()) {
        return {};
    }

    const QFileInfoList candidates = executables.entryInfoList(
        QDir::Files | QDir::Executable | QDir::NoDotAndDotDot, QDir::Name);
    // Prefer the engine over helper executables shipped in the same bundle.
    for (const QFileInfo &candidate : candidates) {
        if (candidate.fileName().compare("eduke32", Qt::CaseInsensitive) == 0) {
            return candidate.absoluteFilePath();
        }
    }

    // A uniquely named executable is safe to select; multiple unknown names
    // require the user to configure the desired binary directly.
    if (candidates.size() == 1) {
        return candidates.front().absoluteFilePath();
    }
    return {};
}
#endif

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

    auto *propertiesPanel = new PropertiesPanel(editor, textureBrowserWindow, propertiesDock);
    propertiesDock->setWidget(propertiesPanel);
    addDockWidget(Qt::LeftDockWidgetArea, propertiesDock);
    connect(views, &QStackedWidget::currentChanged, propertiesPanel, [=](int) {
        propertiesPanel->setMapViewActive(views->currentWidget() == editor);
    });

    auto *reorientGridAction = new QAction("Reorient Grid to Line", this);
    reorientGridAction->setShortcut(QKeySequence(Qt::Key_F11));
    reorientGridAction->setEnabled(false);
    connect(reorientGridAction, &QAction::triggered, editor, &MapEditor::reorientGridToSelectedLine);
    auto *resetGridAction = new QAction("Reset Grid Orientation", this);
    resetGridAction->setShortcut(QKeySequence(Qt::Key_F12));
    connect(resetGridAction, &QAction::triggered, editor, &MapEditor::resetGridOrientation);

    editor->setPropertiesCallback(
        [panel = QPointer<PropertiesPanel>(propertiesPanel), reorientGridAction]
        (std::optional<MapEditor::SelectionProperties> properties) {
            reorientGridAction->setEnabled(properties && properties->wall.has_value());
            if (panel) panel->setProperties(std::move(properties));
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

    auto *referenceAction = fileMenu->addAction("Reference Image…");
    referenceAction->setObjectName("referenceImageAction");
    connect(referenceAction, &QAction::triggered, editor, &MapEditor::showReferenceImageControls);
    connect(views, &QStackedWidget::currentChanged, editor, [=] {
        referenceAction->setEnabled(views->currentWidget() == editor);
    });
    editor->referenceImageWarning = [this](const QString &error) {
        QMessageBox::warning(this, "Reference image", error);
    };

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
    auto *moveAction = editMenu->addAction(toolbarIcon(ToolbarSymbol::Move), "Move selection");
    moveAction->setShortcut(QKeySequence(Qt::Key_M));
    moveAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    moveAction->setAutoRepeat(false);
    moveAction->setToolTip("Move selection (M) — click to confirm, Escape to cancel");
    editor->addAction(moveAction);
    connect(moveAction, &QAction::triggered, editor, &MapEditor::moveSelection);
    auto *rotateAction = editMenu->addAction(toolbarIcon(ToolbarSymbol::Rotate), "Rotate selection…");
    rotateAction->setShortcut(QKeySequence(Qt::Key_R));
    rotateAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    rotateAction->setAutoRepeat(false);
    rotateAction->setToolTip("Rotate selection (R) — click to confirm, Escape to cancel");
    editor->addAction(rotateAction);
    auto *mirrorHorizontalAction = editMenu->addAction(toolbarIcon(ToolbarSymbol::MirrorHorizontal), "Mirror selection horizontally…");
    auto *mirrorVerticalAction = editMenu->addAction(toolbarIcon(ToolbarSymbol::MirrorVertical), "Mirror selection vertically…");
    connect(rotateAction, &QAction::triggered, editor, &MapEditor::rotateSelection);
    connect(mirrorHorizontalAction, &QAction::triggered, editor, [editor] { editor->mirrorSelection(true); });
    connect(mirrorVerticalAction, &QAction::triggered, editor, [editor] { editor->mirrorSelection(false); });
    auto *addSpriteAction = editMenu->addAction(toolbarIcon(ToolbarSymbol::AddSprite), "Add sprite");
    addSpriteAction->setShortcut(QKeySequence(Qt::Key_A));
    addSpriteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addSpriteAction->setAutoRepeat(false);
    addSpriteAction->setToolTip("Add sprite (A) — place a sprite in Sprites mode");
    editor->addAction(addSpriteAction);
    connect(addSpriteAction, &QAction::triggered, editor, &MapEditor::addSprite);
    editorToolBar->addSeparator();
    editorToolBar->addAction(moveAction);
    editorToolBar->addAction(rotateAction);
    editorToolBar->addAction(mirrorHorizontalAction);
    editorToolBar->addAction(mirrorVerticalAction);
    editorToolBar->addAction(addSpriteAction);
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
    auto *editingScopeLabel = new QLabel("All sectors", this);
    editingScopeLabel->setObjectName("editingScopeStatusLabel");
    editingScopeLabel->setToolTip("2D editing scope. Sectors → Show all sectors resets the filter.");
    statusBar()->addPermanentWidget(editingScopeLabel);
    editor->editingScopeChanged = [editingScopeLabel](const QString &text) { editingScopeLabel->setText(text); };
    connect(views, &QStackedWidget::currentChanged, editingScopeLabel, [=](int) {
        editingScopeLabel->setVisible(views->currentWidget() == editor);
    });
    connect(views, &QStackedWidget::currentChanged, cursorStatusLabel, [=](int) {
        cursorStatusLabel->setVisible(views->currentWidget() == editor);
    });

    editorToolBar->addSeparator();
    auto *geometryMode = new QComboBox(editorToolBar);
    geometryMode->setObjectName("GeometryMode");
    geometryMode->setAccessibleName("Drawing and paste connections");
    geometryMode->addItems({"Connect", "Independent"});
    geometryMode->setToolTip("Connect joins drawing and matching paste boundaries to editable geometry. Independent keeps new geometry separate.");
    editorToolBar->addWidget(geometryMode);
    const bool independent = QSettings().value("editor/independentGeometry", false).toBool();
    geometryMode->setCurrentIndex(independent ? 1 : 0);
    editor->setGeometryMode(independent ? MapEditor::GeometryMode::Independent : MapEditor::GeometryMode::Connect);
    editor->geometryModeChanged = [geometryMode](MapEditor::GeometryMode mode) {
        const bool independent = mode == MapEditor::GeometryMode::Independent;
        geometryMode->setCurrentIndex(independent ? 1 : 0);
        QSettings().setValue("editor/independentGeometry", independent);
    };
    connect(geometryMode, &QComboBox::activated, editor, [editor](int index) {
        editor->setGeometryMode(index == 0 ? MapEditor::GeometryMode::Connect : MapEditor::GeometryMode::Independent);
    });
    connect(views, &QStackedWidget::currentChanged, geometryMode, [=](int) {
        geometryMode->setEnabled(views->currentWidget() == editor);
    });
    auto *drawToolCombo = new QComboBox(editorToolBar);
    drawToolCombo->setAccessibleName("Drawing tool");
    drawToolCombo->addItems({"Freeform", "Rectangle", "Circle", "Regular polygon"});
    editorToolBar->addWidget(drawToolCombo);
    auto *shapeSides = new QSpinBox(editorToolBar);
    shapeSides->setRange(3, 128);
    shapeSides->setValue(16);
    shapeSides->setPrefix("Sides: ");
    shapeSides->setAccessibleName("Shape sides");
    editorToolBar->addWidget(shapeSides);
    shapeSides->setVisible(false);
    connect(shapeSides, &QSpinBox::valueChanged, editor, &MapEditor::setShapeSides);
    connect(drawToolCombo, &QComboBox::activated, editor, [=](int index) {
        modeGroup->actions().front()->trigger();
        editor->setDrawTool(static_cast<MapEditor::DrawTool>(index));
        shapeSides->setVisible(index >= 2);
        editor->setFocus();
    });
    connect(views, &QStackedWidget::currentChanged, drawToolCombo, [=](int) {
        drawToolCombo->setEnabled(views->currentWidget() == editor);
        shapeSides->setEnabled(views->currentWidget() == editor);
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

    const auto addModeAction = [modeMenu, modeGroup, editor, editorToolBar, gridAction](
                                   const QString &name,
                                   const QKeySequence &shortcut,
                                   MapEditor::Mode mode, ToolbarSymbol symbol,
                                   bool checked = false) {
        auto *action = modeMenu->addAction(toolbarIcon(symbol), name);
        action->setObjectName("mode" + name + "Action");
        action->setToolTip(name + " mode (" + shortcut.toString(QKeySequence::NativeText) + ")");
        editorToolBar->insertAction(gridAction, action);
        action->setActionGroup(modeGroup);
        action->setData(static_cast<int>(mode));
        action->setCheckable(true);
        action->setChecked(checked);
        action->setShortcut(shortcut);
        QObject::connect(action, &QAction::triggered, editor, [editor, mode] {
            editor->setMode(mode);
            editor->setFocus();
        });
        return action;
    };

    addModeAction("Draw", QKeySequence(Qt::CTRL | Qt::Key_D), MapEditor::Mode::Draw, ToolbarSymbol::DrawMode, true);
    addModeAction("Lines", QKeySequence(Qt::Key_L), MapEditor::Mode::Lines, ToolbarSymbol::LinesMode);
    addModeAction("Vertices", QKeySequence(Qt::Key_V), MapEditor::Mode::Vertices, ToolbarSymbol::VerticesMode);
    addModeAction("Sectors", QKeySequence(Qt::Key_S), MapEditor::Mode::Sectors, ToolbarSymbol::SectorsMode);
    addModeAction("Sprites", QKeySequence(Qt::Key_T), MapEditor::Mode::Sprites, ToolbarSymbol::SpritesMode);

    editorToolBar->insertSeparator(gridAction);

    const auto updatePlacementActions = [=] {
        const bool in2D = views->currentWidget() == editor;
        const auto mode = static_cast<MapEditor::Mode>(modeGroup->checkedAction()->data().toInt());
        moveAction->setEnabled(in2D && mode != MapEditor::Mode::Draw
                               && !editor->scene()->selectedItems().empty());
        for (auto *action : {rotateAction, mirrorHorizontalAction, mirrorVerticalAction})
            action->setEnabled(in2D && mode != MapEditor::Mode::Draw
                               && (!editor->scene()->selectedItems().empty() || editor->hasFloatingPaste()));
        addSpriteAction->setEnabled(in2D && mode == MapEditor::Mode::Sprites);
    };
    editor->modeChanged = [=](MapEditor::Mode mode) {
        for (auto *action : modeGroup->actions()) {
            if (action->data().toInt() == static_cast<int>(mode)) {
                action->setChecked(true);
                modeLabel->setText("Mode: " + action->text());
                break;
            }
        }
        updatePlacementActions();
    };
    connect(modeGroup, &QActionGroup::triggered, editor, updatePlacementActions);
    connect(editor->scene(), &QGraphicsScene::selectionChanged, editor, updatePlacementActions);
    connect(views, &QStackedWidget::currentChanged, editor, updatePlacementActions);
    updatePlacementActions();

    auto *sectorsMenu = menuBar()->addMenu("&Sectors");
    auto *isolateAction = sectorsMenu->addAction("Toggle sector isolation");
    isolateAction->setShortcut(QKeySequence(Qt::Key_I));
    isolateAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    editor->addAction(isolateAction);
    connect(isolateAction, &QAction::triggered, editor, &MapEditor::toggleSectorIsolation);
    auto *heightScopeAction = sectorsMenu->addAction("Edit height range…");
    connect(heightScopeAction, &QAction::triggered, editor, &MapEditor::filterEditingHeight);
    auto *focusHeightAction = sectorsMenu->addAction("Focus selected height range");
    connect(focusHeightAction, &QAction::triggered, editor, &MapEditor::focusSelectedHeightRange);
    auto *hideSectorsAction = sectorsMenu->addAction("Hide selected sectors");
    hideSectorsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    hideSectorsAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    editor->addAction(hideSectorsAction);
    connect(hideSectorsAction, &QAction::triggered, editor, &MapEditor::hideSelectedSectors);
    auto *showHiddenAction = sectorsMenu->addAction("Show hidden sectors");
    showHiddenAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
    showHiddenAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    editor->addAction(showHiddenAction);
    connect(showHiddenAction, &QAction::triggered, editor, &MapEditor::showHiddenSectors);
    auto *clearScopeAction = sectorsMenu->addAction("Show all sectors");
    connect(clearScopeAction, &QAction::triggered, editor, &MapEditor::clearEditingScope);
    auto *independentAction = sectorsMenu->addAction("Draw independent sector");
    connect(independentAction, &QAction::triggered, editor, &MapEditor::drawIndependentSector);
    auto *spritesMenu = menuBar()->addMenu("S&prites");
    auto *trorMenu = menuBar()->addMenu("T&ROR");
    const auto trorAction = [&](const QString &title, const std::function<void()> &operation) {
        auto *action = trorMenu->addAction(title);
        connect(action, &QAction::triggered, editor, operation);
        return action;
    };
    trorAction("Isolate current layer", [editor] { editor->isolateCurrentLayer(); });
    trorAction("Layer above", [editor] { editor->goTrorLayer(false); });
    trorAction("Layer below", [editor] { editor->goTrorLayer(true); });
    trorMenu->addSeparator();
    trorAction("Extend ceiling upward…", [editor] { editor->extendSelectedTror(false); });
    trorAction("Extend floor downward…", [editor] { editor->extendSelectedTror(true); });
    trorAction("Connect selected sectors…", [editor] { editor->connectSelectedTror(); });
    trorAction("Disconnect ceiling bunch…", [editor] { editor->disconnectSelectedTror(false); });
    trorAction("Disconnect floor bunch…", [editor] { editor->disconnectSelectedTror(true); });
    connect(views, &QStackedWidget::currentChanged, trorMenu, [=](int) {
        trorMenu->setEnabled(views->currentWidget() == editor);
    });
    connect(views, &QStackedWidget::currentChanged, editor, [=](int) {
        for (auto *action : {isolateAction, heightScopeAction, focusHeightAction, hideSectorsAction, showHiddenAction, clearScopeAction, independentAction}) {
            action->setEnabled(views->currentWidget() == editor);
        }
    });
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
    auto *joinSectorsAction = sectorsMenu->addAction("Join Sectors");
    joinSectorsAction->setShortcut(QKeySequence(Qt::Key_J));
    joinSectorsAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    joinSectorsAction->setAutoRepeat(false);
    joinSectorsAction->setEnabled(false);
    editor->addAction(joinSectorsAction);
    editor->joinAvailabilityChanged = [joinSectorsAction](bool enabled) {
        joinSectorsAction->setEnabled(enabled);
    };
    connect(joinSectorsAction, &QAction::triggered, editor, &MapEditor::joinSelectedSectors);

    auto *stickSpriteAction = spritesMenu->addAction("Stick Sprite to Wall");
    stickSpriteAction->setShortcut(QKeySequence(Qt::Key_O));
    stickSpriteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    stickSpriteAction->setAutoRepeat(false);
    editor->addAction(stickSpriteAction);
    connect(stickSpriteAction, &QAction::triggered, this, [editor, view3D] {
        if (view3D->isVisible()) { view3D->stickSpriteToWall(); }
        else { editor->stickSelectedSpriteToWall(); }
    });
    for (bool floor : {true, false}) {
        auto *action = spritesMenu->addAction(floor ? "Align Sprites to Floor" : "Align Sprites to Ceiling");
        action->setShortcut(QKeySequence(Qt::CTRL | (floor ? Qt::Key_PageDown : Qt::Key_PageUp)));
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        action->setAutoRepeat(false);
        editor->addAction(action);
        connect(action, &QAction::triggered, this, [editor, view3D, floor] {
            if (view3D->isVisible()) view3D->alignSpritesToSurface(floor);
            else editor->alignSelectedSpritesToSurface(floor);
        });
    }
    auto *textureBrowserAction = toolsMenu->addAction("&Texture Browser");
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
#ifdef Q_OS_MACOS
        if (binaryFile.isDir() && binaryFile.suffix().compare("app", Qt::CaseInsensitive) == 0) {
            const QString executable = macBundleExecutable(binaryFile.absoluteFilePath());
            if (executable.isEmpty()) {
                QMessageBox::warning(this, "Unable to run eDuke32",
                                     "Could not identify the EDuke32 executable in Contents/MacOS. "
                                     "Select the executable inside the app bundle directly.");
                return;
            }
            // Launch directly to preserve map arguments and detached-process handling.
            // Keep -usecwd pointed at the directory beside the configured bundle.
            process.setProgram(executable);
        } else {
            process.setProgram("./" + binaryFile.fileName());
        }
#elif defined(Q_OS_WIN)
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
    menuBar()->removeAction(toolsMenu->menuAction());
    menuBar()->insertMenu(viewMenu->menuAction(), toolsMenu);
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
    auto *trorPlanes = viewMenu->addAction("Show solid TROR planes");
    trorPlanes->setObjectName("showTrorPlanesAction");
    trorPlanes->setCheckable(true);
    trorPlanes->setChecked(view3D->trorPlanesVisible());
    trorPlanes->setShortcut(QKeySequence(Qt::Key_T));
    trorPlanes->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    trorPlanes->setAutoRepeat(false);
    view3D->addAction(trorPlanes);
    connect(trorPlanes, &QAction::toggled, view3D, &MapView3D::setTrorPlanesVisible);
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
    auto *mapInfoAction = toolsMenu->addAction("Map Info");
    mapInfoAction->setObjectName("mapInfoAction");
    connect(mapInfoAction, &QAction::triggered, this, [=] {
        if (editor->isVisible()) editor->setFocus(); // Commit property cell edits.
        MapInfoWindow dialog(editor->document(), m_mapFilename, editor->hasUnsavedChanges(), this);
        view3D->runModal([&] { dialog.exec(); });
    });
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
        // Selection restoration blocks scene signals; refresh after it is complete.
        updatePlacementActions();
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
        if (!view3D->start(editor->document(), start, error, editor->editingScope())) {
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
    auto *tutorialsMenu = helpMenu->addMenu("Mini Tutorials");
    const auto addTutorial = [this, view3D, tutorialsMenu](const QString &title, const QString &path) {
        auto *action = tutorialsMenu->addAction(title);
        connect(action, &QAction::triggered, this, [this, view3D, title, path] {
            view3D->releaseMouseLook();
            auto *dialog = new QDialog(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            // Use a regular window: native dialog decorations can disable minimize.
            dialog->setWindowFlags(Qt::Window | Qt::WindowTitleHint
                                   | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint
                                   | Qt::WindowCloseButtonHint);
            dialog->setWindowTitle("Mini Tutorial: " + title);
            dialog->resize(1100, 800);
            dialog->setMinimumSize(640, 480);
            auto *layout = new QVBoxLayout(dialog);
            auto *browser = new QTextBrowser(dialog);
            browser->setOpenExternalLinks(true);
            browser->setSource(QUrl("qrc:/tutorials/" + path + "/index.html"));
            layout->addWidget(browser);
            dialog->show();
            dialog->raise();
            dialog->activateWindow();
        });
    };
    addTutorial("Vertical Doors", "vertical-doors");
    addTutorial("TROR: Stacked Rooms (Beginner)", "tror-stacked-rooms");
    helpMenu->addSeparator();
    auto *aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this,
            "About " PROGRAM_NAME,
            QString("<h3>" PROGRAM_NAME "</h3>"
            "<p>" PROGRAM_DESCRIPTION "<br>" "Version: " PROGRAM_VERSION "</p>"
            "<p>Qt version: %1<br>libduke version: " LIBDUKE_GIT_COMMIT "</p>"
            "<p>" PROGRAM_COPYRIGHT "<br/>" PROGRAM_TERMS "</p>")
                .arg(QString::fromLatin1(qVersion())));
    });

    auto *autosaveTimer = m_autosaveTimer = new QTimer(this);
    autosaveTimer->setObjectName("autosaveTimer");
    autosaveTimer->setInterval(60000);
    connect(autosaveTimer, &QTimer::timeout, this, [this, editor] {
        if (!editor->canAutosave()) return;
        if (!editor->hasUnsavedChanges()) { m_recovery->remove(); return; }
        RecoverySnapshot snapshot{editor->document(), editor->drawingPoints(),
            m_mapFilename.isEmpty() ? m_recoveryOrigin : m_mapFilename, QDateTime::currentDateTimeUtc(), editor->referenceImageMetadata()};
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
            editor->restoreReferenceImage(snapshot.referenceImage, error);
            if (!error.isEmpty()) QMessageBox::warning(this, "Recovered reference image", error);
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
