#include "ime/engine.h"
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace ime {
namespace {
bool integer(std::string_view text,int& value) {const auto result=std::from_chars(text.data(),text.data()+text.size(),value);return result.ec==std::errc{} && result.ptr==text.data()+text.size();}
bool valid_domain(std::string_view value) {return !value.empty() && value.size()<254 && value.find('.')!=std::string_view::npos && value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-")==std::string_view::npos;}
}
Settings load_settings(const std::string& path) {
    Settings settings;std::ifstream stream(std::filesystem::path(std::u8string(path.begin(),path.end())));if(!stream) return settings;
    std::string line;std::size_t total{};
    while(std::getline(stream,line)) {
        total+=line.size();if(total>65536) return Settings{};
        if(!line.empty() && line.back()=='\r') line.pop_back();
        auto equal=line.find('=');if(equal==std::string::npos) continue;
        auto key=std::string_view(line).substr(0,equal);auto value=std::string_view(line).substr(equal+1);
        int number{};
        if(key=="email_domains") {
            std::vector<std::string> domains;
            while(!value.empty() && domains.size()<32) {auto comma=value.find(',');auto domain=value.substr(0,comma);if(valid_domain(domain)) domains.emplace_back(domain);if(comma==std::string_view::npos) break;value.remove_prefix(comma+1);}
            if(!domains.empty()) settings.email_domains=std::move(domains);
            continue;
        }
        if(!integer(value,number)) continue;
        if(key=="version" && number!=1) return Settings{};
        if(key=="candidate_count" && number>=2 && number<=50) settings.candidate_count=static_cast<std::size_t>(number);
        else if(key=="beam_width" && number>=1 && number<=128) settings.beam_width=static_cast<std::size_t>(number);
        else if(key.size()==7 && key.substr(0,6)=="hotkey" && key[6]>='0' && key[6]<='4' && (number==0 || (number>=0x70 && number<=0x87))) settings.hotkeys[key[6]-'0']=number;
        else if(number==0 || number==1) {
            const bool enabled=number==1;
            if(key=="chaining") settings.chaining=enabled;
            else if(key=="typo_tolerance") settings.typo_tolerance=enabled;
            else if(key=="english_fallback") settings.english_fallback=enabled;
            else if(key=="explanations") settings.explanations=enabled;
            else if(key=="neural_enabled") settings.neural_enabled=enabled;
            else if(key=="smart_paste") settings.smart_paste=enabled;
            else if(key=="internet_enabled") settings.internet_enabled=enabled;
            else if(key=="learning") settings.learning=enabled;
        }
    }
    return settings;
}
bool save_settings(const Settings& settings,const std::string& path) {
    try {
        auto target=std::filesystem::path(std::u8string(path.begin(),path.end()));auto temporary=target;temporary+=".tmp";
        if(target.has_parent_path()) std::filesystem::create_directories(target.parent_path());
        std::ofstream stream(temporary,std::ios::trunc);if(!stream) return false;
        stream<<"version=1\nchaining="<<settings.chaining<<"\ntypo_tolerance="<<settings.typo_tolerance
          <<"\nenglish_fallback="<<settings.english_fallback<<"\nexplanations="<<settings.explanations
          <<"\nneural_enabled="<<settings.neural_enabled<<"\nsmart_paste="<<settings.smart_paste
          <<"\ninternet_enabled="<<settings.internet_enabled<<"\nlearning="<<settings.learning
          <<"\ncandidate_count="<<std::clamp<std::size_t>(settings.candidate_count,2,50)<<"\nbeam_width="<<std::clamp<std::size_t>(settings.beam_width,1,128)<<'\n';
        for(int i=0;i<5;++i) stream<<"hotkey"<<i<<'='<<settings.hotkeys[i]<<'\n';
        stream<<"email_domains=";bool first=true;for(const auto& domain:settings.email_domains) if(valid_domain(domain)) {if(!first) stream<<',';stream<<domain;first=false;}stream<<'\n';
        stream.close();if(!stream) return false;
#ifdef _WIN32
        return MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        std::filesystem::rename(temporary,target);return true;
#endif
    } catch(...) {return false;}
}
}
