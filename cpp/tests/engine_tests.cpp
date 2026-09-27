// Tests for the C++ engine and composer; mirrors Tests/PyawCoreTests. Needs a built model:
//   engine_tests [MODEL_DIR]     (default build/model)
#include <cstdio>
#include <string>
#include <vector>

#include "../pyaw/burmese.hpp"
#include "../pyaw/composer.hpp"
#include "../pyaw/engine.hpp"
#include "../pyaw/romanizer.hpp"
#include "../pyaw/unicode.hpp"

using namespace pyaw;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++checks;                                                                \
        if (!(cond)) { ++failures; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static const Engine* gEngine = nullptr;

static std::string type(Composer& c, const std::string& text) {
    std::string out;
    for (char ch : text) out += c.handle(ch == ' ' ? ComposerKey::of(ComposerKey::Space) : ComposerKey::letter(ch)).commit;
    return out;
}

static Composer makeComposer() { return Composer([] { return gEngine; }); }

static void textTests() {
    CHECK(syllables(u8"မင်္ဂလာပါ") == (std::vector<std::string>{u8"မင်္", u8"ဂ", u8"လာ", u8"ပါ"}));
    CHECK(syllables(u8"ကမ္ဘာ") == (std::vector<std::string>{u8"ကမ္", u8"ဘာ"}));
    CHECK(normalize_burmese(std::string(u8"ကုိ")) == u8"ကို");
    CHECK(normalize_burmese(std::string(u8"၀ယ်")) == u8"ဝယ်");
    CHECK(normalize_burmese(std::string(u8"ကျွန်ုပ်")) == u8"ကျွန်ုပ်");
    auto spellings = [](const std::string& syl) {
        std::vector<std::string> v;
        for (auto& r : romanize(utf8_to_u32(syl))) v.push_back(r.text);
        return v;
    };
    auto has = [&](const std::string& syl, const std::string& s) {
        auto v = spellings(syl);
        return std::find(v.begin(), v.end(), s) != v.end();
    };
    CHECK(has(u8"ကောင်း", "kg") && has(u8"ကောင်း", "kaung"));
    CHECK(has(u8"ပါ", "pr") && has(u8"တယ်", "tl") && has(u8"နေ", "ny") && has(u8"လဲ", "ll"));
    CHECK(!has(u8"အေး", "y"));
}

static void engineTests(const Engine& e) {
    struct Case { const char* in; const char* out; };
    const Case cases[] = {
        {"br lote ny ll", u8"ဘာလုပ်နေလဲ"}, {"min ny kg lr", u8"မင်းနေကောင်းလား"},
        {"kyayzu tin pr tl", u8"ကျေးဇူးတင်ပါတယ်"}, {"chit tl", u8"ချစ်တယ်"}, {"sr p p lr", u8"စားပြီးပြီလား"},
        {"mingalarpar", u8"မင်္ဂလာပါ"}, {"kyay zu tin par tal", u8"ကျေးဇူးတင်ပါတယ်"}, {"kya naw", u8"ကျွန်တော်"},
        {"hma:", u8"မှား"},
    };
    for (auto& c : cases) {
        std::string got = e.decode(c.in).text();
        ++checks;
        if (got != c.out) { ++failures; std::printf("FAIL decode(%s) = %s, want %s\n", c.in, got.c_str(), c.out); }
    }
    CHECK(e.decode("br lote ny ll").outputs == (std::vector<std::string>{u8"ဘာ", u8"လုပ်", u8"နေ", u8"လဲ"}));
    CHECK(e.decode("lo at").text() != u8"လုပ်");
    CHECK(e.decode("br lote ny ll", {}, {{3, u8"လယ်"}}).text() == u8"ဘာလုပ်နေလယ်");
    auto options = e.phraseOptions("ma thwar lar");
    CHECK(!options.empty() && options[0].text == e.decode("ma thwar lar").text());
    bool found = false;
    for (auto& o : options) found |= o.text == u8"မသွားလား";
    CHECK(found);
}

static void composerTests() {
    {
        Composer c = makeComposer();
        CHECK(type(c, "br lote ny ll").empty());
        CHECK(c.preview() == u8"ဘာလုပ်နေလဲ");
        auto out = c.handle(ComposerKey::of(ComposerKey::Enter));
        CHECK(out.commit == u8"ဘာလုပ်နေလဲ" && out.handled);
        CHECK(!c.isComposing());
        CHECK(!c.handle(ComposerKey::of(ComposerKey::Enter)).handled);  // Return goes to the app
    }
    {
        Composer c = makeComposer();
        type(c, "hote kae");
        CHECK(c.handle(ComposerKey::of(ComposerKey::Space)).commit.empty());
        CHECK(c.handle(ComposerKey::of(ComposerKey::Space)).commit == u8"ဟုတ်ကဲ့ ");
    }
    {
        Composer c = makeComposer();
        type(c, "thwar ml");
        CHECK(c.handle(ComposerKey::punctuation('.')).commit == u8"သွားမယ်။");
    }
    {
        Composer c = makeComposer();
        type(c, "OK");
        CHECK(c.handle(ComposerKey::of(ComposerKey::RawEnter)).commit == "OK");
    }
    {
        Composer c = makeComposer();
        c.settings.spaceMode = SpaceMode::Commit;
        type(c, "chit");
        CHECK(c.handle(ComposerKey::of(ComposerKey::Space)).commit == u8"ချစ်");
    }
    {
        Composer c = makeComposer();
        c.settings.returnAlsoSends = true;
        type(c, "hote");
        auto out = c.handle(ComposerKey::of(ComposerKey::Enter));
        CHECK(out.commit == u8"ဟုတ်" && !out.handled);
    }
    {
        Composer c = makeComposer();
        type(c, "chitt");
        c.handle(ComposerKey::of(ComposerKey::Backspace));
        CHECK(c.raw() == "chit");
        c.handle(ComposerKey::of(ComposerKey::Escape));
        CHECK(!c.isComposing());
    }
    {
        Composer c = makeComposer();
        type(c, "ma thwar lar");
        std::string original = c.preview();
        c.handle(ComposerKey::of(ComposerKey::Tab));
        CHECK(c.listMode() == Composer::ListMode::Phrase);
        CHECK(c.phraseOptions().size() > 1 && c.preview() == c.phraseOptions()[1].text && c.preview() != original);
        c.handle(ComposerKey::of(ComposerKey::BackTab));
        CHECK(c.preview() == original);
        c.handle(ComposerKey::of(ComposerKey::Tab));
        c.handle(ComposerKey::of(ComposerKey::Escape));
        CHECK(c.listMode() == Composer::ListMode::Word && c.isComposing() && c.preview() == original);
    }
    {
        Composer c = makeComposer();
        type(c, "ma thwar lar");
        c.handle(ComposerKey::of(ComposerKey::Tab));
        int idx = -1;
        for (size_t i = 0; i < c.phraseOptions().size(); ++i) if (c.phraseOptions()[i].text == u8"မသွားလား") idx = static_cast<int>(i);
        CHECK(idx >= 0 && idx < 9);
        if (idx >= 0) {
            c.handle(ComposerKey::number(idx + 1));
            CHECK(c.preview() == u8"မသွားလား");
            CHECK(c.handle(ComposerKey::of(ComposerKey::Enter)).commit == u8"မသွားလား");
        }
    }
    {
        Composer c = makeComposer();
        type(c, "ma thwar lar");
        auto before = c.result()->outputs;
        CHECK(c.focusedSegment() == 2);
        c.handle(ComposerKey::number(2));
        auto after = c.result()->outputs;
        CHECK(after[0] == before[0] && after[1] == before[1] && after[2] != before[2]);
    }
    {
        // wouldHandle agrees with handle for idle keys.
        Composer c = makeComposer();
        CHECK(c.wouldHandle(ComposerKey::letter('a')));
        CHECK(!c.wouldHandle(ComposerKey::of(ComposerKey::Enter)));
        CHECK(!c.wouldHandle(ComposerKey::punctuation('.')));
        type(c, "thwar");
        c.handle(ComposerKey::of(ComposerKey::Enter));
        CHECK(c.wouldHandle(ComposerKey::punctuation('.')));   // ။ right after Burmese
        CHECK(c.handle(ComposerKey::punctuation('.')).commit == u8"။");
        CHECK(!c.wouldHandle(ComposerKey::punctuation('.')));
    }
    {
        Composer c = makeComposer();
        type(c, "nin");
        std::string first = c.preview();
        c.handle(ComposerKey::of(ComposerKey::Tab));
        CHECK(c.listMode() == Composer::ListMode::Word && c.highlighted() == 1 && c.preview() != first);
    }
}

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "build/model";
    auto engine = Engine::load(dir);
    textTests();
    if (!engine) {
        std::printf("model not found in %s: skipping engine and composer tests\n", dir.c_str());
    } else {
        gEngine = engine.get();
        engineTests(*engine);
        composerTests();
    }
    std::printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
