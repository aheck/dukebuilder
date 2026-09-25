#pragma once

#include <QString>
#include <QStringList>

#include <algorithm>

inline QStringList eduke32MapArguments(const QString &mapDirectory,
                                      const QString &mapFilename,
                                      int difficulty,
                                      bool enemiesEnabled)
{
    QStringList arguments{"-usecwd", "-nosetup", "-j", mapDirectory,
                          "-map", mapFilename};
    if (!enemiesEnabled) arguments.append("-m");
    // EDuke32's -m switch resets skill to zero, so apply the selected skill
    // afterward to keep the two launch settings independent.
    // The toolbar index is zero-based; Duke's four actual skills are 1..4.
    arguments.append("-s" + QString::number(std::clamp(difficulty, 0, 3) + 1));
    return arguments;
}
