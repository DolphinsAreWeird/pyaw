#include "syllable.hpp"

#include <unordered_set>

#include "burmese.hpp"

namespace pyaw {

namespace {

enum class Vowel { none, aa, i, ii, u, uu, e, ai, aw, o };
enum class FinalClass { none, velar, ng, palatal, nyaSmall, nyaBig, stop, nasal, ya, la, other };

const std::unordered_set<char32_t> takesYa = {0x1000, 0x1001, 0x1002, 0x1003, 0x1015, 0x1016, 0x1017,
                                              0x1018, 0x1019, 0x101C, 0x1014, 0x101E, 0x1005};
const std::unordered_set<char32_t> takesRa = {0x1000, 0x1001, 0x1002, 0x1003, 0x1004, 0x1015,
                                              0x1016, 0x1017, 0x1018, 0x1019, 0x101E};
const std::unordered_set<char32_t> takesHa = {0x1004, 0x100A, 0x1009, 0x100F, 0x1014,
                                              0x1019, 0x101A, 0x101B, 0x101C, 0x101D};
const std::unordered_set<char32_t> takesTallAa = {0x1001, 0x1002, 0x1004, 0x1012, 0x1015, 0x101D};

bool aaSpellingOK(const Syllable& s, bool tall) {
    bool wantsTall = takesTallAa.count(s.onset) && !s.medialY && !s.medialR && !s.medialW;
    return tall == wantsTall;
}

bool medialsAllowed(const Syllable& s) {
    char32_t c = s.onset;
    if (s.medialY) {
        if (!takesYa.count(c)) return false;
        if (c == 0x101E && !s.medialH) return false;
    }
    if (s.medialR && !takesRa.count(c)) return false;
    if (s.medialH && !(takesHa.count(c) || (c == 0x101E && s.medialY))) return false;
    if (s.medialW && (c == 0x101D || c == mm::letterA || c == mm::greatSa)) return false;
    return true;
}

FinalClass finalClass(char32_t c) {
    if (c == 0) return FinalClass::none;
    if (c >= 0x1000 && c <= 0x1003) return FinalClass::velar;
    if (c == 0x1004) return FinalClass::ng;
    if (c >= 0x1005 && c <= 0x1008) return FinalClass::palatal;
    if (c == 0x1009) return FinalClass::nyaSmall;
    if (c == 0x100A) return FinalClass::nyaBig;
    if ((c >= 0x100B && c <= 0x100E) || (c >= 0x1010 && c <= 0x1013) || (c >= 0x1015 && c <= 0x1018)) return FinalClass::stop;
    if (c == 0x100F || c == 0x1014 || c == 0x1019) return FinalClass::nasal;
    if (c == 0x101A) return FinalClass::ya;
    if (c == 0x101C) return FinalClass::la;
    return FinalClass::other;
}

const std::unordered_set<std::u32string>& specialSpellings() {
    static const std::unordered_set<std::u32string> set = {
        U"၌", U"၍", U"၏", U"၎င်း",
        U"ယောက်ျား",   // ယောက်ျား
        U"ကျွန်ုပ်",   // ကျွန်ုပ်
        U"လက်ျာ",                     // လက်ျာ
    };
    return set;
}

}  // namespace

std::optional<Syllable> parse_syllable(const std::u32string& s) {
    if (s.empty()) return std::nullopt;
    const char32_t first = s[0];
    if (specialSpellings().count(s)) {
        Syllable sym;
        sym.onset = first;
        sym.symbol = s;
        sym.hasSymbol = true;
        return sym;
    }
    if (mm::isSymbolSyllable(first)) return std::nullopt;

    Syllable syl;
    syl.onset = first;
    bool hasImplied = false;
    Vowel implied = Vowel::none;
    if (mm::isIndependentVowel(first)) {
        syl.onset = mm::letterA;
        hasImplied = true;
        switch (first) {
            case 0x1023: implied = Vowel::i; break;
            case 0x1024: implied = Vowel::ii; break;
            case 0x1025: implied = Vowel::u; break;
            case 0x1026: implied = Vowel::uu; break;
            case 0x1027: implied = Vowel::e; break;
            case 0x1029: implied = Vowel::aw; break;
            case 0x102A: implied = Vowel::aw; break;
            default: return std::nullopt;
        }
    } else if (!(mm::isConsonant(first) || first == mm::greatSa)) {
        return std::nullopt;
    }

    const size_t n = s.size();
    size_t i = 1;
    char32_t lastMedial = 0;
    while (i < n && s[i] >= mm::medialYa && s[i] <= mm::medialHa) {
        if (s[i] <= lastMedial || hasImplied) return std::nullopt;
        switch (s[i]) {
            case mm::medialYa: syl.medialY = true; break;
            case mm::medialRa: syl.medialR = true; break;
            case mm::medialWa: syl.medialW = true; break;
            default: syl.medialH = true; break;
        }
        lastMedial = s[i];
        ++i;
    }
    if (syl.medialY && syl.medialR) return std::nullopt;
    if (!medialsAllowed(syl)) return std::nullopt;

    auto take = [&](char32_t c) { if (i < n && s[i] == c) { ++i; return true; } return false; };

    bool hasE = take(mm::eVowel);
    char32_t upper = 0;
    if (i < n && (s[i] == mm::iVowel || s[i] == mm::iiVowel || s[i] == mm::aiVowel)) { upper = s[i]; ++i; }
    char32_t lower = 0;
    if (i < n && (s[i] == 0x102F || s[i] == 0x1030)) { lower = s[i]; ++i; }
    char32_t aaSign = 0;
    if (i < n && (s[i] == 0x102B || s[i] == 0x102C)) { aaSign = s[i]; ++i; }
    bool hasAa = aaSign != 0;
    if (hasAa && !aaSpellingOK(syl, aaSign == mm::tallAa)) return std::nullopt;
    bool anus = take(mm::anusvara);
    bool dot = take(mm::dotBelow);
    bool vowelAsat = take(mm::asat);
    bool vis = take(mm::visarga);
    bool toneOnVowel = dot || vis;

    char32_t fin = 0;
    if (i < n && mm::isConsonant(s[i])) {
        fin = s[i]; ++i;
        if (take(mm::dotBelow)) dot = true;
        if (take(mm::asat)) {
            if (take(mm::virama)) {
                if (fin != mm::nga) return std::nullopt;
                syl.stacked = true; syl.isKinzi = true;
            }
        } else if (take(mm::virama)) {
            syl.stacked = true;
        } else {
            return std::nullopt;
        }
        if (take(mm::dotBelow)) dot = true;
        if (take(mm::visarga)) vis = true;
    }
    if (i != n) return std::nullopt;
    if (anus && fin != 0) return std::nullopt;
    if (dot && vis) return std::nullopt;

    Vowel vowel;
    if (!hasE && upper == 0 && lower == 0 && !hasAa) vowel = Vowel::none;
    else if (!hasE && upper == 0 && lower == 0 && hasAa) vowel = Vowel::aa;
    else if (!hasE && upper == mm::iVowel && lower == 0 && !hasAa) vowel = Vowel::i;
    else if (!hasE && upper == mm::iiVowel && lower == 0 && !hasAa) vowel = Vowel::ii;
    else if (!hasE && upper == 0 && lower == mm::uVowel && !hasAa) vowel = Vowel::u;
    else if (!hasE && upper == 0 && lower == mm::uuVowel && !hasAa) vowel = Vowel::uu;
    else if (hasE && upper == 0 && lower == 0 && !hasAa) vowel = Vowel::e;
    else if (hasE && upper == 0 && lower == 0 && hasAa) vowel = Vowel::aw;
    else if (!hasE && upper == mm::aiVowel && lower == 0 && !hasAa) vowel = Vowel::ai;
    else if (!hasE && upper == mm::iVowel && lower == mm::uVowel && !hasAa) vowel = Vowel::o;
    else return std::nullopt;

    if (hasImplied) {
        if (vowel != Vowel::none) return std::nullopt;
        vowel = implied;
    }
    if (vowelAsat) {
        if (!(vowel == Vowel::aw && fin == 0)) return std::nullopt;
    }
    if (syl.medialW && (vowel == Vowel::u || vowel == Vowel::uu || vowel == Vowel::o || vowel == Vowel::aw))
        return std::nullopt;

    syl.finalConsonant = fin;
    FinalClass fc = finalClass(fin);
    const bool w = syl.medialW;
    if (fc == FinalClass::la && !syl.stacked && vowel != Vowel::o) {
        syl.trailingL = true;
        fc = FinalClass::none;
    }
    if (vowel == Vowel::aa && fc != FinalClass::none) {
        if (toneOnVowel) return std::nullopt;
        vowel = Vowel::none;
    }
    if (vowel == Vowel::none && fc == FinalClass::none && !anus && (dot || vis) && !hasImplied) return std::nullopt;

    Rhyme rhyme;
    using V = Vowel;
    using F = FinalClass;
    if (vowel == V::none && fc == F::none && !anus) rhyme = w ? Rhyme::wa : Rhyme::a;
    else if (vowel == V::aa && fc == F::none && !anus) rhyme = w ? Rhyme::waa : Rhyme::aa;
    else if (vowel == V::i && fc == F::none && !anus) rhyme = w ? Rhyme::wi : Rhyme::i;
    else if (vowel == V::ii && fc == F::none && !anus) rhyme = w ? Rhyme::wi : Rhyme::ii;
    else if (vowel == V::u && fc == F::none && !anus) rhyme = Rhyme::u;
    else if (vowel == V::uu && fc == F::none && !anus) rhyme = Rhyme::uu;
    else if (vowel == V::e && fc == F::none && !anus) rhyme = w ? Rhyme::we : Rhyme::e;
    else if (!anus && ((vowel == V::ai && fc == F::none) || (vowel == V::none && fc == F::ya))) rhyme = w ? Rhyme::wai : Rhyme::ai;
    else if (vowel == V::aw && fc == F::none && !anus) rhyme = Rhyme::aw;
    else if (vowel == V::o && !anus && (fc == F::none || fc == F::la || fc == F::ya || fc == F::other)) rhyme = Rhyme::o;
    else if (vowel == V::none && ((fc == F::nasal && !anus) || (fc == F::none && anus))) rhyme = w ? Rhyme::un : Rhyme::an;
    else if (vowel == V::none && fc == F::stop && !anus) rhyme = w ? Rhyme::ut : Rhyme::at;
    else if (vowel == V::none && fc == F::velar && !anus) rhyme = w ? Rhyme::wet : Rhyme::et;
    else if (vowel == V::none && (fc == F::ng || fc == F::nyaSmall) && !anus) rhyme = w ? Rhyme::win : Rhyme::in;
    else if (vowel == V::none && fc == F::palatal && !anus) rhyme = w ? Rhyme::wit : Rhyme::it;
    else if (vowel == V::none && fc == F::nyaBig && !anus) rhyme = Rhyme::ie;
    else if (vowel == V::i && ((fc == F::nasal && !anus) || (fc == F::none && anus))) rhyme = Rhyme::ein;
    else if (vowel == V::i && !anus && (fc == F::stop || fc == F::velar || fc == F::palatal)) rhyme = Rhyme::eik;
    else if (vowel == V::u && ((fc == F::nasal && !anus) || (fc == F::none && anus))) rhyme = Rhyme::oun;
    else if (vowel == V::u && !anus && (fc == F::stop || fc == F::velar)) rhyme = Rhyme::ok;
    else if (vowel == V::o && fc == F::ng && !anus) rhyme = Rhyme::aing;
    else if (vowel == V::o && fc == F::velar && !anus) rhyme = Rhyme::aik;
    else if (vowel == V::aw && fc == F::ng && !anus) rhyme = Rhyme::aung;
    else if (vowel == V::aw && fc == F::velar && !anus) rhyme = Rhyme::auk;
    else if (vowel == V::none && fc == F::la && !anus && syl.stacked) rhyme = Rhyme::an;
    else if (vowel == V::none && fc == F::other && !anus && syl.stacked) rhyme = Rhyme::at;
    else return std::nullopt;
    syl.rhyme = rhyme;

    switch (rhyme) {
        case Rhyme::at: case Rhyme::et: case Rhyme::it: case Rhyme::eik: case Rhyme::ok:
        case Rhyme::aik: case Rhyme::auk: case Rhyme::ut: case Rhyme::wet: case Rhyme::wit:
            syl.tone = Tone::checked;
            break;
        case Rhyme::a: case Rhyme::wa:
            syl.tone = vis ? Tone::high : Tone::creaky;
            break;
        case Rhyme::aw:
            if (dot) syl.tone = Tone::creaky;
            else if (vowelAsat || first == 0x102A) syl.tone = Tone::low;
            else syl.tone = Tone::high;
            break;
        case Rhyme::ai: case Rhyme::wai:
            if (dot) syl.tone = Tone::creaky;
            else if (fin == 0x101A) syl.tone = Tone::low;
            else syl.tone = Tone::high;
            break;
        default:
            syl.tone = dot ? Tone::creaky : (vis ? Tone::high : Tone::low);
            break;
    }
    return syl;
}

}  // namespace pyaw
