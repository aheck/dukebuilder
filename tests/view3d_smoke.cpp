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
    std::cout << "3D toggle, render, resize and document preservation passed\n";
    return 0;
}
