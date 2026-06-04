#pragma once

#include <flecs.h>
#include <glm/glm.hpp>
#include <miniaudio.h>

#include <list>
#include <memory>

#include "component/object.h"

struct Sounded {};

struct Music {
    short* pcm = nullptr;
    ma_audio_buffer buffer = {};
    ma_sound sound = {};
    float volume = 0.0F;
    bool ok = false;
};

struct AudioState {
    std::unique_ptr<ma_engine> engine;
    std::unique_ptr<Music> menu;
    std::unique_ptr<Music> game;
    std::list<ma_sound> voices;
    glm::vec2 listener = {0.0F, 0.0F};
    bool haveListener = false;
    bool ready = false;
};

struct Audio {
    Audio(flecs::world& world);

   private:
    static void init(flecs::iter& it, size_t i, AudioState& audio);
    static void reap(flecs::iter& it, size_t i, AudioState& audio);
    static void listener(flecs::iter& it, size_t i, AudioState& audio, const Position& pos);
    static void music(flecs::iter& it, size_t i, AudioState& audio);
    static void shoot(flecs::iter& it, size_t i, AudioState& audio, const Position& pos);
    static void puff(flecs::entity entity);
};
