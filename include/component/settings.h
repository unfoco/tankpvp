#pragma once

#include <string>

struct Settings {
    std::string username;
    float volume = 1.0F;
    float music = 1.0F;
    float render_scale = 1.0F;
    float light_scale = 0.5F;
    bool bloom = true;
};
