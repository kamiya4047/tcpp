#include "ime/providers.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>

namespace ime::providers {
namespace {
constexpr std::size_t kMaxQueryBytes = 4096;

void add(std::vector<Candidate>& out, CandidateKind kind, std::u32string text,
         std::string provider, std::u32string explanation, std::size_t input_size,
         double trigger = 1.0) {
    Candidate c;
    c.kind = kind;
    c.text = std::move(text);
    c.provider = std::move(provider);
    c.explanation = std::move(explanation);
    c.trigger_confidence = trigger;
    c.confidence = 1.0;
    c.score = trigger >= 0.9 ? 200.0 - static_cast<double>(out.size()) : 10.0;
    c.deterministic = true;
    c.source_end = input_size;
    // FNV-1a gives stable IDs without implementation-defined std::hash behavior.
    c.id = 14695981039346656037ULL;
    for (const auto ch : c.provider) {
        c.id ^= static_cast<unsigned char>(ch);
        c.id *= 1099511628211ULL;
    }
    for (const auto ch : c.text) {
        c.id ^= static_cast<std::uint32_t>(ch);
        c.id *= 1099511628211ULL;
    }
    out.push_back(std::move(c));
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

std::optional<int> integer(std::string_view s) {
    int value{};
    if (s.empty()) return std::nullopt;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || end != s.data() + s.size()) return std::nullopt;
    return value;
}

// No evaluation engine or executable input. Limits bound recursive work and output.
class Arithmetic {
public:
    explicit Arithmetic(std::string_view input) : input_(input) {}
    std::optional<long double> evaluate() {
        if (input_.empty() || input_.size() > 256) return std::nullopt;
        auto value = expression(0);
        spaces();
        if (!valid_ || position_ != input_.size() || !bounded(value)) return std::nullopt;
        return value;
    }
private:
    bool bounded(long double n) {
        if (!std::isfinite(n) || std::abs(n) > 1.0e18L) valid_ = false;
        return valid_;
    }
    void spaces() {
        while (position_ < input_.size() && input_[position_] == ' ') ++position_;
    }
    bool take(char c) {
        spaces();
        if (position_ < input_.size() && input_[position_] == c) { ++position_; return true; }
        return false;
    }
    long double expression(unsigned depth) {
        auto value = term(depth);
        while (valid_) {
            if (take('+')) value += term(depth);
            else if (take('-')) value -= term(depth);
            else break;
            bounded(value);
        }
        return value;
    }
    long double term(unsigned depth) {
        auto value = factor(depth);
        while (valid_) {
            if (take('*')) value *= factor(depth);
            else if (take('/')) {
                const auto rhs = factor(depth);
                if (rhs == 0) { valid_ = false; break; }
                value /= rhs;
            } else if (take('%')) {
                const auto rhs = factor(depth);
                if (rhs == 0) { valid_ = false; break; }
                value = std::fmod(value, rhs);
            } else break;
            bounded(value);
        }
        return value;
    }
    long double factor(unsigned depth) {
        if (depth > 32 || ++tokens_ > 128) { valid_ = false; return 0; }
        if (take('+')) return factor(depth + 1);
        if (take('-')) return -factor(depth + 1);
        if (take('(')) {
            const auto value = expression(depth + 1);
            if (!take(')')) valid_ = false;
            return value;
        }
        spaces();
        long double value = 0;
        bool digits = false;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
            digits = true;
            value = value * 10 + (input_[position_++] - '0');
            if (!bounded(value)) return 0;
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            long double place = 0.1L;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                digits = true;
                value += (input_[position_++] - '0') * place;
                place *= 0.1L;
            }
        }
        if (!digits) valid_ = false;
        return value;
    }
    std::string_view input_;
    std::size_t position_{};
    unsigned tokens_{};
    bool valid_{true};
};

std::string decimal(long double value) {
    if (value == 0) return "0";
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(16) << value;
    return stream.str();
}

std::u32string group_number(unsigned value, bool financial) {
    const std::u32string_view digits = financial ? U"零壹貳參肆伍陸柒捌玖" : U"零一二三四五六七八九";
    const std::array<std::u32string_view, 4> units = financial
        ? std::array<std::u32string_view, 4>{U"", U"拾", U"佰", U"仟"}
        : std::array<std::u32string_view, 4>{U"", U"十", U"百", U"千"};
    std::u32string out;
    bool zero = false;
    unsigned divisor = 1000;
    for (int i = 3; i >= 0; --i, divisor /= 10) {
        const auto digit = value / divisor % 10;
        if (digit) {
            if (zero && !out.empty()) out += U'零';
            out += digits[digit];
            out += units[static_cast<std::size_t>(i)];
            zero = false;
        } else if (!out.empty()) zero = true;
    }
    return out;
}

std::optional<std::u32string> chinese_number(std::string_view s, bool financial) {
    if (s.empty() || s.size() > 40) return std::nullopt;
    bool negative = false;
    if (s.front() == '-' || s.front() == '+') {
        negative = s.front() == '-';
        s.remove_prefix(1);
    }
    const auto dot = s.find('.');
    auto whole = s.substr(0, dot);
    const auto fraction = dot == std::string_view::npos ? std::string_view{} : s.substr(dot + 1);
    if (whole.empty() || whole.size() > 16 || (dot != std::string_view::npos && fraction.empty())) return std::nullopt;
    std::uint64_t number = 0;
    for (const auto c : whole) {
        if (c < '0' || c > '9') return std::nullopt;
        number = number * 10 + static_cast<unsigned>(c - '0');
    }
    for (const auto c : fraction) if (c < '0' || c > '9') return std::nullopt;
    std::array<unsigned, 4> groups{};
    for (auto& group : groups) { group = static_cast<unsigned>(number % 10000); number /= 10000; }
    const std::array<std::u32string_view, 4> units{U"", U"萬", U"億", U"兆"};
    std::u32string out;
    bool gap = false;
    for (int i = 3; i >= 0; --i) {
        const auto index = static_cast<std::size_t>(i);
        if (!groups[index]) { if (!out.empty()) gap = true; continue; }
        if (!out.empty() && (gap || groups[index] < 1000)) out += U'零';
        out += group_number(groups[index], financial);
        out += units[index];
        gap = false;
    }
    if (out.empty()) out = U"零";
    if (!financial && out.starts_with(U"一十")) out.erase(out.begin());
    if (!fraction.empty()) {
        const std::u32string_view digits = financial ? U"零壹貳參肆伍陸柒捌玖" : U"零一二三四五六七八九";
        out += U'點';
        for (const auto c : fraction) out += digits[static_cast<std::size_t>(c - '0')];
    }
    if (negative && out != U"零") out.insert(out.begin(), U'負');
    return out;
}

struct Date { int year; int month; int day; };
bool valid_date(Date d) {
    if (d.year < 1 || d.year > 9999 || d.month < 1 || d.month > 12) return false;
    constexpr std::array<int, 12> days{31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = d.year % 4 == 0 && (d.year % 100 != 0 || d.year % 400 == 0);
    return d.day >= 1 && d.day <= days[static_cast<std::size_t>(d.month - 1)] + (d.month == 2 && leap ? 1 : 0);
}
std::optional<Date> date(std::string_view value) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') return std::nullopt;
    const auto y = integer(value.substr(0, 4));
    const auto m = integer(value.substr(5, 2));
    const auto d = integer(value.substr(8, 2));
    if (!y || !m || !d || !valid_date({*y, *m, *d})) return std::nullopt;
    return Date{*y, *m, *d};
}
std::u32string roc_year(int year) {
    return year > 1911 ? U"民國" + from_utf8(std::to_string(year - 1911)) + U"年"
                       : U"民國前" + from_utf8(std::to_string(1912 - year)) + U"年";
}

std::optional<Date> reference_date(const InputContext& context) {
    if (!context.reference_datetime.empty()) return date(std::string_view(context.reference_datetime).substr(0, 10));
    const auto now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &now) != 0) return std::nullopt;
#else
    if (!localtime_r(&now, &local)) return std::nullopt;
#endif
    return Date{local.tm_year + 1900, local.tm_mon + 1, local.tm_mday};
}

Date shift_day(Date d, int offset) {
    using namespace std::chrono;
    const year_month_day shifted{sys_days{year{d.year} / month{static_cast<unsigned>(d.month)} / day{static_cast<unsigned>(d.day)}} + days{offset}};
    return Date{static_cast<int>(shifted.year()), static_cast<int>(static_cast<unsigned>(shifted.month())), static_cast<int>(static_cast<unsigned>(shifted.day()))};
}

std::u32string normalize(std::u32string_view input) {
    std::u32string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto ch = input[i];
        if (ch == U'\r') {
            out += U'\n';
            if (i + 1 < input.size() && input[i + 1] == U'\n') ++i;
        } else if (ch == U'\u00a0' || ch == U'\u202f') out += U' ';
        else out += ch;
    }
    return out;
}
std::u32string typography(std::u32string_view input) {
    std::u32string out;
    bool open_double = true;
    for (const auto ch : input) {
        switch (ch) {
        case U',': out += U'，'; break;
        case U'.': out += U'。'; break;
        case U'?': out += U'？'; break;
        case U'!': out += U'！'; break;
        case U';': out += U'；'; break;
        case U':': out += U'：'; break;
        case U'(': out += U'（'; break;
        case U')': out += U'）'; break;
        case U'"': out += open_double ? U'「' : U'」'; open_double = !open_double; break;
        default: out += ch; break;
        }
    }
    return out;
}

bool domain_valid(std::string_view s) {
    if (s.empty() || s.size() > 253 || s.find('.') == std::string_view::npos) return false;
    std::size_t length = 0;
    char previous = '.';
    for (const auto c : s) {
        if (c == '.') {
            if (length == 0 || length > 63 || previous == '-') return false;
            length = 0;
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
            if (!length && c == '-') return false;
            ++length;
        }
        previous = c;
    }
    return length > 0 && length <= 63 && previous != '-';
}
} // namespace

std::vector<Candidate> query(const InputContext& context) {
    std::vector<Candidate> out;
    if (context.sensitive || context.raw_keys.size() > kMaxQueryBytes) return out;
    const auto raw = trim(context.raw_keys);
    if (raw.empty()) return out;
    const auto size = context.raw_keys.size();
    if (raw.starts_with('=')) {
        if (const auto result = Arithmetic(raw.substr(1)).evaluate())
            add(out, CandidateKind::Calculation, from_utf8(decimal(*result)), "calculator", U"算式結果", size);
        return out;
    }
    // Plain expressions require an operator and a fully valid numeric grammar.
    // ISO dates and bare numbers are left to normal composition.
    if (raw.find_first_of("+-*/%") != std::string_view::npos && !date(raw)) {
        const auto first = raw.find_first_not_of(" +-\t");
        const bool numeric_start = first != std::string_view::npos && ((raw[first] >= '0' && raw[first] <= '9') || raw[first] == '(' || raw[first] == '.');
        if (numeric_start && raw.find_first_of("+-*/%", first + 1) != std::string_view::npos) {
            if (const auto result = Arithmetic(raw).evaluate()) {
                add(out, CandidateKind::Calculation, from_utf8(decimal(*result)), "calculator", U"算式結果", size, 0.95);
                return out;
            }
        }
    }
    const bool relative_year = raw == "今年" || raw == "去年" || raw == "明年";
    const bool relative_day = raw == "今天" || raw == "昨天" || raw == "明天" || raw == "today" || raw == "yesterday" || raw == "tomorrow";
    if (relative_year || relative_day) {
        if (auto reference = reference_date(context)) {
            if (relative_year) {
                reference->year += raw == "去年" ? -1 : raw == "明年" ? 1 : 0;
                if (reference->year < 1 || reference->year > 9999) return out;
                add(out, CandidateKind::DateTime, from_utf8(std::to_string(reference->year)) + U"年", "relative-date", U"本機日曆年份", size, 0.7);
                add(out, CandidateKind::RocCalendar, roc_year(reference->year), "relative-date", U"本機日曆民國年份", size, 0.7);
            } else {
                const int offset = raw == "昨天" || raw == "yesterday" ? -1 : raw == "明天" || raw == "tomorrow" ? 1 : 0;
                const auto shifted = shift_day(*reference, offset);
                if (!valid_date(shifted)) return out;
                add(out, CandidateKind::DateTime, from_utf8(std::to_string(shifted.year)) + U"年" + from_utf8(std::to_string(shifted.month)) + U"月" + from_utf8(std::to_string(shifted.day)) + U"日", "relative-date", U"本機日曆日期", size, 0.7);
            }
        }
        return out;
    }
    if (raw.starts_with("num:") || raw.starts_with("money:")) {
        const bool financial = raw.starts_with("money:");
        if (auto result = chinese_number(raw.substr(financial ? 6 : 4), financial))
            add(out, CandidateKind::NumberConversion, *result, financial ? "financial" : "numbers", financial ? U"中文大寫數字" : U"中文數字", size);
        return out;
    }
    if (raw.starts_with("roc:")) {
        auto value = raw.substr(4);
        const bool before = value.starts_with("before") || value.starts_with("前");
        if (before) value.remove_prefix(value.starts_with("before") ? 6 : std::string_view("前").size());
        const auto year = integer(value);
        if (year && *year >= 1 && *year <= (before ? 1911 : 8088)) {
            const auto ad = before ? 1912 - *year : *year + 1911;
            add(out, CandidateKind::RocCalendar, U"西元" + from_utf8(std::to_string(ad)) + U"年", "roc-calendar", U"民國年換算西元年", size);
        }
        return out;
    }
    if (raw.starts_with("ad:")) {
        const auto year = integer(raw.substr(3));
        if (year && *year >= 1 && *year <= 9999)
            add(out, CandidateKind::RocCalendar, roc_year(*year), "roc-calendar", U"西元年換算民國年（無民國零年）", size);
        return out;
    }
    if (raw.starts_with("date:")) {
        if (const auto parsed = date(raw.substr(5))) {
            const auto md = from_utf8(std::to_string(parsed->month)) + U"月" + from_utf8(std::to_string(parsed->day)) + U"日";
            add(out, CandidateKind::DateTime, from_utf8(std::to_string(parsed->year)) + U"年" + md, "date", U"西元日期", size);
            add(out, CandidateKind::RocCalendar, roc_year(parsed->year) + md, "roc-calendar", U"民國日期", size);
            auto slash = std::string(raw.substr(5));
            std::replace(slash.begin(), slash.end(), '-', '/');
            add(out, CandidateKind::DateTime, from_utf8(slash), "date", U"西元日期", size);
        }
        return out;
    }
    if (raw.starts_with("time:")) {
        const auto value = raw.substr(5);
        if ((value.size() == 5 || value.size() == 8) && value[2] == ':' && (value.size() == 5 || value[5] == ':')) {
            const auto h = integer(value.substr(0, 2));
            const auto m = integer(value.substr(3, 2));
            const auto s = value.size() == 8 ? integer(value.substr(6, 2)) : std::optional<int>{0};
            if (h && m && s && *h >= 0 && *h <= 23 && *m >= 0 && *m <= 59 && *s >= 0 && *s <= 59) {
                auto result = std::u32string(*h < 12 ? U"上午" : U"下午") + from_utf8(std::to_string(*h % 12 == 0 ? 12 : *h % 12)) + U"時" + from_utf8(std::to_string(*m)) + U"分";
                if (value.size() == 8) result += from_utf8(std::to_string(*s)) + U"秒";
                add(out, CandidateKind::DateTime, std::move(result), "time", U"12 小時制", size);
            }
        }
        return out;
    }
    if (raw.starts_with("U+") || raw.starts_with("u+")) {
        const auto hex = raw.substr(2);
        std::uint32_t scalar{};
        const auto [end, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), scalar, 16);
        if (!hex.empty() && hex.size() <= 6 && ec == std::errc{} && end == hex.data() + hex.size()
            && scalar <= 0x10ffff && !(scalar >= 0xd800 && scalar <= 0xdfff) && scalar >= 0x20 && !(scalar >= 0x7f && scalar <= 0x9f))
            add(out, CandidateKind::Unicode, std::u32string(1, static_cast<char32_t>(scalar)), "unicode", from_utf8(std::string(raw)), size);
        return out;
    }
    struct Alias { std::string_view name; std::u32string_view text; bool emoji; };
    static constexpr Alias aliases[] = {
        {"右", U"→➡➔⇛☞▶", false}, {"right", U"→➡➔⇛☞▶", false},
        {"左", U"←⬅⇐☜◀", false}, {"left", U"←⬅⇐☜◀", false},
        {"上", U"↑⇑▲", false}, {"下", U"↓⇓▼", false},
        {"星", U"★☆", false}, {"star", U"★☆", false},
        {"心", U"♥♡", false}, {"heart", U"❤💖", true},
        {"笑", U"😀😃😊🙂", true}, {"smile", U"😀😃😊🙂", true},
        {"讚", U"👍👏", true}, {"thumbsup", U"👍", true},
        {"台灣", U"🇹🇼", true}, {"taiwan", U"🇹🇼", true},
        {"度", U"°℃℉", false}, {"degree", U"°℃℉", false}
    };
    auto alias = raw;
    const bool explicit_alias = alias.starts_with(':');
    if (explicit_alias) { alias.remove_prefix(1); if (alias.ends_with(':')) alias.remove_suffix(1); }
    for (const auto& entry : aliases) {
        if (entry.name != alias || (!explicit_alias && static_cast<unsigned char>(alias.front()) < 128)) continue;
        if (entry.name == "台灣" || entry.name == "taiwan") {
            add(out, CandidateKind::Emoji, std::u32string(entry.text), "emoji", U"台灣旗幟", size, explicit_alias ? 1.0 : 0.25);
        } else for (const auto symbol : entry.text)
            add(out, entry.emoji ? CandidateKind::Emoji : CandidateKind::Symbol, std::u32string(1, symbol), entry.emoji ? "emoji" : "symbols", from_utf8(std::string(entry.name)), size, explicit_alias ? 1.0 : 0.25);
        return out;
    }
    const auto at = raw.find('@');
    if (at != std::string_view::npos && raw.find('@', at + 1) == std::string_view::npos) {
        const auto local = raw.substr(0, at);
        const bool valid_local = local.size() <= 64 && std::all_of(local.begin(), local.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '+' || c == '-';
        });
        if (valid_local) {
            for (const auto& domain : context.settings.email_domains) {
                if (out.size() >= 20) break;
                if (domain_valid(domain) && std::string_view(domain).starts_with(raw.substr(at + 1)))
                    add(out, CandidateKind::Email, from_utf8(std::string(local) + '@' + domain), "email", U"電子郵件網域", size);
            }
            return out;
        }
    }
    struct Transform { std::string_view prefix; std::u32string (*apply)(std::u32string_view); std::u32string_view label; };
    const Transform transforms[] = {{"full:", full_width, U"全形"}, {"half:", half_width, U"半形"}, {"norm:", normalize, U"換行與空白正規化"}, {"type:", typography, U"中文標點"}};
    for (const auto& transform : transforms) if (raw.starts_with(transform.prefix)) {
        const auto payload_offset = context.raw_keys.find(transform.prefix) + transform.prefix.size();
        add(out, CandidateKind::Typography, transform.apply(from_utf8(std::string_view(context.raw_keys).substr(payload_offset))), "typography", std::u32string(transform.label), size);
        break;
    }
    return out;
}

std::vector<Candidate> smart_paste(std::u32string_view original) {
    std::vector<Candidate> out;
    add(out, CandidateKind::Clipboard, std::u32string(original), "clipboard-original", U"貼上原文（原始格式由主程式保留）", original.size());
    add(out, CandidateKind::Clipboard, std::u32string(original), "clipboard-plain", U"純文字", original.size());
    // Preserve original even for unusually large clipboard data; skip costly extras.
    if (original.size() > 1024 * 1024) return out;
    add(out, CandidateKind::Clipboard, normalize(original), "clipboard-normalize", U"換行與空白正規化", original.size());
    add(out, CandidateKind::Clipboard, full_width(original), "clipboard-full", U"全形", original.size());
    add(out, CandidateKind::Clipboard, half_width(original), "clipboard-half", U"半形", original.size());
    add(out, CandidateKind::Clipboard, typography(original), "clipboard-typography", U"中文標點", original.size());
    return out;
}
} // namespace ime::providers
