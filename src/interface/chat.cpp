#include "interface.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>

#include "component/input.h"
#include "component/network.h"
#include "component/script.h"
#include "util/format.h"

struct Completion {
    bool slash = false;
    bool selectingName = true;
    const CommandInfo* node = nullptr;
    int argCurrent = 0;
    bool enumArg = false;
    std::string pathPrefix;
    std::string argBase;
    std::vector<const CommandInfo*> matches;
    std::vector<const std::string*> valueMatches;
};

static auto analyze(const CommandBook* book, const std::string& draft) -> Completion {
    Completion comp;

    auto lower = [](std::string s) -> std::string {
        for (auto& c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    };

    size_t lead = draft.find_first_not_of(' ');
    std::string trimmed = (lead == std::string::npos) ? std::string() : draft.substr(lead);
    comp.slash = !trimmed.empty() && trimmed[0] == '/';

    std::vector<std::string> tokens;
    std::string rest = comp.slash ? trimmed.substr(1) : std::string();
    for (size_t i = 0; i < rest.size();) {
        while (i < rest.size() && rest[i] == ' ') {
            ++i;
        }
        size_t begin = i;
        while (i < rest.size() && rest[i] != ' ') {
            ++i;
        }
        if (i > begin) {
            tokens.push_back(rest.substr(begin, i - begin));
        }
    }
    bool trailingSpace = !rest.empty() && rest.back() == ' ';

    const std::vector<CommandInfo>* level = (book != nullptr) ? &book->commands : nullptr;
    size_t ti = 0;
    if (comp.slash && level != nullptr) {
        while (ti < tokens.size()) {
            bool complete = (ti + 1 < tokens.size()) || trailingSpace;
            const CommandInfo* found = nullptr;
            for (const auto& c : *level) {
                if (lower(c.name) == lower(tokens[ti])) {
                    found = &c;
                    break;
                }
            }
            if (found != nullptr && complete) {
                comp.node = found;
                ++ti;
                if (!found->subcommands.empty()) {
                    level = &found->subcommands;
                    continue;
                }
                comp.selectingName = false;
            }
            break;
        }
    }

    if (comp.slash && level != nullptr && comp.selectingName) {
        std::string prefix = (ti < tokens.size()) ? lower(tokens[ti]) : std::string();
        for (const auto& c : *level) {
            if (lower(c.name).rfind(prefix, 0) == 0) {
                comp.matches.push_back(&c);
            }
        }
    }

    comp.pathPrefix = "/";
    for (size_t k = 0; k < ti; ++k) {
        comp.pathPrefix += tokens[k] + " ";
    }

    if (!comp.selectingName) {
        int argsTyped = static_cast<int>(tokens.size()) - static_cast<int>(ti);
        comp.argCurrent = trailingSpace ? argsTyped : argsTyped - 1;
        if (comp.argCurrent < 0) {
            comp.argCurrent = 0;
        }
    }

    const CommandArgument* curArg = (!comp.selectingName && comp.node != nullptr && comp.argCurrent < static_cast<int>(comp.node->arguments.size())) ? &comp.node->arguments[comp.argCurrent] : nullptr;
    comp.enumArg = curArg != nullptr && !curArg->values.empty();

    comp.argBase = comp.pathPrefix;
    if (comp.enumArg) {
        for (int k = 0; k < comp.argCurrent; ++k) {
            comp.argBase += tokens[ti + k] + " ";
        }
        std::string partial = (ti + static_cast<size_t>(comp.argCurrent) < tokens.size()) ? lower(tokens[ti + comp.argCurrent]) : std::string();
        for (const auto& v : curArg->values) {
            if (lower(v).rfind(partial, 0) == 0) {
                comp.valueMatches.push_back(&v);
            }
        }
    }

    return comp;
}

auto Interface::chat(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray {
    prev.page = InterfacePage::Ingame;
    const auto& chatLog = it.world().get<ChatLog>();
    auto& chatInput = it.world().get_mut<ChatInput>();
    uint32_t chatId = CLAY_ID("ChatPageInput").id;

    float lineHeight = static_cast<float>(format::line_height(state.font, 32));

    bool enter = false;
    bool escape = false;
    bool histUp = false;
    bool histDown = false;
    bool tab = false;
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
            } else if (ev.key.key == SDLK_TAB) {
                tab = true;
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

    Completion comp = analyze(it.world().try_get<CommandBook>(), chatInput.draft);
    bool slash = comp.slash;
    bool selectingName = comp.selectingName;
    const CommandInfo* node = comp.node;
    int argCurrent = comp.argCurrent;
    bool enumArg = comp.enumArg;
    const std::string& pathPrefix = comp.pathPrefix;
    const std::string& argBase = comp.argBase;
    const std::vector<const CommandInfo*>& matches = comp.matches;
    const std::vector<const std::string*>& valueMatches = comp.valueMatches;

    bool suggesting = slash && chatInput.history_index == -1;
    if (suggesting) {
        int n = selectingName ? static_cast<int>(matches.size()) : static_cast<int>(valueMatches.size());
        if (n > 0) {
            if (histUp) {
                chatInput.complete = ((chatInput.complete - 1) % n + n) % n;
            }
            if (histDown) {
                chatInput.complete = (chatInput.complete + 1) % n;
            }
            chatInput.complete = std::clamp(chatInput.complete, 0, n - 1);
        }
    } else {
        if (histUp && !chatInput.sent.empty()) {
            if (chatInput.history_index == -1) {
                chatInput.stash = chatInput.draft;
                chatInput.history_index = static_cast<int>(chatInput.sent.size()) - 1;
            } else if (chatInput.history_index > 0) {
                --chatInput.history_index;
            }
            chatInput.draft = chatInput.sent[chatInput.history_index];
            recall();
        }
        if (histDown && chatInput.history_index != -1) {
            ++chatInput.history_index;
            if (chatInput.history_index >= static_cast<int>(chatInput.sent.size())) {
                chatInput.history_index = -1;
                chatInput.draft = chatInput.stash;
            } else {
                chatInput.draft = chatInput.sent[chatInput.history_index];
            }
            recall();
        }
    }

    if (tab && selectingName && !matches.empty()) {
        int sel = std::clamp(chatInput.complete, 0, static_cast<int>(matches.size()) - 1);
        chatInput.draft = pathPrefix + matches[sel]->name + " ";
        chatInput.complete = 0;
        recall();
    } else if (tab && enumArg && !valueMatches.empty()) {
        int sel = std::clamp(chatInput.complete, 0, static_cast<int>(valueMatches.size()) - 1);
        chatInput.draft = argBase + *valueMatches[sel] + " ";
        chatInput.complete = 0;
        recall();
    }

    std::function<bool(char)> chatFilter;
    if (slash && !selectingName && node != nullptr && argCurrent < static_cast<int>(node->arguments.size())) {
        const std::string& type = node->arguments[argCurrent].type;
        if (type == "number" || type == "integer") {
            bool integer = (type == "integer");
            chatFilter = [integer](char c) -> bool {
                if (c == ' ' || c == '/' || c == '-') {
                    return true;
                }
                if (c >= '0' && c <= '9') {
                    return true;
                }
                return !integer && c == '.';
            };
        }
    }

    int winW = 0;
    int winH = 0;
    SDL_GetWindowSize(events.target, &winW, &winH);
    float wrapWidth = static_cast<float>(winW) - 40.0F;
    float inputHeight = 32.0F + 4.0F;
    float viewHeight = std::max(0.0F, static_cast<float>(winH) - 40.0F - 8.0F - inputHeight);

    int lines = 0;
    for (int i = 0; i < chatLog.count; ++i) {
        const std::string& w = widget::wrap(state, chatLog.at(i), 32, wrapWidth);
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

    const auto* touch = it.world().try_get<TouchOverlay>();
    bool touch_mode = touch != nullptr && touch->active;
    if (!touch_mode && state.focusedId != chatId) {
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
              .clip = {.vertical = true, .childOffset = {0, suggesting ? 0.0F : offsetY}}}) {
            if (suggesting) {
                if (selectingName) {
                    if (matches.empty()) {
                        widget::rich(state, "§8no matching command", 32, {255, 255, 255, 255});
                    } else {
                        int sel = std::clamp(chatInput.complete, 0, static_cast<int>(matches.size()) - 1);
                        int shown = std::min(static_cast<int>(matches.size()), 12);
                        for (int m = 0; m < shown; ++m) {
                            const CommandInfo* c = matches[m];
                            std::string row = "§f" + pathPrefix + c->name;
                            if (!c->subcommands.empty()) {
                                row += " §8…";
                            }
                            if (!c->description.empty()) {
                                row += "  §7" + c->description;
                            }
                            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIT()}, .padding = {6, 6, 1, 1}},
                                  .backgroundColor = (m == sel) ? Clay_Color{70, 130, 255, 90} : Clay_Color{0, 0, 0, 0},
                                  .cornerRadius = CLAY_CORNER_RADIUS(3)}) {
                                widget::rich(state, row, 32, {255, 255, 255, 255});
                            }
                        }
                    }
                } else if (node != nullptr) {
                    if (enumArg && !valueMatches.empty()) {
                        int sel = std::clamp(chatInput.complete, 0, static_cast<int>(valueMatches.size()) - 1);
                        int shown = std::min(static_cast<int>(valueMatches.size()), 12);
                        for (int m = 0; m < shown; ++m) {
                            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIT()}, .padding = {6, 6, 1, 1}},
                                  .backgroundColor = (m == sel) ? Clay_Color{70, 130, 255, 90} : Clay_Color{0, 0, 0, 0},
                                  .cornerRadius = CLAY_CORNER_RADIUS(3)}) {
                                widget::rich(state, "§f" + *valueMatches[m], 32, {255, 255, 255, 255});
                            }
                        }
                    }
                    std::string usage = "§7" + pathPrefix;
                    if (!usage.empty() && usage.back() == ' ') {
                        usage.pop_back();
                    }
                    for (size_t a = 0; a < node->arguments.size(); ++a) {
                        const CommandArgument& arg = node->arguments[a];
                        std::string inner = arg.name + ": " + arg.type;
                        std::string slot = arg.optional ? "[" + inner + "]" : "<" + inner + ">";
                        usage += (static_cast<int>(a) == argCurrent) ? " §e" + slot + "§7" : " " + slot;
                    }
                    widget::rich(state, usage, 32, {255, 255, 255, 255});
                    if (!node->description.empty()) {
                        widget::rich(state, "§8" + node->description, 32, {255, 255, 255, 255});
                    }
                }
            } else {
                for (int i = 0; i < chatLog.count; ++i) {
                    std::string wrapped = widget::wrap(state, chatLog.at(i), 32, wrapWidth);
                    widget::rich(state, wrapped, 32, {255, 255, 255, 255});
                }
            }
        }

        InputStyle style = {
            .textColor = {.r = 255, .g = 255, .b = 255, .a = 255},
            .bgColor = {.r = 30, .g = 30, .b = 38, .a = 200},
            .focusBorderColor = {.r = 80, .g = 80, .b = 85, .a = 255},
            .fontSize = 32,
            .sizing = {.width = CLAY_SIZING_GROW(), .height = CLAY_SIZING_FIT()},
            .padding = {.left = 8, .right = 8, .top = 2, .bottom = 2},
        };
        std::string draftBefore = chatInput.draft;
        widget::input(state, events, CLAY_ID("ChatPageInput"), chatInput.draft,
                         {
                             .maxLength = 200,
                             .commitOnEnter = false,
                             .allowFormatting = true,
                             .allow = InputFilter::Printable,
                             .allowFn = chatFilter,
                         },
                         style);
        if (chatInput.draft != draftBefore && chatInput.history_index != -1) {
            chatInput.history_index = -1;
            chatInput.stash.clear();
        }
    }

    auto cmds = Clay_EndLayout();

    if (enter) {
        size_t start = chatInput.draft.find_first_not_of(' ');
        if (start != std::string::npos) {
            std::string text = chatInput.draft.substr(start, chatInput.draft.find_last_not_of(' ') - start + 1);
            it.world().entity().set(RequestChat{.text = text});
            chatInput.sent.push_back(text);
            if (chatInput.sent.size() > 64) {
                chatInput.sent.erase(chatInput.sent.begin());
            }
            chatInput.draft.clear();
            chatInput.scroll = 0;
        }
        chatInput.history_index = -1;
        chatInput.stash.clear();
    }
    if (escape) {
        chatInput.draft.clear();
        chatInput.scroll = 0;
        chatInput.history_index = -1;
        chatInput.stash.clear();
        state.focusedId = 0;
        SDL_StopTextInput(events.target);
        page = InterfacePage::Ingame;
    }

    return cmds;
}
