#pragma once

#include <string>

struct RequestSound {
    std::string asset;
    float x = 0;
    float y = 0;
    float volume = 1.0F;
    bool global = false;
};
