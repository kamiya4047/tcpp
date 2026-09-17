#include "ime/user_dictionary.h"
#include "ime/bopomofo.h"
#include <algorithm>
#include <charconv>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace ime {
namespace {
bool valid(std::string_view keys,std::u32string_view text) {
    return !keys.empty() && keys.size()<=256 && !text.empty() && text.size()<=64 &&
        keys.find_first_of("\t\r\n")==std::string_view::npos && text.find_first_of(U"\t\r\n\0",0,4)==std::u32string_view::npos &&
        std::all_of(keys.begin(),keys.end(),[](char key){return bopomofo::map_key(key)!=0 || key==' ';}) &&
        std::any_of(keys.begin(),keys.end(),[](char key){return key!=' ';}) &&
        std::all_of(text.begin(),text.end(),[](char32_t c){return c<=0x10ffff && !(c>=0xd800 && c<=0xdfff);});
}
}
bool UserDictionary::add(std::string keys,std::u32string text) {
    if(!valid(keys,text)) return false;
    if(std::any_of(phrases_.begin(),phrases_.end(),[&](const auto& p){return p.keys==keys && p.text==text;})) return true;
    if(phrases_.size()>=10000) return false;
    phrases_.push_back({std::move(keys),std::move(text),0});return true;
}
bool UserDictionary::remove(std::string_view keys,std::u32string_view text) {
    auto size=phrases_.size();std::erase_if(phrases_,[&](const auto& p){return p.keys==keys && p.text==text;});undo_.reset();return phrases_.size()!=size;
}
const std::vector<UserPhrase>& UserDictionary::phrases() const noexcept {return phrases_;}
std::vector<Candidate> UserDictionary::query(std::string_view keys) const {
    std::vector<Candidate> result;
    for(const auto& phrase:phrases_) if(phrase.keys==keys) {
        Candidate candidate;candidate.kind=CandidateKind::Chinese;candidate.text=phrase.text;
        candidate.reading=bopomofo::display_reading(keys);candidate.provider="user-dictionary";candidate.source_end=keys.size();
        candidate.score=95+std::min(4.0,static_cast<double>(phrase.selections)*0.1);result.push_back(std::move(candidate));
    }
    return result;
}
bool UserDictionary::learn(std::string_view keys,std::u32string_view text) {
    auto found=std::find_if(phrases_.begin(),phrases_.end(),[&](const auto& p){return p.keys==keys && p.text==text;});
    if(found==phrases_.end()) return false;
    undo_=*found;found->selections=std::min<std::uint32_t>(100000,found->selections+1);return true;
}
bool UserDictionary::undo_learning() {
    if(!undo_) return false;
    auto found=std::find_if(phrases_.begin(),phrases_.end(),[&](const auto& p){return p.keys==undo_->keys && p.text==undo_->text;});
    if(found==phrases_.end()) {undo_.reset();return false;}*found=*undo_;undo_.reset();return true;
}
bool UserDictionary::load(const std::string& path) {
    std::ifstream stream(std::filesystem::path(std::u8string(path.begin(),path.end())));if(!stream) return false;
    std::string line;if(!std::getline(stream,line) || (line!="tcpp-user-dictionary-v1" && line!="tcpp-user-dictionary-v1\r")) return false;
    UserDictionary parsed;std::size_t bytes{};
    while(std::getline(stream,line)) {
        bytes+=line.size();if(bytes>4*1024*1024 || parsed.phrases_.size()>=10000) return false;
        if(!line.empty() && line.back()=='\r') line.pop_back();
        auto tab=line.find('\t');auto next=line.find('\t',tab==std::string::npos?0:tab+1);if(tab==std::string::npos || next==std::string::npos) return false;
        auto keys=line.substr(0,tab);auto surface=line.substr(tab+1,next-tab-1);auto text=from_utf8(surface);if(to_utf8(text)!=surface) return false;
        std::uint32_t selections{};auto count=std::string_view(line).substr(next+1);auto result=std::from_chars(count.data(),count.data()+count.size(),selections);
        if(result.ec!=std::errc{} || result.ptr!=count.data()+count.size() || selections>100000 ||
           std::any_of(parsed.phrases_.begin(),parsed.phrases_.end(),[&](const auto& p){return p.keys==keys && p.text==text;})) return false;
        if(!parsed.add(keys,text)) return false;
        parsed.phrases_.back().selections=selections;
    }
    if(stream.bad()) return false;
    // Reloads between compositions should preserve an undo when its learned value is unchanged.
    if(undo_) {
        const auto current=std::find_if(phrases_.begin(),phrases_.end(),[&](const auto& p){return p.keys==undo_->keys && p.text==undo_->text;});
        const auto loaded=std::find_if(parsed.phrases_.begin(),parsed.phrases_.end(),[&](const auto& p){return p.keys==undo_->keys && p.text==undo_->text;});
        if(current!=phrases_.end() && loaded!=parsed.phrases_.end() && current->selections==loaded->selections)parsed.undo_=undo_;
    }
    *this=std::move(parsed);return true;
}
bool UserDictionary::save(const std::string& path) const {
    std::filesystem::path temporary;
    try {
        static std::atomic<unsigned long long> serial{};
        auto target=std::filesystem::path(std::u8string(path.begin(),path.end()));temporary=target;
        temporary+=".tmp."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"."+std::to_string(serial.fetch_add(1));
        if(target.has_parent_path()) std::filesystem::create_directories(target.parent_path());
        std::ofstream stream(temporary,std::ios::trunc);if(!stream) return false;
        stream<<"tcpp-user-dictionary-v1\n";for(const auto& p:phrases_) stream<<p.keys<<'\t'<<to_utf8(p.text)<<'\t'<<p.selections<<'\n';stream.close();if(!stream) return false;
#ifdef _WIN32
        if(MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0)return true;
        std::error_code error;std::filesystem::remove(temporary,error);return false;
#else
        std::filesystem::rename(temporary,target);return true;
#endif
    } catch(...) {std::error_code error;if(!temporary.empty())std::filesystem::remove(temporary,error);return false;}
}
}
