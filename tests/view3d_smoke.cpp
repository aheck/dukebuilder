#include "mainwindow.h"
#include "mapeditor.h"
#include "mapview3d.h"
#include "mapsave.h"
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
    const auto wheel = [&](double y, int delta) {
        QPoint point(view->width()/2, int(view->height()*y));
        QCursor::setPos(view->mapToGlobal(point));
        QTest::qWait(50);
        QWheelEvent event(QPointF(point), QPointF(view->mapToGlobal(point)), {}, QPoint(0,delta),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
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
    QTest::keyClick(view, Qt::Key_Q);
    require(editor->isVisible(), "return to 2D after edits");
    require(editor->document().sectors()[0].ceilingz == -33792, "3D edit persists into 2D");
    require(editor->saveMap(path,error), "save edited map");
    MapDocument saved;
    require(saved.openMap(path,error), "reload edited map");
    require(saved.sectors()[0].ceilingz == -33792, "saved ceiling height");
    std::cout << "3D toggle, rendering, wheel edits, validation and save persistence passed\n";
    return 0;
}
