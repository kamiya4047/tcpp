#include "ime/bopomofo.h"
#include "test.h"

#include <algorithm>
#include <random>
#include <set>

namespace {
ime::bopomofo::DecodeResult convert(std::string keys) {
    ime::InputContext input;
    input.raw_keys = std::move(keys);
    return ime::bopomofo::decode(input);
}
bool has_text(const ime::bopomofo::DecodeResult& result, std::u32string_view text) {
    return std::any_of(result.candidates.begin(), result.candidates.end(), [&](const auto& item) {
        return item.text == text;
    });
}
}

void run_decoder_tests() {
    using namespace ime::bopomofo;
    {
        CHECK(has_text(convert("g/ "), U"生"));
        CHECK(has_text(convert("g/ cji6"), U"生活"));
        ime::InputContext input;
        input.settings.typo_tolerance = false;
        input.raw_keys = "g/ ";
        const auto candidates = direct_candidates(input);
        CHECK(std::any_of(candidates.begin(), candidates.end(), [](const auto& c) { return c.text == U"生"; }));
        CHECK(std::none_of(candidates.begin(), candidates.end(), [](const auto& c) { return c.text == U"省"; }));
        const auto definitions = same_sound_characters("g/ ");
        CHECK(std::any_of(definitions.begin(), definitions.end(), [](const auto& e) { return e.text == U"生"; }));
        input.raw_keys = "g/6";
        const auto second_tone = direct_candidates(input);
        CHECK(std::none_of(second_tone.begin(), second_tone.end(), [](const auto& c) { return c.text == U"生"; }));
    }
    {
        ime::InputContext input;input.raw_keys="qu/3";input.settings.explanations=false;
        const auto candidates=direct_candidates(input);
        const auto pin=std::find_if(candidates.begin(),candidates.end(),[](const auto& c){return c.text==U"品";});
        CHECK(pin!=candidates.end());CHECK(pin->correction_label==U"ㄣ");
        CHECK(pin->kind==ime::CandidateKind::Correction);
        input.raw_keys="qu/6";
        const auto both=direct_candidates(input);
        CHECK(std::any_of(both.begin(),both.end(),[](const auto& c){return c.text==U"平" && c.kind==ime::CandidateKind::Chinese;}));
        CHECK(std::any_of(both.begin(),both.end(),[](const auto& c){return c.text==U"貧" && c.correction_label==U"ㄣ";}));
        input.raw_keys="qu/3";
        input.settings.typo_tolerance=false;
        const auto exact=direct_candidates(input);
        CHECK(std::none_of(exact.begin(),exact.end(),[](const auto& c){return c.text==U"品";}));
        // Nearby keyboard substitutions work for initials and medials too:
        // ㄆ→ㄅ (q→1), ㄧ→ㄨ (u→j).
        input.settings.typo_tolerance=true;input.raw_keys="qu3";
        const auto medial=direct_candidates(input);
        CHECK(std::any_of(medial.begin(),medial.end(),[](const auto& c){return c.text==U"譜" && c.correction_label==U"ㄨ";}));
        input.raw_keys="q03";
        const auto initial=direct_candidates(input);
        CHECK(std::any_of(initial.begin(),initial.end(),[](const auto& c){return c.text==U"板" && c.correction_label==U"ㄅ";}));
    }
    CHECK(map_key('1') == U'ㄅ');
    CHECK(map_key('q') == U'ㄆ');
    CHECK(map_key('s') == U'ㄋ');
    CHECK(map_key('u') == U'ㄧ');
    CHECK(map_key('j') == U'ㄨ');
    CHECK(map_key('m') == U'ㄩ');
    CHECK(map_key('/') == U'ㄥ');
    CHECK(map_key('-') == U'ㄦ');
    CHECK(map_key('6') == U'ˊ');
    CHECK(map_key('@') == 0);
    auto tokens = parse("su3cl3");
    CHECK(tokens.size() == 2);
    CHECK(tokens[0].symbols == U"ㄋㄧ");
    CHECK(tokens[0].tone == 3);
    CHECK(tokens[0].source_begin == 0 && tokens[0].source_end == 3);
    CHECK(tokens[1].source_begin == 3 && tokens[1].source_end == 6);
    CHECK(display_reading("su3cl3") == U"ㄋㄧˇ ㄏㄠˇ");
    CHECK(display_reading("ak7") == U"˙ㄇㄜ");
    CHECK(romanize("su3cl3") == U"ni3 hao3");
    CHECK(romanize("5j/ jp6") == U"zhong1 wen2");
    CHECK(romanize("rm,6") == U"jue2");
    CHECK(romanize("m4") == U"yu4");
    CHECK(romanize("xmp4") == U"lvn4");
    CHECK(parse("sucl").size() == 2);
    CHECK(parse("sc").size() == 2 && parse("sc")[0].abbreviated);
    CHECK(parse("").empty());
    CHECK(parse("3su").empty());
    CHECK(parse("su33").empty());
    CHECK(parse("su#").empty());
    CHECK(parse("su  ").empty());
    CHECK(can_mark_first_tone("su"));
    CHECK(can_mark_first_tone("su3cl"));
    CHECK(can_mark_first_tone("s"));
    CHECK(!can_mark_first_tone(""));
    CHECK(!can_mark_first_tone("su3"));
    CHECK(!can_mark_first_tone("su#"));
    CHECK(!can_mark_first_tone(std::string(128, 's')));
    CHECK(parse(std::string(129, 's')).empty());
    CHECK(parse(std::string(33, 's')).empty());
    CHECK(convert("su3cl3").candidates.front().text == U"你好");
    CHECK(convert("sucl").candidates.front().text == U"你好");
    CHECK(has_text(convert("sc"), U"你好"));
    CHECK(convert("5j/ jp6").candidates.front().text == U"中文");
    CHECK(convert("w96j0 ").candidates.front().text == U"臺灣");
    CHECK(has_text(convert("w96j0 "), U"台灣"));
    CHECK(convert("g4").candidates.front().text == U"是");
    CHECK(convert("2k7").candidates.front().text == U"的");
    CHECK(has_text(convert("2k7"), U"得"));
    CHECK(has_text(convert("2k7"), U"地"));
    CHECK(!convert("2k7").candidates.front().explanation.empty());
    CHECK(convert("su3cl3").segments.size() == 1);
    CHECK(convert("su3cl3").segments.front().raw_keys == "su3cl3");
    CHECK(convert("su3cl3").segments.front().source_end == 6);
    auto long_result = convert("ji3su3cl3");
    CHECK(long_result.candidates.front().text == U"我你好");
    std::string reconstructed;
    for (const auto& segment : long_result.segments) reconstructed += segment.raw_keys;
    CHECK(reconstructed == "ji3su3cl3");
    CHECK(reverse_reading(U"你好") == std::optional<std::string>{"su3cl3"});
    CHECK(!reverse_reading(U"😀"));
    CHECK(!reverse_reading(U""));
    const auto same_sound = ime::bopomofo::same_sound_characters("2k7");
    const auto particle = std::find_if(same_sound.begin(), same_sound.end(), [](const auto& entry) {
        return entry.text == U"的";
    });
    CHECK(particle != same_sound.end() && !particle->explanation.empty());
    if (lexicon().size() > 10000) {
        const auto expanded = ime::bopomofo::same_sound_characters("ru");
        CHECK(std::any_of(expanded.begin(), expanded.end(), [](const auto& entry) {
            return entry.text == U"雞" && !entry.explanation.empty();
        }));
    }
    const auto fuzzy = convert("xu3cl3");
    CHECK(has_text(fuzzy, U"你好"));
    CHECK(fuzzy.candidates.front().kind == ime::CandidateKind::Chinese);
    const auto first_fuzzy = std::find_if(fuzzy.candidates.begin(), fuzzy.candidates.end(), [](const auto& item) {
        return item.kind == ime::CandidateKind::Correction;
    });
    CHECK(first_fuzzy != fuzzy.candidates.end());
    CHECK(std::all_of(first_fuzzy, fuzzy.candidates.end(), [](const auto& item) {
        return item.kind == ime::CandidateKind::Correction;
    }));
    ime::InputContext input;
    input.raw_keys = "xu3cl3";
    input.settings.typo_tolerance = false;
    CHECK(!has_text(decode(input), U"你好"));
    input.raw_keys = "sc";
    input.settings.chaining = false;
    CHECK(!has_text(decode(input), U"你好"));
    input = {};
    input.raw_keys = "2k7";
    input.preceding_text = U"跑";
    CHECK(decode(input).candidates.front().text == U"得");
    input.sensitive = true;
    CHECK(decode(input).candidates.front().text == U"的");
    input.settings.explanations = false;
    CHECK(decode(input).candidates.front().explanation.empty());
    input.settings.candidate_count = 0;
    input.settings.beam_width = 0;
    CHECK(decode(input).candidates.size() == 1);
    CHECK(lexicon().size() >= 650);
    // With one visible candidate, an input with only fuzzy matches must still
    // produce a recoverable candidate. Locate such a case for either data set.
    bool checked_correction_only = false;
    for (const auto& entry : lexicon()) {
        if (entry.text.size() != 1) continue;
        auto raw = entry.keys;
        for (const char replacement : std::string("1qaz2wsxedcrfv5tgbyhnu")) {
            raw.front() = replacement;
            ime::InputContext trial;
            trial.raw_keys = raw;
            trial.settings.candidate_count = 9;
            const auto many = decode(trial);
            if (many.candidates.empty() || many.candidates.front().kind != ime::CandidateKind::Correction) continue;
            trial.settings.candidate_count = 1;
            CHECK(decode(trial).candidates.size() == 1);
            CHECK(decode(trial).candidates.front().text == many.candidates.front().text);
            checked_correction_only = true;
            break;
        }
        if (checked_correction_only) break;
    }
    CHECK(checked_correction_only);
    std::set<std::pair<std::u32string, std::string>> unique_entries;
    for (const auto& entry : lexicon()) {
        CHECK(!entry.text.empty() && !entry.keys.empty());
        CHECK(parse(entry.keys).size() == entry.text.size());
        CHECK(!display_reading(entry.keys).empty());
        CHECK(!romanize(entry.keys).empty());
        CHECK(entry.frequency > 0);
        CHECK(unique_entries.emplace(entry.text, entry.keys).second);
    }
    // Malformed byte streams and boundary lengths must remain bounded and deterministic.
    std::mt19937 random(0x54435050);
    for (int trial = 0; trial < 200; ++trial) {
        std::string raw;
        const auto size = random() % 150;
        for (unsigned index = 0; index < size; ++index) raw += static_cast<char>(random() % 256);
        const auto result = convert(raw);
        CHECK(result.tokens.size() <= 32);
        CHECK(result.candidates.size() <= 9);
        for (const auto& segment : result.segments) {
            CHECK(segment.source_begin < segment.source_end);
            CHECK(segment.source_end <= raw.size());
            CHECK(segment.raw_keys == raw.substr(segment.source_begin, segment.source_end - segment.source_begin));
        }
    }
}
