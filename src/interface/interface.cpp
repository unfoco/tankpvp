#include "interface.h"

#include "component/asset.h"
#include "component/render.h"
#include "component/network.h"
#include "component/script.h"
#include "component/world.h"
#include "util/time.h"

Interface::Interface(flecs::world& world) {
    world.component<InterfaceState>().add(flecs::Singleton);
    world.component<InterfacePage>().add(flecs::Singleton);
    world.component<InterfacePrevious>().add(flecs::Singleton);
    world.component<InterfaceCommands>().add(flecs::Singleton);
    world.component<InterfaceTransition>().add(flecs::Singleton);
    world.component<ContentLoad>().add(flecs::Singleton);

    static format::Font font;
    if (!font.load("asset/font/normal.ttf")) {
        SDL_Log("Failed to load asset/font/normal.ttf");
        exit(EXIT_FAILURE);
    }

    static format::Font fontItalic;
    if (!fontItalic.load("asset/font/italic.ttf")) {
        SDL_Log("Failed to load asset/font/italic.ttf");
        exit(EXIT_FAILURE);
    }

    world.set<InterfaceState>({.font = &font, .fontItalic = &fontItalic});
    world.set<MinimapHandle>({});
    world.set<InterfaceCommands>({});
    world.set(InterfacePage::Main);
    world.set<InterfacePrevious>({.page = InterfacePage::Main});
    world.set<ChatLog>({});
    world.set<ChatInput>({});
    world.set<ViewState>({});
    world.set<InputCapture>({});
    world.set<InterfaceTransition>({});
    world.set<ContentLoad>({});
    world.set<ServerList>({.entries = {{.name = "Localhost", .address = "127.0.0.1", .port = 5000}}});

    world.system<InterfaceState>("interface::frame").kind(flecs::PreFrame).each(Interface::frame);
    world.system<InterfaceState, WindowEvents>("interface::event").kind(flecs::PreUpdate).each(Interface::event);
    world.system<InterfaceState, InterfaceCommands, InterfacePage, InterfacePrevious, WindowEvents>("interface::build").kind(flecs::OnUpdate).each(Interface::build);
}

void Interface::frame(flecs::iter&, size_t, InterfaceState& state) {
    state.prevFocusedId = state.focusedId;
    state.mousePressed = false;
    state.mouseReleased = false;
    state.focusConsumed = false;
    state.cursor = InterfaceCursor::Default;
    state.textPool.clear();
}

void Interface::event(flecs::iter&, size_t, InterfaceState& state, const WindowEvents& events) {
    for (const auto& e : events) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            state.mousePressed = true;
            state.mouseDown = true;
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
            state.mouseDown = false;
            state.mouseReleased = true;
            state.activeId = 0;
        } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
            state.mouseX = e.motion.x;
            state.mouseY = e.motion.y;
        }
    }
}

static auto reverse_dir(uint8_t d) -> uint8_t {
    return d ^ 1U;
}

static auto reverse_slide(SlideMode s) -> SlideMode {
    if (s == SlideMode::Cover) {
        return SlideMode::Reveal;
    }
    if (s == SlideMode::Reveal) {
        return SlideMode::Cover;
    }
    return SlideMode::Push;
}

static auto pick_transition(InterfacePage from, InterfacePage to, glm::vec2 cursor) -> RequestTransition {
    if (to == InterfacePage::Ingame && from != InterfacePage::Pause && from != InterfacePage::Chat)
        return {.kind = ScreenTransitionKind::Circle, .duration = 1.0F, .color = {0, 0, 0, 1}, .center = cursor};

    if (to == InterfacePage::Chat || from == InterfacePage::Chat)
        return {.kind = ScreenTransitionKind::Slide, .duration = 0.16F, .color = {0, 0, 0, 0}, .direction = 2, .scope = TransitionScope::Interface, .slide = SlideMode::Cover};

    if ((to == InterfacePage::Pause && from == InterfacePage::Ingame) ||
        (to == InterfacePage::Ingame && from == InterfacePage::Pause))
        return {.kind = ScreenTransitionKind::Fade, .duration = 0.12F, .color = {0, 0, 0, 0}, .scope = TransitionScope::Interface};

    if (to == InterfacePage::Main && (from == InterfacePage::Pause || from == InterfacePage::Ingame))
        return {.kind = ScreenTransitionKind::Pixelate, .duration = 0.5F, .color = {0, 0, 0, 0}};

    if (to == InterfacePage::Content || from == InterfacePage::Content)
        return {.kind = ScreenTransitionKind::Fade, .duration = 0.30F, .color = {0, 0, 0, 0}};

    if (to == InterfacePage::Status || from == InterfacePage::Status)
        return {.kind = ScreenTransitionKind::Dissolve, .duration = 0.30F, .color = {0, 0, 0, 0}};

    return {.kind = ScreenTransitionKind::Slide, .duration = 0.26F, .color = {0, 0, 0, 0}, .direction = 1};
}

void Interface::build(flecs::iter& it, size_t, InterfaceState& state, InterfaceCommands& cmds, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) {
    if (state.cursors[0] == nullptr) {
        state.cursors[static_cast<int>(InterfaceCursor::Default)] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        state.cursors[static_cast<int>(InterfaceCursor::Pointer)] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
        state.cursors[static_cast<int>(InterfaceCursor::Text)] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    }

    if (auto* minimap = it.world().try_get_mut<MinimapHandle>()) {
        minimap->size = 0;
    }

    InterfacePage effective = page;
    {
        const auto* store = it.world().try_get<AssetStore>();
        const auto* conn = it.world().try_get<ConnectionStatus>();
        const bool in_session = (conn != nullptr) && conn->state == ConnectionState::Connected;
        bool busy = false;
        if (in_session) {
            bool downloading = store != nullptr && store->downloading();
            bool mod_loading = false;
            it.world().query<const Loading>().each([&](const Loading& l) -> void { mod_loading = mod_loading || l.active > 0.5F; });
            bool chunks_meshing = it.world().count<ChunkDirty>() > 0;
            busy = downloading || mod_loading || chunks_meshing;
        }

        constexpr double READY_DEBOUNCE = 0.4;
        constexpr double MIN_SHOW = 0.5;
        auto& cl = it.world().get_mut<ContentLoad>();
        double now = util::now();
        if (!in_session) {
            cl.showing = false;
        } else {
            if (!cl.in_session) {
                cl.showing = true;
                cl.shown_since = now;
                cl.busy_since = now;
            }
            if (busy) {
                cl.busy_since = now;
            }
            if (cl.showing && (now - cl.busy_since) > READY_DEBOUNCE && (now - cl.shown_since) > MIN_SHOW) {
                cl.showing = false;
            }
        }
        cl.in_session = in_session;
        if (cl.showing) {
            effective = InterfacePage::Content;
        }
    }

    InterfacePage shown = effective;
    if (auto& tr = it.world().get_mut<InterfaceTransition>(); shown != tr.shown) {
        InterfacePage from = tr.shown;
        const bool back = tr.history.size() >= 2 && tr.history[tr.history.size() - 2] == shown;
        if (back) {
            tr.history.pop_back();
        } else {
            tr.history.push_back(shown);
            if (tr.history.size() > 32) {
                tr.history.erase(tr.history.begin());
            }
        }
        tr.shown = shown;

        if (from != InterfacePage::None) {
            int ww = 0;
            int wh = 0;
            SDL_GetWindowSize(events.target, &ww, &wh);
            glm::vec2 cursor = {ww > 0 ? state.mouseX / static_cast<float>(ww) : 0.5F,
                                wh > 0 ? state.mouseY / static_cast<float>(wh) : 0.5F};
            RequestTransition req = pick_transition(from, shown, cursor);
            if (back) {
                req.direction = reverse_dir(req.direction);
                req.slide = reverse_slide(req.slide);
            }
            it.world().entity().set(req);
        }
    }

    switch (effective) {
        case InterfacePage::Content:
            cmds.list = Interface::content(it, state, page, prev, events);
            break;
        case InterfacePage::Main:
            cmds.list = Interface::main(it, state, page, prev, events);
            break;
        case InterfacePage::Host:
            cmds.list = Interface::host(it, state, page, prev, events);
            break;
        case InterfacePage::Pause:
            cmds.list = Interface::pause(it, state, page, prev, events);
            break;
        case InterfacePage::Ingame:
            cmds.list = Interface::ingame(it, state, page, prev, events);
            break;
        case InterfacePage::Server:
            cmds.list = Interface::server(it, state, page, prev, events);
            break;
        case InterfacePage::Connect:
            cmds.list = Interface::connect(it, state, page, prev, events);
            break;
        case InterfacePage::Settings:
            cmds.list = Interface::settings(it, state, page, prev, events);
            break;
        case InterfacePage::Chat:
            cmds.list = Interface::chat(it, state, page, prev, events);
            break;
        case InterfacePage::Status:
            cmds.list = Interface::status(it, state, page, prev, events);
            break;
        case InterfacePage::None:
            cmds.list = {};
            break;
    }

    for (int32_t i = 0; i < cmds.list.length; ++i) {
        auto* cmd = Clay_RenderCommandArray_Get(&cmds.list, i);
        if (auto* f = state.findField(cmd->id)) {
            f->bounds = cmd->boundingBox;
        }
    }

    if (state.mousePressed && !state.focusConsumed && state.focusedId != 0) {
        state.focusedId = 0;
        SDL_StopTextInput(events.target);
    }

    if (state.cursor != state.cursorApplied && state.cursors[static_cast<int>(state.cursor)] != nullptr) {
        SDL_SetCursor(state.cursors[static_cast<int>(state.cursor)]);
        state.cursorApplied = state.cursor;
    }

    it.world().get_mut<InputCapture>().active = (state.focusedId != 0);
}
