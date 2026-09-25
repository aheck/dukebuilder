#include "mapeditor.h"
#include "mapsave.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTemporaryDir>
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
}
