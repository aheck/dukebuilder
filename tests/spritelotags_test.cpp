#include "spritelotags.h"
#include <cstdlib>
#include <iostream>
#include <string>

static void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

int main()
{
    const auto effector = spriteLotags(1);
    require(effector.presets.size() > 30 && effector.presets[0].tag == 0,
            "SE uses effect presets");
    for (int tile : {1680, 1682, 2000, 2045, 2360, 4610, 4740, 21, 100}) {
        const auto tags = spriteLotags(tile);
        require(tags.presets.size() == 5 && tags.presets.back().tag == 4,
                "Live actors and supported pickups offer minimum difficulty");
    }
    for (int tile : {1734, 2060, 2005, 26, 60, 6000, -1}) {
        require(spriteLotags(tile).presets.empty(),
                "Corpses, artwork frames, unfiltered pickups and unknown tiles have no guessed presets");
    }
    require(std::string(spriteLotags(9).description).find("channel") != std::string::npos,
            "Respawn lotag is a channel, not difficulty or spawned tile");
    require(spriteLotags(1405).presets.size() == 2, "Player sprite uses multiplayer starts");
    require(spriteLotags(6).presets[0].tag == 0, "Locator route starts at zero");
    for (int tile : {130, 131, 146, 149, 1155, 1156}) {
        require(spriteLotags(tile).presets.back().tag == -1, "Switch states support exit tag");
    }
    const auto musicAndSfx = spriteLotags(5);
    require(std::string(musicAndSfx.description).find("sound ID") != std::string::npos,
            "MusicAndSFX explains sound IDs");
    require(musicAndSfx.presets.size() >= 20 && musicAndSfx.presets.front().tag == 0,
            "MusicAndSFX offers standard sound suggestions");
    require(musicAndSfx.presets.back().tag == 1255,
            "MusicAndSFX offers echo amount presets");
    bool hasSpaceDoorSound = false;
    for (const auto &preset : musicAndSfx.presets) {
        if (preset.tag == 256) {
            hasSpaceDoorSound = std::string(preset.meaning).find("Space door") != std::string::npos;
        }
    }
    require(hasSpaceDoorSound, "MusicAndSFX includes the space door sound");
    require(std::string(spriteLotags(10).description).find("speed") != std::string::npos,
            "GPSpeed explains speed");
}
