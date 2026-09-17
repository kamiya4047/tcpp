#include "ime/engine.h"
#include "ime/bopomofo.h"
#include "ime/providers.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <utility>

namespace ime {
namespace {
std::vector<Candidate> candidates_for(const InputContext& supplied, const UserDictionary& dictionary) {
    auto input = supplied;
    if (input.sensitive) {
        input.preceding_text.clear();
        input.settings.english_fallback = false;
        input.settings.neural_enabled = false;
        input.settings.learning = false;
    }
    auto decoded = bopomofo::decode(input);
    auto result = std::move(decoded.candidates);
    // Predictions may complete an unfinished word. Their consumed span still
    // refers only to the original keystrokes; Space never inserts completion text.
    for(auto candidate:bopomofo::direct_candidates(input,true)) {
        if(candidate.text.size()<=bopomofo::parse(input.raw_keys).size()) continue;
        candidate.provider="prediction";
        candidate.score=70;
        for(const auto& phrase:dictionary.phrases()) if(!input.sensitive && phrase.text==candidate.text)
            candidate.score+=std::min(20.0,static_cast<double>(phrase.selections));
        result.push_back(std::move(candidate));
    }
    for (auto& candidate : result) {
        // Exact dictionary interpretations occupy a separate score band from fuzzy guesses.
        candidate.score = 500.0 + 100.0 * std::clamp(candidate.confidence, 0.0, 1.0) +
                          std::clamp(candidate.score, -100.0, 100.0);
    }
    if (!input.sensitive) {
        for (auto candidate : dictionary.query(input.raw_keys)) {
            if(candidate.text.size()>bopomofo::parse(input.raw_keys).size()) candidate.provider="prediction";
            candidate.score += 650.0;
            candidate.confidence = 1.0;
            candidate.deterministic = true;
            result.push_back(std::move(candidate));
        }
    }
    auto utilities = providers::query(input);
    for (auto& candidate : utilities) {
        candidate.score = (candidate.trigger_confidence >= 0.95 ? 800.0 : 100.0) +
                          std::clamp(candidate.score, -50.0, 50.0);
        result.push_back(std::move(candidate));
    }
    if (input.settings.english_fallback && input.raw_keys.size() >= 3) {
        static constexpr std::array<std::pair<std::string_view,std::string_view>, 23> words{{
            {"facebook","Facebook"},{"google","Google"},{"hello","hello"},{"world","world"},
            {"windows","Windows"},{"microsoft","Microsoft"},{"github","GitHub"},{"email","email"},
            {"test","test"},{"python","Python"},{"computer","computer"},{"code","code"},
            {"apple","Apple"},{"iphone","iPhone"},{"openai","OpenAI"},{"chatgpt","ChatGPT"},
            {"chrome","Chrome"},{"firefox","Firefox"},{"excel","Excel"},{"word","Word"},
            {"face","face"},{"linux","Linux"},{"taiwan","Taiwan"}}};
        std::string lower = input.raw_keys;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c) {return static_cast<char>(std::tolower(c));});
        for (const auto& [word, surface] : words) if (lower == word) {
            Candidate english;
            english.kind = CandidateKind::English; english.text = from_utf8(surface);
            english.provider = "english"; english.confidence = 0.98;
            // An explicit known Latin word should remain selectable even
            // when its letters also happen to form a valid Bopomofo path.
            english.score = 850.0;
            result.push_back(std::move(english)); break;
        }
    }
    std::stable_sort(result.begin(),result.end(),[](const Candidate& a,const Candidate& b) {return a.score>b.score;});
    std::vector<Candidate> unique;
    for (auto& candidate : result) {
        if (candidate.text.empty()) continue;
        if (std::none_of(unique.begin(),unique.end(),[&](const auto& old) {return old.text==candidate.text;})) {
            if (!input.settings.explanations) candidate.explanation.clear();
            unique.push_back(std::move(candidate));
        }
    }
    const auto count = std::clamp<std::size_t>(input.settings.candidate_count,2,50);
    if (unique.size() >= count) {
        auto correction = std::find_if(unique.begin() + static_cast<std::ptrdiff_t>(count - 1), unique.end(),
                                       [](const Candidate& candidate) { return candidate.kind == CandidateKind::Correction; });
        if (correction != unique.end()) {
            Candidate preserved = std::move(*correction);
            unique.resize(count - 2);
            unique.push_back(std::move(preserved));
        } else {
            unique.resize(count - 1);
        }
    }
    Candidate raw;
    raw.kind=CandidateKind::RawInput; raw.text=from_utf8(input.raw_keys); raw.provider="raw";
    raw.deterministic=true; raw.source_end=input.raw_keys.size(); raw.score=-1000;
    unique.push_back(std::move(raw));
    for (std::size_t i=0;i<unique.size();++i) unique[i].id=(input.generation<<8) + i;
    return unique;
}
}
Engine::Engine(Settings settings) : settings_(std::move(settings)) {}
bool Engine::set_user_dictionary_path(std::string path) {
    user_dictionary_path_=std::move(path);user_dictionary_=UserDictionary{};
    user_dictionary_write_failed_=false;return reload_user_dictionary();
}
bool Engine::reload_user_dictionary() {
    if(user_dictionary_path_.empty()) return true;
    std::error_code error;
    const auto path=std::filesystem::path(std::u8string(user_dictionary_path_.begin(),user_dictionary_path_.end()));
    if(!std::filesystem::exists(path,error)) {
        if(error) return false;
        user_dictionary_=UserDictionary{};
        if(state_.mode==Mode::Composing) refresh();
        return true;
    }
    if(!user_dictionary_.load(user_dictionary_path_)) return false;
    if(state_.mode==Mode::Composing) refresh();
    return true;
}
const UserDictionary& Engine::user_dictionary() const noexcept {return user_dictionary_;}
bool Engine::user_dictionary_write_failed() const noexcept {return user_dictionary_write_failed_;}
bool Engine::persist_user_dictionary(const UserDictionary& previous) {
    user_dictionary_write_failed_=!user_dictionary_path_.empty() && !user_dictionary_.save(user_dictionary_path_);
    if(user_dictionary_write_failed_) {user_dictionary_=previous;return false;}
    if(state_.mode==Mode::Composing) refresh();
    return true;
}
bool Engine::register_user_phrase(std::string keys,std::u32string text) {
    if(sensitive_ || !reload_user_dictionary()) return false;
    const auto previous=user_dictionary_;
    return user_dictionary_.add(std::move(keys),std::move(text)) && persist_user_dictionary(previous);
}
bool Engine::remove_user_phrase(std::string_view keys,std::u32string_view text) {
    if(sensitive_ || !reload_user_dictionary()) return false;
    const auto previous=user_dictionary_;
    return user_dictionary_.remove(keys,text) && persist_user_dictionary(previous);
}
bool Engine::undo_user_learning() {
    if(sensitive_ || !reload_user_dictionary()) return false;
    const auto previous=user_dictionary_;
    return user_dictionary_.undo_learning() && persist_user_dictionary(previous);
}
bool Engine::learn_user_phrase(std::string_view keys,std::u32string_view text) {
    if(!settings_.learning || sensitive_ || !reload_user_dictionary()) return false;
    const auto previous=user_dictionary_;
    return user_dictionary_.add(std::string(keys),std::u32string(text)) && user_dictionary_.learn(keys,text) && persist_user_dictionary(previous);
}
const CompositionState& Engine::state() const noexcept {return state_;}
const Settings& Engine::settings() const noexcept {return settings_;}
void Engine::set_settings(Settings settings) {settings_=std::move(settings); if(state_.mode==Mode::Composing) refresh();}
void Engine::set_context(std::u32string context,bool sensitive) {
    const bool changed = sensitive_ != sensitive;
    context_ = sensitive ? std::u32string{} : std::move(context);
    sensitive_ = sensitive;
    // Rebuild stale predictions on a field privacy transition without losing keys.
    if(changed && state_.mode != Mode::Empty) refresh();
}
void Engine::refresh() {
    ++state_.generation;
    menu_baseline_.clear();
    state_.segments.clear(); state_.active_segment=0; state_.selected=0; state_.candidate_menu_expanded=false; state_.candidate_menu_closed=false;
    if(state_.raw_keys.empty()) {state_.mode=Mode::Empty;state_.reading.clear();state_.candidates.clear();return;}
    state_.mode=Mode::Composing;
    state_.reading=bopomofo::display_reading(state_.raw_keys);
    InputContext input; input.raw_keys=state_.raw_keys;input.preceding_text=context_;input.settings=settings_;
    input.sensitive=sensitive_;input.generation=state_.generation;
    state_.candidates=candidates_for(input,user_dictionary_);
}
void Engine::type(char key) {if(state_.mode==Mode::Converted) return;state_.raw_keys+=key;refresh();}
void Engine::set_input(std::string keys) {state_.raw_keys=std::move(keys);refresh();}
void Engine::backspace() {
    if(state_.mode==Mode::Converted) {refresh();return;}
    if(!state_.raw_keys.empty()) {
        std::size_t begin=state_.raw_keys.size()-1;
        while(begin>0 && (static_cast<unsigned char>(state_.raw_keys[begin])&0xC0)==0x80) --begin;
        state_.raw_keys.erase(begin);refresh();
    }
}
std::vector<Segment> Engine::segment_input(std::size_t begin) {
    InputContext input; input.raw_keys=state_.raw_keys.substr(begin); input.settings=settings_;
    input.preceding_text=context_; input.sensitive=sensitive_;
    auto decoded=bopomofo::decode(input);
    auto segments=std::move(decoded.segments);
    // Preserve the entire unmatched suffix, including invalid or incomplete input.
    if(segments.empty() && !input.raw_keys.empty()) {
        Segment fallback; fallback.raw_keys=input.raw_keys; fallback.source_end=input.raw_keys.size();
        fallback.surface=from_utf8(input.raw_keys); segments.push_back(std::move(fallback));
    }
    for(auto& segment:segments) {
        segment.source_begin+=begin; segment.source_end+=begin;
        refresh_segment(segment);
    }
    return segments;
}
void Engine::convert() {
    if(state_.mode==Mode::Empty) return;
    if(state_.mode==Mode::Converted) { if(state_.candidate_menu_closed) open_candidates(); else cycle(); return; }
    state_.segments=segment_input(0);
    // Explicit utility inputs (calculator, English, etc.) retain their result.
    if((!state_.candidates.empty() && state_.candidates.front().kind!=CandidateKind::Chinese &&
       state_.candidates.front().kind!=CandidateKind::Correction && state_.candidates.front().kind!=CandidateKind::RawInput) ||
       (!state_.candidates.empty() && state_.candidates.front().provider=="user-dictionary")) {
        Segment segment; segment.raw_keys=state_.raw_keys; segment.source_end=state_.raw_keys.size();
        segment.surface=state_.candidates.front().text; segment.candidates=state_.candidates;
        state_.segments={std::move(segment)};
    }
    state_.mode=Mode::Converted; state_.active_segment=0; state_.candidate_menu_closed=true;
    menu_baseline_.clear(); sync_active(); ++state_.generation;
}
void Engine::refresh_segment(Segment& segment) {
    InputContext input; input.raw_keys=segment.raw_keys; input.settings=settings_; input.sensitive=sensitive_;
    auto direct=bopomofo::direct_candidates(input);
    segment.candidates.clear();
    for(auto candidate:direct) if(candidate.source_end==segment.raw_keys.size()) {
        candidate.source_begin=segment.source_begin; candidate.source_end=segment.source_end;
        segment.candidates.push_back(std::move(candidate));
    }
    if(segment.surface.empty()) segment.surface=segment.candidates.empty()?from_utf8(segment.raw_keys):segment.candidates.front().text;
    if(std::none_of(segment.candidates.begin(),segment.candidates.end(),[&](const auto& c){return c.text==segment.surface;})) {
        Candidate current; current.text=segment.surface; current.source_begin=segment.source_begin; current.source_end=segment.source_end;
        segment.candidates.insert(segment.candidates.begin(),std::move(current));
    }
    Candidate raw; raw.kind=CandidateKind::RawInput;raw.provider="raw";raw.text=from_utf8(segment.raw_keys);
    raw.source_begin=segment.source_begin;raw.source_end=segment.source_end;raw.score=-1000;
    segment.candidates.push_back(std::move(raw));
    segment.selected=0;
    for(std::size_t i=0;i<segment.candidates.size();++i) {
        segment.candidates[i].id=i+1;
        if(segment.candidates[i].text==segment.surface) segment.selected=i;
    }
}
void Engine::sync_active() {
    if(state_.segments.empty()) return;
    const auto& segment=state_.segments[state_.active_segment];
    state_.candidates=segment.candidates; state_.selected=segment.selected;
}
void Engine::open_candidates() {
    if(state_.mode!=Mode::Converted || state_.segments.empty()) return;
    if(!state_.candidate_menu_closed && !menu_baseline_.empty()) return;
    menu_baseline_=state_.segments; menu_anchor_=state_.active_segment;
    const auto& segment=menu_baseline_[menu_anchor_];
    InputContext input;input.raw_keys=state_.raw_keys.substr(segment.source_begin);
    input.settings=settings_;input.preceding_text=context_;input.sensitive=sensitive_;
    auto candidates=bopomofo::direct_candidates(input);
    for(auto& candidate:candidates) {
        candidate.source_end+=segment.source_begin;candidate.source_begin=segment.source_begin;
    }
    // Utility/clipboard menus keep distinct actions even when their text matches.
    if(!segment.candidates.empty() && segment.candidates.front().kind!=CandidateKind::Chinese &&
       segment.candidates.front().kind!=CandidateKind::Correction) candidates=segment.candidates;
    else {
        std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){
            if(a.source_end!=b.source_end) return a.source_end>b.source_end;
            return a.score>b.score;
        });
        Candidate raw;raw.kind=CandidateKind::RawInput;raw.provider="raw";raw.text=from_utf8(segment.raw_keys);
        raw.source_begin=segment.source_begin;raw.source_end=segment.source_end;candidates.push_back(std::move(raw));
    }
    auto selected=std::find_if(candidates.begin(),candidates.end(),[&](const auto& c){return c.text==segment.surface && c.source_end==segment.source_end;});
    if(selected==candidates.end()) {
        Candidate current;current.text=segment.surface;current.source_begin=segment.source_begin;current.source_end=segment.source_end;
        candidates.insert(candidates.begin(),std::move(current));
    } else std::rotate(candidates.begin(),selected,selected+1);
    state_.candidates=std::move(candidates);state_.selected=0;state_.candidate_menu_closed=false;
    ++state_.generation;
}
void Engine::cycle(int delta) {
    if(state_.mode!=Mode::Converted) {convert();return;}
    if(state_.candidate_menu_closed) {open_candidates();return;}
    if(state_.candidates.empty()) return;
    auto count=static_cast<long long>(state_.candidates.size());
    auto index=(static_cast<long long>(state_.selected)+delta)%count;
    if(index<0) index+=count;
    select(static_cast<std::size_t>(index));
    // Reaching row 9 opens the multi-column view. Once opened, it remains
    // open until this composition is refreshed or finished.
    if (!state_.candidate_menu_expanded && state_.selected == 8)
        set_candidate_menu_expanded(true);
}
void Engine::select(std::size_t index) {
    if(state_.mode==Mode::Composing) {
        if(index>=state_.candidates.size()) return;
        // These indices refer to full-composition candidates. Converting first
        // would replace them with the first segment's unrelated candidate list.
        Segment segment;
        segment.raw_keys=state_.raw_keys;segment.source_end=state_.raw_keys.size();
        segment.reading=state_.reading;segment.candidates=state_.candidates;
        segment.selected=index;segment.surface=segment.candidates[index].text;
        state_.segments={std::move(segment)};state_.active_segment=0;
        state_.mode=Mode::Converted;sync_active();++state_.generation;
        return;
    }
    if(state_.segments.empty() || index>=state_.candidates.size()) return;
    const auto choice=state_.candidates[index];
    if(state_.candidate_menu_closed) {
        open_candidates();
        const auto found=std::find_if(state_.candidates.begin(),state_.candidates.end(),[&](const auto& c){
            return c.text==choice.text && c.source_end==choice.source_end;
        });
        if(found==state_.candidates.end()) return;
        index=static_cast<std::size_t>(found-state_.candidates.begin());
    }
    const Candidate selected=state_.candidates[index];
    if(menu_baseline_.empty()) {menu_baseline_=state_.segments;menu_anchor_=state_.active_segment;}
    const auto begin=menu_baseline_[menu_anchor_].source_begin;
    const auto end=selected.source_end;
    if(end<=begin || end>state_.raw_keys.size()) return;
    std::vector<Segment> preview(menu_baseline_.begin(),menu_baseline_.begin()+static_cast<std::ptrdiff_t>(menu_anchor_));
    Segment segment;segment.source_begin=begin;segment.source_end=end;
    segment.raw_keys=state_.raw_keys.substr(begin,end-begin);segment.surface=selected.text;
    segment.reading=bopomofo::display_reading(segment.raw_keys);segment.candidates={selected};
    preview.push_back(std::move(segment));
    if(end==menu_baseline_[menu_anchor_].source_end) {
        preview.insert(preview.end(),menu_baseline_.begin()+static_cast<std::ptrdiff_t>(menu_anchor_+1),menu_baseline_.end());
    } else {
        auto suffix=segment_input(end);
        // Preserve the previous wording where it is a valid alternative after
        // resegmentation (e.g. the 葉 from 茶葉 when previewing 苦茶 + 葉).
        std::u32string original;
        bool aligned=true;
        for(const auto& previous:menu_baseline_) {
            if(bopomofo::parse(previous.raw_keys).size()!=previous.surface.size()) {aligned=false;break;}
            original+=previous.surface;
        }
        if(aligned) for(auto& tail:suffix) {
            const auto offset=bopomofo::parse(state_.raw_keys.substr(0,tail.source_begin)).size();
            const auto length=bopomofo::parse(tail.raw_keys).size();
            const auto preferred=original.substr(offset,length);
            for(std::size_t i=0;i<tail.candidates.size();++i) if(tail.candidates[i].text==preferred) {
                tail.surface=preferred;tail.selected=i;break;
            }
        }
        preview.insert(preview.end(),suffix.begin(),suffix.end());
    }
    state_.segments=std::move(preview);state_.active_segment=menu_anchor_;
    state_.selected=index; ++state_.generation;
    // The menu list and its anchor remain unchanged throughout previewing.
}

void Engine::set_candidate_menu_expanded(bool expanded) noexcept {
    state_.candidate_menu_expanded = expanded && state_.mode != Mode::Empty && state_.candidates.size() > 9;
}
void Engine::confirm_candidate(std::size_t index) {
    select(index);
}
void Engine::move_segment(int delta) {
    if(state_.mode!=Mode::Converted || state_.segments.empty()) return;
    menu_baseline_.clear();state_.candidate_menu_closed=true;
    auto index=static_cast<long long>(state_.active_segment)+delta;
    state_.active_segment=static_cast<std::size_t>(std::clamp<long long>(index,0,static_cast<long long>(state_.segments.size()-1)));
    sync_active();++state_.generation;
}
bool Engine::resize_segment(int delta) {
    if(state_.mode!=Mode::Converted || state_.segments.empty() || delta==0) return false;
    menu_baseline_.clear();state_.candidate_menu_closed=true;
    auto index=state_.active_segment;
    auto& segment=state_.segments[index];
    if(delta>0) {
        if(index+1>=state_.segments.size()) return false;
        auto& next=state_.segments[index+1];auto tokens=bopomofo::parse(next.raw_keys);
        if(tokens.empty()) return false;
        auto amount=tokens[0].source_end;
        if(amount==0 || amount>next.raw_keys.size()) return false;
        segment.raw_keys+=next.raw_keys.substr(0,amount);segment.source_end+=amount;
        next.raw_keys.erase(0,amount);next.source_begin+=amount;segment.surface.clear();refresh_segment(segment);
        if(next.raw_keys.empty()) state_.segments.erase(state_.segments.begin()+static_cast<std::ptrdiff_t>(index+1));
        else {next.surface.clear();refresh_segment(next);}
    } else {
        auto tokens=bopomofo::parse(segment.raw_keys);if(tokens.size()<2) return false;
        auto boundary=tokens.back().source_begin;if(boundary==0 || boundary>=segment.raw_keys.size()) return false;
        Segment remainder;remainder.raw_keys=segment.raw_keys.substr(boundary);remainder.source_begin=segment.source_begin+boundary;remainder.source_end=segment.source_end;
        segment.raw_keys.resize(boundary);segment.source_end=remainder.source_begin;segment.surface.clear();refresh_segment(segment);
        if(index+1<state_.segments.size()) {
            auto& next=state_.segments[index+1];next.raw_keys=remainder.raw_keys+next.raw_keys;next.source_begin=remainder.source_begin;next.surface.clear();refresh_segment(next);
        } else {refresh_segment(remainder);state_.segments.push_back(std::move(remainder));}
    }
    ++state_.generation;sync_active();return true;
}
void Engine::transform(int function_key) {
    if(function_key<6 || function_key>10) return;
    if(state_.mode==Mode::Empty) return;
    if(state_.mode==Mode::Composing) convert();
    if(state_.segments.empty()) return;
    auto& segment=state_.segments[state_.active_segment];
    switch(function_key) {
    case 6: segment.surface=segment.candidates.empty()?from_utf8(segment.raw_keys):segment.candidates.front().text;break;
    case 7: segment.surface=bopomofo::display_reading(segment.raw_keys);break;
    case 8: segment.surface=bopomofo::romanize(segment.raw_keys);break;
    case 9: segment.surface=full_width(from_utf8(segment.raw_keys));break;
    case 10: segment.surface=from_utf8(segment.raw_keys);break;
    default:return;
    }
    ++state_.generation;
}
void Engine::escape() {
    if(state_.mode==Mode::Converted && !state_.candidate_menu_closed && !menu_baseline_.empty()) {
        state_.segments=menu_baseline_;state_.active_segment=menu_anchor_;menu_baseline_.clear();
        state_.candidate_menu_closed=true;sync_active();++state_.generation;
    } else if(state_.mode==Mode::Converted) refresh(); else cancel();
}
void Engine::cancel() {auto generation=state_.generation+1;state_=CompositionState{};menu_baseline_.clear();state_.generation=generation;}
std::u32string Engine::commit(bool learn) {
    auto text=preedit();
    if(learn && settings_.learning && !sensitive_ && state_.mode==Mode::Converted && reload_user_dictionary()) {
        const auto previous=user_dictionary_;
        bool learned=false;
        for(const auto& segment:state_.segments) {
            if(bopomofo::parse(segment.raw_keys).empty()) continue;
            user_dictionary_.add(segment.raw_keys,segment.surface);
            learned=user_dictionary_.learn(segment.raw_keys,segment.surface) || learned;
        }
        if(learned) persist_user_dictionary(previous);
    }
    cancel();if(!sensitive_) {context_+=text;if(context_.size()>128) context_.erase(0,context_.size()-128);}return text;
}
std::u32string Engine::preedit() const {
    if(state_.mode==Mode::Converted) {std::u32string text;for(const auto& segment:state_.segments) text+=segment.surface;return text;}
    if(state_.mode==Mode::Composing) return state_.reading.empty()?from_utf8(state_.raw_keys):state_.reading;
    return {};
}
bool Engine::reconvert(std::u32string_view text) {
    auto reading=bopomofo::reverse_reading(text);if(!reading) return false;
    set_input(*reading);convert();
    // Inferred pronunciation is never marked as exact; preserve selected original surface.
    Segment segment;segment.raw_keys=*reading;segment.source_end=reading->size();segment.surface=std::u32string(text);refresh_segment(segment);
    Candidate original;original.text=std::u32string(text);original.kind=CandidateKind::Chinese;original.provider="reconversion";original.confidence=0.7;
    segment.candidates.insert(segment.candidates.begin(),original);segment.selected=0;segment.surface=original.text;
    for(auto& candidate:segment.candidates) if(!candidate.deterministic) candidate.confidence=std::min(candidate.confidence,0.7);
    state_.segments={std::move(segment)};state_.active_segment=0;sync_active();return true;
}
void Engine::paste(std::u32string_view text) {
    // Caller must finish an active composition before opening a clipboard menu.
    if(!settings_.smart_paste || sensitive_ || state_.mode != Mode::Empty) return;
    cancel();state_.raw_keys=to_utf8(text);state_.mode=Mode::Converted;state_.candidate_menu_expanded=false;state_.candidate_menu_closed=false;
    Segment segment;segment.raw_keys=state_.raw_keys;segment.source_end=state_.raw_keys.size();segment.surface=std::u32string(text);segment.candidates=providers::smart_paste(text);
    for(std::size_t index=0;index<segment.candidates.size();++index) {
        auto& candidate=segment.candidates[index];
        candidate.source_begin=0;candidate.source_end=segment.source_end;
        candidate.id=(state_.generation<<8)+index;
    }
    state_.segments.push_back(std::move(segment));sync_active();
}
}
