#include "desktopintegration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace {
QString desktopString(QString value)
{
    value.replace("\\", "\\\\");
    value.replace("\n", "\\n");
    value.replace("\r", "\\r");
    value.replace("\t", "\\t");
    return value;
}

QString execArgument(QString value)
{
    // Exec quoting is decoded after desktop-entry string escaping.
    value.replace("\\", "\\\\");
    value.replace("\"", "\\\"");
    value.replace("`", "\\`");
    value.replace("$", "\\$");
    value.replace("%", "%%");
    return desktopString('"' + value + '"');
}

bool writeFile(const QString &path, const QByteArray &contents, QString &error)
{
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.readAll() == contents) return true;
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        error = "Cannot create desktop integration directory: " + QFileInfo(path).absolutePath();
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}
}

bool installAppImageDesktopEntry(const QString &appImage, const QString &dataHome,
                                const QByteArray &icon, QString &error)
{
    error.clear();
    const QFileInfo image(appImage);
    if (!image.isAbsolute() || !image.isFile() || !image.isExecutable()
        || !QDir::isAbsolutePath(dataHome) || icon.isEmpty()) {
        error = "Invalid AppImage path, desktop data directory, or icon.";
        return false;
    }
    const QString iconPath = dataHome + "/icons/hicolor/256x256/apps/dukebuilder-appimage.png";
    if (!writeFile(iconPath, icon, error)) return false;
    const QString entry = "[Desktop Entry]\nType=Application\nName=Duke Builder\n"
        "Comment=Create and edit Duke Nukem 3D maps\nExec=" + execArgument(appImage)
        + "\nIcon=" + desktopString(iconPath)
        + "\nTerminal=false\nCategories=Development;\nKeywords=Duke;Build;Map;Editor;\n";
    return writeFile(dataHome + "/applications/dukebuilder-appimage.desktop", entry.toUtf8(), error);
}
