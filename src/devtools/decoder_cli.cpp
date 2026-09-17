#include "ime/engine.h"
#include "ime/bopomofo.h"
#include <chrono>
#include <iostream>
#include <algorithm>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
void output(ime::Engine& engine,const std::string& keys) {
    engine.set_input(keys);
    std::cout<<"keys: "<<keys<<"\nreading: "<<ime::to_utf8(engine.state().reading)<<'\n';
    std::size_t index{};for(const auto& candidate:engine.state().candidates) std::cout<<++index<<'\t'<<ime::to_utf8(candidate.text)<<'\t'<<candidate.provider<<'\t'<<candidate.score<<'\t'<<ime::to_utf8(candidate.explanation)<<'\n';
    engine.convert();std::cout<<"segments:";for(const auto& segment:engine.state().segments) std::cout<<" ["<<ime::to_utf8(segment.surface)<<" | "<<segment.raw_keys<<"]";std::cout<<'\n';
}
#ifdef _WIN32
std::string utf8_argument(const wchar_t* value) {
    const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,nullptr,0,nullptr,nullptr);
    if(size<=0) return {};
    std::string result(static_cast<std::size_t>(size),'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,result.data(),size,nullptr,nullptr)) return {};
    result.pop_back();return result;
}
#endif
int run(const std::vector<std::string>& arguments) {
    ime::Engine engine;
    if(arguments.size()>=2 && arguments[1]=="--dictionary-status") {
        std::cout<<"Lazy phonetic index: "<<ime::bopomofo::lazy_dictionary_status()<<'\n';
        std::cout<<"Loaded lexicon entries (including derived characters): "<<ime::bopomofo::lexicon().size()<<'\n';
        return 0;
    }
    if(arguments.size()>=2 && arguments[1]=="--benchmark") {
        const std::vector<std::string> samples{"su3cl3","5j/ jp6","hk4g4","1+1","u+2192","facebook","sucl"};
        std::vector<double> times;for(int i=0;i<500;++i) {auto start=std::chrono::steady_clock::now();engine.set_input(samples[static_cast<std::size_t>(i)%samples.size()]);times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
        std::sort(times.begin(),times.end());std::cout<<"candidate latency ms: p50="<<times[250]<<" p95="<<times[475]<<" p99="<<times[495]<<"\n";return times[475]<20?0:1;
    }
    if(arguments.size()>=3 && arguments[1]=="--lookup") {
        auto text=ime::from_utf8(arguments[2]);for(const auto& entry:ime::bopomofo::lexicon()) if(entry.text.find(text)!=std::u32string::npos) std::cout<<ime::to_utf8(entry.text)<<'\t'<<entry.keys<<'\t'<<entry.frequency<<'\t'<<ime::to_utf8(entry.explanation)<<'\n';return 0;
    }
    if(arguments.size()>=2) {output(engine,arguments[1]);return 0;}
    std::cout<<"Taiwan Bopomofo decoder. Enter keys (su3cl3 = 你好); Ctrl+Z then Enter exits.\n";
    std::string line;while(std::getline(std::cin,line)) output(engine,line);
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> arguments;arguments.reserve(static_cast<std::size_t>(argc));
    for(int i=0;i<argc;++i) arguments.push_back(utf8_argument(argv[i]));
    return run(arguments);
}
#else
int main(int argc,char** argv) {return run(std::vector<std::string>(argv,argv+argc));}
#endif
