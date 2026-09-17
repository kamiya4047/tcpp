#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ime {
enum class CandidateKind { Chinese, English, RawInput, Correction, Dictionary, Symbol, Emoji, Calculation, NumberConversion, DateTime, RocCalendar, Unicode, Email, Typography, Clipboard, Internet };
struct Candidate {
    std::uint64_t id{};
    CandidateKind kind{CandidateKind::Chinese};
    std::u32string text;
    std::u32string reading;
    std::u32string explanation;
    std::u32string correction_label;
    std::string provider;
    double confidence{1.0};
    double trigger_confidence{1.0};
    double score{};
    bool deterministic{};
    std::size_t source_begin{};
    std::size_t source_end{};
};
struct Settings {
    bool chaining{true};
    bool typo_tolerance{true};
    bool english_fallback{true};
    bool explanations{true};
    bool neural_enabled{false};
    bool smart_paste{false};
    bool internet_enabled{false};
    bool learning{false};
    std::size_t candidate_count{9};
    std::size_t beam_width{64};
    int hotkeys[5]{0x75,0x76,0x77,0x78,0x79};
    std::vector<std::string> email_domains{"gmail.com", "outlook.com", "yahoo.com.tw", "icloud.com"};
};
struct InputContext {
    std::string raw_keys;
    std::u32string preceding_text;
    Settings settings;
    bool sensitive{};
    std::uint64_t generation{};
    std::string reference_datetime;
};
struct Segment {
    std::size_t source_begin{};
    std::size_t source_end{};
    std::string raw_keys;
    std::u32string reading;
    std::u32string surface;
    std::vector<Candidate> candidates;
    std::size_t selected{};
};
// Value objects are owned by callers. Engine and adapters are thread-confined.
std::u32string from_utf8(std::string_view text);
std::string to_utf8(std::u32string_view text);
std::u32string full_width(std::u32string_view text);
std::u32string half_width(std::u32string_view text);
}
