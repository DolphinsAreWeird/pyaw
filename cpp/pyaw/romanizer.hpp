// Myanglish spellings for a Burmese syllable, with typicality costs.
// A C++ port of Sources/PyawCore/Romanizer.swift; the tables must stay identical.
#pragma once

#include <string>
#include <vector>

#include "syllable.hpp"

namespace pyaw {

struct RomanVariant {
    std::string text;
    float cost;
};

std::vector<RomanVariant> romanize(const Syllable& syl);
std::vector<RomanVariant> romanize(const std::u32string& syllableText);

}  // namespace pyaw
