#include "test.h"
#include "ime/engine.h"
#include "ime/bopomofo.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
void run_decoder_tests();
void run_provider_tests();
void run_user_dictionary_tests();
void run_qa_tests();
namespace {
void core_tests() {
    CHECK(ime::from_utf8(ime::to_utf8(U"臺灣😀"))==U"臺灣😀");
    CHECK(ime::from_utf8("\xF0\x80\x80\x80").size()==4);
    CHECK(ime::from_utf8("\xED\xA0\x80")[0]==U'\uFFFD');
    CHECK(ime::half_width(ime::full_width(U"Abc 123!"))==U"Abc 123!");
    ime::Engine engine;engine.set_input("su3cl3");
    CHECK(engine.state().raw_keys=="su3cl3");CHECK(!engine.state().candidates.empty());
    CHECK(engine.state().candidates.back().kind==ime::CandidateKind::RawInput);
    engine.convert();CHECK(engine.state().mode==ime::Mode::Converted);CHECK(engine.preedit()==U"你好");
    engine.transform(10);CHECK(engine.preedit()==U"su3cl3");
    engine.escape();CHECK(engine.state().raw_keys=="su3cl3");
    engine.convert();
    const auto raw=std::find_if(engine.state().candidates.begin(),engine.state().candidates.end(),[](const auto& candidate) {
        return candidate.kind==ime::CandidateKind::RawInput && candidate.source_end==6;
    });
    CHECK(raw!=engine.state().candidates.end());engine.select(static_cast<std::size_t>(raw-engine.state().candidates.begin()));CHECK(engine.commit()==U"su3cl3");
    CHECK(engine.state().mode==ime::Mode::Empty);
    engine.set_input("su3cl3");engine.convert();
    CHECK(engine.resize_segment(-1));CHECK(engine.state().segments.size()==2);
    CHECK(engine.state().segments[0].raw_keys+engine.state().segments[1].raw_keys=="su3cl3");
    CHECK(engine.resize_segment(1));CHECK(engine.state().segments.size()==1);
    CHECK(engine.state().raw_keys=="su3cl3");
    CHECK(engine.reconvert(U"你好"));CHECK(engine.preedit()==U"你好");
    auto before=engine.preedit();CHECK(!engine.reconvert(U"🧬未知詞🧬"));CHECK(engine.preedit()==before);
    engine.cancel();
    ime::Settings settings;settings.smart_paste=true;engine.set_settings(settings);engine.paste(U"Ａ exact\r\n😀");CHECK(engine.preedit()==U"Ａ exact\r\n😀");CHECK(engine.commit()==U"Ａ exact\r\n😀");
    engine.set_context({},true);engine.paste(U"secret");CHECK(engine.state().mode==ime::Mode::Empty);
    engine.set_context({},false);
    engine.set_input("facebook");CHECK(engine.state().candidates.front().text==U"Facebook");
    engine.set_input("1+1");CHECK(engine.state().candidates.front().text==U"2");
    auto path=(std::filesystem::current_path()/"test-settings.ini").string();
    settings.candidate_count=7;settings.internet_enabled=false;settings.email_domains={"example.tw"};CHECK(ime::save_settings(settings,path));
    auto loaded=ime::load_settings(path);CHECK(loaded.candidate_count==7);CHECK(loaded.email_domains==settings.email_domains);
    {std::ofstream stream(path);stream<<"version=1\ncandidate_count=-1\ninternet_enabled=garbage\nsmart_paste=2\n";}
    loaded=ime::load_settings(path);CHECK(loaded.candidate_count==9);CHECK(!loaded.internet_enabled);CHECK(!loaded.smart_paste);
    std::filesystem::remove(path);
}
void fuzz_smoke() {
    std::mt19937 random(20260913);
    ime::Engine engine;
    for(int run=0;run<300;++run) {
        std::string keys;for(unsigned i=0,n=random()%24;i<n;++i) keys+=static_cast<char>(32+random()%95);
        engine.set_input(keys);CHECK(engine.state().raw_keys==keys);
        if(!keys.empty()) {CHECK(engine.state().candidates.back().text==ime::from_utf8(keys));engine.convert();engine.cycle(-1);engine.move_segment(1);engine.resize_segment(-1);engine.transform(7);engine.escape();CHECK(engine.state().raw_keys==keys);}
        engine.cancel();
        std::string bytes;for(unsigned i=0,n=random()%64;i<n;++i) bytes+=static_cast<char>(random()%256);
        auto decoded=ime::from_utf8(bytes);CHECK(ime::from_utf8(ime::to_utf8(decoded))==decoded);
    }
}
}
int main() {
    try {run_user_dictionary_tests();std::cout<<"PASS user dictionary persistence and learning\n";}
    catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
    try {run_decoder_tests();std::cout<<"PASS decoder\n";run_provider_tests();std::cout<<"PASS providers\n";core_tests();std::cout<<"PASS composition, settings, UTF-8\n";run_qa_tests();std::cout<<"PASS QA regressions\n";fuzz_smoke();std::cout<<"PASS 300 randomized composition/Unicode scenarios\n";}
    catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
