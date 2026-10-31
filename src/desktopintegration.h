#pragma once

#include <QByteArray>
#include <QString>

bool installAppImageDesktopEntry(const QString &appImage, const QString &dataHome,
                                const QByteArray &icon, QString &error);
