#pragma once
#include "ime/types.h"
#include <optional>
namespace ime::bopomofo {
struct Token { std::u32string symbols; std::size_t source_begin{}; std::size_t source_end{}; int tone{}; bool abbreviated{}; };
struct Entry { std::u32string text; std::string keys; double frequency{}; std::u32string explanation; };
struct DecodeResult { std::vector<Token> tokens; std::vector<Candidate> candidates; std::vector<Segment> segments; };
// Pure, bounded local operations; malformed input has an empty interpretation.
char32_t map_key(char key);
std::vector<Token> parse(std::string_view keys);
bool can_mark_first_tone(std::string_view keys);
std::u32string display_reading(std::string_view keys);
std::u32string romanize(std::string_view keys);
const std::vector<Entry>& lexicon();
// Diagnostic only: true when the v2 index can serve phonetic buckets lazily.
bool lazy_dictionary_available();
std::string lazy_dictionary_status();
std::vector<Entry> same_sound_characters(std::string_view keys);
DecodeResult decode(const InputContext& context);
// Dictionary entries beginning at the input, without composing multiple entries.
std::vector<Candidate> direct_candidates(const InputContext& context, bool prediction = false);
std::optional<std::string> reverse_reading(std::u32string_view text);
}
