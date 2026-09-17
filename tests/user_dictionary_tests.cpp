#include "ime/engine.h"
#include "test.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

void run_user_dictionary_tests() {
    const auto path=(std::filesystem::current_path()/"test-user-phrases.tsv").string();
    std::filesystem::remove(path);
    ime::Engine engine;
    CHECK(engine.set_user_dictionary_path(path));
    CHECK(engine.register_user_phrase("su3cl3",U"妳好"));
    CHECK(!engine.register_user_phrase(" ",U"空"));
    CHECK(!engine.register_user_phrase("su3",std::u32string(1,0xd800)));
    engine.set_input("su3cl3");
    CHECK(engine.state().candidates.front().text==U"妳好");
    CHECK(engine.state().candidates.front().provider=="user-dictionary");
    engine.convert();CHECK(engine.commit()==U"妳好");
    CHECK(engine.user_dictionary().phrases().front().selections==0);
    auto settings=engine.settings();settings.learning=true;engine.set_settings(settings);
    engine.set_input("su3cl3");engine.convert();CHECK(engine.commit()==U"妳好");
    CHECK(engine.user_dictionary().phrases().front().selections==1);
    CHECK(engine.undo_user_learning());
    CHECK(engine.user_dictionary().phrases().front().selections==0);
    engine.set_input("su3cl3");engine.convert();CHECK(engine.commit(false)==U"妳好");
    CHECK(engine.user_dictionary().phrases().front().selections==0);
    CHECK(engine.learn_user_phrase("su3cl3",U"妳好"));
    ime::Engine reopened;
    CHECK(reopened.set_user_dictionary_path(path));
    CHECK(reopened.user_dictionary().phrases().front().selections==1);
    reopened.set_context({},true);reopened.set_input("su3cl3");
    CHECK(std::none_of(reopened.state().candidates.begin(),reopened.state().candidates.end(),[](const auto& candidate){return candidate.provider=="user-dictionary";}));
    CHECK(!reopened.register_user_phrase("su3",U"妳"));
    CHECK(!reopened.learn_user_phrase("su3cl3",U"妳好"));
    CHECK(!reopened.remove_user_phrase("su3cl3",U"妳好"));
    reopened.set_context({},false);
    CHECK(engine.register_user_phrase("su3",U"妳"));
    CHECK(reopened.reload_user_dictionary());
    reopened.set_input("su3");CHECK(reopened.state().candidates.front().text==U"妳");
    CHECK(reopened.remove_user_phrase("su3",U"妳"));
    CHECK(reopened.reload_user_dictionary());CHECK(reopened.user_dictionary().phrases().size()==1);
    // A malformed disk file must neither partially load nor be overwritten by an edit.
    {std::ofstream file(path);file<<"tcpp-user-dictionary-v1\nsu3\t"<<ime::to_utf8(U"妳")<<"\t1\nsu3\t"<<ime::to_utf8(U"妳")<<"\t2\n";}
    CHECK(!reopened.reload_user_dictionary());CHECK(reopened.user_dictionary().phrases().size()==1);
    CHECK(!reopened.register_user_phrase("cl3",U"好"));
    std::filesystem::remove(path);
    CHECK(reopened.reload_user_dictionary());CHECK(reopened.user_dictionary().phrases().empty());
    // Force atomic-replace failure with an existing nonempty directory at the target.
    const auto blocked=std::filesystem::current_path()/"test-user-phrases-blocked";
    std::filesystem::create_directories(blocked);
    {std::ofstream file(blocked/"marker");file<<"preserve";}
    ime::UserDictionary dictionary;CHECK(dictionary.add("su3",U"妳"));CHECK(!dictionary.save(blocked.string()));
    CHECK(std::filesystem::exists(blocked/"marker"));
    std::filesystem::remove(blocked/"marker");std::filesystem::remove(blocked);
    std::filesystem::remove(blocked.string()+".tmp");
}
