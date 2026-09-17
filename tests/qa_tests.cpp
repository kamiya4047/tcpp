#include "ime/bopomofo.h"
#include "ime/engine.h"
#include "test.h"
#include <algorithm>
#include <climits>
#include <random>

namespace {
void check_segments(const ime::Engine& engine) {
    const auto& state = engine.state();
    if (state.mode != ime::Mode::Converted) return;
    CHECK(!state.segments.empty());
    CHECK(state.active_segment < state.segments.size());
    std::string joined;
    std::size_t end = 0;
    for (const auto& segment : state.segments) {
        CHECK(segment.source_begin == end);
        CHECK(segment.source_end >= segment.source_begin);
        CHECK(segment.source_end - segment.source_begin == segment.raw_keys.size());
        CHECK(segment.selected < segment.candidates.size());
        joined += segment.raw_keys;
        end = segment.source_end;
    }
    CHECK(joined == state.raw_keys);
    CHECK(end == state.raw_keys.size());
}
bool has_kind(const ime::Engine& engine, ime::CandidateKind kind) {
    return std::any_of(engine.state().candidates.begin(), engine.state().candidates.end(), [kind](const auto& c) { return c.kind == kind; });
}
}

void run_qa_tests() {
    // Stable menu previews: every move preserves a contiguous partition of
    // original input, including after a shorter candidate resegments the tail.
    {
        ime::Engine flow;
        flow.set_input("g/cji6dj;4fm06gjo3");flow.convert();
        CHECK(flow.state().candidate_menu_closed);
        CHECK(flow.state().segments.size()==2);
        CHECK(flow.state().segments[0].surface==U"生活");
        CHECK(flow.state().segments[1].surface==U"礦泉水");
        flow.move_segment(1);flow.open_candidates();
        const auto menu=flow.state().candidates;
        auto find=[&](std::u32string_view text) {
            const auto it=std::find_if(flow.state().candidates.begin(),flow.state().candidates.end(),[&](const auto& c){return c.text==text;});
            CHECK(it!=flow.state().candidates.end());return static_cast<std::size_t>(it-flow.state().candidates.begin());
        };
        const auto phrase=find(U"礦泉水"),character=find(U"礦");
        for(int repeat=0;repeat<5;++repeat) {
            flow.select(character);check_segments(flow);
            CHECK(flow.state().candidates.size()==menu.size());
            CHECK(flow.state().segments[0].surface==U"生活");
            flow.select(phrase);check_segments(flow);
            CHECK(flow.preedit()==U"生活礦泉水");
        }
        flow.select(character);flow.escape();
        CHECK(flow.preedit()==U"生活礦泉水");CHECK(flow.state().candidate_menu_closed);
        flow.open_candidates();CHECK(!flow.state().candidate_menu_closed);
        flow.select(find(U"礦"));flow.move_segment(-1);flow.move_segment(1);flow.open_candidates();
        flow.select(find(U"礦泉水"));CHECK(flow.commit()==U"生活礦泉水");
        flow.set_input("dj3t86u,4");flow.convert();
        flow.move_segment(1);flow.open_candidates();flow.select(find(U"葉"));
        flow.move_segment(-1);flow.open_candidates();
        flow.select(find(U"苦茶"));flow.select(find(U"苦"));check_segments(flow);
        CHECK(flow.state().segments.size()==2);
        CHECK(flow.state().segments[1].surface==U"茶葉");
        flow.select(find(U"苦茶"));CHECK(flow.preedit()==U"苦茶葉");check_segments(flow);
        flow.set_input("dj;4");
        CHECK(std::any_of(flow.state().candidates.begin(),flow.state().candidates.end(),[](const auto& c){return c.text==U"礦泉水";}));
        flow.convert();CHECK(flow.state().raw_keys=="dj;4");CHECK(flow.preedit().size()==1);
        ime::Settings learning;learning.learning=true;
        ime::Engine learner(learning);learner.set_input("dj;4");
        const auto prediction=std::find_if(learner.state().candidates.begin(),learner.state().candidates.end(),[](const auto& c){return c.text==U"礦泉水";});
        CHECK(prediction!=learner.state().candidates.end());
        learner.select(static_cast<std::size_t>(prediction-learner.state().candidates.begin()));
        CHECK(learner.user_dictionary().phrases().empty());
        CHECK(learner.commit()==U"礦泉水");
        CHECK(!learner.user_dictionary().phrases().empty());
        learner.set_input("dj;4");learner.convert();
        CHECK(learner.preedit().size()==1);
    }
    ime::Engine engine;
    // Selection while composing must preserve the displayed candidate's meaning.
    for (const auto keys : {"su3cl3", "su3cl3su3cl3", "1+1", "facebook"}) {
        engine.set_input(keys);
        const auto candidates = engine.state().candidates;
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            engine.set_input(keys);
            engine.select(index);
            CHECK(engine.preedit() == candidates[index].text);
            CHECK(engine.state().raw_keys == keys);
            check_segments(engine);
        }
        engine.set_input(keys);
        const auto raw_index = engine.state().candidates.size() - 1;
        engine.select(raw_index);
        CHECK(engine.commit() == ime::from_utf8(keys));
    }
    engine.set_input("su3cl3");
    engine.transform(99);
    CHECK(engine.state().mode == ime::Mode::Composing);
    engine.select(999999);
    CHECK(engine.state().mode == ime::Mode::Composing);
    engine.convert();
    engine.cycle(INT_MAX);
    engine.cycle(INT_MIN);
    check_segments(engine);
    engine.set_input("su3cl3");engine.convert();
    CHECK(engine.resize_segment(-1));
    check_segments(engine);
    const auto source = engine.state().raw_keys;
    engine.move_segment(1);
    engine.transform(10);
    CHECK(engine.state().raw_keys == source);
    check_segments(engine);
    engine.move_segment(-1);
    CHECK(engine.resize_segment(1));
    check_segments(engine);
    // A whole-composition candidate can be resized to phrase or character spans.
    ime::Engine segmented;
    segmented.set_input("su3cl3su3cl3");
    segmented.select(0);
    CHECK(segmented.state().segments.size() == 1);
    CHECK(ime::bopomofo::parse(segmented.state().segments[0].raw_keys).size() == 4);
    // Candidate rows now stay within the active phrase segment. Segment
    // boundaries are changed explicitly with resize_segment(), not hidden in
    // a menu of whole-input recombinations.
    ime::Engine dynamic;
    dynamic.set_input("su3cl3");
    dynamic.select(0);
    CHECK(!dynamic.state().candidates.empty());
    const auto active_end=dynamic.state().segments[dynamic.state().active_segment].source_end;
    CHECK(std::all_of(dynamic.state().candidates.begin(),dynamic.state().candidates.end(),[&](const auto& candidate) {
        return candidate.source_end==active_end;
    }));
    // Shortening a segment prepends the removed reading to the following
    // segment, equivalent to [苦茶][葉] -> 苦[茶葉], rather than [苦][茶][葉].
    ime::Engine shifted;
    shifted.set_input("su3cl3su3cl3");
    shifted.select(0);
    CHECK(shifted.resize_segment(-1));
    CHECK(shifted.resize_segment(-1));
    CHECK(ime::bopomofo::parse(shifted.state().segments[0].raw_keys).size()==2);
    CHECK(shifted.resize_segment(-1));
    shifted.confirm_candidate(shifted.state().selected);
    CHECK(shifted.state().segments.size()==2);
    CHECK(ime::bopomofo::parse(shifted.state().segments[0].raw_keys).size()==1);
    CHECK(ime::bopomofo::parse(shifted.state().segments[1].raw_keys).size()==3);
    CHECK(shifted.state().active_segment==0);
    CHECK(segmented.resize_segment(-1));
    CHECK(segmented.resize_segment(-1));
    CHECK(segmented.state().segments.size() == 2);
    CHECK(ime::bopomofo::parse(segmented.state().segments[0].raw_keys).size() == 2);
    CHECK(ime::bopomofo::parse(segmented.state().segments[1].raw_keys).size() == 2);
    CHECK(segmented.state().active_segment == 0);
    segmented.confirm_candidate(segmented.state().selected);
    segmented.move_segment(1);
    CHECK(segmented.state().active_segment == 1);
    CHECK(segmented.resize_segment(-1));
    CHECK(segmented.state().segments.size() == 3);
    CHECK(ime::bopomofo::parse(segmented.state().segments[1].raw_keys).size() == 1);
    CHECK(ime::bopomofo::parse(segmented.state().segments[2].raw_keys).size() == 1);
    segmented.confirm_candidate(segmented.state().selected);
    segmented.move_segment(1);
    CHECK(segmented.state().active_segment == 2);
    check_segments(segmented);
    CHECK(!engine.resize_segment(1));
    CHECK(!engine.resize_segment(0));
    // Repeated segment edits must keep a contiguous partition of original keys.
    std::mt19937 random{20260913};
    for (int step = 0; step < 200; ++step) {
        engine.resize_segment(random() % 2 == 0 ? 1 : -1);
        engine.move_segment(random() % 2 == 0 ? 1 : -1);
        engine.cycle(random() % 2 == 0 ? 1 : -1);
        check_segments(engine);
        CHECK(engine.state().raw_keys == source);
    }
    ime::Settings settings;
    settings.smart_paste = true;
    engine.set_settings(settings);
    engine.set_input("su3cl3");
    const auto prior = engine.preedit();
    engine.paste(U"clipboard");
    CHECK(engine.state().raw_keys == "su3cl3");
    CHECK(engine.preedit() == prior);
    engine.convert();
    const auto converted_prior = engine.preedit();
    engine.paste(U"clipboard");
    CHECK(engine.preedit() == converted_prior);
    engine.cancel();
    const std::u32string clipboard = U"Ａ exact 😀\r\n";
    engine.paste(clipboard);
    check_segments(engine);
    engine.transform(9);
    CHECK(engine.state().raw_keys == ime::to_utf8(clipboard));
    engine.select(0);
    CHECK(engine.commit() == clipboard);
    engine.set_input("=1+1");
    CHECK(has_kind(engine, ime::CandidateKind::Calculation));
    engine.set_context(U"sensitive context", true);
    CHECK(engine.state().raw_keys == "=1+1");
    CHECK(!has_kind(engine, ime::CandidateKind::Calculation));
    engine.set_input("facebook");
    CHECK(!has_kind(engine, ime::CandidateKind::English));
    CHECK(has_kind(engine, ime::CandidateKind::RawInput));
    engine.set_context({}, false);
    CHECK(has_kind(engine, ime::CandidateKind::English));
    engine.set_input("=1+1");
    CHECK(engine.state().candidates.front().kind == ime::CandidateKind::Calculation);
    engine.set_input("su3cl3");
    CHECK(engine.state().candidates.front().kind == ime::CandidateKind::Chinese);
    CHECK(!has_kind(engine, ime::CandidateKind::Calculation));
    settings.candidate_count = 1;
    engine.set_settings(settings);
    CHECK(engine.state().candidates.size() <= 2);
    CHECK(engine.state().candidates.back().kind == ime::CandidateKind::RawInput);
    // Bounded decoding may decline long input, but the literal must stay intact.
    const std::string large(8192, 's');
    engine.set_input(large);
    CHECK(engine.state().raw_keys == large);
    CHECK(engine.state().candidates.back().text == ime::from_utf8(large));
    engine.select(engine.state().candidates.size() - 1);
    CHECK(engine.commit() == ime::from_utf8(large));
    engine.set_input(ime::to_utf8(U"你好😀"));
    engine.backspace();
    CHECK(engine.state().raw_keys == ime::to_utf8(U"你好"));
    engine.backspace();
    CHECK(engine.state().raw_keys == ime::to_utf8(U"你"));
    engine.backspace();
    CHECK(engine.state().mode == ime::Mode::Empty);
}
