#pragma once
#include "common.h"
#include <functional>

namespace ime::windows {
// Thread-confined native popup; never takes keyboard focus from the host.
// Owner-drawn items with Fluent-style appearance; MSAA accessibility via
// WM_GETOBJECT / IAccessible proxied from tracked selection state.
class CandidateWindow {
public:
    CandidateWindow() = default;
    ~CandidateWindow();
    CandidateWindow(const CandidateWindow&) = delete;
    CandidateWindow& operator=(const CandidateWindow&) = delete;
    void show(const Engine& engine, RECT caret, bool show_reading = false);
    void hide() noexcept;
    void set_selection_handler(std::function<void(std::size_t)> handler) { selected_ = std::move(handler); }
    void set_navigation_handler(std::function<void(WPARAM)> handler) { navigated_ = std::move(handler); }
    HWND handle() const noexcept { return window_; }
private:
    bool create();
    void theme();
    void layout(UINT dpi);
    void populate();
    void keep_on_screen();
    void paint(HDC dc, const RECT& clip);
    void paint_controls(HDC dc);
    void paint_related(HDC dc);
    void finish_paint(HDC target, HDC buffer, DWORD* pixels, int width, int height);
    void paint_item(HDC dc, std::size_t visible_index, const RECT& item_rect, bool selected, bool hovered);
    RECT item_bounds(std::size_t visible_index) const;
    std::size_t hit_test(int x, int y) const;
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    HWND window_{};
    HWND related_window_{};
    HFONT font_{};
    HFONT font_small_{};
    HBRUSH background_{};
    COLORREF foreground_{};
    COLORREF foreground_dim_{};
    COLORREF background_color_{};
    COLORREF accent_color_{};
    COLORREF hover_color_{};
    COLORREF select_color_{};
    UINT dpi_{96};
    int item_height_{};
    int content_top_{};
    int content_height_{};
    int footer_height_{};
    int total_height_{};
    int window_width_{};
    std::vector<int> column_widths_;
    std::vector<int> column_lefts_;
    std::vector<int> column_label_widths_;
    RECT caret_{};
    std::size_t page_start_{};
    std::size_t count_{};
    std::size_t selected_index_{};
    std::size_t hovered_index_{SIZE_MAX};
    std::size_t page_count_{};
    std::size_t active_page_{};
    std::vector<std::wstring> candidate_texts_;
    std::vector<std::wstring> candidate_labels_;
    std::wstring reading_text_;
    std::vector<std::pair<std::wstring, std::wstring>> definitions_;
    int definition_scroll_{};
    int definition_scroll_max_{};
    bool acrylic_{};
    bool show_reading_{};
    bool expanded_{};
    bool related_{};
    bool dirty_{true};
    UINT_PTR generation_{};
    std::function<void(std::size_t)> selected_;
    std::function<void(WPARAM)> navigated_;
};
}
