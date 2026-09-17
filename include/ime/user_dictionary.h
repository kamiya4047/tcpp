#pragma once
#include "ime/types.h"
#include <optional>
namespace ime {
struct UserPhrase {std::string keys;std::u32string text;std::uint32_t selections{};};
// Caller-owned local dictionary; explicit disk writes only. Thread-confined, bounded to 10,000 phrases.
class UserDictionary {
public:
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    bool add(std::string keys,std::u32string text);
    bool remove(std::string_view keys,std::u32string_view text);
    std::vector<Candidate> query(std::string_view keys) const;
    bool learn(std::string_view keys,std::u32string_view text);
    bool undo_learning();
    const std::vector<UserPhrase>& phrases() const noexcept;
private:
    std::vector<UserPhrase> phrases_;
    std::optional<UserPhrase> undo_;
};
}
