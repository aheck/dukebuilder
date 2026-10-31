#include "mainwindow.h"
#include "desktopintegration.h"

#include <QApplication>
#include <QIcon>
#include <QtPlugin>
#include <QSurfaceFormat>
#include <QFile>
#include <QStandardPaths>
#include <QDebug>

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
#ifdef Q_OS_LINUX
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty()) {
        QGuiApplication::setDesktopFileName("dukebuilder-appimage");
        const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        if (!qEnvironmentVariableIsSet("DUKEBUILDER_NO_DESKTOP_INTEGRATION")
            && !QFile::exists(dataHome + "/appimagekit/no_desktopintegration")) {
            QFile icon(":/icons/icon-256x256.png");
            const QByteArray iconBytes = icon.open(QIODevice::ReadOnly) ? icon.readAll() : QByteArray();
            QString error;
            if (!installAppImageDesktopEntry(appImage, dataHome, iconBytes, error))
                qWarning().noquote() << "Could not add Duke Builder to the applications menu:" << error;
        }
    }
#endif
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
