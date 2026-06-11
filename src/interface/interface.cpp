#include "interface.h"

#include "component/asset.h"
#include "component/network.h"
#include "component/script.h"
#include "util/time.h"

Interface::Interface(flecs::world& world) {
    world.component<InterfaceState>().add(flecs::Singleton);
    world.component<InterfacePage>().add(flecs::Singleton);
    world.component<InterfacePrevious>().add(flecs::Singleton);
    world.component<InterfaceCommands>().add(flecs::Singleton);
    world.component<InterfaceTransition>().add(flecs::Singleton);

    TTF_Init();
    auto* font = TTF_OpenFont("asset/font/normal.ttf", 16);
    if (font == nullptr) {
        SDL_Log("Failed to load font: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    TTF_SetFontHinting(font, TTF_HINTING_MONO);

    world.set<InterfaceState>({.font = font});
    world.set<InterfaceCommands>({});
    world.set(InterfacePage::Main);
    world.set<InterfacePrevious>({.page = InterfacePage::Main});
    world.set<ChatLog>({});
    world.set<ChatInput>({});
    world.set<ViewState>({});
    world.set<InputCapture>({});
    world.set<InterfaceTransition>({});
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

static auto opposite_dir(TransitionDir d) -> TransitionDir {
    switch (d) {
        case TransitionDir::Left:
            return TransitionDir::Right;
        case TransitionDir::Right:
            return TransitionDir::Left;
        case TransitionDir::Up:
            return TransitionDir::Down;
        case TransitionDir::Down:
            return TransitionDir::Up;
    }
    return d;
}

static void pick_transition(InterfacePage from, InterfacePage to, TransitionKind& kind, TransitionDir& dir, double& duration) {
    (void)from;
    switch (to) {
        case InterfacePage::Host:
        case InterfacePage::Connect:
        case InterfacePage::Server:
        case InterfacePage::Settings:
            kind = TransitionKind::Slide;
            dir = TransitionDir::Left;
            duration = 0.3;
            break;
        case InterfacePage::Main:
            kind = TransitionKind::Slide;
            dir = TransitionDir::Right;
            duration = 0.3;
            break;
        default:
            kind = TransitionKind::Crossfade;
            dir = TransitionDir::Left;
            duration = 0.1;
            break;
    }
}

void Interface::build(flecs::iter& it, size_t, InterfaceState& state, InterfaceCommands& cmds, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) {
    if (state.cursors[0] == nullptr) {
        state.cursors[static_cast<int>(InterfaceCursor::Default)] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        state.cursors[static_cast<int>(InterfaceCursor::Pointer)] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
        state.cursors[static_cast<int>(InterfaceCursor::Text)] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    }

    InterfacePage effective = page;
    {
        const auto* store = it.world().try_get<AssetStore>();
        const auto* conn = it.world().try_get<ConnectionStatus>();
        const bool in_session = (conn != nullptr) && conn->state != ConnectionState::Disconnected;
        if (store != nullptr && store->downloading() && in_session) {
            effective = InterfacePage::Assets;
        }
    }

    InterfacePage shown = effective;
    if (auto& tr = it.world().get_mut<InterfaceTransition>(); shown != tr.shown) {
        InterfacePage from = tr.shown;
        if (from == tr.last_to && shown == tr.last_from) {
            tr.dir = opposite_dir(tr.dir);
        } else {
            pick_transition(from, shown, tr.kind, tr.dir, tr.duration);
        }
        tr.last_from = from;
        tr.last_to = shown;
        tr.shown = shown;
        tr.start = util::now();
    }

    switch (effective) {
        case InterfacePage::Assets:
            cmds.list = Interface::assets(it, state, page, prev, events);
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
