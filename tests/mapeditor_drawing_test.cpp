#include "mapeditor.h"
#include "mapsave.h"

#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QScrollBar>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QInputDialog>
#include <QTimer>
#include <QGraphicsItem>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QAbstractButton>
#include <QtPlugin>
#include <algorithm>
#include <cstdlib>
#include <iostream>

#ifdef DUKE_BUILDER_STATIC_MINIMAL_PLUGIN
Q_IMPORT_PLUGIN(QMinimalIntegrationPlugin)
#endif

static void require(bool value, const char *message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(EXIT_FAILURE); }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    MapEditor editor;
    editor.resize(1000, 800);
    editor.show();
    const auto click = [&](QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPoint local = editor.mapFromScene(point);
        const QPoint global = editor.viewport()->mapToGlobal(local);
        QMouseEvent press(QEvent::MouseButtonPress, local, global,
                          Qt::LeftButton, Qt::LeftButton, modifiers);
        QApplication::sendEvent(editor.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                            Qt::LeftButton, Qt::NoButton, modifiers);
        QApplication::sendEvent(editor.viewport(), &release);
    };
    const auto rightClick = [&](QPointF point) {
        const QPoint local = editor.mapFromScene(point);
        const QPoint global = editor.viewport()->mapToGlobal(local);
        QMouseEvent press(QEvent::MouseButtonPress, local, global,
                          Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                            Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &release);
    };
    // Shape tools preview without changing the map, then commit one undo step.
    editor.setZoomPercent(200);
    editor.centerOn(0, 0);
    QApplication::processEvents();
    for (const auto tool : {MapEditor::DrawTool::Rectangle, MapEditor::DrawTool::Circle,
                            MapEditor::DrawTool::Polygon}) {
        editor.newMap();
        editor.setDrawTool(tool);
        editor.setShapeSides(tool == MapEditor::DrawTool::Circle ? 16 : 5);
        click({0, 0});
        require(editor.document().sectors().empty() && !editor.drawingPoints().empty(),
                "Shape anchor only starts a preview");
        require(!editor.canAutosave(), "Do not recover a shape anchor as a freeform drawing");
        click({0, 0});
        require(editor.document().sectors().empty() && !editor.drawingPoints().empty(),
                "Degenerate shape remains adjustable");
        click({1024, 1024});
        require(editor.document().sectors().size() == 1 && editor.drawingPoints().empty(),
                "Second shape click creates a sector");
        const auto count = tool == MapEditor::DrawTool::Rectangle ? 4u
                         : tool == MapEditor::DrawTool::Circle ? 16u : 5u;
        require(editor.document().walls().size() == count, "Shape has requested wall count");
        require(editor.undoStack()->count() == 1, "Shape creates one undo entry");
        editor.undo();
        require(editor.document().sectors().empty(), "Undo removes the complete shape");
        editor.redo();
        require(editor.document().walls().size() == count, "Redo restores the complete shape");
        click({-2048, -2048});
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&editor, &escape);
        require(editor.drawingPoints().empty() && editor.document().walls().size() == count,
                "Escape cancels only the shape preview");
    }
    editor.newMap();
    editor.setDrawTool(MapEditor::DrawTool::Freeform);

    editor.setDrawTool(MapEditor::DrawTool::Rectangle);
    auto *gridScene = static_cast<MapScene *>(editor.scene());
    gridScene->setGridAngle(0.4);
    click({0, 0});
    click(gridScene->fromGrid({1024, 512}), Qt::ShiftModifier);
    require(editor.document().walls().size() == 4, "Rotated square is created");
    const auto &square = editor.document().vertices();
    const auto edgeA = square[1].position - square[0].position;
    const auto edgeB = square[2].position - square[1].position;
    require(std::abs(QPointF::dotProduct(edgeA, edgeB)) < 0.01
            && std::abs(QLineF({}, edgeA).length() - QLineF({}, edgeB).length()) < 0.01,
            "Shift rectangle keeps equal perpendicular sides on rotated grid");
    gridScene->setGridAngle(0);
    editor.newMap();
    click({0, 0});
    click({1024, 1024});
    click({1024, 0});
    click({2048, 1024});
    require(editor.document().sectors().size() == 2, "Rectangle attaches to neighboring sector");
    require(std::count_if(editor.document().walls().begin(), editor.document().walls().end(),
            [](const auto &wall) { return wall.forwardSector && wall.reverseSector; }) == 1,
            "Adjacent rectangles share a portal wall");
    editor.newMap();
    editor.setDrawTool(MapEditor::DrawTool::Freeform);

    MapDocument source;
    require(source.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}}, true), "Create source room");
    source.setPlayerStartPosition({512,512});
    source.setSectorFloorZ(0, -2048);
    source.setSectorCeilingZ(0, -16384);
    editor.recoverDocument(source, {});
    editor.setMode(MapEditor::Mode::Draw);
    editor.setZoomPercent(200);
    editor.centerOn(768,512);
    QApplication::processEvents();

    // Panning must not place drawing points or change map history.
    const auto pan = [&](Qt::MouseButton button, bool space) {
        const QPoint start(400, 300), end(440, 330);
        const int horizontal = editor.horizontalScrollBar()->value();
        const int vertical = editor.verticalScrollBar()->value();
        if (space) {
            QKeyEvent key(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
            QApplication::sendEvent(&editor, &key);
        }
        QMouseEvent press(QEvent::MouseButtonPress, start, editor.viewport()->mapToGlobal(start),
                          button, button, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &press);
        if (space) {
            QKeyEvent key(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
            QApplication::sendEvent(&editor, &key);
        }
        QMouseEvent move(QEvent::MouseMove, end, editor.viewport()->mapToGlobal(end),
                         Qt::NoButton, button, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &move);
        require(editor.horizontalScrollBar()->value() == horizontal - 40
                && editor.verticalScrollBar()->value() == vertical - 30,
                "Both pan bindings move the viewport, even if Space is released first");
        QMouseEvent release(QEvent::MouseButtonRelease, end, editor.viewport()->mapToGlobal(end),
                            button, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &release);
        require(editor.cursor().shape() == Qt::CrossCursor, "Mouse release ends panning");
        require(editor.document() == source && editor.drawingPoints().empty()
                && editor.undoStack()->count() == 0, "Panning leaves geometry and history unchanged");
    };
    pan(Qt::LeftButton, true);
    pan(Qt::MiddleButton, false);
    QKeyEvent heldSpace(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&editor, &heldSpace);
    QFocusEvent focusOut(QEvent::FocusOut);
    QApplication::sendEvent(&editor, &focusOut);
    editor.centerOn(768,512);

    click({1024,256});
    click({1536,256});
    click({1536,768});
    require(editor.document() == source && editor.drawingPoints().size() == 3,
            "Drawing preview does not change the existing wall");
    click({1024,768});
    require(editor.drawingPoints().empty() && editor.document().sectors().size() == 2,
            "Clicking back on the wall completes the room without retracing or Enter");
    require(editor.document().walls().size() == 9, "No overlapping closing wall");
    require(std::count_if(editor.document().walls().begin(), editor.document().walls().end(),
                         [](const auto &wall) { return wall.isTwoSided(); }) == 1,
            "The shared segment becomes a portal");
    require(editor.document().sectors()[1].floorz == -2048
            && editor.document().sectors()[1].ceilingz == -16384,
            "Attachment shares the source room's vertical opening");
    require(editor.undoStack()->count() == 1, "Attachment is one undo operation");
    const auto attached = editor.document();
    editor.undo();
    require(editor.document() == source, "Undo removes the room and both wall splits");
    editor.redo();
    require(editor.document() == attached, "Redo restores portal and room");

    QTemporaryDir directory;
    require(directory.isValid(), "Create temporary directory");
    QString error;
    const auto path = directory.filePath("attached.map");
    require(editor.saveMap(path, error), error.toUtf8().constData());
    MapDocument reopened;
    require(reopened.openMap(path, error), error.toUtf8().constData());
    require(reopened.sectors().size() == 2 && reopened.walls().size() == 9
            && std::count_if(reopened.walls().begin(), reopened.walls().end(),
                             [](const auto &wall) { return wall.isTwoSided(); }) == 1,
            "Saved map retains one shared portal rather than two solid walls");

    editor.undo();
    click({1024,256});
    click({1536,256});
    QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&editor, &cancel);
    require(editor.drawingPoints().empty() && editor.document() == source,
            "Cancelling an attachment leaves the source wall unchanged");

    MapDocument spriteMap;
    require(spriteMap.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}}, true),
            "Create sprite-copy test room");
    auto firstSprite = MapDocument::Sprite{};
    firstSprite.position = {128, 128};
    firstSprite.z = -2048;
    firstSprite.texture = 1680;
    firstSprite.hitag = 17;
    firstSprite.lotag = 2;
    firstSprite.palette = 3;
    firstSprite.sectorId = 0;
    auto secondSprite = firstSprite;
    secondSprite.position = {384, 384};
    secondSprite.texture = 2000;
    secondSprite.lotag = 3;
    const auto firstId = spriteMap.addSprite(firstSprite.position);
    spriteMap.setSprite(firstId, firstSprite);
    const auto secondId = spriteMap.addSprite(secondSprite.position);
    spriteMap.setSprite(secondId, secondSprite);
    editor.recoverDocument(spriteMap, {});
    editor.setMode(MapEditor::Mode::Sprites);
    click(firstSprite.position);
    click(secondSprite.position, Qt::ShiftModifier);
    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(&editor, &copy);
    QKeyEvent paste(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
    QApplication::sendEvent(&editor, &paste);
    require(editor.document().sprites().size() == 4,
            "Pasting duplicates all selected sprites");
    const auto &pastedFirst = editor.document().sprites()[2];
    const auto &pastedSecond = editor.document().sprites()[3];
    auto expectedFirst = firstSprite;
    auto expectedSecond = secondSprite;
    expectedFirst.position += QPointF(256, 256);
    expectedSecond.position += QPointF(256, 256);
    require(pastedFirst == expectedFirst && pastedSecond == expectedSecond,
            "Pasted sprites preserve their spacing and offset by one grid cell");
    editor.undo();
    require(editor.document().sprites().size() == 2, "Undo removes the pasted sprites together");
    editor.redo();
    require(editor.document().sprites().size() == 4, "Redo restores the pasted sprites");

    MapDocument raisedFloorMap;
    require(raisedFloorMap.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}}, true),
            "Create raised-floor sprite test room");
    auto raisedFloor = raisedFloorMap.sectors()[0];
    raisedFloor.floorz = -2048;
    raisedFloorMap.setSector(0, raisedFloor);
    editor.recoverDocument(raisedFloorMap, {});
    editor.setMode(MapEditor::Mode::Sprites);
    rightClick({512,512});
    require(editor.document().sprites().size() == 1
            && editor.document().sprites()[0].z == -2048
            && editor.document().sprites()[0].sectorId == 0,
            "New sprites inherit the flat floor height and sector at their placement point");

    raisedFloor.floorstat |= 2;
    raisedFloor.floorheinum = 256;
    raisedFloorMap.setSector(0, raisedFloor);
    editor.recoverDocument(raisedFloorMap, {});
    editor.setMode(MapEditor::Mode::Sprites);
    rightClick({512,512});
    require(editor.document().sprites().size() == 1
            && editor.document().sprites()[0].z == -1536,
            "New sprites follow the local height of a sloped floor");

    MapDocument stacked = source;
    require(stacked.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}},true,&error,
        std::set<MapDocument::SectorId>{}), "Create independent overlapping fixture");
    stacked.setSectorCeilingZ(1,-32768);
    stacked.setSectorFloorZ(1,-24576);
    editor.recoverDocument(stacked,{});
    editor.setMode(MapEditor::Mode::Draw);
    QString scopeStatus;
    editor.editingScopeChanged = [&](const QString &text) { scopeStatus = text; };
    editor.centerOn(512,512);
    QApplication::processEvents();
    const auto choose = [&](int index, const std::function<void()> &operation) {
        bool prompted = false;
        QTimer timer;
        QObject::connect(&timer,&QTimer::timeout,[&] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            if (!dialog) { return; }
            prompted = true;
            if (index < 0) { dialog->reject(); }
            else {
                require(index < dialog->comboBoxItems().size(), "Target choice exists");
                dialog->setTextValue(dialog->comboBoxItems()[index]);
                dialog->accept();
            }
        });
        timer.start(1);
        operation();
        timer.stop();
        require(prompted,"Ambiguous target prompts instead of guessing");
    };
    choose(-1,[&] { click({512,0}); });
    require(editor.document() == stacked && editor.drawingPoints().empty(), "Cancelled target chooser changes nothing");
    choose(0,[&] { click({512,0}); });
    click({512,1024});
    require(editor.document().sectors().size() == 3 && editor.document().sectors()[1] == stacked.sectors()[1],
        "Scoped UI drawing splits lower room only");
    for (auto w : stacked.sectors()[1].walls) { require(editor.document().walls()[w] == stacked.walls()[w], "UI drawing preserves upper wall"); }
    require(editor.undoStack()->count() == 1, "Scoped drawing and boundary splits form one undo command");
    const auto split = editor.document();
    editor.undo();
    require(editor.document() == stacked && scopeStatus == "1 editable sector(s)","Undo restores independent overlapping topology and scope");
    editor.redo();
    require(editor.document() == split && scopeStatus == "2 editable sector(s)","Redo restores only intended edit and both child sectors in scope");

    editor.recoverDocument(stacked,{});
    editor.setMode(MapEditor::Mode::Vertices);
    bool heightPrompted = false;
    QTimer heightTimer;
    QObject::connect(&heightTimer,&QTimer::timeout,[&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || !dialog->windowTitle().startsWith("Editable height")) { return; }
        auto fields = dialog->findChildren<QSpinBox *>();
        require(fields.size() == 2,"Height range fields exist");
        fields[0]->setValue(-32768);
        fields[1]->setValue(-24576);
        heightPrompted = true;
        dialog->accept();
    });
    heightTimer.start(1);
    editor.filterEditingHeight();
    heightTimer.stop();
    require(heightPrompted && editor.document() == stacked && editor.undoStack()->count() == 0,
        "Height filtering is view state, not a map edit");
    click({0,0});
    require(editor.scene()->selectedItems().size() == 1,"Scope makes coincident vertex selection unambiguous");
    const auto drag = [&](QPointF from, QPointF to) {
        const QPoint start = editor.mapFromScene(from), end = editor.mapFromScene(to);
        QMouseEvent press(QEvent::MouseButtonPress,start,editor.viewport()->mapToGlobal(start),Qt::RightButton,Qt::RightButton,Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(),&press);
        QMouseEvent move(QEvent::MouseMove,end,editor.viewport()->mapToGlobal(end),Qt::NoButton,Qt::RightButton,Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(),&move);
        QMouseEvent release(QEvent::MouseButtonRelease,end,editor.viewport()->mapToGlobal(end),Qt::RightButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(),&release);
    };
    drag({0,0},{-256,0});
    require(editor.document().sectors()[0] == stacked.sectors()[0], "Dragging upper geometry keeps lower boundary");
    for (auto v : stacked.sectors()[0].vertices) { require(editor.document().vertices()[v] == stacked.vertices()[v],"Dragging upper vertex leaves coincident lower vertex unchanged"); }
    require(!(editor.document() == stacked),"Intended upper vertex moved");
    editor.undo();
    require(editor.document() == stacked,"Vertex move undo restores map");
    require(editor.setEditingScope(std::set<MapDocument::SectorId>{1}),"Restore upper editing scope");
    click({0,0});
    drag({0,0},{1024,1024});
    require(editor.document() == stacked,"Invalid collapsing drag is rolled back instead of deleting geometry");
    editor.recoverDocument(stacked,{});
    editor.setMode(MapEditor::Mode::Sprites);
    choose(1,[&] { rightClick({768,768}); });
    require(editor.document().sprites().size() == 1 && editor.document().sprites()[0].sectorId == 1
        && editor.document().sprites()[0].z == -24576,"Sprite placement resolves overlapping sector and inherits its floor");
    // Isolation controls picking, not explicit TROR constraints.
    auto tror = source;
    require(tror.extendTror(0,true,8192,error).has_value(), "Create linked fixture");
    editor.recoverDocument(tror,{});
    editor.setMode(MapEditor::Mode::Vertices);
    require(editor.setEditingScope(std::set<MapDocument::SectorId>{0}), "Isolate linked upper room");
    editor.centerOn(512,512);
    click({0,0}); drag({0,0},{-256,0});
    for (const auto &s : editor.document().sectors()) {
        require(editor.document().vertices()[s.vertices[0]].position == QPointF(-256,0), "Drag follows link into hidden layer");
    }
    require(editor.undoStack()->index() == 1, "Linked drag is one undo action");
    editor.undo();
    require(editor.document() == tror && editor.editingScope() == std::optional<std::set<MapDocument::SectorId>>({0}), "Undo restores geometry, links and scope");
    const auto splitPoint = editor.mapFromScene(QPointF(512,0));
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick,splitPoint,editor.viewport()->mapToGlobal(splitPoint),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(editor.viewport(),&doubleClick);
    require(editor.document().walls().size() == tror.walls().size()+2, "Double click splits linked wall pair");
    require(editor.editingScope() == std::optional<std::set<MapDocument::SectorId>>({0}), "Linked split preserves isolation");
    editor.undo(); require(editor.document() == tror, "Linked split undo restores all boundaries");
    editor.goTrorLayer(true);
    require(editor.editingScope() == std::optional<std::set<MapDocument::SectorId>>({1}), "Navigate to lower layer");
    editor.goTrorLayer(false);
    require(editor.editingScope() == std::optional<std::set<MapDocument::SectorId>>({0})
        && editor.document() == tror && editor.undoStack()->index() == 0, "Layer navigation changes no map history");
    bool extensionPrompted = false;
    QTimer extensionTimer;
    QObject::connect(&extensionTimer,&QTimer::timeout,&editor,[&] {
        if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
            extensionPrompted = true; dialog->setIntValue(8192); dialog->accept();
        }
    });
    extensionTimer.start(1); editor.extendSelectedTror(false); extensionTimer.stop();
    require(extensionPrompted && editor.document().sectors().size() == 3
        && editor.editingScope() == std::optional<std::set<MapDocument::SectorId>>({2}), "Extension command creates and isolates new layer");
    const auto extended = editor.document();
    editor.undo(); require(editor.document() == tror, "Extension undo restores map");
    editor.redo(); require(editor.document() == extended, "Extension redo restores bunch and scope");
    bool disconnected = false;
    QTimer disconnectTimer;
    QObject::connect(&disconnectTimer,&QTimer::timeout,&editor,[&] {
        if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            disconnected = true; dialog->button(QMessageBox::Yes)->click();
        }
    });
    disconnectTimer.start(1); editor.disconnectSelectedTror(true); disconnectTimer.stop();
    require(disconnected && !editor.document().sectors()[2].floorBunch, "Disconnect command clears selected whole bunch");
    editor.undo(); require(editor.document() == extended, "Disconnection undo restores links");
    editor.editingScopeChanged = {};

    // Keyboard/toolbar movement shares geometry validation and one undo transaction.
    editor.setGridSize(256);
    // The minimal Qt platform has no system cursor; commands use toolbar placement.
    const auto hover = [&](QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPoint local = editor.mapFromScene(point);
        QMouseEvent event(QEvent::MouseMove, local, editor.viewport()->mapToGlobal(local),
                          Qt::NoButton, Qt::NoButton, modifiers);
        QApplication::sendEvent(editor.viewport(), &event);
    };
    const auto key = [&](int code) {
        QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
        QApplication::sendEvent(&editor, &event);
    };
    {
        MapDocument original;
        require(original.addPolyline({{0,0},{1024,0},{1024,512},{0,512}}, true), "Transform UI source");
        original.setPlayerStartPosition({256,256});
        const auto sprite = original.addSprite({128,128});
        original.setSpriteAngle(sprite, 30);
        editor.recoverDocument(original, {});
        editor.clearEditingScope();
        editor.setMode(MapEditor::Mode::Sectors);
        editor.centerOn(512,256);
        click({512,256});
        const auto angleBox = [&]() -> QDoubleSpinBox * {
            for (auto *box : editor.findChildren<QDoubleSpinBox *>("transformAngle"))
                if (box->isVisible()) return box;
            require(false, "Transform angle control exists");
            return nullptr;
        };
        editor.rotateSelection();
        angleBox()->setValue(90);
        require(editor.document() == original && editor.undoStack()->count() == 0,
                "Rotation preview leaves document and history untouched");
        key(Qt::Key_Escape);
        require(editor.document() == original && editor.scene()->selectedItems().size() == 1,
                "Rotation cancellation preserves selection and geometry");
        editor.rotateSelection();
        angleBox()->setValue(90);
        key(Qt::Key_Return);
        const auto rotated = editor.document();
        require(!(rotated == original) && editor.undoStack()->count() == 1,
                "Exact rotation commits one undo operation");
        require(rotated.sprites()[0].angle == 120 && rotated.playerStart().angle == 90,
                "Sector rotation includes directional objects");
        editor.undo(); require(editor.document() == original, "Rotation undo restores all properties");
        editor.redo(); require(editor.document() == rotated, "Rotation redo restores all properties");
        editor.undo();
        editor.mirrorSelection(true);
        key(Qt::Key_Return);
        require(editor.document().sprites()[0].position == QPointF(896,128)
                && editor.document().sprites()[0].angle == 150, "Horizontal mirror flips X and facing");
        editor.undo(); require(editor.document() == original, "Mirror is undoable");
        editor.rotateSelection();
        for (auto *button : editor.findChildren<QPushButton *>())
            if (button->isVisible() && button->text() == "Pick pivot in map") button->click();
        click({0,0});
        angleBox()->setValue(90);
        key(Qt::Key_Return);
        require(editor.document().vertices()[1].position == QPointF(0,1024),
                "Custom pivot rotation uses picked origin");
        editor.undo();
        editor.rotateSelection();
        for (auto *box : editor.findChildren<QDoubleSpinBox *>("transformPivotX"))
            if (box->isVisible()) box->setValue(131072);
        angleBox()->setValue(180);
        key(Qt::Key_Return);
        require(editor.document() == original && angleBox()->isVisible(),
                "Invalid preview cannot commit and remains adjustable");
        key(Qt::Key_Escape);
        editor.rotateSelection();
        hover({1024,256}); hover({512,768});
        require(angleBox()->value() == 90, "Pointer rotation uses pivot-relative angle");
        key(Qt::Key_Escape);
        editor.rotateSelection();
        angleBox()->setValue(90);
        editor.setMode(MapEditor::Mode::Lines);
        require(editor.document() == original,
                "Mode switch cancels preview");
        editor.setMode(MapEditor::Mode::Sectors);
        click({512,256});
        editor.copySelectedSectors(); editor.pasteCopiedSectors();
        require(editor.hasFloatingPaste(), "Transform floating paste source");
        editor.rotateSelection(); angleBox()->setValue(90); key(Qt::Key_Return);
        require(editor.hasFloatingPaste() && editor.document() == original,
                "Transforming a floating paste keeps placement pending");
        key(Qt::Key_Escape);
        require(!editor.hasFloatingPaste() && editor.document() == original,
                "Cancelling transformed paste leaves source intact");
    }
    for (auto mode : {MapEditor::Mode::Vertices, MapEditor::Mode::Lines, MapEditor::Mode::Sectors}) {
        editor.recoverDocument(source, {});
        editor.clearEditingScope();
        editor.setMode(mode);
        editor.centerOn(512,512);
        const QPointF grab = mode == MapEditor::Mode::Vertices ? QPointF(0,0)
            : mode == MapEditor::Mode::Lines ? QPointF(512,0) : QPointF(512,512);
        click(grab);
        require(editor.scene()->selectedItems().size() == 1, "Select object for keyboard move");
        editor.moveSelection();
        hover(grab);
        require(!editor.canAutosave(), "Move preview is an open transaction");
        hover(grab + QPointF(-256,0));
        require(editor.document().vertices()[0].position == QPointF(-256,0), "Keyboard move snaps geometry");
        key(Qt::Key_Escape);
        require(editor.document() == source && editor.undoStack()->count() == 0
            && editor.scene()->selectedItems().size() == 1 && editor.canAutosave(),
            "Escape restores document and selection without undo history");
        editor.moveSelection();
        hover(grab);
        hover(grab + QPointF(-256,0));
        click(grab + QPointF(-256,0));
        const auto moved = editor.document();
        require(!(moved == source) && editor.undoStack()->count() == 1 && editor.canAutosave(),
            "Click commits one keyboard move");
        if (mode == MapEditor::Mode::Lines)
            require(moved.vertices()[1].position == QPointF(768,0)
                && moved.vertices()[2] == source.vertices()[2], "Line move translates both endpoints only");
        if (mode == MapEditor::Mode::Sectors)
            for (std::size_t v = 0; v < source.vertices().size(); ++v)
                require(moved.vertices()[v].position == source.vertices()[v].position + QPointF(-256,0),
                    "Sector keyboard move translates all boundary vertices");
        editor.undo(); require(editor.document() == source, "Keyboard move undo");
        editor.redo(); require(editor.document() == moved, "Keyboard move redo");
    }

    editor.recoverDocument(source, {});
    editor.setMode(MapEditor::Mode::Vertices);
    editor.centerOn(512,512);
    click({0,0}); click({1024,0}, Qt::ShiftModifier);
    // Toolbar movement starts at the center of the selected vertices.
    const QPointF selectionCenter(512,0);
    editor.moveSelection();
    require(editor.document() == source, "Starting toolbar move leaves the selection in place");
    hover(selectionCenter);
    require(editor.document() == source, "Centered pointer leaves the selection in place");
    hover(selectionCenter + QPointF(-100,0), Qt::AltModifier);
    const auto freeDelta = editor.mapToScene(editor.mapFromScene(selectionCenter + QPointF(-100,0)))
        - selectionCenter;
    require(editor.document().vertices()[0].position == freeDelta
        && editor.document().vertices()[1].position == QPointF(1024,0) + freeDelta,
        "Alt moves multiple selected vertices freely while preserving spacing");
    key(Qt::Key_Escape);
    click({0,0});
    editor.moveSelection(); hover({0,0}); hover({1024,1024}); key(Qt::Key_Return);
    require(editor.document() == source && editor.undoStack()->count() == 0,
        "Invalid keyboard move rolls back without history");
    editor.moveSelection(); key(Qt::Key_Return);
    require(editor.document() == source && editor.undoStack()->count() == 0,
        "Confirming without moving creates no history");

    editor.recoverDocument(source, {});
    editor.setMode(MapEditor::Mode::Sprites);
    editor.centerOn(512,512);
    const auto checkEmptySpritePreview = [&] {
        require(editor.document().sprites().size() == 1
            && editor.document().sprites()[0].texture == -1
            && editor.document().sprites()[0].position == QPointF(256,256)
            && editor.document().sprites()[0].sectorId == 0
            && editor.document().sprites()[0].z == -2048,
            "Empty sprite exists at its final placement before texture picker opens");
        const auto selected = editor.scene()->selectedItems();
        require(selected.size() == 1 && selected.front()->isVisible()
            && selected.front()->pos() == QPointF(256,256),
            "Empty sprite preview is visible and selected before texture picker opens");
        require(!editor.canAutosave() && editor.undoStack()->count() == 0,
            "Sprite preview remains pending until texture choice");
    };
    editor.setTextureSelector([&](std::optional<int>) -> std::optional<MapEditor::SpriteTexture> {
        checkEmptySpritePreview();
        return std::nullopt;
    });
    editor.addSprite(); click({256,256});
    require(editor.document() == source && editor.undoStack()->count() == 0,
        "Cancel add texture picker creates no sprite or history");
    editor.setTextureSelector([&](std::optional<int>) -> std::optional<MapEditor::SpriteTexture> {
        checkEmptySpritePreview();
        return MapEditor::SpriteTexture{42, QImage()};
    });
    editor.addSprite(); click({256,256});
    editor.setTextureSelector([](std::optional<int>) -> std::optional<MapEditor::SpriteTexture> {
        return MapEditor::SpriteTexture{42, QImage()};
    });
    require(editor.document().sprites().size() == 1 && editor.document().sprites()[0].texture == 42
        && editor.document().sprites()[0].position == QPointF(256,256)
        && editor.document().sprites()[0].sectorId == 0 && editor.document().sprites()[0].z == -2048
        && editor.undoStack()->count() == 1, "Add sprite sets texture, snapped position, sector and floor in one edit");
    const auto withSprite = editor.document();
    editor.moveSelection(); hover({256,256}); hover({512,256}); key(Qt::Key_Return);
    require(editor.document().sprites()[0].position == QPointF(512,256)
        && editor.undoStack()->count() == 2, "Enter commits sprite keyboard movement");
    editor.undo(); require(editor.document() == withSprite, "Undo sprite movement");
    click({512,512}); editor.moveSelection(); hover({512,512}); hover({768,512}); key(Qt::Key_Return);
    require(editor.document().playerStart().position == QPointF(768,512), "Keyboard move supports player start");
    editor.undo();

    // A toolbar invocation starts outside the viewport and waits for a placement click.
    QCursor::setPos(editor.viewport()->mapToGlobal(QPoint(-30,-30)));
    editor.addSprite();
    require(editor.document() == withSprite, "Toolbar add waits for a position");
    key(Qt::Key_Escape); click({768,768});
    require(editor.document() == withSprite, "Escape cancels pending sprite placement");
    QCursor::setPos(editor.viewport()->mapToGlobal(QPoint(-30,-30)));
    editor.addSprite(); click({768,768});
    require(editor.document().sprites().size() == 2, "Toolbar add places sprite at clicked position");
    editor.undo(); require(editor.document() == withSprite, "Toolbar add undo");
    click({256,256});
    QCursor::setPos(editor.viewport()->mapToGlobal(QPoint(-30,-30)));
    editor.moveSelection(); hover({256,256});
    require(editor.document() == withSprite, "Toolbar move establishes origin on entering the map");
    hover({512,256});
    QFocusEvent lostFocus(QEvent::FocusOut);
    QApplication::sendEvent(&editor, &lostFocus);
    require(editor.document() == withSprite && editor.canAutosave(), "Losing focus cancels movement safely");

    // Enter edits a selected sprite, but never the player start or an empty selection.
    editor.recoverDocument(withSprite, {});
    editor.setMode(MapEditor::Mode::Sprites);
    click({256,256});
    int pickerCalls = 0;
    editor.setTextureSelector([&](std::optional<int> current) -> std::optional<MapEditor::SpriteTexture> {
        ++pickerCalls;
        require(current == 42, "Enter picker starts at the current sprite texture");
        return MapEditor::SpriteTexture{43, QImage()};
    });
    key(Qt::Key_Return);
    auto textured = withSprite;
    textured.setSpriteTexture(0, 43);
    require(editor.document() == textured && pickerCalls == 1
        && editor.undoStack()->count() == 1 && editor.scene()->selectedItems().size() == 1,
        "Enter changes only sprite texture in one undoable edit and retains selection");
    editor.undo(); require(editor.document() == withSprite, "Undo Enter texture change");
    editor.redo(); require(editor.document() == textured, "Redo Enter texture change");
    editor.setTextureSelector([&](std::optional<int>) -> std::optional<MapEditor::SpriteTexture> {
        ++pickerCalls;
        return std::nullopt;
    });
    key(Qt::Key_Enter);
    require(pickerCalls == 2 && editor.document() == textured && editor.undoStack()->count() == 1,
        "Keypad Enter opens picker; cancellation changes neither map nor history");
    QKeyEvent repeatedEnter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QString(), true);
    QApplication::sendEvent(&editor, &repeatedEnter);
    require(pickerCalls == 2, "Held Enter does not reopen picker");
    click({512,512}); key(Qt::Key_Return);
    editor.scene()->clearSelection(); key(Qt::Key_Return);
    require(pickerCalls == 2 && editor.document() == textured,
        "Enter ignores player start and empty selection");
    click({256,256}); editor.moveSelection(); hover({256,256}); hover({512,256}); key(Qt::Key_Return);
    require(pickerCalls == 2 && editor.document().sprites()[0].position == QPointF(512,256),
        "Enter confirms active move without opening texture picker");

    // Sector paste remains outside the document until the floating group is deselected.
    editor.recoverDocument(source, {});
    editor.setMode(MapEditor::Mode::Sectors);
    editor.centerOn(1024,512);
    QApplication::processEvents();
    click({512,512});
    const auto shortcut = [&](int code) {
        QKeyEvent event(QEvent::KeyPress, code, Qt::ControlModifier);
        QApplication::sendEvent(&editor, &event);
    };
    shortcut(Qt::Key_C);
    QCursor::setPos(editor.viewport()->mapToGlobal(editor.mapFromScene({2048,0})));
    shortcut(Qt::Key_V);
    require(editor.hasFloatingPaste() && editor.document() == source && editor.undoStack()->count() == 0,
        "Paste creates a floating group without changing geometry or history");
    require(!editor.canAutosave() && editor.hasUnsavedChanges(), "Pending paste participates in dirty and autosave state");
    const auto dragFloating = [&](QPointF from, QPointF to) {
        const QPoint a = editor.mapFromScene(from), b = editor.mapFromScene(to);
        QMouseEvent press(QEvent::MouseButtonPress, a, editor.viewport()->mapToGlobal(a),
            Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &press);
        QMouseEvent move(QEvent::MouseMove, b, editor.viewport()->mapToGlobal(b),
            Qt::NoButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &move);
        QMouseEvent release(QEvent::MouseButtonRelease, b, editor.viewport()->mapToGlobal(b),
            Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &release);
    };
    require(editor.scene()->selectedItems().size() == 1, "Floating sectors are selected as one group");
    const auto floatingItem = editor.scene()->selectedItems().front();
    const auto floatingOffset = floatingItem->pos();
    const auto floatingCenter = floatingItem->sceneBoundingRect().center();
    dragFloating(floatingCenter, floatingCenter + QPointF(512,0));
    require(editor.hasFloatingPaste() && editor.document() == source && editor.undoStack()->count() == 0,
        "Releasing a floating-sector drag does not settle it");
    click({-512,-512});
    require(!editor.hasFloatingPaste() && editor.document().sectors().size() == 2
        && editor.undoStack()->count() == 1 && editor.undoStack()->undoText() == "Paste sectors",
        "Deselect settles the entire paste as one undo step");
    require(editor.document().vertices()[4].position == source.vertices()[0].position + floatingOffset + QPointF(512,0),
        "Floating movement snaps the group without moving its source");
    const auto pastedSectors = editor.document();
    editor.undo(); require(editor.document() == source, "Undo removes the entire sector paste");
    editor.redo(); require(editor.document() == pastedSectors, "Redo restores final placement");
    shortcut(Qt::Key_V); key(Qt::Key_Escape);
    require(!editor.hasFloatingPaste() && editor.document() == pastedSectors && editor.undoStack()->count() == 1,
        "Escape cancels a paste without creating history");
    shortcut(Qt::Key_V); editor.undo();
    require(!editor.hasFloatingPaste() && editor.document() == pastedSectors && editor.undoStack()->index() == 1,
        "Undo during floating paste cancels only the pending copy");
    shortcut(Qt::Key_V);
    editor.scene()->clearSelection();
    QApplication::processEvents();
    require(!editor.hasFloatingPaste() && editor.document().sectors().size() == 3,
        "Explicit selection clearing also settles the paste");
    editor.undo();
    shortcut(Qt::Key_V);
    editor.setMode(MapEditor::Mode::Lines);
    require(!editor.hasFloatingPaste() && editor.document().sectors().size() == 3,
        "Mode switch settles a floating paste");
    editor.undo();
    editor.setMode(MapEditor::Mode::Sectors);
    shortcut(Qt::Key_V);
    QTemporaryDir pasteDirectory;
    QString pasteError;
    require(editor.saveMap(pasteDirectory.filePath("paste.map"), pasteError), "Save settles and saves floating sectors");
    require(!editor.hasFloatingPaste() && !editor.hasUnsavedChanges(), "Saved paste is clean");
    MapDocument savedPaste;
    require(savedPaste.openMap(pasteDirectory.filePath("paste.map"), pasteError)
        && savedPaste.sectors().size() == 3, "Saved map contains settled paste");
    shortcut(Qt::Key_V);
    editor.newMap();
    require(!editor.hasFloatingPaste() && editor.document().sectors().empty(), "New map discards pending paste");

    MapDocument adjacent = source;
    require(adjacent.addPolyline({{1024,0},{2048,0},{2048,1024},{1024,1024}}, true), "Create multiselection fixture");
    const auto sectorSprite = adjacent.addSprite({256,256});
    adjacent.setSpriteTexture(sectorSprite, 123);
    editor.recoverDocument(adjacent, {});
    editor.setMode(MapEditor::Mode::Sectors);
    editor.centerOn(1024,512);
    click({512,512}); click({1536,512}, Qt::ShiftModifier);
    require(editor.scene()->selectedItems().size() == 2, "Select two sectors to copy");
    shortcut(Qt::Key_C); shortcut(Qt::Key_V);
    const auto beforeKeyboardMove = editor.scene()->selectedItems().front()->pos();
    editor.moveSelection();
    hover({2048,2048});
    hover({2560,2048});
    key(Qt::Key_Return);
    require(editor.hasFloatingPaste() && editor.document() == adjacent
        && editor.scene()->selectedItems().front()->pos() != beforeKeyboardMove,
        "Keyboard positioning leaves the copied group floating");
    const auto groupCenter = editor.scene()->selectedItems().front()->sceneBoundingRect().center();
    click(groupCenter, Qt::ShiftModifier);
    require(!editor.hasFloatingPaste() && editor.document().sectors().size() == 4
        && editor.document().sprites().size() == 2 && editor.undoStack()->count() == 1,
        "Shift-deselect settles all copied sectors and their sprites together");
    editor.undo(); require(editor.document() == adjacent, "Undo multisection paste restores source");
    editor.redo();
    require(editor.document().sectors().size() == 4, "Redo restores multisection paste");

    // A rejected commit must leave the pending fragment available for cancellation.
    MapDocument invalidSource = source;
    invalidSource.setVertexPositions({{1, invalidSource.vertices()[0].position}});
    editor.recoverDocument(invalidSource, {});
    editor.setMode(MapEditor::Mode::Sectors);
    click({256,768});
    shortcut(Qt::Key_C); shortcut(Qt::Key_V);
    require(editor.hasFloatingPaste(), "Invalid fixture can be previewed before placement validation");
    editor.scene()->clearSelection();
    QApplication::processEvents();
    require(editor.hasFloatingPaste() && editor.document() == invalidSource && editor.undoStack()->count() == 0,
        "Rejected placement stays floating and creates no undo entry");
    require(!editor.saveMap(pasteDirectory.filePath("invalid.map"), pasteError)
        && !pasteError.isEmpty() && editor.hasFloatingPaste(), "Save reports an invalid paste without discarding it");
    key(Qt::Key_Escape);

}
