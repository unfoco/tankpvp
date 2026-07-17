#include "persist.h"

#include <SDL3/SDL.h>
#include <glaze/glaze.hpp>

#include "component/settings.h"
#include "util/time.h"

static constexpr const char* CONFIG_PATH = "settings.json";
static constexpr double SAVE_DEBOUNCE = 0.4;

static auto snapshot(flecs::world& world) -> PersistConfig {
    PersistConfig config;
    if (const auto* settings = world.try_get<Settings>()) {
        config.username = settings->username;
        config.volume = settings->volume;
        config.music = settings->music;
        config.render_scale = settings->render_scale;
        config.light_scale = settings->light_scale;
        config.bloom = settings->bloom;
    }
    if (const auto* list = world.try_get<ServerList>()) {
        config.servers = list->entries;
    }
    return config;
}

static void write_config(const PersistConfig& config) {
    std::string buffer;
    if (glz::write_file_json<glz::opts{.prettify = true}>(config, std::string(CONFIG_PATH), buffer)) {
        SDL_Log("persist: failed to write %s", CONFIG_PATH);
    }
}

Persist::Persist(flecs::world& world) {
    world.component<PersistState>().add(flecs::Singleton);

    PersistConfig config;
    std::string buffer;
    if (glz::read_file_json(config, CONFIG_PATH, buffer)) {
        write_config(snapshot(world));
    } else {
        auto& settings = world.get_mut<Settings>();
        settings.username = config.username;
        settings.volume = config.volume;
        settings.music = config.music;
        settings.render_scale = config.render_scale;
        settings.light_scale = config.light_scale;
        settings.bloom = config.bloom;
        if (world.try_get<ServerList>() != nullptr) {
            world.get_mut<ServerList>().entries = config.servers;
        }
    }

    world.set<PersistState>({.last = snapshot(world)});
    world.system<PersistState>("persist::save").kind(flecs::OnStore).each(Persist::save);
}

void Persist::save(flecs::iter& it, size_t, PersistState& state) {
    flecs::world world = it.world();
    PersistConfig current = snapshot(world);
    double now = util::now();

    if (current != state.last) {
        state.last = current;
        state.dirty = true;
        state.saveAt = now + SAVE_DEBOUNCE;
    }
    if (state.dirty && now >= state.saveAt) {
        write_config(state.last);
        state.dirty = false;
    }
}
