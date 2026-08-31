#include "mainwindow.h"

#include <QApplication>
#include <QtPlugin>

#ifdef DUKE_BUILDER_STATIC_XCB_PLUGIN
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
#endif

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("Duke Builder");
    QApplication::setOrganizationName("Duke Builder");

    MainWindow window;
    window.show();

    return application.exec();
}
