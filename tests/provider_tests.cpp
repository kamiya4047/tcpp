#include "ime/providers.h"
#include "test.h"
#include <algorithm>
#include <random>

namespace {
std::vector<ime::Candidate> query(std::string text) {
    ime::InputContext context;
    context.raw_keys = std::move(text);
    context.reference_datetime = "2024-02-28T23:59:59";
    return ime::providers::query(context);
}
std::u32string first(std::string text) {
    const auto results = query(std::move(text));
    CHECK(!results.empty());
    CHECK(results.front().deterministic);
    return results.front().text;
}
}

void run_provider_tests() {
    CHECK(first("=1+2*3") == U"7");
    CHECK(first("1+1") == U"2");
    CHECK(first("(1+2)*3") == U"9");
    CHECK(first("=-(2+3)/2") == U"-2.5");
    CHECK(first("=0.1+0.2") == U"0.3");
    CHECK(first("=9%4") == U"1");
    CHECK(first("=12.5 * 2") == U"25");
    CHECK(first("=-0") == U"0");
    for (const auto bad : {"=1/0", "=1%0", "=1+", "=()", "=1e9", "=nan", "=1;exit()", "=1 2", "=1..2", "=999999999999999999999", "=2(3)", "abc", "1", "2024-02-29"}) CHECK(query(bad).empty());
    CHECK(query("=" + std::string(100, '(') + "1" + std::string(100, ')')).empty());
    CHECK(query("=" + std::string(500, '1')).empty());
    CHECK(query("=1 .2").empty());
    CHECK(first("num:0") == U"零");
    CHECK(first("num:10") == U"十");
    CHECK(first("num:11") == U"十一");
    CHECK(first("num:110") == U"一百一十");
    CHECK(first("num:10001") == U"一萬零一");
    CHECK(first("num:100000001") == U"一億零一");
    CHECK(first("num:10001000") == U"一千萬一千");
    CHECK(first("num:10010001") == U"一千零一萬零一");
    CHECK(first("num:-12.05") == U"負十二點零五");
    CHECK(first("money:123456789") == U"壹億貳仟參佰肆拾伍萬陸仟柒佰捌拾玖");
    CHECK(first("money:10") == U"壹拾");
    CHECK(query("num:1.2.3").empty());
    CHECK(query("num:10000000000000000").empty());
    CHECK(first("ad:1912") == U"民國1年");
    CHECK(first("ad:1911") == U"民國前1年");
    CHECK(first("ad:1") == U"民國前1911年");
    CHECK(first("roc:1") == U"西元1912年");
    CHECK(first("roc:115") == U"西元2026年");
    CHECK(first("roc:before1") == U"西元1911年");
    CHECK(first("roc:前1911") == U"西元1年");
    CHECK(first("roc:8088") == U"西元9999年");
    for (const auto bad : {"roc:0", "roc:-1", "roc:8089", "roc:before1912", "ad:0", "ad:10000", "date:1900-02-29", "date:2024-13-01", "date:2024-04-31", "date:2024-02-00", "date:2024-2-01"}) CHECK(query(bad).empty());
    CHECK(first("date:2000-02-29") == U"2000年2月29日");
    const auto dates = query("date:1911-12-31");
    CHECK(dates.size() == 3);
    CHECK(dates[1].text == U"民國前1年12月31日");
    CHECK(dates[2].text == U"1911/12/31");
    CHECK(first("time:00:00") == U"上午12時0分");
    CHECK(first("time:12:59:01") == U"下午12時59分1秒");
    CHECK(first("time:23:59") == U"下午11時59分");
    CHECK(query("time:24:00").empty());
    CHECK(query("time:12:60").empty());
    CHECK(query("time:12:00:60").empty());
    CHECK(first("今年") == U"2024年");
    CHECK(first("去年") == U"2023年");
    CHECK(first("明年") == U"2025年");
    CHECK(first("明天") == U"2024年2月29日");
    ime::InputContext relative;
    relative.raw_keys = "tomorrow";
    relative.reference_datetime = "2023-12-31T00:00:00";
    CHECK(ime::providers::query(relative).front().text == U"2024年1月1日");
    relative.reference_datetime = "invalid";
    CHECK(ime::providers::query(relative).empty());
    CHECK(first("U+4E00") == U"一");
    CHECK(first("u+1F600") == U"😀");
    CHECK(first("U+10FFFF") == std::u32string(1, 0x10ffff));
    for (const auto bad : {"U+", "U+D800", "U+DFFF", "U+110000", "U+-1", "U+ZZZZ", "U+0000000", "U+0000", "U+001B", "U+007F"}) CHECK(query(bad).empty());
    CHECK(first(":right") == U"→");
    CHECK(first(":smile:") == U"😀");
    CHECK(first(":taiwan:") == U"🇹🇼");
    CHECK(query("右").front().trigger_confidence < 0.5);
    CHECK(query(":右").front().trigger_confidence == 1.0);
    CHECK(query("right").empty());
    CHECK(first("@gm") == U"@gmail.com");
    CHECK(first("alice+test@gm") == U"alice+test@gmail.com");
    CHECK(query("a@@gmail").empty());
    CHECK(query("a b@gm").empty());
    ime::InputContext email;
    email.raw_keys = "@";
    email.settings.email_domains = {"example.tw", "bad domain.tw", "-bad.tw", "bad..tw", "bad-.tw"};
    const auto domains = ime::providers::query(email);
    CHECK(domains.size() == 1 && domains.front().text == U"@example.tw");
    CHECK(first("full:Ab 12!") == U"Ａｂ　１２！");
    CHECK(first("full: A ") == U"　Ａ　");
    CHECK(first("full:a@gmail.com") == U"ａ＠ｇｍａｉｌ．ｃｏｍ");
    CHECK(first("half:Ａｂ　１２！") == U"Ab 12!");
    CHECK(first("type:\"你好,世界!\"") == U"「你好，世界！」");
    CHECK(first("norm:a\r\nb\rc") == U"a\nb\nc");
    const std::u32string original = U"Ａ\u00a0B\r\nC,\"你好!\"";
    const auto paste = ime::providers::smart_paste(original);
    CHECK(paste.size() == 6);
    CHECK(paste[0].text == original && paste[1].text == original);
    CHECK(paste[0].provider == "clipboard-original");
    CHECK(paste[2].text == U"Ａ B\nC,\"你好!\"");
    CHECK(paste[4].text == U"A\u00a0B\r\nC,\"你好!\"");
    CHECK(original == U"Ａ\u00a0B\r\nC,\"你好!\"");
    CHECK(ime::providers::smart_paste(U"").front().text.empty());
    const auto repeated = query("=123*456");
    CHECK(repeated.front().id == query("=123*456").front().id);
    CHECK(repeated.front().id != query("=123*457").front().id);
    ime::InputContext sensitive;
    sensitive.raw_keys = "=1+1";
    sensitive.sensitive = true;
    CHECK(ime::providers::query(sensitive).empty());
    std::mt19937 random{42};
    constexpr std::string_view alphabet = "0123456789+-*/%(). U+abcdef:date";
    for (int trial = 0; trial < 2000; ++trial) {
        std::string input = trial % 2 == 0 ? "=" : "date:";
        const auto length = random() % 128;
        for (unsigned i = 0; i < length; ++i) input += alphabet[random() % alphabet.size()];
        const auto result = query(input);
        for (const auto& candidate : result) {
            CHECK(candidate.deterministic);
            CHECK(candidate.text.size() < 256);
            CHECK(candidate.trigger_confidence >= 0 && candidate.trigger_confidence <= 1);
        }
    }
}
