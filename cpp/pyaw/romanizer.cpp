#include "romanizer.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>

namespace pyaw {

namespace {

constexpr float kMaxCost = 3.6f;

struct Onset { std::string text; float cost; };
struct RhymeSpelling { std::string text; float cost; bool abbreviated; };
using Onsets = std::vector<Onset>;
using Rhymes = std::vector<RhymeSpelling>;

const std::unordered_map<char32_t, Onsets>& baseOnsets() {
    static const std::unordered_map<char32_t, Onsets> t = {
        {0x1000, {{"k", 0.1f}, {"g", 1.0f}}},
        {0x1001, {{"kh", 0.3f}, {"k", 0.7f}, {"g", 1.5f}, {"hk", 1.6f}}},
        {0x1002, {{"g", 0.1f}, {"k", 2.2f}}},
        {0x1003, {{"g", 0.2f}, {"gh", 1.6f}}},
        {0x1004, {{"ng", 0.1f}}},
        {0x1005, {{"s", 0.1f}, {"z", 1.2f}, {"c", 2.2f}}},
        {0x1006, {{"s", 0.3f}, {"hs", 0.6f}, {"z", 1.5f}, {"ss", 2.0f}, {"sh", 2.2f}}},
        {0x1007, {{"z", 0.1f}, {"j", 2.0f}}},
        {0x1008, {{"z", 0.2f}, {"zh", 2.0f}, {"j", 2.0f}}},
        {0x1009, {{"ny", 0.2f}, {"n", 1.6f}}},
        {0x100A, {{"ny", 0.1f}, {"n", 1.6f}}},
        {0x100B, {{"t", 0.1f}, {"d", 1.5f}}},
        {0x100C, {{"ht", 0.4f}, {"th", 0.6f}, {"t", 1.0f}, {"d", 1.5f}}},
        {0x100D, {{"d", 0.1f}}},
        {0x100E, {{"d", 0.1f}, {"dh", 1.6f}}},
        {0x100F, {{"n", 0.1f}}},
        {0x1010, {{"t", 0.1f}, {"d", 1.0f}}},
        {0x1011, {{"ht", 0.3f}, {"th", 0.5f}, {"t", 0.9f}, {"d", 1.4f}}},
        {0x1012, {{"d", 0.1f}}},
        {0x1013, {{"d", 0.1f}, {"dh", 1.6f}}},
        {0x1014, {{"n", 0.1f}}},
        {0x1015, {{"p", 0.1f}, {"b", 1.0f}}},
        {0x1016, {{"ph", 0.3f}, {"p", 0.8f}, {"hp", 1.0f}, {"b", 1.4f}, {"f", 2.0f}}},
        {0x1017, {{"b", 0.1f}, {"v", 1.6f}}},
        {0x1018, {{"b", 0.1f}, {"bh", 2.0f}}},
        {0x1019, {{"m", 0.1f}}},
        {0x101A, {{"y", 0.1f}}},
        {0x101B, {{"y", 0.3f}, {"r", 0.9f}}},
        {0x101C, {{"l", 0.1f}}},
        {0x101D, {{"w", 0.1f}}},
        {0x101E, {{"th", 0.1f}, {"t", 1.0f}, {"s", 2.2f}}},
        {0x101F, {{"h", 0.1f}}},
        {0x1020, {{"l", 0.1f}}},
        {0x1021, {{"", 0.0f}}},
        {0x103F, {{"th", 0.5f}, {"tth", 1.6f}}},
    };
    return t;
}

Onsets base(char32_t c) {
    auto it = baseOnsets().find(c);
    return it == baseOnsets().end() ? Onsets{} : it->second;
}

Onsets plus(Onsets a, const Onsets& b) { a.insert(a.end(), b.begin(), b.end()); return a; }

Onsets palatalOnsets(char32_t c, bool r) {
    switch (c) {
        case 0x1000: return plus({{"ky", 0.3f}, {"ch", 0.8f}, {"gy", 1.0f}, {"j", 1.4f}}, r ? Onsets{{"kr", 1.8f}} : Onsets{});
        case 0x1001: return {{"ch", 0.3f}, {"ky", 1.1f}, {"khy", 1.2f}, {"hky", 1.6f}, {"chy", 1.6f}, {"j", 1.8f}, {"gy", 1.8f}};
        case 0x1002: return plus({{"gy", 0.2f}, {"j", 0.8f}, {"ky", 1.6f}}, r ? Onsets{{"gr", 2.0f}} : Onsets{});
        case 0x1003: return {{"gy", 0.3f}, {"j", 1.0f}};
        case 0x1004: return {{"ny", 0.3f}, {"ngr", 2.0f}, {"ngy", 2.0f}};
        case 0x1015: return plus({{"py", 0.2f}, {"by", 1.4f}, {"p", 2.2f}}, r ? Onsets{{"pr", 1.4f}} : Onsets{});
        case 0x1016: return plus({{"phy", 0.3f}, {"py", 0.8f}, {"hpy", 1.0f}, {"by", 1.8f}, {"fy", 2.2f}}, r ? Onsets{{"phr", 1.8f}} : Onsets{});
        case 0x1017: return plus({{"by", 0.2f}}, r ? Onsets{{"br", 1.8f}} : Onsets{});
        case 0x1018: return plus({{"by", 0.2f}}, r ? Onsets{{"bhr", 2.4f}} : Onsets{});
        case 0x1019: return plus({{"my", 0.2f}, {"m", 2.0f}}, r ? Onsets{{"mr", 1.4f}} : Onsets{});
        case 0x101C: return {{"ly", 0.3f}, {"y", 1.2f}};
        case 0x1005: return {{"z", 0.3f}, {"s", 0.4f}, {"zy", 1.5f}, {"sy", 1.5f}};
        case 0x101E: return r ? Onsets{{"thy", 1.2f}, {"", 1.5f}} : Onsets{{"thy", 0.5f}};
        default: {
            Onsets out;
            for (auto& o : base(c)) out.push_back({o.text + (r ? "r" : "y"), o.cost + 0.3f});
            return out;
        }
    }
}

Onsets aspiratedOnsets(char32_t c, bool palatal) {
    if (palatal) {
        switch (c) {
            case 0x101C: case 0x101E: return {{"sh", 0.1f}, {"hly", 2.0f}, {"ly", 2.2f}};
            case 0x1019: return {{"hmy", 0.4f}, {"my", 0.8f}, {"mhy", 1.0f}};
            case 0x1014: case 0x1004: return {{"hny", 0.5f}, {"ny", 1.0f}};
            default: {
                Onsets out;
                for (auto& o : palatalOnsets(c, false)) out.push_back({"h" + o.text, o.cost + 0.5f});
                return out;
            }
        }
    }
    switch (c) {
        case 0x1019: return {{"hm", 0.4f}, {"mh", 0.5f}, {"m", 0.9f}};
        case 0x1014: return {{"hn", 0.4f}, {"nh", 0.5f}, {"n", 0.9f}};
        case 0x100A: case 0x1009: return {{"hny", 0.4f}, {"ny", 0.9f}, {"nyh", 1.6f}};
        case 0x1004: return {{"hng", 0.4f}, {"ng", 0.9f}, {"ngh", 1.6f}};
        case 0x101C: return {{"hl", 0.4f}, {"lh", 0.6f}, {"l", 0.9f}};
        case 0x101D: return {{"hw", 0.5f}, {"wh", 0.6f}, {"w", 0.9f}};
        case 0x101B: return {{"sh", 0.1f}, {"rh", 2.0f}, {"hr", 2.0f}};
        case 0x101A: return {{"sh", 0.2f}, {"hy", 1.4f}};
        default: {
            Onsets out;
            for (auto& o : base(c)) out.push_back({"h" + o.text, o.cost + 1.0f});
            for (auto& o : base(c)) out.push_back({o.text + "h", o.cost + 1.0f});
            return out;
        }
    }
}

Onsets onsetVariants(const Syllable& s) {
    bool palatal = s.medialY || s.medialR;
    if (s.medialH) return aspiratedOnsets(s.onset, palatal);
    if (palatal) return palatalOnsets(s.onset, s.medialR);
    return base(s.onset);
}

const std::map<Rhyme, Rhymes>& rhymeTable() {
    static const std::map<Rhyme, Rhymes> t = {
        {Rhyme::a, {{"a", 0.2f, false}, {"", 1.4f, true}, {"ah", 2.2f, false}, {"ar", 2.4f, false}}},
        {Rhyme::aa, {{"ar", 0.3f, false}, {"a", 0.7f, false}, {"r", 0.7f, true}, {"rr", 1.6f, true}, {"aa", 2.0f, false}, {"ah", 2.0f, false}}},
        {Rhyme::i, {{"i", 0.2f, false}, {"e", 1.6f, false}, {"ee", 1.6f, false}, {"ih", 2.2f, false}}},
        {Rhyme::ii, {{"i", 0.3f, false}, {"ee", 0.7f, false}, {"e", 1.6f, false}, {"ii", 1.8f, false}}},
        {Rhyme::u, {{"u", 0.2f, false}, {"oo", 1.2f, false}, {"o", 2.2f, false}}},
        {Rhyme::uu, {{"u", 0.3f, false}, {"oo", 0.7f, false}, {"uu", 1.8f, false}}},
        {Rhyme::e, {{"ay", 0.3f, false}, {"y", 0.8f, true}, {"e", 1.2f, false}, {"ei", 1.2f, false}, {"yy", 1.6f, true},
                    {"ayy", 1.6f, false}, {"ey", 2.0f, false}, {"ae", 2.2f, false}}},
        {Rhyme::ai, {{"al", 0.5f, false}, {"ae", 0.5f, false}, {"l", 0.7f, true}, {"e", 1.0f, false}, {"el", 1.0f, false},
                     {"eh", 1.2f, false}, {"ll", 1.8f, true}, {"ai", 2.4f, false}, {"ay", 2.6f, false}}},
        {Rhyme::aw, {{"aw", 0.3f, false}, {"w", 1.2f, true}, {"or", 1.2f, false}, {"o", 1.6f, false}, {"au", 2.0f, false}, {"oh", 2.2f, false}}},
        {Rhyme::o, {{"o", 0.3f, false}, {"oe", 0.8f, false}, {"oh", 1.2f, false}}},
        {Rhyme::an, {{"an", 0.3f, false}, {"n", 1.2f, true}, {"am", 1.6f, false}, {"un", 2.4f, false}, {"en", 2.4f, false}}},
        {Rhyme::at, {{"at", 0.3f, false}, {"t", 1.5f, true}, {"et", 1.5f, false}, {"ut", 2.4f, false}}},
        {Rhyme::et, {{"et", 0.3f, false}, {"ek", 1.0f, false}, {"k", 1.5f, true}, {"t", 1.6f, true}, {"ak", 2.0f, false}, {"at", 2.2f, false}}},
        {Rhyme::in, {{"in", 0.3f, false}, {"n", 1.3f, true}, {"ing", 1.4f, false}, {"inn", 1.8f, false}}},
        {Rhyme::it, {{"it", 0.3f, false}, {"t", 1.5f, true}, {"eit", 2.2f, false}, {"eet", 2.4f, false}}},
        {Rhyme::ie, {{"i", 0.5f, false}, {"ee", 1.0f, false}, {"e", 1.0f, false}, {"ay", 1.5f, false}, {"ae", 1.6f, false},
                     {"ei", 1.6f, false}, {"al", 1.8f, false}, {"ih", 2.0f, false}}},
        {Rhyme::ein, {{"ein", 0.3f, false}, {"ain", 1.2f, false}, {"en", 1.6f, false}, {"n", 2.0f, true}, {"eim", 2.0f, false}}},
        {Rhyme::eik, {{"eik", 0.4f, false}, {"ate", 0.6f, false}, {"eit", 1.0f, false}, {"ait", 1.3f, false}, {"ake", 1.6f, false},
                      {"k", 2.0f, true}, {"t", 2.2f, true}}},
        {Rhyme::oun, {{"on", 0.5f, false}, {"one", 0.5f, false}, {"oun", 0.6f, false}, {"own", 1.2f, false}, {"ohn", 1.5f, false},
                      {"om", 1.6f, false}, {"ome", 1.6f, false}, {"un", 2.0f, false}, {"n", 2.0f, true}, {"oon", 2.2f, false}}},
        {Rhyme::ok, {{"ote", 0.4f, false}, {"ok", 0.6f, false}, {"oke", 0.8f, false}, {"oat", 1.2f, false}, {"t", 2.0f, true},
                     {"oak", 2.0f, false}, {"out", 2.6f, false}}},
        {Rhyme::aing, {{"aing", 0.5f, false}, {"ai", 0.6f, false}, {"ine", 0.7f, false}, {"ain", 1.0f, false}, {"ing", 2.0f, false}}},
        {Rhyme::aik, {{"ike", 0.5f, false}, {"aik", 0.5f, false}, {"ite", 1.0f, false}, {"ait", 1.0f, false}, {"ik", 1.6f, false},
                      {"k", 2.0f, true}}},
        {Rhyme::aung, {{"aung", 0.3f, false}, {"g", 0.6f, true}, {"aun", 1.5f, false}, {"ng", 1.6f, true}, {"ong", 1.8f, false},
                       {"owng", 2.2f, false}}},
        {Rhyme::auk, {{"auk", 0.4f, false}, {"out", 0.6f, false}, {"aut", 1.0f, false}, {"ouk", 1.5f, false}, {"k", 1.8f, true},
                      {"awk", 2.2f, false}}},
        {Rhyme::wa, {{"wa", 0.3f, false}, {"w", 1.5f, true}}},
        {Rhyme::waa, {{"war", 0.3f, false}, {"wa", 0.8f, false}, {"wr", 0.8f, true}, {"wrr", 1.6f, true}, {"waa", 2.0f, false}}},
        {Rhyme::wi, {{"wi", 0.5f, false}, {"wee", 1.2f, false}}},
        {Rhyme::we, {{"way", 0.3f, false}, {"we", 0.8f, false}, {"wy", 1.0f, true}, {"wei", 1.5f, false}}},
        {Rhyme::wai, {{"wal", 0.5f, false}, {"wae", 0.5f, false}, {"wl", 1.0f, true}, {"wel", 1.0f, false}, {"we", 1.2f, false},
                      {"weh", 1.6f, false}}},
        {Rhyme::un, {{"un", 0.4f, false}, {"wan", 1.0f, false}, {"wun", 1.0f, false}, {"oon", 1.5f, false}, {"wn", 1.5f, true}}},
        {Rhyme::ut, {{"ut", 0.4f, false}, {"wut", 1.0f, false}, {"wat", 1.0f, false}, {"oot", 1.6f, false}, {"wt", 1.6f, true}}},
        {Rhyme::wet, {{"wet", 0.4f, false}, {"wat", 1.0f, false}, {"wek", 1.2f, false}, {"wt", 1.6f, true}}},
        {Rhyme::win, {{"win", 0.4f, false}, {"wn", 1.6f, true}}},
        {Rhyme::wit, {{"wit", 0.4f, false}, {"wt", 1.8f, true}}},
    };
    return t;
}

const std::map<Rhyme, std::map<Tone, Rhymes>>& toneExtras() {
    static const std::map<Rhyme, std::map<Tone, Rhymes>> t = {
        {Rhyme::aa, {{Tone::high, {{"rr", 1.0f, true}, {"arr", 1.6f, false}}}}},
        {Rhyme::ii, {{Tone::high, {{"ee", 0.5f, false}}}}},
        {Rhyme::uu, {{Tone::high, {{"oo", 0.5f, false}}}}},
        {Rhyme::o, {{Tone::high, {{"oe", 0.6f, false}}}, {Tone::creaky, {{"ot", 1.8f, false}}}}},
        {Rhyme::e, {{Tone::high, {{"ayy", 1.4f, false}}}}},
        {Rhyme::ai, {{Tone::high, {{"ae", 0.4f, false}}},
                     {Tone::creaky, {{"ae", 0.4f, false}, {"e", 0.8f, false}, {"eh", 1.0f, false}, {"et", 1.6f, false}, {"ek", 1.8f, false}}}}},
        {Rhyme::aw, {{Tone::creaky, {{"ot", 1.4f, false}, {"awt", 1.8f, false}}}, {Tone::low, {{"or", 0.9f, false}}}}},
        {Rhyme::aung, {{Tone::creaky, {{"ount", 1.0f, false}, {"aunt", 1.0f, false}}}}},
        {Rhyme::in, {{Tone::creaky, {{"int", 0.9f, false}}}, {Tone::high, {{"inn", 1.4f, false}}}}},
        {Rhyme::an, {{Tone::creaky, {{"ant", 1.4f, false}}}}},
    };
    return t;
}

Rhymes rhymeVariants(Rhyme rhyme, Tone tone) {
    Rhymes list;
    auto it = rhymeTable().find(rhyme);
    if (it != rhymeTable().end()) list = it->second;
    auto ex = toneExtras().find(rhyme);
    if (ex != toneExtras().end()) {
        auto tx = ex->second.find(tone);
        if (tx != ex->second.end()) {
            for (auto& e : tx->second) {
                auto found = std::find_if(list.begin(), list.end(), [&](const RhymeSpelling& r) { return r.text == e.text; });
                if (found != list.end()) { if (e.cost < found->cost) *found = e; }
                else list.push_back(e);
            }
        }
    }
    return list;
}

bool droppedCoda(Rhyme r, Rhyme& open) {
    switch (r) {
        case Rhyme::an: case Rhyme::at: case Rhyme::et: open = Rhyme::a; return true;
        case Rhyme::it: case Rhyme::ein: case Rhyme::eik: open = Rhyme::i; return true;
        case Rhyme::oun: case Rhyme::ok: open = Rhyme::u; return true;
        case Rhyme::un: case Rhyme::ut: open = Rhyme::wa; return true;
        default: return false;
    }
}

const std::map<std::u32string, Onsets>& specialTable() {
    static const std::map<std::u32string, Onsets> t = {
        {U"၌", {{"hnaik", 0.3f}, {"naik", 0.8f}, {"nike", 0.8f}, {"hnike", 0.8f}}},
        {U"၍", {{"ywe", 0.3f}, {"ywae", 0.5f}, {"ywal", 0.8f}, {"yway", 1.0f}}},
        {U"၏", {{"i", 0.6f}, {"ei", 0.8f}, {"e", 1.0f}}},
        {U"၎င်း", {{"lagaung", 0.3f}, {"lakaung", 0.8f}, {"lingaung", 1.2f}}},
        {U"ယောက်ျား",
         {{"yaukkyar", 0.4f}, {"youtkyar", 0.5f}, {"yautkyar", 0.8f}, {"youkyar", 0.8f}, {"yaukyar", 0.8f}, {"yaukjar", 1.2f}}},
        {U"ကျွန်ုပ်",
         {{"kyanote", 0.4f}, {"kyunote", 0.6f}, {"kyunoke", 0.6f}, {"kyanoke", 0.7f}, {"kyanok", 0.8f}, {"kyunok", 1.0f}}},
        {U"လက်ျာ", {{"letyar", 0.4f}, {"letya", 0.8f}, {"lakyar", 1.0f}, {"letkyar", 1.0f}}},
    };
    return t;
}

}  // namespace

std::vector<RomanVariant> romanize(const Syllable& syl) {
    std::vector<RomanVariant> out;
    if (syl.hasSymbol) {
        auto it = specialTable().find(syl.symbol);
        if (it != specialTable().end()) for (auto& o : it->second) out.push_back({o.text, o.cost});
        return out;
    }
    std::unordered_map<std::string, float> best;
    const Onsets onsets = onsetVariants(syl);
    auto combine = [&](const Rhymes& rhymes, float extra) {
        for (auto& o : onsets) {
            for (auto& r : rhymes) {
                if (o.text.empty() && (r.abbreviated || r.text.empty())) continue;
                std::string text = o.text + r.text;
                float cost = o.cost + r.cost + extra;
                if (cost > kMaxCost) continue;
                auto it = best.find(text);
                if (it != best.end() && it->second <= cost) continue;
                best[text] = cost;
            }
        }
    };
    Rhymes rhymes = rhymeVariants(syl.rhyme, syl.tone);
    if (syl.trailingL) {
        Rhymes withL;
        for (auto& r : rhymes) {
            if (r.abbreviated) withL.push_back({r.text, r.cost + 1.0f, true});
            else { withL.push_back({r.text + "l", r.cost + 0.2f, false}); withL.push_back({r.text, r.cost + 0.8f, false}); }
        }
        rhymes = withL;
    }
    combine(rhymes, 0);
    Rhyme open;
    if (syl.stacked && !syl.isKinzi && droppedCoda(syl.rhyme, open)) combine(rhymeVariants(open, Tone::creaky), 0.6f);
    for (auto& kv : best) out.push_back({kv.first, kv.second});
    std::sort(out.begin(), out.end(), [](const RomanVariant& a, const RomanVariant& b) {
        return a.cost != b.cost ? a.cost < b.cost : a.text < b.text;
    });
    return out;
}

std::vector<RomanVariant> romanize(const std::u32string& syllableText) {
    auto syl = parse_syllable(syllableText);
    return syl ? romanize(*syl) : std::vector<RomanVariant>{};
}

}  // namespace pyaw
