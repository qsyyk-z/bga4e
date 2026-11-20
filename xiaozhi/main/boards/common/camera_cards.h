#pragma once

#include <string>
#include <vector>

// Reads the camera module's per-game JSON from the 4.3C TF card.
// The camera itself has no SD slot, so this is the file hand-off path:
// copy/write cards.json to /sdcard/bga4e/cards.json, then tap "SD读牌".
class CameraCards {
public:
    static CameraCards& Instance();

    // Mounts the TF card on first use. Safe to call repeatedly.
    bool Mount();
    bool mounted() const { return mounted_; }

    // Accepts either a JSON array of card objects, {"hand":[...]}, or a
    // single {"name":"5万",...}. Returns Chinese mahjong tile names.
    bool LoadMahjongHand(std::vector<std::string>* tiles, std::string* error);

private:
    CameraCards() = default;
    bool mounted_ = false;
    bool attempted_ = false;
};
