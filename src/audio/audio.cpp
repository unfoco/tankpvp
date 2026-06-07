#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include <stb/stb_vorbis.c>
#undef L
#undef C
#undef R

#include "audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "component/asset.h"
#include "component/audio.h"
#include "component/interface.h"
#include "component/network.h"
#include "component/settings.h"

static constexpr float AUDIO_MAX_DIST = 900.0F;
static constexpr float AUDIO_PAN_DIST = 640.0F;
static constexpr float MUSIC_TAU = 0.6F;
static constexpr float MUSIC_DAMPEN = 0.35F;
static constexpr float MUSIC_GAIN = 0.55F;

static auto load_music(ma_engine* engine, const char* path, Music* music) -> bool {
    int channels = 0;
    int rate = 0;
    short* pcm = nullptr;
    int frames = stb_vorbis_decode_filename(path, &channels, &rate, &pcm);
    if (frames < 0 || pcm == nullptr) {
        SDL_Log("audio: failed to decode %s", path);
        return false;
    }
    music->pcm = pcm;

    ma_audio_buffer_config config = ma_audio_buffer_config_init(ma_format_s16, static_cast<ma_uint32>(channels), static_cast<ma_uint64>(frames), pcm, nullptr);
    config.sampleRate = static_cast<ma_uint32>(rate);
    if (ma_audio_buffer_init(&config, &music->buffer) != MA_SUCCESS) {
        SDL_Log("audio: failed to wrap %s", path);
        return false;
    }
    if (ma_sound_init_from_data_source(engine, &music->buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &music->sound) != MA_SUCCESS) {
        SDL_Log("audio: failed to init music %s", path);
        return false;
    }
    ma_sound_set_looping(&music->sound, MA_TRUE);
    ma_sound_set_volume(&music->sound, 0.0F);
    ma_sound_start(&music->sound);
    music->ok = true;
    return true;
}

static void emit(AudioState& audio, const char* path, glm::vec2 position, float volume) {
    if (!audio.ready) {
        return;
    }
    float gain = volume;
    float pan = 0.0F;
    if (audio.haveListener) {
        glm::vec2 delta = position - audio.listener;
        float distance = glm::length(delta);
        float falloff = std::clamp(1.0F - (distance / AUDIO_MAX_DIST), 0.0F, 1.0F);
        gain *= falloff * falloff;
        pan = std::clamp(delta.x / AUDIO_PAN_DIST, -1.0F, 1.0F);
    }
    if (gain <= 0.01F) {
        return;
    }

    ma_sound& voice = audio.voices.emplace_back();
    if (ma_sound_init_from_file(audio.engine.get(), path, MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, nullptr, &voice) != MA_SUCCESS) {
        audio.voices.pop_back();
        return;
    }
    ma_sound_set_volume(&voice, 0.5F * gain);
    ma_sound_set_pan(&voice, pan);
    ma_sound_start(&voice);
}

Audio::Audio(flecs::world& world) {
    world.component<AudioState>().add(flecs::Singleton);
    world.set<AudioState>({});

    world.system<AudioState>("audio::init").kind(flecs::OnStart).each(Audio::init);
    world.system<AudioState>("audio::reap").kind(flecs::PreFrame).each(Audio::reap);
    world.system<AudioState, const Position>("audio::listener").with<Local>().kind(flecs::PreFrame).each(Audio::listener);
    world.system<AudioState>("audio::music").kind(flecs::OnStore).each(Audio::music);
    world.system<AudioState, const Position>("audio::shoot").with<Bullet>().without<Latent>().without<Sounded>().kind(flecs::OnStore).each(Audio::shoot);
    world.observer("audio::puff").with<Bullet>().event(flecs::OnRemove).each(Audio::puff);
    world.observer<const RequestSound>("audio::custom").event(flecs::OnSet).each([](flecs::entity e, const RequestSound& s) -> void {
        flecs::world world = e.world();
        AudioState& audio = world.get_mut<AudioState>();
        const auto* store = world.try_get<AssetStore>();
        std::string path = store != nullptr ? store->path_for(s.asset) : std::string();
        if (!path.empty()) {
            float volume = world.has<Settings>() ? world.get<Settings>().volume : 1.0F;
            emit(audio, path.c_str(), {s.x, s.y}, volume * s.volume);
        }
    });
}

void Audio::init(flecs::iter&, size_t, AudioState& audio) {
    audio.engine = std::make_unique<ma_engine>();
    if (ma_engine_init(nullptr, audio.engine.get()) != MA_SUCCESS) {
        SDL_Log("audio: failed to init engine");
        audio.engine.reset();
        return;
    }
    audio.ready = true;

    audio.menu = std::make_unique<Music>();
    audio.game = std::make_unique<Music>();
    load_music(audio.engine.get(), "asset/sound/music/menu.ogg", audio.menu.get());
    load_music(audio.engine.get(), "asset/sound/music/game.ogg", audio.game.get());
}

void Audio::reap(flecs::iter&, size_t, AudioState& audio) {
    for (auto voice = audio.voices.begin(); voice != audio.voices.end();) {
        if (ma_sound_at_end(&*voice) != 0U) {
            ma_sound_uninit(&*voice);
            voice = audio.voices.erase(voice);
        } else {
            ++voice;
        }
    }
}

void Audio::listener(flecs::iter&, size_t, AudioState& audio, const Position& pos) {
    audio.listener = pos.value;
    audio.haveListener = true;
}

static void drive_music(Music* music, float target, float rate) {
    if (music == nullptr || !music->ok) {
        return;
    }
    music->volume += (target - music->volume) * rate;
    ma_sound_set_volume(&music->sound, music->volume);
}

void Audio::music(flecs::iter& it, size_t, AudioState& audio) {
    if (!audio.ready) {
        return;
    }
    const auto* page = it.world().try_get<InterfacePage>();
    float volume = it.world().get<Settings>().music * MUSIC_GAIN;

    float menuTarget = 0.0F;
    float gameTarget = 0.0F;
    switch (page != nullptr ? *page : InterfacePage::None) {
        case InterfacePage::Main:
        case InterfacePage::Connect:
            menuTarget = volume;
            break;
        case InterfacePage::Host:
        case InterfacePage::Server:
        case InterfacePage::Settings:
        case InterfacePage::Status:
            menuTarget = volume * MUSIC_DAMPEN;
            break;
        case InterfacePage::Ingame:
            gameTarget = volume;
            break;
        case InterfacePage::Pause:
        case InterfacePage::Chat:
            gameTarget = volume * MUSIC_DAMPEN;
            break;
        case InterfacePage::None:
            break;
    }

    float rate = std::clamp(1.0F - std::exp(-it.delta_time() / MUSIC_TAU), 0.0F, 1.0F);
    drive_music(audio.menu.get(), menuTarget, rate);
    drive_music(audio.game.get(), gameTarget, rate);
}

void Audio::shoot(flecs::iter& it, size_t i, AudioState& audio, const Position& pos) {
    it.entity(i).add<Sounded>();
    static const char* paths[] = {"asset/sound/bullet/shoot0.wav", "asset/sound/bullet/shoot1.wav"};
    float volume = it.world().get<Settings>().volume;
    emit(audio, paths[it.entity(i).id() % 2], pos.value, volume);
}

void Audio::puff(flecs::entity entity) {
    if (!entity.has<Sounded>()) {
        return;
    }
    flecs::world world = entity.world();
    auto& audio = world.get_mut<AudioState>();
    static const char* paths[] = {"asset/sound/bullet/puff0.wav", "asset/sound/bullet/puff1.wav", "asset/sound/bullet/puff2.wav"};
    glm::vec2 position = audio.listener;
    if (const auto* pos = entity.try_get<Position>()) {
        position = pos->value;
    }
    float volume = world.get<Settings>().volume;
    emit(audio, paths[entity.id() % 3], position, volume);
}
