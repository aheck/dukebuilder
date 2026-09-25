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
                                        "-map", "test.map", "-s3"}),
            "Toolbar Come Get Some maps to Duke skill 3");

    const auto withoutEnemies = eduke32MapArguments("/maps", "test.map", 3, false);
    require(withoutEnemies == QStringList({"-usecwd", "-nosetup", "-j", "/maps",
                                           "-map", "test.map", "-m", "-s4"}),
            "No-monsters launch retains Damn I'm Good as Duke skill 4");

    require(eduke32MapArguments("/maps", "test.map", 0, true).last() == "-s1",
            "Piece of Cake maps to Duke skill 1");
    require(eduke32MapArguments("/maps", "test.map", 99, true).last() == "-s4",
            "Difficulty is constrained to the four editor choices");
}
