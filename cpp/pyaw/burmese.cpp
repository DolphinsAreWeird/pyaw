#include "burmese.hpp"

#include <algorithm>
#include <unordered_set>

#include "unicode.hpp"

namespace pyaw {

namespace {

int mark_rank(char32_t c) {
    switch (c) {
        case 0x103B: return 1;
        case 0x103C: return 2;
        case 0x103D: return 3;
        case 0x103E: return 4;
        case 0x1031: return 5;
        case 0x102D: case 0x102E: case 0x1032: return 6;
        case 0x102F: case 0x1030: return 7;
        case 0x102B: case 0x102C: return 8;
        case 0x1036: return 9;
        case 0x1037: return 10;
        case 0x103A: return 11;
        case 0x1038: return 12;
        default: return 20;
    }
}

// Canonical combining class for the Myanmar marks that have one.
int ccc(char32_t c) {
    if (c == 0x1037) return 7;
    if (c == 0x1039 || c == 0x103A) return 9;
    return 0;
}

// The part of Unicode NFC that affects Burmese: ဥ + ီ composes to ဦ, and runs of non-zero
// combining classes are stably sorted.
std::u32string nfc_burmese(const std::u32string& in) {
    std::u32string s;
    s.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == 0x1025 && i + 1 < in.size() && in[i + 1] == 0x102E) { s.push_back(0x1026); ++i; }
        else s.push_back(in[i]);
    }
    size_t i = 0;
    while (i < s.size()) {
        if (ccc(s[i]) == 0) { ++i; continue; }
        size_t j = i;
        while (j < s.size() && ccc(s[j]) != 0) ++j;
        std::stable_sort(s.begin() + i, s.begin() + j, [](char32_t a, char32_t b) { return ccc(a) < ccc(b); });
        i = j;
    }
    return s;
}

}  // namespace

std::u32string normalize_burmese(const std::u32string& input) {
    std::u32string nfc = nfc_burmese(input);
    std::u32string s;
    s.reserve(nfc.size());
    for (char32_t c : nfc) {
        if (c == 0x200B || c == 0x200C || c == 0x200D || c == 0xFEFF || c == 0x00AD) continue;
        s.push_back(c);
    }
    const size_t n = s.size();
    for (size_t i = 0; i < n; ++i) {
        char32_t next = i + 1 < n ? s[i + 1] : 0;
        char32_t prev = i > 0 ? s[i - 1] : 0;
        switch (s[i]) {
            case 0x1040:  // ၀ used for ဝ
                if (mm::isMark(next)) {
                    s[i] = 0x101D;
                } else if (!mm::isDigit(prev) && !mm::isDigit(next) &&
                           (mm::isConsonant(next) || mm::isConsonant(prev) || mm::isMark(prev))) {
                    s[i] = 0x101D;
                }
                break;
            case 0x1047:  // ၇ used for ရ
                if (mm::isMark(next)) s[i] = 0x101B;
                break;
            case 0x1025:  // ဥ် used for ဉ်
                if (next == mm::asat) s[i] = 0x1009;
                break;
            case 0x1044:  // ၄င်း used for ၎င်း
                if (next == mm::nga && i + 2 < n && s[i + 2] == mm::asat) s[i] = 0x104E;
                break;
            default: break;
        }
    }
    std::u32string out;
    out.reserve(n);
    size_t i = 0;
    while (i < n) {
        if (mm::isMark(s[i])) {
            size_t j = i;
            while (j < n && mm::isMark(s[j])) ++j;
            // Marks after an asat other than း/့ are deliberate irregular spellings.
            bool irregular = false;
            for (size_t a = i; a < j; ++a) {
                if (s[a] == mm::asat) {
                    for (size_t b = a + 1; b < j; ++b)
                        if (s[b] != mm::visarga && s[b] != mm::dotBelow) { irregular = true; break; }
                    break;
                }
            }
            if (j - i == 1 || irregular) {
                out.append(s, i, j - i);
            } else {
                std::u32string run(s, i, j - i);
                std::stable_sort(run.begin(), run.end(), [](char32_t a, char32_t b) { return mark_rank(a) < mark_rank(b); });
                char32_t last = 0;
                for (char32_t c : run) if (c != last) { out.push_back(c); last = c; }
            }
            i = j;
        } else {
            out.push_back(s[i]);
            ++i;
        }
    }
    return out;
}

std::string normalize_burmese(const std::string& utf8) { return u32_to_utf8(normalize_burmese(utf8_to_u32(utf8))); }

std::vector<std::vector<std::u32string>> syllable_runs(const std::u32string& s) {
    std::vector<std::vector<std::u32string>> runs;
    std::vector<std::u32string> run;
    std::u32string syl;
    auto flushSyllable = [&] { if (!syl.empty()) { run.push_back(syl); syl.clear(); } };
    auto flushRun = [&] { flushSyllable(); if (!run.empty()) { runs.push_back(run); run.clear(); } };
    const size_t n = s.size();
    for (size_t i = 0; i < n; ++i) {
        char32_t c = s[i];
        if (mm::isStarter(c)) {
            char32_t n1 = i + 1 < n ? s[i + 1] : 0;
            char32_t n2 = i + 2 < n ? s[i + 2] : 0;
            bool closes = n1 == mm::asat || (n1 == mm::dotBelow && n2 == mm::asat) || n1 == mm::virama;
            bool afterVirama = i > 0 && s[i - 1] == mm::virama;
            if (closes && !syl.empty() && !afterVirama && mm::isConsonant(c)) {
                syl.push_back(c);
            } else {
                flushSyllable();
                syl.push_back(c);
            }
        } else if (mm::isMark(c) || c == mm::virama) {
            if (syl.empty()) { syl.push_back(c); flushSyllable(); }
            else syl.push_back(c);
        } else if (c == 0x20 || c == 0xA0 || c == 0x3000) {
            flushSyllable();
        } else {
            flushRun();
        }
    }
    flushRun();
    return runs;
}

std::vector<std::string> syllables(const std::string& utf8) {
    std::vector<std::string> out;
    for (auto& r : syllable_runs(normalize_burmese(utf8_to_u32(utf8))))
        for (auto& syl : r) out.push_back(u32_to_utf8(syl));
    return out;
}

bool is_plausible_stack(char32_t upper, char32_t lower) {
    static const std::unordered_set<unsigned long long> pairs = [] {
        std::unordered_set<unsigned long long> set;
        auto add = [&](char32_t a, char32_t b) { set.insert((unsigned long long)a << 32 | b); };
        const std::vector<std::vector<char32_t>> rows = {
            {0x1000, 0x1001, 0x1002, 0x1003, 0x1004},
            {0x1005, 0x1006, 0x1007, 0x1008, 0x1009, 0x100A},
            {0x100B, 0x100C, 0x100D, 0x100E, 0x100F},
            {0x1010, 0x1011, 0x1012, 0x1013, 0x1014},
            {0x1015, 0x1016, 0x1017, 0x1018, 0x1019},
        };
        for (auto& r : rows) {
            add(r[0], r[0]); add(r[0], r[1]); add(r[2], r[2]); add(r[2], r[3]);
            for (size_t k = 4; k < r.size(); ++k) {
                for (size_t x = 0; x < 4; ++x) add(r[k], r[x]);
                add(r[k], r[k]);
            }
        }
        add(0x101C, 0x101C); add(0x101E, 0x101E); add(0x101F, 0x1019); add(0x101A, 0x101A); add(0x1020, 0x1020);
        return set;
    }();
    return pairs.count((unsigned long long)upper << 32 | lower) != 0;
}

}  // namespace pyaw
