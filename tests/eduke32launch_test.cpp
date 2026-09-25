#include "eduke32launch.h"

#include <cstdlib>
#include <iostream>

static void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main()
{
    const auto withEnemies = eduke32MapArguments("/maps", "test.map", 2, true);
    require(withEnemies == QStringList({"-usecwd", "-nosetup", "-j", "/maps",
                                        "-map", "test.map", "-s2"}),
            "Enabled-enemy launch passes the selected difficulty");

    const auto withoutEnemies = eduke32MapArguments("/maps", "test.map", 3, false);
    require(withoutEnemies == QStringList({"-usecwd", "-nosetup", "-j", "/maps",
                                           "-map", "test.map", "-m", "-s3"}),
            "No-monsters switch precedes skill so the selected difficulty is retained");

    require(eduke32MapArguments("/maps", "test.map", 99, true).last() == "-s3",
            "Difficulty is constrained to the four editor choices");
}
