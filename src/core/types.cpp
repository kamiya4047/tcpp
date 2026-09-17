#include "ime/types.h"
namespace ime {
std::u32string from_utf8(std::string_view text) {
    std::u32string out;
    for (std::size_t i = 0; i < text.size();) {
        auto lead = static_cast<unsigned char>(text[i]);
        char32_t cp{};
        std::size_t count{};
        if (lead < 0x80) { cp = lead; count = 1; }
        else if (lead >= 0xC2 && lead <= 0xDF) { cp = lead & 0x1F; count = 2; }
        else if (lead >= 0xE0 && lead <= 0xEF) { cp = lead & 0x0F; count = 3; }
        else if (lead >= 0xF0 && lead <= 0xF4) { cp = lead & 7; count = 4; }
        else { out += U'\uFFFD'; ++i; continue; }
        bool valid = i + count <= text.size();
        for (std::size_t j = 1; valid && j < count; ++j) {
            auto next = static_cast<unsigned char>(text[i+j]);
            if ((next & 0xC0) != 0x80) valid = false;
            else cp = (cp << 6) | (next & 0x3F);
        }
        if (!valid || (count == 2 && cp < 0x80) || (count == 3 && cp < 0x800) ||
            (count == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            out += U'\uFFFD'; ++i; continue;
        }
        out += cp; i += count;
    }
    return out;
}
std::string to_utf8(std::u32string_view text) {
    std::string out;
    for (auto cp : text) {
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = U'\uFFFD';
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 63)); out += static_cast<char>(0x80 | (cp & 63)); }
        else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 63)); out += static_cast<char>(0x80 | ((cp >> 6) & 63)); out += static_cast<char>(0x80 | (cp & 63)); }
    }
    return out;
}
std::u32string full_width(std::u32string_view text) {
    std::u32string out(text);
    for (auto& cp : out) { if (cp == U' ') cp = U'\u3000'; else if (cp >= 0x21 && cp <= 0x7E) cp += 0xFEE0; }
    return out;
}
std::u32string half_width(std::u32string_view text) {
    std::u32string out(text);
    for (auto& cp : out) { if (cp == U'\u3000') cp = U' '; else if (cp >= 0xFF01 && cp <= 0xFF5E) cp -= 0xFEE0; }
    return out;
}
}
