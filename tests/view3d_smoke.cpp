#include "mainwindow.h"
#include "mapeditor.h"
#include "mapview3d.h"
#include "mapsave.h"
#include "texturebrowserwindow.h"
#include "texturebrowserwidget.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QSettings>
#include <QTest>
#include <QAction>
#include <QCursor>
#include <QWheelEvent>
#include <QTreeWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QtPlugin>
#include <iostream>
#ifdef DUKE_BUILDER_STATIC_XCB_PLUGIN
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
Q_IMPORT_PLUGIN(QXcbGlxIntegrationPlugin)
#endif
static void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main(int argc, char **argv)
{
    if (argc != 2) { return 2; }
    QSurfaceFormat format;
    format.setVersion(4,1); format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24); format.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    app.setOrganizationName("DukeBuilderSmoke"); app.setApplicationName("Preview");
    QSettings().setValue("gameData/grpFiles", QStringList{QString::fromLocal8Bit(argv[1])});
    MainWindow window;
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "window exposed");
    MapEditor *editor = nullptr; MapView3D *view = nullptr;
    for (auto *widget : window.findChildren<QWidget *>()) {
        if (auto *e = dynamic_cast<MapEditor *>(widget)) { editor = e; }
        if (auto *v = dynamic_cast<MapView3D *>(widget)) { view = v; }
    }
    require(editor && view, "view widgets");
    MapDocument room;
    require(room.addPolyline({{-4096,-4096},{4096,-4096},{4096,4096},{-4096,4096}}, true), "room");
    room.setSectorCeilingZ(0,-32768);
    QString error;
    QString path = settings.filePath("preview.map");
    require(saveBuildMap(room,path,error), "save fixture");
    require(editor->openMap(path,error), "load fixture");
    auto original = editor->document();
    for (int i=0;i<3;i++) {
        editor->setFocus();
        QCursor::setPos(editor->viewport()->mapToGlobal(editor->viewport()->rect().center()));
        QTest::keyClick(editor, Qt::Key_Q);
        QTest::qWait(300);
        require(view->isVisible() && !editor->isVisible(), "Q enters 3D");
        auto pixels = view->grabFramebuffer();
        require(!pixels.isNull(), "3D framebuffer");
        bool varied = false;
        const auto first = pixels.pixel(0,0);
        for (int y=0;y<pixels.height();y+=16) {
            for (int x=0;x<pixels.width();x+=16) { varied |= pixels.pixel(x,y)!=first; }
        }
        require(varied, "rendered textured map rather than blank frame");
        QTest::keyPress(view, Qt::Key_S); QTest::qWait(80); QTest::keyRelease(view, Qt::Key_S);
        QTest::keyClick(view, Qt::Key_H);
        window.resize(1100+i*50,700); QTest::qWait(80);
        QTest::keyClick(view, Qt::Key_Q); QTest::qWait(80);
        require(editor->isVisible() && !view->isVisible(), "Q returns to 2D");
        require(editor->document() == original && !editor->hasUnsavedChanges(), "preview preserves map");
    }
    editor->setFocus();
    QCursor::setPos(editor->viewport()->mapToGlobal(editor->viewport()->rect().center()));
    QTest::keyClick(editor, Qt::Key_Q);
    QTest::qWait(150);
    QTest::keyClick(view, Qt::Key_Escape);
    require(view->cursor().shape() == Qt::CrossCursor, "released cross cursor");
    const auto wheel = [&](double y, int delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                           bool horizontal = false) {
        QPoint point(view->width()/2, int(view->height()*y));
        QCursor::setPos(view->mapToGlobal(point));
        QTest::qWait(50);
        QWheelEvent event(QPointF(point), QPointF(view->mapToGlobal(point)), {},
                          horizontal ? QPoint(delta,0) : QPoint(0,delta),
                          Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view, &event);
        QTest::qWait(100);
    };
    wheel(0.1,120);
    require(editor->document().sectors()[0].ceilingz == -33792, "wheel raises ceiling");
    require(editor->document().sectors()[0].floorz == 0, "ceiling edit leaves floor alone");
    require(editor->hasUnsavedChanges(), "3D edit marks document dirty");
    wheel(0.9,60);
    require(editor->document().sectors()[0].floorz == 0, "partial wheel notch accumulates");
    wheel(0.9,60);
    require(editor->document().sectors()[0].floorz == -1024, "wheel raises floor");
    wheel(0.9,12000);
    require(editor->document().sectors()[0].floorz == -1024, "reject floor above ceiling");
    QTest::keyClick(view, Qt::Key_H);
    wheel(0.9,120);
    require(editor->document().sectors()[0].floorz == -1024, "disabled highlighting prevents edits");
    QTest::keyClick(view, Qt::Key_H);
    wheel(0.9,-120);
    require(editor->document().sectors()[0].floorz == 0, "wheel lowers floor");
    wheel(0.9,120,Qt::ShiftModifier);
    require(editor->document().sectors()[0].floorz == -128
            && editor->document().sectors()[0].floorheinum == 0, "Shift finely raises floor without slope");
    wheel(0.9,-120,Qt::ShiftModifier);
    const auto ceilingBeforeFine = editor->document().sectors()[0].ceilingz;
    wheel(0.1,120,Qt::ShiftModifier);
    require(editor->document().sectors()[0].ceilingz == ceilingBeforeFine - 128,
            "Shift finely raises ceiling");
    wheel(0.1,-120,Qt::ShiftModifier);
    const auto beforeSlope = editor->document().sectors()[0];
    wheel(0.9,120,Qt::AltModifier,true);
    require(editor->document().sectors()[0].floorheinum == 256,
            "Alt horizontal wheel delta raises slope on X11");
    wheel(0.9,-120,Qt::AltModifier,true);
    require(editor->document().sectors()[0] == beforeSlope,
            "Alt horizontal wheel restores flat floor");
    wheel(0.1,-120,Qt::AltModifier | Qt::ShiftModifier,true);
    require(editor->document().sectors()[0].ceilingheinum == -16,
            "Shift Alt horizontal wheel uses fine slope");
    wheel(0.1,120,Qt::AltModifier | Qt::ShiftModifier,true);
    wheel(0.9,120,Qt::AltModifier);
    require(editor->document().sectors()[0].floorheinum == 256
            && (editor->document().sectors()[0].floorstat & 2), "coarse floor slope enables flag");
    wheel(0.9,-120,Qt::AltModifier);
    require(editor->document().sectors()[0].floorheinum == 0
            && !(editor->document().sectors()[0].floorstat & 2), "zero floor slope clears flag");
    wheel(0.1,-120,Qt::AltModifier | Qt::ShiftModifier);
    require(editor->document().sectors()[0].ceilingheinum == -16
            && (editor->document().sectors()[0].ceilingstat & 2), "fine negative ceiling slope");
    wheel(0.1,120,Qt::AltModifier | Qt::ShiftModifier);
    require(editor->document().sectors()[0].ceilingheinum == 0
            && !(editor->document().sectors()[0].ceilingstat & 2), "fine Alt slope clears ceiling flag");
    wheel(0.9,60,Qt::AltModifier);
    wheel(0.9,60,Qt::AltModifier | Qt::ShiftModifier);
    require(editor->document().sectors()[0].floorheinum == 0, "modifier change resets partial notch");
    wheel(0.9,60,Qt::AltModifier | Qt::ShiftModifier);
    require(editor->document().sectors()[0].floorheinum == 16, "fine slope accumulation");
    require(editor->document().sectors()[0].walls == beforeSlope.walls
            && editor->document().sectors()[0].vertices == beforeSlope.vertices
            && editor->document().sectors()[0].floorz == beforeSlope.floorz,
            "slope preserves first wall and base height");
    const auto aim = [&](double y) {
        QCursor::setPos(view->mapToGlobal(QPoint(view->width()/2, int(view->height()*y))));
        QTest::qWait(50);
    };
    aim(0.1);
    QTest::keyClick(view, Qt::Key_Left);
    require(editor->document().sectors()[0].ceilingxpanning == 1, "left increases ceiling offset");
    QTest::keyClick(view, Qt::Key_Down);
    require(editor->document().sectors()[0].ceilingypanning == 1, "ceiling vertical panning");
    QTest::keyClick(view, Qt::Key_Left, Qt::ShiftModifier);
    require((editor->document().sectors()[0].ceilingstat & 8) != 0, "ceiling smaller size");
    QTest::keyClick(view, Qt::Key_Right, Qt::ShiftModifier);
    require((editor->document().sectors()[0].ceilingstat & 8) == 0, "ceiling larger size");
    aim(0.9);
    QTest::keyClick(view, Qt::Key_Right);
    require(editor->document().sectors()[0].floorxpanning == 255, "floor panning");
    QTest::keyClick(view, Qt::Key_Down, Qt::ShiftModifier);
    require((editor->document().sectors()[0].floorstat & 8) != 0, "floor smaller size");
    aim(0.5);
    QTest::keyClick(view, Qt::Key_Right);
    QTest::keyClick(view, Qt::Key_Up, Qt::ShiftModifier);
    int changedSides = 0;
    for (const auto &wall : editor->document().walls()) {
        for (const auto &side : {wall.forwardSide, wall.reverseSide}) {
            if (side.xpanning == 255 && side.yrepeat == 7 && side.xrepeat == 64) { ++changedSides; }
        }
    }
    require(changedSides == 1, "wall panning and vertical scaling affect one side only");
    const auto choose = [&](double y, int tile) {
        aim(y);
        QTimer closer;
        closer.setInterval(50);
        bool opened = false;
        QObject::connect(&closer, &QTimer::timeout, &window, [&] {
            for (auto *widget : window.findChildren<QWidget *>()) {
                auto *dialog = dynamic_cast<TextureBrowserWindow *>(widget);
                if (!dialog || !dialog->isVisible()) { continue; }
                opened = true;
                if (tile < 0) { dialog->reject(); return; }
                for (auto *child : dialog->findChildren<QWidget *>()) {
                    if (auto *browser = dynamic_cast<TextureBrowserWidget *>(child)) {
                        browser->selectTile(tile);
                        require(browser->selectedTile() == tile, "texture selectable");
                    }
                }
                dialog->accept();
                return;
            }
        });
        closer.start();
        QTest::mouseClick(view, Qt::RightButton, Qt::NoModifier, view->mapFromGlobal(QCursor::pos()));
        closer.stop();
        require(opened, "right click opens existing chooser");
    };
    aim(0.1);
    const auto beforeEmptyPaste = editor->document();
    QTest::keyClick(view, Qt::Key_V, Qt::ControlModifier);
    require(editor->document() == beforeEmptyPaste, "empty texture clipboard does nothing");
    auto beforeCancel = editor->document();
    choose(0.1, -1);
    require(editor->document() == beforeCancel, "cancel texture selection preserves document");
    choose(0.1, 0);
    require(editor->document().sectors()[0].ceilingTexture == 0, "ceiling texture changed");
    choose(0.9, 0);
    require(editor->document().sectors()[0].floorTexture == 0, "floor texture changed");
    int copyTile = -1;
    for (auto *widget : window.findChildren<QWidget *>()) {
        auto *browser = dynamic_cast<TextureBrowserWindow *>(widget);
        if (!browser) { continue; }
        for (int tile = 100; tile < 200 && copyTile < 0; ++tile) {
            const auto image = browser->textureImage(tile);
            bool opaque = !image.isNull();
            for (int y = 0; y < image.height() && opaque; ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (qAlpha(image.pixel(x,y)) != 255) { opaque = false; break; }
                }
            }
            if (opaque) { copyTile = tile; }
        }
    }
    require(copyTile >= 0, "opaque texture fixture");
    choose(0.5, copyTile);
    int changedTextures = 0;
    for (const auto &wall : editor->document().walls()) {
        for (const auto &side : {wall.forwardSide, wall.reverseSide}) {
            if (side.texture == copyTile) { ++changedTextures; }
        }
    }
    require(changedTextures == 1, "wall texture changed on one side");
    aim(0.5);
    const auto beforeCopy = editor->document();
    QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
    require(editor->document() == beforeCopy, "copy does not edit map");
    aim(0.1);
    QTest::keyClick(view, Qt::Key_V, Qt::ControlModifier);
    require(editor->document().sectors()[0].ceilingTexture == copyTile, "wall texture pasted onto ceiling");
    aim(0.9);
    QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
    aim(0.5);
    QTest::keyClick(view, Qt::Key_V, Qt::ControlModifier);
    int pastedSides = 0;
    for (const auto &wall : editor->document().walls()) {
        for (const auto &side : {wall.forwardSide, wall.reverseSide}) {
            if (side.texture == 0 && side.xpanning == 255 && side.yrepeat == 7) { ++pastedSides; }
        }
    }
    require(pastedSides == 1, "paste changes one wall side and preserves mapping");
    QTest::keyClick(view, Qt::Key_R);
    int resetSides = 0;
    for (const auto &wall : editor->document().walls()) {
        for (const auto &side : {wall.forwardSide, wall.reverseSide}) {
            if (side.texture == 0 && side.xpanning == 255 && side.yrepeat == 8 && side.xrepeat == 64) {
                ++resetSides;
            }
        }
    }
    require(resetSides == 1, "reset restores wall scale and preserves panning");
    QTest::keyClick(view, Qt::Key_Q);
    require(editor->isVisible(), "return to 2D after edits");
    require(editor->document().sectors()[0].ceilingz == -33792, "3D edit persists into 2D");
    require(editor->saveMap(path,error), "save edited map");
    MapDocument saved;
    require(saved.openMap(path,error), "reload edited map");
    require(saved.sectors()[0].ceilingz == -33792, "saved ceiling height");
    require(saved.sectors()[0].floorheinum == 16 && (saved.sectors()[0].floorstat & 2),
            "saved slope and flag");
    require(saved.sectors()[0].ceilingxpanning == 1 && saved.sectors()[0].ceilingypanning == 1,
            "saved ceiling texture offsets");
    require(saved.sectors()[0].floorxpanning == 255 && (saved.sectors()[0].floorstat & 8),
            "saved floor offsets and scale");
    require(saved.sectors()[0].ceilingTexture == copyTile && saved.sectors()[0].floorTexture == 0,
            "chosen textures persist in saved map");
    editor->setMode(MapEditor::Mode::Sectors);
    QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::NoModifier,
                      editor->mapFromScene(QPointF(0,0)));
    auto *properties = window.findChild<QTreeWidget *>("PropertiesControl");
    require(properties, "properties tree");
    QTreeWidgetItem *lotag = nullptr;
    for (int i=0; i<properties->topLevelItemCount(); ++i) {
        if (properties->topLevelItem(i)->text(0) == "Lotag") { lotag = properties->topLevelItem(i); }
    }
    require(lotag, "sector lotag row");
    properties->scrollToItem(lotag);
    auto *combo = qobject_cast<QComboBox *>(properties->itemWidget(lotag,1));
    require(combo, "lotag editor");
    combo->lineEdit()->setFocus();
    combo->lineEdit()->selectAll();
    QTest::keyClicks(combo->lineEdit(), "12345");
    editor->setFocus();
    QTest::qWait(100);
    require(editor->document().sectors()[0].lotag == 12345, "custom lotag committed");
    for (int i=0; i<properties->topLevelItemCount(); ++i) {
        auto *row = properties->topLevelItem(i);
        if (row->text(0) != "Lotag") { continue; }
        combo = qobject_cast<QComboBox *>(properties->itemWidget(row,1));
        require(combo, "refreshed lotag editor");
        combo->lineEdit()->setFocus();
        combo->lineEdit()->selectAll();
        QTest::keyClicks(combo->lineEdit(), "23456");
        QTest::keyClick(combo->lineEdit(), Qt::Key_Return);
        break;
    }
    QTest::qWait(100);
    require(editor->document().sectors()[0].lotag == 23456, "custom lotag committed with Enter");
    auto spriteMap = editor->document();
    const auto spriteId = spriteMap.addSprite({512,512});
    spriteMap.setSpriteTexture(spriteId, 1);
    spriteMap.setSpriteZ(spriteId, -4096);
    require(saveBuildMap(spriteMap, path, error) && editor->openMap(path, error), "sprite lotag fixture");
    editor->setMode(MapEditor::Mode::Sprites);
    QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::NoModifier,
                      editor->mapFromScene(QPointF(512,512)));
    for (const bool enter : {false, true}) {
        QComboBox *tagEditor = nullptr;
        for (int i=0; i<properties->topLevelItemCount(); ++i) {
            auto *row = properties->topLevelItem(i);
            if (row->text(0) == "Lotag") {
                tagEditor = qobject_cast<QComboBox *>(properties->itemWidget(row,1));
                properties->scrollToItem(row);
            }
        }
        require(tagEditor, "sprite lotag editor");
        tagEditor->lineEdit()->setFocus();
        tagEditor->lineEdit()->selectAll();
        QTest::keyClicks(tagEditor->lineEdit(), enter ? "65535" : "-12345", Qt::NoModifier, 20);
        if (enter) { QTest::keyClick(tagEditor->lineEdit(), Qt::Key_Return); }
        else { editor->setFocus(); }
        QTest::qWait(100);
        require(editor->document().sprites()[spriteId].lotag == (enter ? -1 : -12345),
                "sprite custom lotag commits on Enter and focus loss");
    }
    editor->setFocus();
    QTest::keyClick(editor, Qt::Key_O);
    require(editor->document().sprites()[spriteId].position == QPointF(4095,512)
            && (editor->document().sprites()[spriteId].cstat & 48) == 16
            && editor->document().sprites()[spriteId].z == -4096,
            "O ornaments the selected sprite in 2D and preserves height");
    require(editor->saveMap(path, error), "save custom sprite lotag");
    MapDocument spriteReload;
    require(spriteReload.openMap(path, error) && spriteReload.sprites()[spriteId].lotag == -1,
            "sprite lotag 65535 alias persists as -1 in saved map");
    MapDocument sprite3D;
    require(sprite3D.addPolyline({{-4096,-4096},{4096,-4096},{4096,4096},{-4096,4096}}, true),
            "3D sprite room");
    sprite3D.setSectorCeilingZ(0,-32768);
    const auto targetId = sprite3D.addSprite({3072,0});
    auto target = sprite3D.sprites()[targetId];
    target.texture = copyTile;
    target.z = -6144;
    target.cstat = 128;
    target.lotag = 123;
    sprite3D.setSprite(targetId,target);
    require(saveBuildMap(sprite3D,path,error) && editor->openMap(path,error), "3D sprite fixture");
    editor->setFocus();
    QCursor::setPos(editor->viewport()->mapToGlobal(editor->mapFromScene(QPointF(0,0))));
    QTest::keyClick(editor, Qt::Key_Q);
    QTest::qWait(200);
    require(view->isVisible(), "enter 3D to select sprite");
    const auto beforeWheel = editor->document().sprites()[targetId];
    QTest::keyClick(view, Qt::Key_Escape);
    wheel(0.5,60);
    require(editor->document().sprites()[targetId].z == -6144, "sprite partial wheel notch");
    wheel(0.5,60);
    require(editor->document().sprites()[targetId].z == -7168, "wheel raises highlighted sprite");
    wheel(0.5,-120);
    require(editor->document().sprites()[targetId] == beforeWheel, "wheel lowers sprite and preserves other properties");
    wheel(0.5,120,Qt::ShiftModifier);
    require(editor->document().sprites()[targetId].z == -6272, "Shift finely raises sprite");
    wheel(0.5,-120,Qt::ShiftModifier);
    QTest::keyClick(view, Qt::Key_O);
    const auto placed3D = editor->document().sprites()[targetId];
    require(placed3D.position == QPointF(4095,0) && placed3D.angle == 180
            && placed3D.z == -6144 && placed3D.lotag == 123 && placed3D.cstat == (128|16),
            "O picks and sticks the sprite under the 3D crosshair to nearest wall");
    wheel(0.5,120);
    require(editor->document().sprites()[targetId].z == -7168, "wheel raises wall-aligned sprite");
    QTest::keyClick(view, Qt::Key_Q);
    require(editor->isVisible() && editor->hasUnsavedChanges(), "3D sprite placement persists into 2D");
    require(editor->saveMap(path,error), "save 3D sprite edit");
    MapDocument placedReload;
    require(placedReload.openMap(path,error)
            && placedReload.sprites()[targetId].position == placed3D.position
            && placedReload.sprites()[targetId].z == -7168,
            "saved 3D sprite placement");
    MapDocument nested;
    require(nested.addPolyline({{-8192,-8192},{8192,-8192},{8192,8192},{-8192,8192}}, true), "nested outer room");
    nested.setSectorCeilingZ(0,-32768);
    require(nested.addPolyline({{-4096,-4096},{4096,-4096},{4096,4096},{-4096,4096}}, true), "nested inner room");
    nested.setPlayerStartPosition({-6144,0});
    nested.setPlayerStartZ(-6144);
    require(saveBuildMap(nested,path,error) && editor->openMap(path,error), "load nested fixture");
    const auto actualStart = editor->document().playerStart();
    editor->centerOn(QPointF(0,0));
    editor->setFocus();
    QCursor::setPos(editor->viewport()->mapToGlobal(editor->mapFromScene(QPointF(0,0))));
    QTest::keyClick(editor, Qt::Key_Q);
    QTest::qWait(200);
    QTest::keyClick(view, Qt::Key_Escape);
    // Cross the preview-only starting Z (-6144) in one edit while the floor
    // is still under the pointer. The old snapshot validation rejected this.
    wheel(0.9,8*120);
    require(editor->document().sectors()[1].floorz == -8192,
            "nested floor can rise past temporary preview start Z");
    require(editor->document().sectors()[0].floorz == 0,
            "raising nested floor preserves parent height");
    QTest::keyClick(view, Qt::Key_Q);
    require(editor->document().playerStart() == actualStart, "preview rebuild preserves actual player start");
    require(editor->saveMap(path,error), "save raised nested floor");
    MapDocument joinRoom;
    require(joinRoom.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}},true), "join UI room 0");
    require(joinRoom.addPolyline({{1024,0},{2048,0},{2048,1024},{1024,1024}},true), "join UI room 1");
    auto sourceProperties = joinRoom.sectors()[1];
    sourceProperties.floorTexture = 42;
    sourceProperties.lotag = 15;
    joinRoom.setSector(1,sourceProperties);
    joinRoom.setPlayerStartPosition({512,512});
    require(saveBuildMap(joinRoom,path,error) && editor->openMap(path,error), "load join UI fixture");
    editor->setMode(MapEditor::Mode::Sectors);
    editor->centerOn(QPointF(1024,512));
    QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::NoModifier, editor->mapFromScene(QPointF(1536,512)));
    QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::ShiftModifier, editor->mapFromScene(QPointF(512,512)));
    require(editor->canJoinSelectedSectors(), "join enabled for multiple sectors");
    QTest::keyClick(editor,Qt::Key_J);
    require(editor->document().sectors().size() == 1
            && editor->document().sectors()[0].floorTexture == 42
            && editor->document().sectors()[0].lotag == 15, "J uses chronological selection, not sector number");
    require(!editor->canJoinSelectedSectors() && editor->hasUnsavedChanges(), "joined sector remains selected and dirty");
    require(editor->saveMap(path,error), "save joined map");
    MapDocument joinReload;
    require(joinReload.openMap(path,error) && joinReload.sectors().size() == 1
            && joinReload.sectors()[0].floorTexture == 42, "joined map reload preserves source properties");
    MapDocument holeRoom;
    require(holeRoom.addPolyline({{0,0},{4096,0},{4096,4096},{0,4096}},true), "hole deletion outer");
    require(holeRoom.addPolyline({{1024,1024},{3072,1024},{3072,3072},{1024,3072}},true), "hole deletion inner");
    holeRoom.setPlayerStartPosition({512,512});
    require(saveBuildMap(holeRoom,path,error) && editor->openMap(path,error), "load hole deletion fixture");
    editor->setMode(MapEditor::Mode::Sectors);
    editor->centerOn(QPointF(2048,2048));
    QTest::mouseClick(editor->viewport(),Qt::LeftButton,Qt::NoModifier,editor->mapFromScene(QPointF(2048,2048)));
    QTest::keyClick(editor,Qt::Key_Delete);
    require(editor->document().sectors().size() == 1 && editor->hasUnsavedChanges(), "Delete removes selected inner sector");
    require(editor->saveMap(path,error), "save sector with void hole");
    MapDocument holeReload;
    require(holeReload.openMap(path,error) && holeReload.sectors().size() == 1
            && holeReload.sectors()[0].loopStarts.size() == 2, "Void hole persists after reload");
    std::cout << "3D toggle, rendering, height and texture edits, validation and save persistence passed\n";
    return 0;
}
