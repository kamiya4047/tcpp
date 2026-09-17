#include "common.h"
#include "ime/bopomofo.h"
#include <shlobj.h>
#include <array>

namespace ime::windows {
std::wstring wide(std::u32string_view text) {
    std::wstring out;
    for (char32_t c : text) {
        if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) c = 0xfffd;
        if (c < 0x10000) out.push_back(static_cast<wchar_t>(c));
        else { c -= 0x10000; out.push_back(static_cast<wchar_t>(0xd800 + (c >> 10))); out.push_back(static_cast<wchar_t>(0xdc00 + (c & 0x3ff))); }
    }
    return out;
}
std::u32string utf32(std::wstring_view text) {
    std::u32string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char32_t c = text[i];
        if (c >= 0xd800 && c <= 0xdbff && i + 1 < text.size() && text[i+1] >= 0xdc00 && text[i+1] <= 0xdfff) {
            c = 0x10000 + ((c - 0xd800) << 10) + (text[++i] - 0xdc00);
        } else if (c >= 0xd800 && c <= 0xdfff) c = 0xfffd;
        out.push_back(c);
    }
    return out;
}
std::filesystem::path settings_path() {
    PWSTR local{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return {};
    std::filesystem::path path(local);
    CoTaskMemFree(local);
    return path / L"TaiwanBopomofo" / L"settings.conf";
}
Settings read_user_settings() {
    const auto path = settings_path();
    return path.empty() ? Settings{} : load_settings(to_utf8(utf32(path.native())));
}
bool write_user_settings(const Settings& settings) {
    const auto path = settings_path();
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    return !error && save_settings(settings, to_utf8(utf32(path.native())));
}
void enable_dpi_awareness() noexcept {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}
char translated_key(WPARAM key, LPARAM lparam) noexcept {
    BYTE keyboard[256]{};
    if (!GetKeyboardState(keyboard)) return 0;
    wchar_t text[4]{};
    // Bit 2 avoids changing the keyboard dead-key state (Windows 10+).
    const int length = ToUnicodeEx(static_cast<UINT>(key), static_cast<UINT>((lparam >> 16) & 0xff), keyboard, text, 4, 4, GetKeyboardLayout(0));
    return length == 1 && text[0] >= 0x21 && text[0] <= 0x7e ? static_cast<char>(text[0]) : 0;
}
bool accepts_key(const Engine& engine, WPARAM key, bool control, bool alt, bool shift) noexcept {
    if (control || alt) return false;
    const bool active = engine.state().mode != Mode::Empty;
    if (key >= 'A' && key <= 'Z') return true;
    if (key >= '0' && key <= '9') {
        // An empty shortcut row is ignored while converting. Passing it to
        // the host would insert a digit inside an unfinished composition.
        return true;
    }
    if (key >= VK_OEM_1 && key <= VK_OEM_3) return true;
    if (key >= VK_OEM_4 && key <= VK_OEM_8) return true;
    if (key == VK_OEM_102) return true;
    if (!active) return false;
    if (key == VK_SPACE && shift) return false;
    for (const int hotkey : engine.settings().hotkeys) if (hotkey && key == static_cast<WPARAM>(hotkey)) return true;
    return key == VK_SPACE || key == VK_RETURN || key == VK_ESCAPE || key == VK_BACK || key == VK_UP || key == VK_DOWN || key == VK_LEFT || key == VK_RIGHT || key == VK_PRIOR || key == VK_NEXT;
}
std::u32string handle_key(Engine& engine, WPARAM key, char text, bool shift, bool& committed, bool learn) {
    committed = false;
    for (std::size_t i = 0; i < 5; ++i) {
        if (engine.settings().hotkeys[i] && key == static_cast<WPARAM>(engine.settings().hotkeys[i])) { engine.transform(static_cast<int>(i) + 6); return {}; }
    }
    switch (key) {
    case VK_RETURN:
        committed = true;
        return engine.commit(learn);
    case VK_ESCAPE: engine.escape(); break;
    case VK_BACK: engine.backspace(); break;
    case VK_SPACE: {
        const auto& state = engine.state();
        if (state.mode == Mode::Composing && bopomofo::can_mark_first_tone(state.raw_keys)) {
            engine.type(' ');
        } else if (state.mode == Mode::Converted) {
            if(state.candidate_menu_closed) engine.open_candidates(); else engine.cycle();
        } else engine.convert();
        break;
    }
    case VK_DOWN: engine.cycle(1); break;
    case VK_UP: engine.cycle(-1); break;
    case VK_PRIOR: engine.cycle(-9); break;
    case VK_NEXT: engine.cycle(9); break;
    case VK_LEFT:
        if (shift) engine.resize_segment(-1);
        else if (engine.state().mode == Mode::Composing) engine.set_candidate_menu_expanded(true);
        else if (engine.state().mode == Mode::Converted) engine.move_segment(-1);
        break;
    case VK_RIGHT:
        if (shift) engine.resize_segment(1);
        else if (engine.state().mode == Mode::Composing) engine.set_candidate_menu_expanded(true);
        else if (engine.state().mode == Mode::Converted) engine.move_segment(1);
        break;
    default:
        if (!shift && engine.state().mode == Mode::Converted && !engine.state().candidate_menu_closed && key >= '1' && key <= '9') {
            const auto target = (engine.state().selected / 9) * 9 + static_cast<std::size_t>(key - '1');
            if (target < engine.state().candidates.size()) engine.confirm_candidate(target);
        }
        else if (text) engine.type(text);
        break;
    }
    return {};
}
std::u32string clipboard_text(HWND owner) {
    if (!OpenClipboard(owner)) return {};
    struct Close { ~Close() { CloseClipboard(); } } close;
    const HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (!handle) return {};
    const auto bytes = GlobalSize(handle);
    if (bytes < sizeof(wchar_t) || bytes > 2 * 1024 * 1024) return {};
    const auto* data = static_cast<const wchar_t*>(GlobalLock(handle));
    if (!data) return {};
    struct Unlock { HANDLE handle; ~Unlock() { GlobalUnlock(handle); } } unlock{handle};
    std::size_t length{};
    while (length < bytes / sizeof(wchar_t) && data[length]) ++length;
    if (length == bytes / sizeof(wchar_t)) return {};
    return utf32(std::wstring_view(data, length));
}
}
