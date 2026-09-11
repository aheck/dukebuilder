#include "mainwindow.h"

#include <QApplication>
#include <QtPlugin>
#include <QSurfaceFormat>

#ifdef DUKE_BUILDER_STATIC_XCB_PLUGIN
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
Q_IMPORT_PLUGIN(QXcbGlxIntegrationPlugin)
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

    MainWindow window;
    window.show();

    return application.exec();
}
