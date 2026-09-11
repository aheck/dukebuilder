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
    const auto wheel = [&](double y, int delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QPoint point(view->width()/2, int(view->height()*y));
        QCursor::setPos(view->mapToGlobal(point));
        QTest::qWait(50);
        QWheelEvent event(QPointF(point), QPointF(view->mapToGlobal(point)), {}, QPoint(0,delta),
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
    const auto beforeSlope = editor->document().sectors()[0];
    wheel(0.9,120,Qt::ShiftModifier);
    require(editor->document().sectors()[0].floorheinum == 256
            && (editor->document().sectors()[0].floorstat & 2), "coarse floor slope enables flag");
    wheel(0.9,-120,Qt::ShiftModifier);
    require(editor->document().sectors()[0].floorheinum == 0
            && !(editor->document().sectors()[0].floorstat & 2), "zero floor slope clears flag");
    wheel(0.1,-120,Qt::ControlModifier);
    require(editor->document().sectors()[0].ceilingheinum == -16
            && (editor->document().sectors()[0].ceilingstat & 2), "fine negative ceiling slope");
    wheel(0.1,120,Qt::ControlModifier | Qt::ShiftModifier);
    require(editor->document().sectors()[0].ceilingheinum == 0
            && !(editor->document().sectors()[0].ceilingstat & 2), "Ctrl takes precedence and zero clears ceiling flag");
    wheel(0.9,60,Qt::ShiftModifier);
    wheel(0.9,60,Qt::ControlModifier);
    require(editor->document().sectors()[0].floorheinum == 0, "modifier change resets partial notch");
    wheel(0.9,60,Qt::ControlModifier);
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
            if (side.xpanning == 255 && side.yrepeat == 7 && side.xrepeat == 8) { ++changedSides; }
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
    choose(0.5, 102);
    int changedTextures = 0;
    for (const auto &wall : editor->document().walls()) {
        for (const auto &side : {wall.forwardSide, wall.reverseSide}) {
            if (side.texture == 102) { ++changedTextures; }
        }
    }
    require(changedTextures == 1, "wall texture changed on one side");
    aim(0.5);
    const auto beforeCopy = editor->document();
    QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
    require(editor->document() == beforeCopy, "copy does not edit map");
    aim(0.1);
    QTest::keyClick(view, Qt::Key_V, Qt::ControlModifier);
    require(editor->document().sectors()[0].ceilingTexture == 102, "wall texture pasted onto ceiling");
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
    require(saved.sectors()[0].ceilingTexture == 102 && saved.sectors()[0].floorTexture == 0,
            "chosen textures persist in saved map");
    std::cout << "3D toggle, rendering, height and texture edits, validation and save persistence passed\n";
    return 0;
}
