#include <QCoreApplication>
#include <QImageReader>
#include <QFile>
#include <QtPlugin>

#ifdef DUKE_BUILDER_STATIC_JPEG_PLUGIN
Q_IMPORT_PLUGIN(QJpegPlugin)
#endif

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
#ifndef Q_OS_MACOS
    for (int size : {16, 32, 48, 64, 128, 256}) {
        QImageReader iconReader(QString(":/icons/icon-%1x%1.png").arg(size));
        const QImage icon = iconReader.read();
        if (icon.isNull() || icon.size() != QSize(size, size)) return 1;
    }
#endif
    QFile tutorial(":/tutorials/tror-stacked-rooms/index.html");
    if (!tutorial.open(QIODevice::ReadOnly)) return 1;
    const auto html = tutorial.readAll();
    if (!html.contains("Extend floor downward") || !html.contains("16384")
        || !html.contains("Layer above") || !html.contains("</html>")) return 1;
    QImageReader reader(":/tutorials/vertical-doors/images/door-sector-select.jpg");
    if (!reader.canRead()) return 1;
    const QImage image = reader.read();
    return image.isNull() || image.width() != 320 || image.height() != 214 ? 1 : 0;
}
