// UTF-8 / UTF-32 / UTF-16 conversion helpers.
#pragma once

#include <string>

namespace pyaw {

std::u32string utf8_to_u32(const std::string& s);
std::string u32_to_utf8(const std::u32string& s);
std::string u32_to_utf8(char32_t c);
std::u16string utf8_to_utf16(const std::string& s);
std::string utf16_to_utf8(const std::u16string& s);
/// Number of UTF-16 code units the UTF-8 string would take (for text ranges on Windows).
size_t utf16_length(const std::string& utf8);
/// Unicode White_Space characters (what Swift's Character.isWhitespace accepts).
bool is_unicode_whitespace(char32_t c);

}  // namespace pyaw
