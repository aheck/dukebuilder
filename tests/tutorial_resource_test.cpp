#include <QCoreApplication>
#include <QImageReader>
#include <QtPlugin>

#ifdef DUKE_BUILDER_STATIC_JPEG_PLUGIN
Q_IMPORT_PLUGIN(QJpegPlugin)
#endif

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QImageReader reader(":/tutorials/vertical-doors/images/door-sector-select.jpg");
    if (!reader.canRead()) return 1;
    const QImage image = reader.read();
    return image.isNull() || image.width() != 320 || image.height() != 214 ? 1 : 0;
}
