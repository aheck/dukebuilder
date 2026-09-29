#include "mapeditor.h"
#include "mapsave.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QInputDialog>
#include <QTimer>
#include <QGraphicsItem>
#include <QSpinBox>
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
}
