#include "ime/bopomofo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <cstdint>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ime::bopomofo {
namespace {
constexpr std::string_view kKeys = "1qaz2wsxedcrfv5tgbyhnu jm8ik,9ol.0p;/-";
constexpr std::u32string_view kSymbols = U"ㄅㄆㄇㄈㄉㄊㄋㄌㄍㄎㄏㄐㄑㄒㄓㄔㄕㄖㄗㄘㄙㄧ ㄨㄩㄚㄛㄜㄝㄞㄟㄠㄡㄢㄣㄤㄥㄦ";
constexpr std::size_t kMaximumKeys = 128;
constexpr std::size_t kMaximumTokens = 32;

int category(char32_t symbol) {
    if (symbol >= U'ㄅ' && symbol <= U'ㄙ') return 1;
    if (symbol >= U'ㄧ' && symbol <= U'ㄩ') return 2;
    if (symbol >= U'ㄚ' && symbol <= U'ㄦ') return 3;
    return 0;
}

int tone_of(char key) {
    switch (key) {
    case ' ': return 1;
    case '6': return 2;
    case '3': return 3;
    case '4': return 4;
    case '7': return 5;
    default: return 0;
    }
}

char tone_key(int tone) {
    constexpr std::array<char, 6> keys{'\0', ' ', '6', '3', '4', '7'};
    return keys[static_cast<std::size_t>(tone)];
}

std::string syllable_keys(std::string_view syllable) {
    if (syllable.size() < 2 || syllable.back() < '1' || syllable.back() > '5')
        throw std::logic_error("Lexicon reading must carry a tone");
    const int tone = syllable.back() - '0';
    syllable.remove_suffix(1);
    static const std::map<std::string_view, std::string_view> zero_initial{
        {"yi", "u"}, {"ya", "u8"}, {"yo", "ui"}, {"ye", "u,"},
        {"yao", "ul"}, {"you", "u."}, {"yan", "u0"}, {"yin", "up"},
        {"yang", "u;"}, {"ying", "u/"}, {"yong", "m/"},
        {"wu", "j"}, {"wa", "j8"}, {"wo", "ji"}, {"wai", "j9"},
        {"wei", "jo"}, {"wan", "j0"}, {"wen", "jp"}, {"wang", "j;"},
        {"weng", "j/"}, {"yu", "m"}, {"yue", "m,"}, {"yuan", "m0"}, {"yun", "mp"}
    };
    if (const auto found = zero_initial.find(syllable); found != zero_initial.end())
        return std::string(found->second) + tone_key(tone);
    static const std::array<std::pair<std::string_view, char>, 21> initials{{
        {"zh", '5'}, {"ch", 't'}, {"sh", 'g'}, {"b", '1'}, {"p", 'q'},
        {"m", 'a'}, {"f", 'z'}, {"d", '2'}, {"t", 'w'}, {"n", 's'},
        {"l", 'x'}, {"g", 'e'}, {"k", 'd'}, {"h", 'c'}, {"j", 'r'},
        {"q", 'f'}, {"x", 'v'}, {"r", 'b'}, {"z", 'y'}, {"c", 'h'}, {"s", 'n'}
    }};
    std::string result;
    std::string_view initial;
    for (const auto& [name, key] : initials) {
        if (syllable.starts_with(name)) {
            initial = name;
            result += key;
            syllable.remove_prefix(name.size());
            break;
        }
    }
    if (syllable == "i" && (initial == "zh" || initial == "ch" || initial == "sh" ||
        initial == "r" || initial == "z" || initial == "c" || initial == "s")) {
        return result + tone_key(tone);
    }
    if (initial == "j" || initial == "q" || initial == "x") {
        if (syllable == "u") return result + "m" + tone_key(tone);
        if (syllable == "ue") return result + "m," + tone_key(tone);
        if (syllable == "uan") return result + "m0" + tone_key(tone);
        if (syllable == "un") return result + "mp" + tone_key(tone);
    }
    static const std::map<std::string_view, std::string_view> finals{
        {"a", "8"}, {"o", "i"}, {"e", "k"}, {"ai", "9"}, {"ei", "o"},
        {"ao", "l"}, {"ou", "."}, {"an", "0"}, {"en", "p"}, {"ang", ";"},
        {"eng", "/"}, {"er", "-"}, {"i", "u"}, {"ia", "u8"}, {"ie", "u,"},
        {"iao", "ul"}, {"iu", "u."}, {"ian", "u0"}, {"in", "up"},
        {"iang", "u;"}, {"ing", "u/"}, {"iong", "m/"},
        {"u", "j"}, {"ua", "j8"}, {"uo", "ji"}, {"uai", "j9"},
        {"ui", "jo"}, {"uan", "j0"}, {"un", "jp"}, {"uang", "j;"},
        {"ong", "j/"}, {"v", "m"}, {"ve", "m,"}, {"van", "m0"}, {"vn", "mp"}
    };
    const auto found = finals.find(syllable);
    if (found == finals.end()) throw std::logic_error("Unsupported lexicon pinyin final");
    return result + std::string(found->second) + tone_key(tone);
}

std::string reading_keys(std::string_view pinyin) {
    std::string result;
    while (!pinyin.empty()) {
        const auto end = pinyin.find(' ');
        result += syllable_keys(pinyin.substr(0, end));
        if (end == std::string_view::npos) break;
        pinyin.remove_prefix(end + 1);
    }
    return result;
}

char key_for(char32_t symbol) {
    const auto position = kSymbols.find(symbol);
    return position == std::u32string_view::npos ? '\0' : kKeys[position];
}

bool adjacent(char32_t left, char32_t right) {
    if (left == right) return false;
    constexpr std::array<std::string_view, 4> rows{"1234567890-", "qwertyuiop", "asdfghjkl;", "zxcvbnm,./"};
    auto position = [&](char key) {
        for (std::size_t row = 0; row < rows.size(); ++row) {
            const auto column = rows[row].find(key);
            if (column != std::string_view::npos)
                return std::pair{static_cast<double>(column) + static_cast<double>(row) * 0.25,
                                 static_cast<double>(row)};
        }
        return std::pair{-100.0, -100.0};
    };
    const auto a = position(key_for(left));
    const auto b = position(key_for(right));
    return std::abs(a.first - b.first) <= 1.0 && std::abs(a.second - b.second) <= 1.0;
}

bool confusion(char32_t left, char32_t right) {
    constexpr std::array<std::u32string_view, 7> groups{U"ㄓㄗ", U"ㄔㄘ", U"ㄕㄙ", U"ㄋㄌ", U"ㄌㄖ", U"ㄣㄥ", U"ㄢㄤ"};
    return std::any_of(groups.begin(), groups.end(), [&](auto group) {
        return left != right && group.find(left) != std::u32string_view::npos &&
               group.find(right) != std::u32string_view::npos;
    });
}

struct Match { double penalty{}; int fuzzy{}; int abbreviated{}; bool valid{true}; };

bool matches_dictionary_tone(const Token& input, const Token& expected) {
    // Dictionary readings omit first tone; unmarked user input remains a wildcard.
    return input.tone == 0 || input.tone == (expected.tone == 0 ? 1 : expected.tone);
}

Match match_token(const Token& input, const Token& expected, const Settings& settings) {
    if (!matches_dictionary_tone(input, expected)) return {0, 0, 0, false};
    const double tone_penalty = input.tone == 0 ? 0.05 : 0;
    if (input.symbols == expected.symbols) return {tone_penalty, 0, 0, true};
    if (settings.chaining && input.tone == 0 && !input.symbols.empty() &&
        expected.symbols.starts_with(input.symbols))
        return {0.7 + tone_penalty, 0, 1, true};
    if (!settings.typo_tolerance || input.symbols.size() != expected.symbols.size())
        return {0, 0, 0, false};
    int differences = 0;
    double penalty = tone_penalty;
    for (std::size_t index = 0; index < input.symbols.size(); ++index) {
        if (input.symbols[index] == expected.symbols[index]) continue;
        if (++differences > 1) return {0, 0, 0, false};
        if (confusion(input.symbols[index], expected.symbols[index])) penalty += 3;
        else if (adjacent(input.symbols[index], expected.symbols[index])) penalty += 4;
        else return {0, 0, 0, false};
    }
    return {penalty, differences, 0, true};
}

struct IndexedEntry { const Entry* entry{}; std::vector<Token> tokens; };
const std::vector<IndexedEntry>& indexed_lexicon() {
    static const auto index = [] {
        std::vector<IndexedEntry> result;
        for (const auto& entry : lexicon()) result.push_back({&entry, parse(entry.keys)});
        return result;
    }();
    return index;
}

struct ReadingBucket { Token first; std::vector<const IndexedEntry*> entries; };
const std::vector<ReadingBucket>& reading_buckets() {
    static const auto buckets = [] {
        std::vector<ReadingBucket> result;
        std::map<std::pair<std::u32string, int>, std::size_t> positions;
        for (const auto& indexed : indexed_lexicon()) {
            if (indexed.tokens.empty()) continue;
            const auto& first = indexed.tokens.front();
            const auto [position, inserted] = positions.emplace(std::pair{first.symbols, first.tone}, result.size());
            if (inserted) result.push_back({first, {}});
            result[position->second].entries.push_back(&indexed);
        }
        return result;
    }();
    return buckets;
}

#if IME_ENABLE_DICTIONARY
std::filesystem::path dictionary_index_path() {
#ifdef _WIN32
    // Resolve the containing DLL, not the host executable (e.g. Notepad).
    static const char module_anchor{};
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&module_anchor), &module)) return {};
    std::wstring filename(32768, L'\0');
    const auto length = GetModuleFileNameW(module, filename.data(), static_cast<DWORD>(filename.size()));
    if (length == 0 || length >= filename.size()) return {};
    filename.resize(length);
    return std::filesystem::path(filename).parent_path() / "data" / "lexicon" / "dictionary.idx";
#elif defined(__linux__)
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : executable.parent_path() / "data" / "lexicon" / "dictionary.idx";
#else
    return {};
#endif
}
struct DiskBucket { std::string keys; Token token; std::uint64_t offset{}; std::uint32_t count{}; };
struct MemoryBucket {
    Token token;
    std::vector<Entry> entries;
    std::vector<IndexedEntry> indexed;
    void build_index() {
        indexed.clear();
        indexed.reserve(entries.size());
        for (const auto& entry : entries) indexed.push_back({&entry, parse(entry.keys)});
    }
};

bool read_u32(std::istream& stream, std::uint32_t& value) {
    stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(stream);
}
bool read_u64(std::istream& stream, std::uint64_t& value) {
    stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(stream);
}
bool read_double(std::istream& stream, double& value) {
    stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(stream);
}

class ProgressiveDictionary {
public:
    bool available() {
        initialise();
        return available_;
    }
    const std::string& status() {
        initialise();
        return status_;
    }
    std::vector<const MemoryBucket*> common_matching(const std::vector<Token>& input, const Settings& settings) {
        initialise();
        std::vector<const MemoryBucket*> result;
        for (const auto& [keys, bucket] : common_buckets_)
            if (std::any_of(input.begin(), input.end(), [&](const auto& token) {
                    return match_token(token, bucket.token, settings).valid;
                })) result.push_back(&bucket);
        return result;
    }
    std::vector<const MemoryBucket*> matching(const std::vector<Token>& input, const Settings& settings) {
        initialise();
        std::vector<const MemoryBucket*> result;
        if (!available_) return result;
        for (const auto& bucket : buckets_) {
            // Load only reading families that pass the exact/prefix or
            // one-symbol correction rules, including nonresident words.
            if (!std::any_of(input.begin(), input.end(), [&](const auto& token) {
                    const bool same_tone = matches_dictionary_tone(token, bucket.token);
                    return same_tone && (match_token(token, bucket.token, settings).valid || token.symbols == bucket.token.symbols ||
                        (settings.chaining && token.tone == 0 && !token.symbols.empty() &&
                         bucket.token.symbols.starts_with(token.symbols)));
                })) continue;
            const auto [found, inserted] = cached_.try_emplace(bucket.keys);
            if (inserted) {
                found->second.token = bucket.token;
                found->second.entries = read_entries(bucket.offset, bucket.count);
                found->second.build_index();
            }
            result.push_back(&found->second);
        }
        return result;
    }
    std::vector<Entry> all_entries() {
        initialise();
        std::vector<Entry> result;
        if (!available_) return result;
        for (const auto& [key, bucket] : common_buckets_)
            result.insert(result.end(), bucket.entries.begin(), bucket.entries.end());
        for (const auto& bucket : buckets_) {
            const auto [found, inserted] = cached_.try_emplace(bucket.keys);
            if (inserted) {
                found->second.token = bucket.token;
                found->second.entries = read_entries(bucket.offset, bucket.count);
                found->second.build_index();
            }
            result.insert(result.end(), found->second.entries.begin(), found->second.entries.end());
        }
        return result;
    }
private:
    std::vector<Entry> read_entries(std::uint64_t offset, std::uint32_t count) const {
        std::ifstream source(dictionary_index_path(), std::ios::binary);
        source.seekg(static_cast<std::streamoff>(offset));
        std::vector<Entry> result;
        result.reserve(count);
        for (std::uint32_t row = 0; source && row < count; ++row) {
            std::uint32_t text_size{}, keys_size{}, explanation_size{};
            double frequency{};
            if (!read_u32(source, text_size) || !read_u32(source, keys_size) ||
                !read_u32(source, explanation_size) || !read_double(source, frequency) ||
                text_size > 4096 || keys_size > kMaximumKeys || explanation_size > (1u << 20) ||
                !std::isfinite(frequency) || frequency <= 0) return {};
            std::string text(text_size, '\0'), keys(keys_size, '\0'), explanation(explanation_size, '\0');
            source.read(text.data(), text.size());
            source.read(keys.data(), keys.size());
            source.read(explanation.data(), explanation.size());
            if (!source) return {};
            Entry entry{from_utf8(text), std::move(keys), frequency, from_utf8(explanation)};
            if (!entry.text.empty() && parse(entry.keys).size() == entry.text.size() && !romanize(entry.keys).empty())
                result.push_back(std::move(entry));
        }
        return result;
    }
    void initialise() {
        if (initialised_) return;
        initialised_ = true;
        std::ifstream source(dictionary_index_path(), std::ios::binary);
        std::array<char, 8> magic{};
        std::uint32_t version{}, bucket_count{}, common_count{}, total_count{};
        source.read(magic.data(), magic.size());
        const std::array<char, 8> expected{'T','B','I','D','X','\0','2','\0'};
        if (!source) { status_ = "index unreadable"; return; }
        if (magic != expected) { status_ = "index magic mismatch"; return; }
        if (!read_u32(source, version) || version != 2) { status_ = "index schema mismatch"; return; }
        if (!read_u32(source, bucket_count) || !read_u32(source, common_count) || !read_u32(source, total_count) ||
            bucket_count > 4096 || common_count > total_count || total_count > 10000000) { status_ = "index limits invalid"; return; }
        for (std::uint32_t row = 0; row < bucket_count; ++row) {
            std::uint16_t size{};
            std::uint64_t offset{};
            std::uint32_t count{};
            source.read(reinterpret_cast<char*>(&size), sizeof(size));
            if (!source || size == 0 || size > kMaximumKeys || !read_u64(source, offset) || !read_u32(source, count) || count == 0) { status_ = "invalid bucket table"; return; }
            std::string keys(size, '\0');
            source.read(keys.data(), keys.size());
            const auto tokens = parse(keys);
            if (!source || tokens.size() != 1) { status_ = "invalid bucket keys"; return; }
            buckets_.push_back({std::move(keys), tokens.front(), offset, count});
        }
        const auto common_offset = static_cast<std::uint64_t>(source.tellg());
        common_ = read_entries(common_offset, common_count);
        // The build artifact preserves every reviewed source row. Some rows
        // are deliberately ignored by this decoder when their key sequence
        // cannot be represented by its one-syllable-per-character model.
        // That is not index corruption and must not trigger a full TSV load.
        if (common_.empty() && common_count != 0) { status_ = "common records rejected"; return; }
        for (const auto& entry : common_) {
            const auto tokens = parse(entry.keys);
            if (tokens.empty()) continue;
            const auto key = entry.keys.substr(0, tokens.front().source_end);
            auto [position, inserted] = common_buckets_.try_emplace(key);
            if (inserted) position->second.token = tokens.front();
            position->second.entries.push_back(entry);
        }
        for (auto& [key, bucket] : common_buckets_) bucket.build_index();
        common_.clear();
        available_ = true;
        status_ = "available";
    }
    bool initialised_{};
    bool available_{};
    std::string status_{"not initialised"};
    std::vector<DiskBucket> buckets_;
    std::vector<Entry> common_;
    std::map<std::string, MemoryBucket> common_buckets_;
    std::map<std::string, MemoryBucket> cached_;
};

ProgressiveDictionary& progressive_dictionary() {
    static ProgressiveDictionary dictionary;
    return dictionary;
}
#endif

struct ProgressiveBucketView {
    std::vector<Entry> fallback_entries;
    std::vector<IndexedEntry> fallback_indexed;
    std::vector<ReadingBucket> buckets;
};

ProgressiveBucketView progressive_reading_buckets(const std::vector<Token>& input, const Settings& settings) {
    ProgressiveBucketView result;
#if IME_ENABLE_DICTIONARY
    auto& dictionary = progressive_dictionary();
    if (dictionary.available()) {
        std::set<std::pair<std::u32string, std::string>> seen;
        const auto append = [&](const MemoryBucket& source, bool resident) {
            ReadingBucket bucket{source.token, {}};
            for (const auto& indexed : source.indexed) {
                if (indexed.entry && (resident ? seen.emplace(indexed.entry->text, indexed.entry->keys).second :
                    !seen.contains({indexed.entry->text, indexed.entry->keys})))
                    bucket.entries.push_back(&indexed);
            }
            if (!bucket.entries.empty()) result.buckets.push_back(std::move(bucket));
        };
        for (const auto* bucket : dictionary.common_matching(input, settings)) append(*bucket, true);
        for (const auto* bucket : dictionary.matching(input, settings)) append(*bucket, false);
    } else
#endif
    {
        result.fallback_entries = lexicon();
        result.fallback_indexed.reserve(result.fallback_entries.size());
        for (const auto& entry : result.fallback_entries) result.fallback_indexed.push_back({&entry, parse(entry.keys)});
        std::map<std::pair<std::u32string, int>, std::size_t> positions;
        for (const auto& indexed : result.fallback_indexed) {
            if (indexed.tokens.empty()) continue;
            const auto& first = indexed.tokens.front();
            const auto [position, inserted] = positions.emplace(std::pair{first.symbols, first.tone}, result.buckets.size());
            if (inserted) result.buckets.push_back({first, {}});
            result.buckets[position->second].entries.push_back(&indexed);
        }
    }
    return result;
}

double context_bonus(std::u32string_view preceding, std::u32string_view text) {
    constexpr std::array<std::pair<std::u32string_view, std::u32string_view>, 12> pairs{{
        {U"我", U"們"}, {U"你", U"好"}, {U"歡迎", U"光臨"}, {U"謝謝", U"你"},
        {U"今天", U"天氣"}, {U"天氣", U"很好"}, {U"臺灣", U"高鐵"}, {U"台灣", U"高鐵"},
        {U"美麗", U"的"}, {U"跑", U"得"}, {U"慢慢", U"地"}, {U"我想", U"要"}
    }};
    for (const auto& [prefix, suffix] : pairs)
        if (preceding.ends_with(prefix) && text.starts_with(suffix)) return 1.8;
    return 0;
}

struct Edge { const Entry* entry{}; std::size_t begin{}; std::size_t end{}; Match match; };

void shortlist_edges(std::vector<Edge>& edges, std::size_t width) {
    // Bound homophone expansion before multiplying it by every beam path.
    // All edges ending at the same token receive the same length score.
    std::stable_sort(edges.begin(), edges.end(), [](const Edge& left, const Edge& right) {
        if (left.end != right.end) return left.end < right.end;
        if (left.match.fuzzy != right.match.fuzzy) return left.match.fuzzy < right.match.fuzzy;
        if (left.match.abbreviated != right.match.abbreviated) return left.match.abbreviated < right.match.abbreviated;
        const auto left_score = std::log1p(left.entry->frequency) - left.match.penalty;
        const auto right_score = std::log1p(right.entry->frequency) - right.match.penalty;
        return left_score != right_score ? left_score > right_score : left.entry->text < right.entry->text;
    });
    std::vector<Edge> result;
    std::size_t end{};
    std::size_t count{};
    bool kept_correction{};
    // Preserve every edge eligible for the decoder's contextual bonus, even
    // outside the frequency shortlist, so context can still promote it.
    constexpr std::array<std::u32string_view, 12> prefixes{
        U"我", U"你", U"歡迎", U"謝謝", U"今天", U"天氣", U"臺灣", U"台灣", U"美麗", U"跑", U"慢慢", U"我想"};
    for (const auto& edge : edges) {
        if (edge.end != end) { end = edge.end; count = 0; kept_correction = false; }
        const bool contextual = std::any_of(prefixes.begin(), prefixes.end(), [&](auto prefix) {
            return context_bonus(prefix, edge.entry->text) != 0;
        });
        if (count < width || contextual || (edge.match.fuzzy != 0 && !kept_correction)) {
            result.push_back(edge);
            ++count;
            kept_correction = kept_correction || edge.match.fuzzy != 0;
        }
    }
    edges = std::move(result);
}

struct Path {
    std::u32string text;
    std::vector<Edge> edges;
    double score{};
    int fuzzy{};
    int abbreviated{};
};

bool better_path(const Path& left, const Path& right) {
    // Match quality is a hard tier; popularity cannot promote a typo over exact input.
    if (left.fuzzy != right.fuzzy) return left.fuzzy < right.fuzzy;
    if (left.abbreviated != right.abbreviated) return left.abbreviated < right.abbreviated;
    if (left.score != right.score) return left.score > right.score;
    return left.text < right.text;
}

void trim_beam(std::vector<Path>& paths, std::size_t width) {
    std::stable_sort(paths.begin(), paths.end(), better_path);
    if (paths.size() <= width) return;
    std::optional<Path> correction;
    const auto found = std::find_if(paths.begin(), paths.end(), [](const Path& path) { return path.fuzzy != 0; });
    if (found != paths.end()) correction = *found;
    paths.resize(width);
    if (correction && width >= 2 && std::none_of(paths.begin(), paths.end(), [&](const Path& path) {
            return path.text == correction->text;
        })) paths.back() = std::move(*correction);
}

Candidate make_candidate(const Path& path, std::string_view raw, bool explanations) {
    Candidate candidate;
    candidate.kind = path.fuzzy ? CandidateKind::Correction : CandidateKind::Chinese;
    candidate.text = path.text;
    candidate.provider = "bopomofo";
    candidate.confidence = path.fuzzy ? 0.65 : (path.abbreviated ? 0.8 : 1.0);
    candidate.score = std::clamp(path.score - 50.0 * path.fuzzy - 5.0 * path.abbreviated, -90.0, 90.0);
    candidate.source_end = raw.size();
    std::string corrected_keys;
    for (const auto& edge : path.edges) {
        corrected_keys += edge.entry->keys;
        if (!candidate.reading.empty()) candidate.reading += U' ';
        candidate.reading += display_reading(edge.entry->keys);
        if (explanations && !edge.entry->explanation.empty()) {
            if (!candidate.explanation.empty()) candidate.explanation += U"；";
            candidate.explanation += edge.entry->explanation;
        }
    }
    if(path.fuzzy) {
        const auto typed=parse(raw), corrected=parse(corrected_keys);
        for(std::size_t i=0;i<std::min(typed.size(),corrected.size());++i)
            for(std::size_t j=0;j<std::min(typed[i].symbols.size(),corrected[i].symbols.size());++j)
                if(typed[i].symbols[j]!=corrected[i].symbols[j] &&
                   candidate.correction_label.find(corrected[i].symbols[j])==std::u32string::npos)
                    candidate.correction_label+=corrected[i].symbols[j];
    }
    if (explanations && path.fuzzy) {
        if (!candidate.explanation.empty()) candidate.explanation += U"；";
        candidate.explanation += U"相近注音或鄰近按鍵修正";
    }
    return candidate;
}
}

bool lazy_dictionary_available() {
#if IME_ENABLE_DICTIONARY
    return progressive_dictionary().available();
#else
    return false;
#endif
}

std::string lazy_dictionary_status() {
#if IME_ENABLE_DICTIONARY
    return progressive_dictionary().status();
#else
    return "disabled at build time";
#endif
}

char32_t map_key(char key) {
    if (key == ' ') return U' ';
    if (key == '6') return U'ˊ';
    if (key == '3') return U'ˇ';
    if (key == '4') return U'ˋ';
    if (key == '7') return U'˙';
    const auto index = kKeys.find(key);
    return index == std::string_view::npos ? U'\0' : kSymbols[index];
}

std::vector<Token> parse(std::string_view keys) {
    if (keys.empty() || keys.size() > kMaximumKeys) return {};
    std::vector<Token> result;
    Token token;
    int last_category = 0;
    auto finish = [&] {
        if (token.symbols.empty()) return;
        token.abbreviated = token.tone == 0 && token.symbols.size() == 1 && category(token.symbols.front()) == 1;
        result.push_back(std::move(token));
        token = {};
        last_category = 0;
    };
    for (std::size_t index = 0; index < keys.size(); ++index) {
        const auto tone = tone_of(keys[index]);
        if (tone != 0) {
            if (token.symbols.empty()) return {};
            token.tone = tone;
            token.source_end = index + 1;
            finish();
        } else {
            const auto symbol = map_key(keys[index]);
            const auto group = category(symbol);
            if (group == 0) return {};
            if (group <= last_category) finish();
            if (token.symbols.empty()) token.source_begin = index;
            token.symbols += symbol;
            token.source_end = index + 1;
            last_category = group;
        }
        if (result.size() > kMaximumTokens) return {};
    }
    finish();
    return result.size() > kMaximumTokens ? std::vector<Token>{} : result;
}

bool can_mark_first_tone(std::string_view keys) {
    if (keys.empty() || keys.size() >= kMaximumKeys) return false;
    std::string marked(keys);
    marked += ' ';
    const auto tokens = parse(marked);
    return !tokens.empty() && tokens.back().tone == 1 && tokens.back().source_end == marked.size();
}

std::u32string display_reading(std::string_view keys) {
    const auto tokens = parse(keys);
    std::u32string result;
    for (const auto& token : tokens) {
        if (!result.empty()) result += U' ';
        if (token.tone == 5) result += U'˙';
        result += token.symbols;
        if (token.tone > 1 && token.tone < 5) result += map_key(tone_key(token.tone));
    }
    return result;
}

std::u32string romanize(std::string_view keys) {
    static const std::map<char32_t, std::string_view> initials{
        {U'ㄅ', "b"}, {U'ㄆ', "p"}, {U'ㄇ', "m"}, {U'ㄈ', "f"}, {U'ㄉ', "d"},
        {U'ㄊ', "t"}, {U'ㄋ', "n"}, {U'ㄌ', "l"}, {U'ㄍ', "g"}, {U'ㄎ', "k"},
        {U'ㄏ', "h"}, {U'ㄐ', "j"}, {U'ㄑ', "q"}, {U'ㄒ', "x"}, {U'ㄓ', "zh"},
        {U'ㄔ', "ch"}, {U'ㄕ', "sh"}, {U'ㄖ', "r"}, {U'ㄗ', "z"}, {U'ㄘ', "c"}, {U'ㄙ', "s"}
    };
    static const std::map<std::u32string_view, std::string_view> finals{
        {U"ㄚ", "a"}, {U"ㄛ", "o"}, {U"ㄜ", "e"}, {U"ㄝ", "e"}, {U"ㄞ", "ai"},
        {U"ㄟ", "ei"}, {U"ㄠ", "ao"}, {U"ㄡ", "ou"}, {U"ㄢ", "an"}, {U"ㄣ", "en"},
        {U"ㄤ", "ang"}, {U"ㄥ", "eng"}, {U"ㄦ", "er"}, {U"ㄧ", "i"}, {U"ㄧㄚ", "ia"},
        {U"ㄧㄛ", "io"}, {U"ㄧㄝ", "ie"}, {U"ㄧㄠ", "iao"}, {U"ㄧㄡ", "iu"},
        {U"ㄧㄢ", "ian"}, {U"ㄧㄣ", "in"}, {U"ㄧㄤ", "iang"}, {U"ㄧㄥ", "ing"},
        {U"ㄨ", "u"}, {U"ㄨㄚ", "ua"}, {U"ㄨㄛ", "uo"}, {U"ㄨㄞ", "uai"},
        {U"ㄨㄟ", "ui"}, {U"ㄨㄢ", "uan"}, {U"ㄨㄣ", "un"}, {U"ㄨㄤ", "uang"},
        {U"ㄨㄥ", "ong"}, {U"ㄩ", "v"}, {U"ㄩㄝ", "ve"}, {U"ㄩㄢ", "van"},
        {U"ㄩㄣ", "vn"}, {U"ㄩㄥ", "iong"}
    };
    std::u32string result;
    for (const auto& token : parse(keys)) {
        std::u32string_view remainder(token.symbols);
        std::string value;
        if (const auto found = initials.find(remainder.front()); found != initials.end()) {
            value = found->second;
            remainder.remove_prefix(1);
        }
        if (remainder.empty()) {
            if (value == "zh" || value == "ch" || value == "sh" || value == "r" ||
                value == "z" || value == "c" || value == "s") value += 'i';
        } else if (const auto found = finals.find(remainder); found != finals.end()) {
            std::string final(found->second);
            if (value.empty()) {
                static const std::map<std::string, std::string> zero{
                    {"i", "yi"}, {"ia", "ya"}, {"io", "yo"}, {"ie", "ye"}, {"iao", "yao"},
                    {"iu", "you"}, {"ian", "yan"}, {"in", "yin"}, {"iang", "yang"},
                    {"ing", "ying"}, {"iong", "yong"}, {"u", "wu"}, {"ua", "wa"},
                    {"uo", "wo"}, {"uai", "wai"}, {"ui", "wei"}, {"uan", "wan"},
                    {"un", "wen"}, {"uang", "wang"}, {"ong", "weng"}, {"v", "yu"},
                    {"ve", "yue"}, {"van", "yuan"}, {"vn", "yun"}
                };
                if (const auto spelling = zero.find(final); spelling != zero.end()) final = spelling->second;
            } else if ((value == "j" || value == "q" || value == "x") && final.starts_with('v')) {
                final[0] = 'u';
            }
            value += final;
        } else {
            return {};
        }
        if (token.tone != 0) value += static_cast<char>('0' + token.tone);
        if (!result.empty()) result += U' ';
        for (const unsigned char letter : value) result += static_cast<char32_t>(letter);
    }
    return result;
}

const std::vector<Entry>& lexicon() {
    static const std::vector<Entry> entries = [] {
        std::vector<Entry> result;
#if IME_ENABLE_DICTIONARY
        // This diagnostic/reverse-lookup API is intentionally the only path
        // that materialises the full canonical index.
        result = progressive_dictionary().all_entries();
#endif
        std::vector<Entry> unique_entries;
        std::set<std::pair<std::u32string, std::string>> seen;
        for (auto& entry : result) {
            if (seen.emplace(entry.text, entry.keys).second) unique_entries.push_back(std::move(entry));
        }
        result = std::move(unique_entries);
        const auto phrase_count = result.size();
        for (std::size_t index = 0; index < phrase_count; ++index) {
            const auto entry = result[index];
            const auto tokens = parse(entry.keys);
            if (tokens.size() != entry.text.size())
                throw std::logic_error("Lexicon text/reading length mismatch: " + to_utf8(entry.text) + " / " + entry.keys +
                                       " (" + std::to_string(tokens.size()) + "/" + std::to_string(entry.text.size()) + ")");
            for (std::size_t token = 0; token < tokens.size(); ++token) {
                const auto& reading = tokens[token];
                std::u32string text(1, entry.text[token]);
                auto keys = entry.keys.substr(reading.source_begin, reading.source_end - reading.source_begin);
                if (seen.emplace(text, keys).second)
                    result.push_back({std::move(text), std::move(keys), std::max(1.0, entry.frequency * 0.02), {}});
            }
        }
        return result;
    }();
    return entries;
}

std::vector<Entry> same_sound_characters(std::string_view keys) {
    const auto input = parse(keys);
    if (input.size() != 1) return {};
    const Settings settings{};
    const auto view = progressive_reading_buckets(input, settings);
    std::vector<Entry> result;
    for (const auto& bucket : view.buckets) {
        if (bucket.first.symbols != input.front().symbols) continue;
        if (!matches_dictionary_tone(input.front(), bucket.first)) continue;
        for (const auto* indexed : bucket.entries) {
            if (indexed->entry->text.size() == 1) result.push_back(*indexed->entry);
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const Entry& left, const Entry& right) {
        if (left.frequency != right.frequency) return left.frequency > right.frequency;
        return left.text < right.text;
    });
    return result;
}

DecodeResult decode(const InputContext& context) {
    DecodeResult result;
    result.tokens = parse(context.raw_keys);
    if (result.tokens.empty()) return result;
    const auto token_count = result.tokens.size();
    const auto beam_width = std::clamp(context.settings.beam_width, std::size_t{1}, std::size_t{64});
    const auto candidate_count = std::clamp(context.settings.candidate_count, std::size_t{1}, std::size_t{50});
    const auto dictionary = progressive_reading_buckets(result.tokens, context.settings);
    std::vector<std::vector<Edge>> lattice(token_count);
    for (std::size_t begin = 0; begin < token_count; ++begin) {
        for (const auto& bucket : dictionary.buckets) {
            const auto first_match = match_token(result.tokens[begin], bucket.first, context.settings);
            if (!first_match.valid) continue;
            for (const auto* indexed_pointer : bucket.entries) {
                const auto& indexed = *indexed_pointer;
                if (indexed.tokens.empty() || begin + indexed.tokens.size() > token_count) continue;
                Match match = first_match;
                for (std::size_t offset = 1; offset < indexed.tokens.size(); ++offset) {
                    const auto next = match_token(result.tokens[begin + offset], indexed.tokens[offset], context.settings);
                    if (!next.valid) { match.valid = false; break; }
                    match.penalty += next.penalty;
                    match.fuzzy += next.fuzzy;
                    match.abbreviated += next.abbreviated;
                    if (match.fuzzy > 1) { match.valid = false; break; }
                }
                if (match.valid) lattice[begin].push_back({indexed.entry, begin, begin + indexed.tokens.size(), match});
            }
        }
        shortlist_edges(lattice[begin], beam_width * 2);
    }
    std::vector<std::vector<Path>> beams(token_count + 1);
    beams.front().push_back({});
    for (std::size_t begin = 0; begin < token_count; ++begin) {
        auto& beam = beams[begin];
        trim_beam(beam, beam_width);
        for (const auto& path : beam) {
            for (const auto& edge : lattice[begin]) {
                if (path.fuzzy + edge.match.fuzzy > 1) continue;
                auto next = path;
                const auto preceding = context.sensitive ? path.text : context.preceding_text + path.text;
                const auto length = static_cast<double>(edge.end - edge.begin);
                next.score += std::log1p(edge.entry->frequency) + 2.0 * length - 12.0 - edge.match.penalty +
                              context_bonus(preceding, edge.entry->text);
                next.fuzzy += edge.match.fuzzy;
                next.abbreviated += edge.match.abbreviated;
                next.text += edge.entry->text;
                next.edges.push_back(edge);
                auto& destination = beams[edge.end];
                destination.push_back(std::move(next));
                if (destination.size() > beam_width * 4) {
                    trim_beam(destination, beam_width);
                }
            }
        }
    }
    auto& complete = beams.back();
    std::stable_sort(complete.begin(), complete.end(), better_path);
    std::set<std::u32string> texts;
    std::vector<const Path*> chosen_paths;
    for (const auto& path : complete) {
        if (path.fuzzy != 0 || !texts.insert(path.text).second) continue;
        chosen_paths.push_back(&path);
        if (chosen_paths.size() >= candidate_count) break;
    }
    // Keep one correction visible when a large dictionary produces more exact
    // paths than the display limit. This preserves typo recovery without
    // allowing fuzzy paths to outrank exact paths.
    if (candidate_count >= 2 || chosen_paths.empty()) {
        const auto correction = std::find_if(complete.begin(), complete.end(), [&](const Path& path) {
            return path.fuzzy != 0 && texts.find(path.text) == texts.end();
        });
        if (correction != complete.end()) {
            if (chosen_paths.size() >= candidate_count) {
                texts.erase(chosen_paths.back()->text);
                chosen_paths.pop_back();
            }
            texts.insert(correction->text);
            chosen_paths.push_back(&*correction);
        }
    }
    for (const auto* path : chosen_paths) {
        auto candidate = make_candidate(*path, context.raw_keys, context.settings.explanations);
        candidate.id = result.candidates.size() + 1;
        candidate.score = 80.0 - static_cast<double>(result.candidates.size());
        result.candidates.push_back(std::move(candidate));
        if (result.candidates.size() >= candidate_count) break;
    }
    if (complete.empty()) return result;
    for (const auto& chosen : complete.front().edges) {
        Segment segment;
        segment.source_begin = result.tokens[chosen.begin].source_begin;
        segment.source_end = result.tokens[chosen.end - 1].source_end;
        segment.raw_keys = context.raw_keys.substr(segment.source_begin, segment.source_end - segment.source_begin);
        segment.reading = display_reading(chosen.entry->keys);
        segment.surface = chosen.entry->text;
        std::vector<Path> alternatives;
        for (const auto& edge : lattice[chosen.begin]) {
            if (edge.end != chosen.end) continue;
            Path path;
            path.text = edge.entry->text;
            path.edges.push_back(edge);
            path.score = std::log1p(edge.entry->frequency) - edge.match.penalty;
            path.fuzzy = edge.match.fuzzy;
            path.abbreviated = edge.match.abbreviated;
            alternatives.push_back(std::move(path));
        }
        std::stable_sort(alternatives.begin(), alternatives.end(), better_path);
        std::set<std::u32string> surfaces;
        // Keep the selected path first even when its contextual score differs from local order.
        auto selected = std::find_if(alternatives.begin(), alternatives.end(), [&](const Path& item) {
            return item.text == segment.surface;
        });
        if (selected != alternatives.end()) std::rotate(alternatives.begin(), selected, selected + 1);
        for (const auto& path : alternatives) {
            if (!surfaces.insert(path.text).second) continue;
            auto candidate = make_candidate(path, segment.raw_keys, context.settings.explanations);
            candidate.id = segment.candidates.size() + 1;
            candidate.score = 80.0 - static_cast<double>(segment.candidates.size());
            candidate.source_begin = segment.source_begin;
            candidate.source_end = segment.source_end;
            segment.candidates.push_back(std::move(candidate));
            if (segment.candidates.size() >= candidate_count) break;
        }
        result.segments.push_back(std::move(segment));
    }
    return result;
}

std::vector<Candidate> direct_candidates(const InputContext& context, bool prediction) {
    std::vector<Candidate> result;
    const auto input=parse(context.raw_keys);
    if(input.empty()) return result;
    const auto view=progressive_reading_buckets(input,context.settings);
    for(const auto& bucket:view.buckets) {
        const auto first=match_token(input.front(),bucket.first,context.settings);
        if(!first.valid) continue;
        for(const auto* indexed:bucket.entries) {
            if(indexed->tokens.empty() || (!prediction && indexed->tokens.size()>input.size()) ||
               (prediction && indexed->tokens.size()<input.size())) continue;
            Match match=first;
            for(std::size_t offset=1;offset<std::min(indexed->tokens.size(),input.size());++offset) {
                const auto next=match_token(input[offset],indexed->tokens[offset],context.settings);
                if(!next.valid) {match.valid=false;break;}
                match.penalty+=next.penalty;match.fuzzy+=next.fuzzy;match.abbreviated+=next.abbreviated;
            }
            if(!match.valid) continue;
            Path path;path.text=indexed->entry->text;path.edges.push_back({indexed->entry,0,indexed->tokens.size(),match});
            path.score=std::log1p(indexed->entry->frequency)-match.penalty;path.fuzzy=match.fuzzy;path.abbreviated=match.abbreviated;
            auto candidate=make_candidate(path,context.raw_keys,context.settings.explanations);
            candidate.source_end=input[std::min(indexed->tokens.size(),input.size())-1].source_end;
            result.push_back(std::move(candidate));
        }
    }
    std::stable_sort(result.begin(),result.end(),[](const Candidate& a,const Candidate& b) {return a.score>b.score;});
    result.erase(std::unique(result.begin(),result.end(),[](const Candidate& a,const Candidate& b) {return a.text==b.text;}),result.end());
    return result;
}

std::optional<std::string> reverse_reading(std::u32string_view text) {
    if (text.empty() || text.size() > kMaximumTokens) return std::nullopt;
    struct Reverse { double score{-std::numeric_limits<double>::infinity()}; std::string keys; };
    std::vector<Reverse> paths(text.size() + 1);
    static const auto by_first_character = [] {
        std::map<char32_t, std::vector<const Entry*>> index;
        for (const auto& entry : lexicon()) index[entry.text.front()].push_back(&entry);
        return index;
    }();
    paths.front().score = 0;
    for (std::size_t begin = 0; begin < text.size(); ++begin) {
        if (!std::isfinite(paths[begin].score)) continue;
        const auto bucket = by_first_character.find(text[begin]);
        if (bucket == by_first_character.end()) continue;
        for (const auto* entry_pointer : bucket->second) {
            const auto& entry = *entry_pointer;
            if (!text.substr(begin).starts_with(entry.text)) continue;
            const auto end = begin + entry.text.size();
            const auto score = paths[begin].score + std::log1p(entry.frequency) - 12;
            if (score > paths[end].score) paths[end] = {score, paths[begin].keys + entry.keys};
        }
    }
    if (!std::isfinite(paths.back().score)) return std::nullopt;
    return paths.back().keys;
}
}
