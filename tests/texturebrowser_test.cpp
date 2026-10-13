#include "texturebrowserwindow.h"
#include "texturebrowserwidget.h"
#include <QApplication>
#include <QDataStream>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtPlugin>
#include <cstdlib>
#include <iostream>

#ifdef DUKE_BUILDER_STATIC_MINIMAL_PLUGIN
Q_IMPORT_PLUGIN(QMinimalIntegrationPlugin)
#endif

static void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

// One opaque ART pixel and one identity palette lookup, without game assets.
static void archive(const QString &path, int channel, int lookupId)
{
    QByteArray palette(768 + 2 + 65536, char(0));
    palette[3 + channel] = 63;
    QByteArray lookup(258, char(0));
    lookup[0] = 1;
    lookup[1] = static_cast<char>(lookupId);
    for (int i = 0; i < 256; ++i) { lookup[2 + i] = static_cast<char>(i); }
    QByteArray art;
    QDataStream artStream(&art, QIODevice::WriteOnly);
    artStream.setByteOrder(QDataStream::LittleEndian);
    artStream << qint32(1) << qint32(1) << qint32(0) << qint32(0)
              << qint16(1) << qint16(1) << quint32(0) << quint8(1);
    const QList<QPair<QByteArray, QByteArray>> entries = {
        {"PALETTE.DAT", palette}, {"LOOKUP.DAT", lookup}, {"TILES000.ART", art}};
    QByteArray grp;
    QDataStream stream(&grp, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("KenSilverman", 12);
    stream << quint32(entries.size());
    for (const auto &entry : entries) {
        const auto name = entry.first.leftJustified(12, '\0');
        stream.writeRawData(name.constData(), 12);
        stream << quint32(entry.second.size());
    }
    for (const auto &entry : entries) {
        stream.writeRawData(entry.second.constData(), entry.second.size());
    }
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(grp) == grp.size(), "Write GRP fixture");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "Temporary directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    app.setOrganizationName("DukeBuilderTests");
    app.setApplicationName("TextureBrowser");
    TextureBrowserWindow window;
    require(window.textureImage(0).isNull(), "No configured archive");

    const auto first = directory.filePath("first.grp");
    archive(first, 0, 1);
    QSettings().setValue("gameData/grpFiles", QStringList{first});
    require(window.textureImage(0).pixelColor(0, 0) == QColor(Qt::red),
            "Adding archive refreshes cached images");
    require(window.textureImage(0, 1).pixelColor(0, 0) == QColor(Qt::red), "Existing palette works");

    // Updating the file lets the test detect any accidental reload on a miss.
    archive(first, 2, 1);
    for (int i = 0; i < 3; ++i) {
        require(window.textureImage(100 + i).isNull(), "Missing tile remains missing");
        require(window.textureImage(0, 27).isNull(), "Missing palette remains missing");
    }
    require(window.textureImage(0).pixelColor(0, 0) == QColor(Qt::red),
            "Missing tiles and palettes do not reload the archive");
    TextureBrowserWidget *browser = nullptr;
    for (auto *widget : window.findChildren<QWidget *>()) {
        if (auto *candidate = dynamic_cast<TextureBrowserWidget *>(widget)) { browser = candidate; }
    }
    require(browser, "Browser widget");
    browser->reload();
    require(window.textureImage(0).pixelColor(0, 0) == QColor(Qt::blue),
            "Explicit reload refreshes an archive changed in place");

    const auto second = directory.filePath("second.grp");
    archive(second, 1, 2);
    QSettings().setValue("gameData/grpFiles", QStringList{second});
    require(window.paletteNumbers().count(2) && !window.paletteNumbers().count(1),
            "Palette list refreshes after changing archives");
    require(window.textureImage(0).pixelColor(0, 0) == QColor(Qt::green),
            "Changing archives replaces cached images");
    QSettings().setValue("gameData/grpFiles", QStringList{});
    require(window.textureImage(0).isNull() && window.textureImage(0, 2).isNull(),
            "Removing archives clears cached textures");
    require(window.paletteNumbers() == std::set<int>{0}, "Removing archives clears palette choices");
    std::cout << "Texture browser cache tests passed\n";
}
