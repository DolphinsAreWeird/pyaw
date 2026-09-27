// Parsing a Burmese syllable into onset, medials, rhyme and tone.
// A C++ port of Sources/PyawCore/Syllable.swift; behaviour must match it exactly.
#pragma once

#include <optional>
#include <string>

namespace pyaw {

enum class Rhyme : unsigned char {
    a, aa, i, ii, u, uu, e, ai, aw, o,
    an, at, et, in, it, ie, ein, eik, oun, ok, aing, aik, aung, auk,
    wa, waa, wi, we, wai, un, ut, wet, win, wit,
};

enum class Tone : unsigned char { low, high, creaky, checked };

struct Syllable {
    char32_t onset = 0;
    bool medialY = false, medialR = false, medialW = false, medialH = false;
    Rhyme rhyme = Rhyme::a;
    Tone tone = Tone::creaky;
    char32_t finalConsonant = 0;
    bool stacked = false;
    bool isKinzi = false;
    bool trailingL = false;
    /// Set for symbols and irregular spellings romanized from a fixed table.
    std::u32string symbol;
    bool hasSymbol = false;
};

std::optional<Syllable> parse_syllable(const std::u32string& text);

}  // namespace pyaw
