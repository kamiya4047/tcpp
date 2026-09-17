#include "candidate_window.h"
#include "ime/bopomofo.h"
#include <algorithm>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace ime::windows {

// ── helpers ────────────────────────────────────────────────────────────
namespace {
constexpr int kPadX = 12;
constexpr int kPadY = 6;
constexpr int kItemPadX = 6;
constexpr int kCornerRadius = 8;
constexpr int kAccentBarWidth = 3;
constexpr int kNumberWidth = 18;
constexpr int kPageSize = 9;
constexpr int kShadowMargin = 2;
constexpr int kFooterHeight = 28;

int px(UINT dpi, int value) { return MulDiv(value, static_cast<int>(dpi), 96); }

COLORREF mix(COLORREF a, COLORREF b, int weight) {
    return RGB((GetRValue(a) * weight + GetRValue(b) * (100 - weight)) / 100,
               (GetGValue(a) * weight + GetGValue(b) * (100 - weight)) / 100,
               (GetBValue(a) * weight + GetBValue(b) * (100 - weight)) / 100);
}

HBITMAP paint_bitmap(HDC dc, int width, int height, DWORD** pixels) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(pixels), nullptr, 0);
}

void round_rect(HDC dc, const RECT& r, int radius, HBRUSH brush) {
    HRGN rgn = CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, radius, radius);
    FillRgn(dc, rgn, brush);
    DeleteObject(rgn);
}

void triangle(HDC dc, POINT a, POINT b, POINT c, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    const auto old = SelectObject(dc, brush);
    const POINT points[] = {a, b, c};
    Polygon(dc, points, 3);
    SelectObject(dc, old);
    DeleteObject(brush);
}
} // namespace

// ── lifecycle ──────────────────────────────────────────────────────────
CandidateWindow::~CandidateWindow() {
    if (window_) DestroyWindow(window_);
    if (related_window_) DestroyWindow(related_window_);
    if (font_) DeleteObject(font_);
    if (font_small_) DeleteObject(font_small_);
    if (background_) DeleteObject(background_);
}

bool CandidateWindow::create() {
    if (window_) return true;
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCWSTR>(&CandidateWindow::procedure), &module);
    WNDCLASSW wc{};
    wc.lpfnWndProc = procedure;
    wc.hInstance = module;
    wc.lpszClassName = L"TaiwanBopomofo.Candidates";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.style = CS_DROPSHADOW;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    // Owner-drawn popup: no border (we draw our own rounded frame).
    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"Taiwan Bopomofo candidates",
        WS_POPUP | WS_CLIPCHILDREN, 0, 0, 10, 10, nullptr, nullptr, module, this);
    if (!window_) return false;

    // Enable DWM rounded corners on Windows 11+.
    auto preference = static_cast<DWORD>(2); // DWMWCP_ROUNDSMALL
    DwmSetWindowAttribute(window_, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &preference, sizeof(preference));

    // Related-character popup shares the same window class.
    related_window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"Same-sound characters",
        WS_POPUP | WS_CLIPCHILDREN, 0, 0, 280, 360, nullptr, nullptr, module, this);
    if (!related_window_) return false;
    DwmSetWindowAttribute(related_window_, 33, &preference, sizeof(preference));

    theme();
    return true;
}

// ── theme ──────────────────────────────────────────────────────────────
void CandidateWindow::theme() {
    HIGHCONTRASTW contrast{sizeof(contrast), 0, nullptr};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    const bool high_contrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;

    DWORD light = 1, bytes = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);

    if (high_contrast) {
        background_color_ = GetSysColor(COLOR_WINDOW);
        foreground_ = GetSysColor(COLOR_WINDOWTEXT);
        foreground_dim_ = GetSysColor(COLOR_GRAYTEXT);
        accent_color_ = GetSysColor(COLOR_HIGHLIGHT);
        hover_color_ = GetSysColor(COLOR_BTNFACE);
        select_color_ = GetSysColor(COLOR_HIGHLIGHT);
    } else if (light) {
        background_color_ = RGB(249, 249, 249);
        foreground_ = RGB(28, 28, 28);
        foreground_dim_ = RGB(110, 110, 110);
        accent_color_ = RGB(0, 103, 192);
        hover_color_ = RGB(235, 235, 235);
        select_color_ = RGB(225, 230, 240);
    } else {
        background_color_ = RGB(44, 44, 44);
        foreground_ = RGB(242, 242, 242);
        foreground_dim_ = RGB(150, 150, 150);
        accent_color_ = RGB(96, 160, 255);
        hover_color_ = RGB(60, 60, 60);
        select_color_ = RGB(55, 65, 80);
    }

    DWORD color{};
    BOOL opaque{};
    if (!high_contrast && SUCCEEDED(DwmGetColorizationColor(&color, &opaque))) {
        const COLORREF system_accent = RGB((color >> 16) & 255, (color >> 8) & 255, color & 255);
        background_color_ = mix(system_accent, light ? RGB(255,255,255) : RGB(20,20,20), light ? 18 : 45);
        accent_color_ = mix(system_accent, light ? RGB(0,0,0) : RGB(255,255,255), 55);
        hover_color_ = mix(foreground_, background_color_, 8);
        select_color_ = mix(foreground_, background_color_, 15);
    }
    DWORD transparency = 1;
    bytes = sizeof(transparency);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &transparency, &bytes);
    acrylic_ = !high_contrast && transparency != 0;
    for (HWND popup : {window_, related_window_}) {
        const DWORD backdrop = acrylic_ ? DWMSBT_TRANSIENTWINDOW : DWMSBT_NONE;
        const HRESULT result = DwmSetWindowAttribute(popup, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
        if (FAILED(result)) acrylic_ = false;
        const MARGINS margins = acrylic_ ? MARGINS{-1,-1,-1,-1} : MARGINS{};
        DwmExtendFrameIntoClientArea(popup, &margins);
    }

    if (background_) DeleteObject(background_);
    background_ = CreateSolidBrush(background_color_);

    // Apply dark/light to DWM frame on Windows 11+.
    BOOL dark_title = light ? FALSE : TRUE;
    DwmSetWindowAttribute(window_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark_title, sizeof(dark_title));
    if (related_window_)
        DwmSetWindowAttribute(related_window_, 20, &dark_title, sizeof(dark_title));

    InvalidateRect(window_, nullptr, TRUE);
    InvalidateRect(related_window_, nullptr, TRUE);
}

// ── layout ─────────────────────────────────────────────────────────────
void CandidateWindow::layout(UINT dpi) {
    dpi_ = dpi ? dpi : 96;
    if (font_) DeleteObject(font_);
    if (font_small_) DeleteObject(font_small_);
    font_ = CreateFontW(-px(dpi_, 17), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");
    font_small_ = CreateFontW(-px(dpi_, 12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");

    item_height_ = px(dpi_, 30);
    MONITORINFO monitor{sizeof(monitor), {}, {}, 0};
    GetMonitorInfoW(MonitorFromRect(&caret_, MONITOR_DEFAULTTONEAREST), &monitor);
    const int monitor_width = static_cast<int>(monitor.rcWork.right - monitor.rcWork.left);
    const int related_space = related_ ? std::min(px(dpi_, 310), monitor_width / 3) + px(dpi_, 4) : 0;
    const int available_width = std::max(1, monitor_width - related_space);
    // Measure each logical column independently. A long item must not make
    // every other column unnecessarily wide.
    HDC dc = GetDC(window_);
    const auto old_font = SelectObject(dc, font_);
    const auto logical_columns = (candidate_texts_.size() + kPageSize - 1) / kPageSize;
    std::vector<int> logical_widths(logical_columns);
    std::vector<int> logical_label_widths(logical_columns);
    for (std::size_t column = 0; column < logical_columns; ++column) {
        int text_width{};
        for (std::size_t index = column * kPageSize;
             index < std::min(candidate_texts_.size(), (column + 1) * kPageSize); ++index) {
            SIZE size{};
            GetTextExtentPoint32W(dc, candidate_texts_[index].c_str(), static_cast<int>(candidate_texts_[index].size()), &size);
            text_width = std::max(text_width, static_cast<int>(size.cx));
        }
        logical_widths[column] = text_width;
    }
    SelectObject(dc, font_small_);
    for (std::size_t column = 0; column < logical_columns; ++column)
        for (std::size_t index = column * kPageSize;
             index < std::min(candidate_labels_.size(), (column + 1) * kPageSize); ++index) {
            SIZE size{};
            GetTextExtentPoint32W(dc, candidate_labels_[index].c_str(), static_cast<int>(candidate_labels_[index].size()), &size);
            logical_label_widths[column] = std::max(logical_label_widths[column], static_cast<int>(size.cx));
        }
    // Match each independently rounded painting inset, plus glyph overhang room.
    // Keep column widths stable when compact mode expands into columns. The
    // compact view uses this gutter for its right-edge dots; expanded mode
    // keeps it as breathing room while its dots move to the footer centre.
    const int compact_indicator = candidate_texts_.size() > kPageSize ? px(dpi_, 12) : 0;
    const int insets = 2 * px(dpi_, kItemPadX) + px(dpi_, kNumberWidth) +
                       px(dpi_, kAccentBarWidth) + px(dpi_, 2) + px(dpi_, 3) + px(dpi_, 2) + compact_indicator;
    for (std::size_t column = 0; column < logical_columns; ++column) {
        if (logical_label_widths[column]) logical_label_widths[column] += px(dpi_, 8);
        logical_widths[column] = std::clamp(logical_widths[column] + logical_label_widths[column] + insets,
                                            px(dpi_, 58), px(dpi_, 320));
    }
    SelectObject(dc, old_font);
    ReleaseDC(window_, dc);
    std::size_t first_column = selected_index_ / kPageSize, last_column = first_column + 1;
    std::vector<std::pair<std::size_t, std::size_t>> pages;
    for (std::size_t begin = 0; begin < logical_columns;) {
        std::size_t end = begin; int used{};
        while (end < logical_columns && (end == begin || used + logical_widths[end] <= available_width)) used += logical_widths[end++];
        pages.emplace_back(begin, end); begin = end;
    }
    if (expanded_) for (const auto& page : pages) if (first_column >= page.first && first_column < page.second) { first_column=page.first;last_column=page.second;break; }
    page_start_ = first_column * kPageSize;
    count_ = std::min(candidate_texts_.size() - page_start_, (last_column - first_column) * kPageSize);
    column_widths_.assign(logical_widths.begin() + static_cast<std::ptrdiff_t>(first_column), logical_widths.begin() + static_cast<std::ptrdiff_t>(last_column));
    column_label_widths_.assign(logical_label_widths.begin() + static_cast<std::ptrdiff_t>(first_column), logical_label_widths.begin() + static_cast<std::ptrdiff_t>(last_column));
    column_lefts_.clear(); int used{}; for (int width : column_widths_) { column_lefts_.push_back(used); used += width; }
    const int rows = std::max(1, std::min(static_cast<int>(count_), kPageSize));
    content_top_ = show_reading_ ? px(dpi_, 28) : px(dpi_, kPadY);
    content_height_ = item_height_ * rows;
    if (expanded_) {
        // A dot represents one screenful of columns, not one column. For
        // example, six columns on a four-column display make two pages.
        page_count_ = pages.size();
        active_page_ = static_cast<std::size_t>(std::distance(pages.begin(), std::find_if(pages.begin(), pages.end(), [&](const auto& page) { return page.first == first_column; })));
    } else {
        page_count_ = (candidate_texts_.size() + kPageSize - 1) / kPageSize;
        active_page_ = selected_index_ / kPageSize;
    }
    // Expanded mode keeps its footer even for a single screenful: its left
    // control is how the user deliberately returns to compact mode.
    footer_height_ = candidate_texts_.size() > kPageSize ? px(dpi_, kFooterHeight) : 0;

    total_height_ = content_top_ + content_height_ + px(dpi_, kPadY) + footer_height_;
    window_width_ = used;

    SetWindowPos(window_, nullptr, 0, 0, window_width_, total_height_,
                 SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
}

// ── populate ───────────────────────────────────────────────────────────
void CandidateWindow::populate() {
    page_start_ = expanded_ ? 0 : (selected_index_ / kPageSize) * kPageSize;
    if (page_start_ >= candidate_texts_.size()) page_start_ = 0;
    count_ = expanded_ ? candidate_texts_.size()
                       : std::min(static_cast<std::size_t>(kPageSize), candidate_texts_.size() - page_start_);
    dirty_ = true;
}

// ── hit test ───────────────────────────────────────────────────────────
RECT CandidateWindow::item_bounds(std::size_t visible_index) const {
    const int column = static_cast<int>(visible_index / kPageSize);
    const int row = static_cast<int>(visible_index % kPageSize);
    return {column_lefts_[column], content_top_ + row * item_height_,
            column_lefts_[column] + column_widths_[column], content_top_ + (row + 1) * item_height_};
}

std::size_t CandidateWindow::hit_test(int x, int y) const {
    if (x < 0 || x >= window_width_) return SIZE_MAX;
    if (y < content_top_ || y >= content_top_ + content_height_) return SIZE_MAX;
    const auto row = static_cast<std::size_t>((y - content_top_) / item_height_);
    const auto column = static_cast<std::size_t>(std::upper_bound(column_lefts_.begin(), column_lefts_.end(), x) - column_lefts_.begin() - 1);
    const auto index = column * kPageSize + row;
    if (index >= count_) return SIZE_MAX;
    return page_start_ + index;
}

// ── painting ───────────────────────────────────────────────────────────
void CandidateWindow::paint_item(HDC dc, std::size_t visible_index, const RECT& item_rect,
                                  bool selected, bool hovered) {
    const auto index = page_start_ + visible_index;
    if (index >= candidate_texts_.size()) return;

    const int pad = px(dpi_, kItemPadX);
    const int number_w = px(dpi_, kNumberWidth);
    const int accent_w = px(dpi_, kAccentBarWidth);
    const int radius = px(dpi_, 4);
    const int indicator_gutter = !expanded_ && page_count_ > 1 ? px(dpi_, 12) : 0;

    // Background: selection > hover > nothing.
    if (selected || hovered) {
        HBRUSH fill = CreateSolidBrush(selected ? select_color_ : hover_color_);
        RECT bg = item_rect;
        bg.left += px(dpi_, 4);
        bg.right -= px(dpi_, 4) + indicator_gutter;
        bg.top += 1;
        bg.bottom -= 1;
        round_rect(dc, bg, radius, fill);
        DeleteObject(fill);
    }

    // Accent bar for selected item.
    if (selected) {
        HBRUSH accent = CreateSolidBrush(accent_color_);
        RECT bar{item_rect.left + px(dpi_, 6), item_rect.top + px(dpi_, 7),
                 item_rect.left + px(dpi_, 6) + accent_w, item_rect.bottom - px(dpi_, 7)};
        round_rect(dc, bar, accent_w, accent);
        DeleteObject(accent);
    }

    SetBkMode(dc, TRANSPARENT);

    // Number label.
    // Number keys choose a row in the column containing the selection.
    const auto number = visible_index % kPageSize + 1;
    const auto label = std::to_wstring(number);
    HFONT old_font = static_cast<HFONT>(SelectObject(dc, font_small_));
    SetTextColor(dc, foreground_dim_);
    RECT num_rect = item_rect;
    num_rect.left += pad + accent_w + px(dpi_, 2);
    num_rect.right = num_rect.left + number_w;
    // Keep every column aligned; label all rows only in the selected column.
    if (index / kPageSize == selected_index_ / kPageSize)
        DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &num_rect,
                  DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);

    // Candidate text.
    SelectObject(dc, font_);
    SetTextColor(dc, foreground_);
    RECT text_rect = item_rect;
    const auto column = visible_index / kPageSize;
    text_rect.left = num_rect.right + px(dpi_, 3);
    text_rect.right -= pad + column_label_widths_[column] + indicator_gutter;
    const auto& text = candidate_texts_[index];
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &text_rect,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);

    if (!candidate_labels_[index].empty()) {
        SelectObject(dc, font_small_);
        SetTextColor(dc, foreground_dim_);
        RECT label_rect = item_rect;
        label_rect.left = text_rect.right;
        label_rect.right -= pad + indicator_gutter;
        DrawTextW(dc, candidate_labels_[index].c_str(), static_cast<int>(candidate_labels_[index].size()),
                  &label_rect, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    SelectObject(dc, old_font);
}

void CandidateWindow::paint_controls(HDC dc) {
    if (footer_height_ == 0) return;
    const int footer_top = content_top_ + content_height_ + px(dpi_, kPadY);
    const int icon = px(dpi_, 5);
    const int first = px(dpi_, 16);
    const int second = px(dpi_, 42);
    const int center_y = footer_top + footer_height_ / 2;
    SetBkMode(dc, TRANSPARENT);
    if (expanded_) {
        triangle(dc, {first - icon / 2, center_y}, {first + icon / 2, center_y - icon}, {first + icon / 2, center_y + icon}, foreground_);
        triangle(dc, {second + icon / 2, center_y}, {second - icon / 2, center_y - icon}, {second - icon / 2, center_y + icon}, foreground_);
    } else {
        triangle(dc, {first, center_y - icon}, {first - icon, center_y + icon / 2}, {first + icon, center_y + icon / 2}, foreground_);
        triangle(dc, {second, center_y + icon}, {second - icon, center_y - icon / 2}, {second + icon, center_y - icon / 2}, foreground_);
    }

    // Compact mode follows the reference layout: dots run down the right edge.
    // When expanded, move them to the footer centre so columns remain clean.
    const auto dots = std::min<std::size_t>(page_count_, 7);
    const auto shown_active = std::min(active_page_, dots - 1);
    for (std::size_t index = 0; index < dots; ++index) {
        const int radius = px(dpi_, index == shown_active ? 3 : 2);
        int x{};
        int y{};
        if (expanded_) {
            const int spacing = px(dpi_, 12);
            x = window_width_ / 2 + (static_cast<int>(index) * spacing - static_cast<int>(dots - 1) * spacing / 2);
            y = center_y;
        } else {
            const int spacing = px(dpi_, 14);
            x = window_width_ - px(dpi_, 13);
            y = content_top_ + content_height_ / 2 + (static_cast<int>(index) * spacing - static_cast<int>(dots - 1) * spacing / 2);
        }
        HBRUSH dot = CreateSolidBrush(index == shown_active ? foreground_ : foreground_dim_);
        const auto old = SelectObject(dc, dot);
        Ellipse(dc, x - radius, y - radius, x + radius + 1, y + radius + 1);
        SelectObject(dc, old);
        DeleteObject(dot);
    }
}

void CandidateWindow::paint(HDC target_dc, const RECT& /*clip*/) {
    RECT client{};
    GetClientRect(window_, &client);
    const int w = client.right, h = client.bottom;

    // Double-buffered painting to eliminate flicker.
    HDC dc = CreateCompatibleDC(target_dc);
    DWORD* pixels{};
    HBITMAP bmp = paint_bitmap(target_dc, w, h, &pixels);
    HBITMAP old_bmp = static_cast<HBITMAP>(SelectObject(dc, bmp));

    // Background fill.
    FillRect(dc, &client, background_);

    // Optional reading line (playground only).
    if (show_reading_ && !reading_text_.empty()) {
        HFONT old_font = static_cast<HFONT>(SelectObject(dc, font_small_));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, foreground_dim_);
        RECT reading_rect{px(dpi_, kPadX), px(dpi_, 4),
                          w - px(dpi_, kPadX), content_top_};
        DrawTextW(dc, reading_text_.c_str(), static_cast<int>(reading_text_.size()),
                  &reading_rect, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, old_font);
    }

    // Candidate items.
    for (std::size_t i = 0; i < count_; ++i) {
        const RECT item = item_bounds(i);
        const auto abs_index = page_start_ + i;
        paint_item(dc, i, item, abs_index == selected_index_, abs_index == hovered_index_);
    }
    paint_controls(dc);

    // Blit to screen.
    finish_paint(target_dc, dc, pixels, w, h);
    SelectObject(dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

void CandidateWindow::finish_paint(HDC target, HDC buffer, DWORD* pixels, int width, int height) {
    // GDI clears alpha. Restore opaque glyphs and a translucent accent tint so
    // DWM's actual acrylic remains visible instead of covering it with a solid fill.
    GdiFlush();
    const DWORD background = (GetRValue(background_color_) << 16) |
                             (GetGValue(background_color_) << 8) | GetBValue(background_color_);
    for (int i = 0; i < width * height; ++i) {
        DWORD rgb = pixels[i] & 0xffffff;
        if (acrylic_ && rgb == background) {
            constexpr DWORD alpha = 165;
            rgb = ((((rgb >> 16) & 255) * alpha / 255) << 16) |
                  ((((rgb >> 8) & 255) * alpha / 255) << 8) | ((rgb & 255) * alpha / 255);
            pixels[i] = (alpha << 24) | rgb;
        } else pixels[i] = 0xff000000 | rgb;
    }
    BitBlt(target, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
}

void CandidateWindow::paint_related(HDC target) {
    RECT client{};
    GetClientRect(related_window_, &client);
    const int width = client.right, height = client.bottom;
    HDC dc = CreateCompatibleDC(target);
    DWORD* pixels{};
    HBITMAP bitmap = paint_bitmap(target, width, height, &pixels);
    const auto old_bitmap = SelectObject(dc, bitmap);
    const auto old_font = SelectObject(dc, font_small_);
    FillRect(dc, &client, background_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, foreground_);
    const int pad = px(dpi_, 16), header = px(dpi_, 40);
    RECT title{pad, 0, width - pad, header};
    DrawTextW(dc, L"國語辭典 · 同音字釋義", -1, &title, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    HBRUSH separator = CreateSolidBrush(hover_color_);
    RECT line{0, header - 1, width, header};
    FillRect(dc, &line, separator);
    DeleteObject(separator);
    SaveDC(dc);
    IntersectClipRect(dc, 0, header, width, height - px(dpi_, 6));
    int y = header + px(dpi_, 12) - definition_scroll_;
    for (const auto& [word, definition] : definitions_) {
        SelectObject(dc, font_);
        RECT label{pad, y, width - pad, y + px(dpi_, 26)};
        DrawTextW(dc, word.c_str(), -1, &label, DT_SINGLELINE | DT_NOPREFIX);
        y += px(dpi_, 28);
        SelectObject(dc, font_small_);
        RECT body{pad, y, width - pad, y};
        DrawTextW(dc, definition.c_str(), -1, &body, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        DrawTextW(dc, definition.c_str(), -1, &body, DT_WORDBREAK | DT_NOPREFIX);
        y = body.bottom + px(dpi_, 20);
    }
    definition_scroll_max_ = std::max(0, y + definition_scroll_ - height + px(dpi_, 6));
    RestoreDC(dc, -1);
    if (definition_scroll_max_ > 0) {
        const int track = std::max(1, height - header - px(dpi_, 8));
        const int thumb = std::max(px(dpi_, 16), track * track / (track + definition_scroll_max_));
        const int top = header + (track - thumb) * definition_scroll_ / definition_scroll_max_;
        RECT bar{width - px(dpi_, 5), top, width - px(dpi_, 3), top + thumb};
        HBRUSH brush = CreateSolidBrush(foreground_dim_);
        FillRect(dc, &bar, brush);
        DeleteObject(brush);
    }
    finish_paint(target, dc, pixels, width, height);
    SelectObject(dc, old_font);
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

// ── show ───────────────────────────────────────────────────────────────
void CandidateWindow::show(const Engine& engine, RECT caret, bool show_reading) {
    if (engine.state().mode == Mode::Empty || engine.state().candidate_menu_closed || !create()) { hide(); return; }
    const auto& state = engine.state();
    const auto& candidates = state.candidates;
    if (candidates.empty()) { hide(); return; }

    // Cache data.
    caret_ = caret;
    show_reading_ = show_reading;
    selected_index_ = std::min(state.selected, candidates.size() - 1);
    expanded_ = state.candidate_menu_expanded && candidates.size() > static_cast<std::size_t>(kPageSize);
    candidate_texts_.clear();
    candidate_labels_.clear();
    candidate_texts_.reserve(candidates.size());
    candidate_labels_.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        candidate_texts_.push_back(wide(candidate.text));
        // Smart Paste is a menu of transformations. Its explanation is a
        // concise action name, unlike ordinary dictionary definitions.
        candidate_labels_.push_back(candidate.kind == CandidateKind::Clipboard
                                        ? wide(candidate.explanation) : wide(candidate.correction_label));
    }
    if (candidate_texts_.size() <= static_cast<std::size_t>(kPageSize))
        expanded_ = false;

    reading_text_ = wide(engine.preedit());

    populate();

    // Related characters.
    std::vector<std::pair<std::wstring, std::wstring>> definitions;
    if (engine.settings().explanations && state.selected < candidates.size() &&
        candidates[state.selected].text.size() == 1) {
        const auto& keys = state.mode == Mode::Converted && state.active_segment < state.segments.size()
                               ? state.segments[state.active_segment].raw_keys : state.raw_keys;
        const auto related = bopomofo::same_sound_characters(keys);
        for (const auto& item : related) {
            if (item.explanation.empty() || std::none_of(candidates.begin(), candidates.end(),
                [&](const auto& candidate) { return candidate.text == item.text; })) continue;
            definitions.emplace_back(wide(item.text), wide(item.explanation));
        }
    }
    if (definitions != definitions_) definition_scroll_ = 0;
    definitions_ = std::move(definitions);
    related_ = !definitions_.empty();

    // Position & size.
    layout(GetDpiForWindow(window_));

    keep_on_screen();
    ShowWindow(window_, SW_SHOWNOACTIVATE);

    // Force a synchronous repaint to avoid flicker.
    InvalidateRect(window_, nullptr, FALSE);
    UpdateWindow(window_);

    NotifyWinEvent(EVENT_OBJECT_SELECTION, window_, OBJID_CLIENT,
                   static_cast<LONG>(selected_index_ - page_start_ + 1));
}

void CandidateWindow::keep_on_screen() {
    MONITORINFO monitor{sizeof(monitor), {}, {}, 0};
    GetMonitorInfoW(MonitorFromRect(&caret_, MONITOR_DEFAULTTONEAREST), &monitor);
    const int gap = px(dpi_, 4);
    const int related_width = std::min(px(dpi_, 310),
        static_cast<int>(monitor.rcWork.right - monitor.rcWork.left) / 3);
    const int related_space = related_ ? related_width + gap : 0;
    const int left = std::clamp(static_cast<int>(caret_.left),
                               static_cast<int>(monitor.rcWork.left),
                               std::max(static_cast<int>(monitor.rcWork.left),
                                        static_cast<int>(monitor.rcWork.right) - window_width_ - related_space));
    const int top = std::max(static_cast<int>(monitor.rcWork.top),
        static_cast<int>(caret_.bottom) + gap + total_height_ <= monitor.rcWork.bottom
            ? static_cast<int>(caret_.bottom) + gap
            : static_cast<int>(caret_.top) - total_height_ - gap);
    SetWindowPos(window_, HWND_TOPMOST, left, top, window_width_, total_height_, SWP_NOACTIVATE);

    if (related_) {
        RECT bounds{};
        GetWindowRect(window_, &bounds);
        const int right = static_cast<int>(bounds.right) + px(dpi_, 4);
        int related_left = right;
        if (right + related_width > static_cast<int>(monitor.rcWork.right))
            related_left = std::max(static_cast<int>(monitor.rcWork.left),
                                    static_cast<int>(bounds.left) - px(dpi_, 4) - related_width);
        const int related_height = std::min(std::max(total_height_, px(dpi_, 320)),
                                            static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
        const int related_top = std::min(top, static_cast<int>(monitor.rcWork.bottom) - related_height);
        SetWindowPos(related_window_, HWND_TOPMOST, related_left, related_top, related_width, related_height,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(related_window_, nullptr, FALSE);
    } else {
        ShowWindow(related_window_, SW_HIDE);
    }

}

void CandidateWindow::hide() noexcept {
    if (window_) ShowWindow(window_, SW_HIDE);
    if (related_window_) ShowWindow(related_window_, SW_HIDE);
    expanded_ = false;
    hovered_index_ = SIZE_MAX;
    selected_index_ = 0;
}

// ── window procedure ───────────────────────────────────────────────────
LRESULT CALLBACK CandidateWindow::procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<CandidateWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wparam, lparam);

    // Only handle messages for the main candidate window, not the related popup.
    if (window != self->window_) {
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (message == WM_PAINT) {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(window, &ps);
            if (dc) self->paint_related(dc);
            EndPaint(window, &ps);
            return 0;
        }
        if (message == WM_MOUSEWHEEL) {
            self->definition_scroll_ = std::clamp(self->definition_scroll_ -
                static_cast<short>(HIWORD(wparam)) * px(self->dpi_, 48) / WHEEL_DELTA,
                0, self->definition_scroll_max_);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (message == WM_ERASEBKGND && self->background_) {
            return 1;
        }
        if (message == WM_NCDESTROY) {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            self->related_window_ = nullptr;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    try {
        switch (message) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_ERASEBKGND:
            return 1; // We handle all painting in WM_PAINT.

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(window, &ps);
            if (dc) self->paint(dc, ps.rcPaint);
            EndPaint(window, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            const int y = static_cast<short>(HIWORD(lparam));
            const int x = static_cast<short>(LOWORD(lparam));
            const int footer_top = self->content_top_ + self->content_height_ + px(self->dpi_, kPadY);
            if (self->footer_height_ != 0 && y >= footer_top && y < self->total_height_ && self->navigated_) {
                const bool previous = x < px(self->dpi_, 29);
                const bool next = x >= px(self->dpi_, 29) && x < px(self->dpi_, 58);
                if (previous || next)
                    self->navigated_(self->expanded_ ? (previous ? VK_LEFT : VK_RIGHT)
                                                       : (previous ? VK_PRIOR : VK_NEXT));
                return 0;
            }
            const auto index = self->hit_test(x, y);
            if (index != SIZE_MAX && self->selected_) {
                self->selected_(index);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            const int y = static_cast<short>(HIWORD(lparam));
            const auto index = self->hit_test(static_cast<short>(LOWORD(lparam)), y);
            if (index != self->hovered_index_) {
                self->hovered_index_ = index;
                InvalidateRect(window, nullptr, FALSE);
            }
            // Request WM_MOUSELEAVE.
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, window, 0};
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE:
            if (self->hovered_index_ != SIZE_MAX) {
                self->hovered_index_ = SIZE_MAX;
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;

        case WM_DPICHANGED:
            self->layout(HIWORD(wparam));
            self->keep_on_screen();
            InvalidateRect(window, nullptr, FALSE);
            return 0;

        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
        case WM_DWMCOLORIZATIONCOLORCHANGED:
        case WM_DWMCOMPOSITIONCHANGED:
            self->theme();
            return 0;

        case WM_NCDESTROY:
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            self->window_ = nullptr;
            break;
        }
    } catch (...) {
        self->hide();
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace ime::windows
