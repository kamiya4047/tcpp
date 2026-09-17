#pragma once
#include <windows.h>
#include <msctf.h>
#include <shellapi.h>
#include "ime/engine.h"
#include <filesystem>
#include <utility>

namespace ime::windows {
inline constexpr CLSID kTextService = {0x3c3de834,0x1c7f,0x4dce,{0x98,0x89,0x3e,0xaa,0x23,0x51,0xe6,0x23}};
inline constexpr GUID kLanguageProfile = {0xf77b5732,0x77f6,0x497b,{0x81,0x20,0xaa,0x25,0xe3,0x75,0xe0,0x97}};
inline constexpr GUID kInputAttribute = {0xd2792c12,0xa53a,0x4532,{0xa5,0x9c,0xe3,0x73,0x19,0xbc,0xb6,0x37}};
inline constexpr GUID kSelectedAttribute = {0x7ac31d2b,0x1f64,0x4b64,{0x9b,0xde,0xf6,0x62,0x86,0x12,0x91,0xe9}};
inline constexpr wchar_t kServiceName[] = L"Taiwan Bopomofo";
inline constexpr LANGID kLanguage = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL);

// Small owning COM pointer. All use is confined to the owning Windows apartment.
template<class T> class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* p) : p_(p) { if (p_) p_->AddRef(); }
    ComPtr(const ComPtr& p) : ComPtr(p.p_) {}
    ComPtr(ComPtr&& p) noexcept : p_(std::exchange(p.p_, nullptr)) {}
    ~ComPtr() { reset(); }
    ComPtr& operator=(ComPtr p) noexcept { std::swap(p_,p.p_); return *this; }
    T* get() const noexcept { return p_; }
    T* operator->() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
    T** put() noexcept { reset(); return &p_; }
    T* detach() noexcept { return std::exchange(p_, nullptr); }
    void reset() noexcept { if (auto p = std::exchange(p_,nullptr)) p->Release(); }
private:
    T* p_{};
};

std::wstring wide(std::u32string_view text);
std::u32string utf32(std::wstring_view text);
std::filesystem::path settings_path();
Settings read_user_settings();
bool write_user_settings(const Settings& settings);
void enable_dpi_awareness() noexcept;
char translated_key(WPARAM key, LPARAM lparam) noexcept;
bool accepts_key(const Engine& engine, WPARAM key, bool control, bool alt, bool shift) noexcept;
// Returns the committed string separately, while all edit/convert operations stay in Engine.
std::u32string handle_key(Engine& engine, WPARAM key, char text, bool shift, bool& committed, bool learn = true);
std::u32string clipboard_text(HWND owner);
}
