#include <algorithm>

#include "component/network.h"
#include "interface.h"

static constexpr uint16_t CHAT_FONT = 32;

static auto chatLineHeight(InterfaceState& state) -> float {
    if (state.font == nullptr) {
        return CHAT_FONT;
    }
    TTF_SetFontSize(state.font, CHAT_FONT);
    int w = 0;
    int h = 0;
    TTF_GetStringSize(state.font, "Ay", 2, &w, &h);
    return static_cast<float>(h);
}

auto Interface::chat(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray {
    prev.page = InterfacePage::Ingame;
    const auto& chatLog = it.world().get<ChatLog>();
    auto& chatInput = it.world().get_mut<ChatInput>();
    uint32_t chatId = CLAY_ID("ChatPageInput").id;

    float lineHeight = chatLineHeight(state);

    bool enter = false;
    bool escape = false;
    bool histUp = false;
    bool histDown = false;
    for (const auto& ev : events) {
        if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
            chatInput.scroll += ev.wheel.y * lineHeight;
        } else if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
            if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) {
                enter = true;
            } else if (ev.key.key == SDLK_ESCAPE) {
                escape = true;
            } else if (ev.key.key == SDLK_UP) {
                histUp = true;
            } else if (ev.key.key == SDLK_DOWN) {
                histDown = true;
            }
        }
    }

    auto recall = [&]() -> void {
        if (auto* f = state.findField(chatId)) {
            f->cursor = chatInput.draft.size();
            f->anchor = f->cursor;
            f->scrollX = 0;
        }
    };

    if (histUp && !chatInput.sent.empty()) {
        if (chatInput.historyIndex == -1) {
            chatInput.stash = chatInput.draft;
            chatInput.historyIndex = static_cast<int>(chatInput.sent.size()) - 1;
        } else if (chatInput.historyIndex > 0) {
            --chatInput.historyIndex;
        }
        chatInput.draft = chatInput.sent[chatInput.historyIndex];
        recall();
    }
    if (histDown && chatInput.historyIndex != -1) {
        ++chatInput.historyIndex;
        if (chatInput.historyIndex >= static_cast<int>(chatInput.sent.size())) {
            chatInput.historyIndex = -1;
            chatInput.draft = chatInput.stash;
        } else {
            chatInput.draft = chatInput.sent[chatInput.historyIndex];
        }
        recall();
    }

    int winW = 0;
    int winH = 0;
    SDL_GetWindowSize(events.target, &winW, &winH);
    float wrapWidth = static_cast<float>(winW) - 40.0F;
    float inputHeight = static_cast<float>(CHAT_FONT) + 4.0F;
    float viewHeight = std::max(0.0F, static_cast<float>(winH) - 40.0F - 8.0F - inputHeight);

    int lines = 0;
    for (int i = 0; i < chatLog.count; ++i) {
        const std::string& w = Interface::wrap(state, chatLog.at(i), CHAT_FONT, wrapWidth);
        lines += 1;
        for (char c : w) {
            if (c == '\n') {
                ++lines;
            }
        }
    }

    float maxScroll = std::max(0.0F, (static_cast<float>(lines) * lineHeight) - viewHeight);
    chatInput.scroll = std::clamp(chatInput.scroll, 0.0F, maxScroll);
    float offsetY = chatInput.scroll;

    if (state.focusedId != chatId) {
        state.focusedId = chatId;
        SDL_StartTextInput(events.target);
    }

    Clay_BeginLayout();

    CLAY({.id = CLAY_ID("ChatScreen"),
          .layout = {
              .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()},
              .padding = {20, 20, 20, 20},
              .childGap = 8,
              .layoutDirection = CLAY_TOP_TO_BOTTOM,
          },
          .backgroundColor = {20, 20, 25, 128}}) {
        CLAY({.id = CLAY_ID("ChatLogView"),
              .layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()}, .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM}, .layoutDirection = CLAY_TOP_TO_BOTTOM},
              .clip = {.vertical = true, .childOffset = {0, offsetY}}}) {
            for (int i = 0; i < chatLog.count; ++i) {
                const std::string& wrapped = state.intern(Interface::wrap(state, chatLog.at(i), CHAT_FONT, wrapWidth));
                CLAY_TEXT(Str(wrapped), CLAY_TEXT_CONFIG({.textColor = {255, 255, 255, 255}, .fontSize = CHAT_FONT, .wrapMode = CLAY_TEXT_WRAP_NEWLINES}));
            }
        }

        InputStyle style = {
            .textColor = {.r = 255, .g = 255, .b = 255, .a = 255},
            .bgColor = {.r = 30, .g = 30, .b = 38, .a = 200},
            .focusBorderColor = {.r = 80, .g = 80, .b = 85, .a = 255},
            .fontSize = CHAT_FONT,
            .sizing = {.width = CLAY_SIZING_GROW(), .height = CLAY_SIZING_FIT()},
            .padding = {.left = 8, .right = 8, .top = 2, .bottom = 2},
        };
        Interface::input(state, events, CLAY_ID("ChatPageInput"), chatInput.draft,
                         {
                             .maxLength = 200,
                             .commitOnEnter = false,
                             .allowFormatting = true,
                             .allow = InputFilter::Printable,
                         },
                         style);
    }

    auto cmds = Clay_EndLayout();

    if (enter) {
        size_t start = chatInput.draft.find_first_not_of(' ');
        if (start != std::string::npos) {
            std::string text = chatInput.draft.substr(start, chatInput.draft.find_last_not_of(' ') - start + 1);
            it.world().entity().set(NetworkRequestChat{.text = text});
            chatInput.sent.push_back(text);
            if (chatInput.sent.size() > 64) {
                chatInput.sent.erase(chatInput.sent.begin());
            }
            chatInput.draft.clear();
            chatInput.scroll = 0;
        }
        chatInput.historyIndex = -1;
        chatInput.stash.clear();
    }
    if (escape) {
        chatInput.draft.clear();
        chatInput.scroll = 0;
        chatInput.historyIndex = -1;
        chatInput.stash.clear();
        state.focusedId = 0;
        SDL_StopTextInput(events.target);
        page = InterfacePage::Ingame;
    }

    return cmds;
}
