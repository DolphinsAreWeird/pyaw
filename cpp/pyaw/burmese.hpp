// Burmese Unicode handling: character classes, normalization, syllable splitting.
// A C++ port of Sources/PyawCore/Burmese.swift; behaviour must match it exactly.
#pragma once

#include <string>
#include <vector>

namespace pyaw {
namespace mm {

constexpr char32_t eVowel = 0x1031, iVowel = 0x102D, iiVowel = 0x102E, uVowel = 0x102F, uuVowel = 0x1030,
                   aiVowel = 0x1032, tallAa = 0x102B, aa = 0x102C, anusvara = 0x1036, dotBelow = 0x1037,
                   visarga = 0x1038, virama = 0x1039, asat = 0x103A, medialYa = 0x103B, medialRa = 0x103C,
                   medialWa = 0x103D, medialHa = 0x103E, greatSa = 0x103F, letterA = 0x1021, nga = 0x1004;

inline bool isConsonant(char32_t c) { return c >= 0x1000 && c <= 0x1021; }
inline bool isIndependentVowel(char32_t c) { return c >= 0x1023 && c <= 0x102A && c != 0x1028; }
inline bool isSymbolSyllable(char32_t c) { return c >= 0x104C && c <= 0x104F; }
inline bool isMark(char32_t c) { return c >= 0x102B && c <= 0x103E && c != virama; }
inline bool isStarter(char32_t c) { return isConsonant(c) || isIndependentVowel(c) || c == greatSa || isSymbolSyllable(c); }
inline bool isDigit(char32_t c) { return c >= 0x1040 && c <= 0x1049; }

}  // namespace mm

/// NFC (as far as Burmese needs it), invisible characters removed, look-alike fixes and
/// canonical mark order. Mirrors BurmeseText.normalize.
std::u32string normalize_burmese(const std::u32string& input);
std::string normalize_burmese(const std::string& utf8);

/// Runs of syllables; stacked consonants are split at the virama. Spaces keep a run going.
std::vector<std::vector<std::u32string>> syllable_runs(const std::u32string& text);

/// Normalized syllables of a UTF-8 string, flattened (non-Burmese characters dropped).
std::vector<std::string> syllables(const std::string& utf8);

/// Whether `lower` may be stacked under a syllable whose coda is `upper`.
bool is_plausible_stack(char32_t upper, char32_t lower);

}  // namespace pyaw
