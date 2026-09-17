#pragma once
#include "ime/types.h"
#include "ime/user_dictionary.h"
namespace ime {
enum class Mode { Empty, Composing, Converted };
struct CompositionState {
    std::string raw_keys;
    std::u32string reading;
    std::vector<Candidate> candidates;
    std::vector<Segment> segments;
    std::size_t active_segment{};
    std::size_t selected{};
    // Presentation state shared with the native candidate window. Selection
    // paging must not implicitly turn into horizontal expansion.
    bool candidate_menu_expanded{};
    bool candidate_menu_closed{};
    Mode mode{Mode::Empty};
    std::uint64_t generation{};
};
// Single-threaded composition controller. No OS calls. Raw input survives until commit/cancel.
class Engine {
public:
    explicit Engine(Settings settings = {});
    const CompositionState& state() const noexcept;
    const Settings& settings() const noexcept;
    void set_settings(Settings settings);
    void set_context(std::u32string context, bool sensitive = false);
    void type(char key);
    void set_input(std::string keys);
    void backspace();
    void convert();
    void open_candidates();
    void cycle(int delta = 1);
    void select(std::size_t index);
    void set_candidate_menu_expanded(bool expanded) noexcept;
    // Apply a menu choice as a preview; Enter is the only explicit commit key.
    void confirm_candidate(std::size_t index);
    void move_segment(int delta);
    bool resize_segment(int delta);
    void transform(int function_key);
    void escape();
    void cancel();
    std::u32string commit(bool learn = true);
    std::u32string preedit() const;
    bool reconvert(std::u32string_view text);
    void paste(std::u32string_view text);
    bool set_user_dictionary_path(std::string path);
    bool reload_user_dictionary();
    bool register_user_phrase(std::string keys, std::u32string text);
    bool remove_user_phrase(std::string_view keys, std::u32string_view text);
    bool undo_user_learning();
    bool learn_user_phrase(std::string_view keys, std::u32string_view text);
    const UserDictionary& user_dictionary() const noexcept;
    bool user_dictionary_write_failed() const noexcept;
private:
    std::vector<Segment> segment_input(std::size_t begin);
    std::vector<Segment> menu_baseline_;
    std::size_t menu_anchor_{};
    bool persist_user_dictionary(const UserDictionary& previous);
    void refresh();
    void sync_active();
    void refresh_segment(Segment& segment);
    Settings settings_;
    CompositionState state_;
    std::u32string context_;
    bool sensitive_{};
    UserDictionary user_dictionary_;
    std::string user_dictionary_path_;
    bool user_dictionary_write_failed_{};
};
// UTF-8 line settings; invalid values recover to safe defaults. No globals.
Settings load_settings(const std::string& path);
bool save_settings(const Settings& settings, const std::string& path);
}
