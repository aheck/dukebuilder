#include "desktopintegration.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

static void require(bool condition, const char *message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(EXIT_FAILURE); }
}

static QByteArray read(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Read installed file");
    return file.readAll();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "Create isolated desktop directory");
    const QString image = temporary.path() + "/Duke $cash `tick` \\\"quoted\" 100%.AppImage";
    QFile executable(image);
    require(executable.open(QIODevice::WriteOnly), "Create AppImage fixture");
    executable.close();
    require(executable.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner), "Make fixture executable");
    const QString data = temporary.path() + "/user data";
    QString error;
    require(installAppImageDesktopEntry(image, data, "icon bytes", error), "Install desktop entry");
    const QString desktop = data + "/applications/dukebuilder-appimage.desktop";
    const auto entry = read(desktop);
    require(entry.contains("100%%.AppImage\"\n"), "Escape desktop field codes");
    require(entry.contains("\\\\$cash") && entry.contains("\\\\`tick\\\\`"), "Escape command metacharacters");
    require(entry.contains("\\\\\\\\\\\\\"quoted\\\\\""), "Escape literal backslash and quotes in Exec");
    require(read(data + "/icons/hicolor/256x256/apps/dukebuilder-appimage.png") == "icon bytes", "Install persistent icon");
    require(installAppImageDesktopEntry(image, data, "icon bytes", error) && read(desktop) == entry, "Repeated launch is idempotent");
    const QString moved = temporary.path() + "/Moved.AppImage";
    require(executable.rename(moved), "Move AppImage");
    require(installAppImageDesktopEntry(moved, data, "new icon", error), "Update moved AppImage");
    require(read(desktop).contains("Moved.AppImage") && !read(desktop).contains("100%%"), "Launcher follows moved image");
    require(!installAppImageDesktopEntry(image, data, "icon", error), "Reject missing image");
    require(read(desktop).contains("Moved.AppImage"), "Failed update preserves launcher");
    return 0;
}
