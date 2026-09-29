#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QtPlugin>
#include <QSurfaceFormat>

#ifdef DUKE_BUILDER_STATIC_XCB_PLUGIN
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
Q_IMPORT_PLUGIN(QXcbGlxIntegrationPlugin)
#endif
#ifdef DUKE_BUILDER_STATIC_JPEG_PLUGIN
Q_IMPORT_PLUGIN(QJpegPlugin)
#endif

int main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc, argv);
    QApplication::setApplicationName("Duke Builder");
    QApplication::setOrganizationName("Duke Builder");
    QGuiApplication::setDesktopFileName("dukebuilder");
#ifndef Q_OS_MACOS
    // macOS uses the bundle's ICNS, including any --icon override.
    QIcon applicationIcon;
    for (int size : {16, 32, 48, 64, 128, 256}) {
        applicationIcon.addFile(QString(":/icons/icon-%1x%1.png").arg(size));
    }
    QApplication::setWindowIcon(applicationIcon);
#endif

    MainWindow window;
    window.show();

    return application.exec();
}
