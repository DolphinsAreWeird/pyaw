#include "lexicon.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "burmese.hpp"
#include "unicode.hpp"

namespace pyaw {

namespace {

// CharacterSet.whitespaces: tab plus the Unicode space separators (Zs).
bool isHorizontalSpace(char32_t c) {
    return c == 0x09 || c == 0x20 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F ||
           c == 0x205F || c == 0x3000;
}

std::u32string trim(const std::u32string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isHorizontalSpace(s[a])) ++a;
    while (b > a && isHorizontalSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::vector<std::u32string> splitLines(const std::string& text) {
    std::vector<std::u32string> lines;
    std::u32string cur;
    for (char32_t c : utf8_to_u32(text)) {
        if (c == '\n' || c == '\r' || c == 0x0B || c == 0x0C || c == 0x85 || c == 0x2028 || c == 0x2029) {
            if (!cur.empty()) lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

std::vector<std::u32string> splitTabs(const std::u32string& s) {
    std::vector<std::u32string> parts;
    std::u32string cur;
    for (char32_t c : s) {
        if (c == '\t') { if (!cur.empty()) parts.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

bool parseFloat(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    float v = std::strtof(s.c_str(), &end);
    if (end != s.c_str() + s.size()) return false;
    out = v;
    return true;
}

}  // namespace

std::string Lexicon::normalizeKey(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || c == '\'' || c == ':') out.push_back(static_cast<char>(c));
    }
    return out;
}

void Lexicon::add(const std::string& roman, const std::string& burmese, float cost) {
    std::string key = normalizeKey(roman);
    std::u32string cps;
    for (char32_t c : normalize_burmese(utf8_to_u32(burmese))) if (!is_unicode_whitespace(c)) cps.push_back(c);
    std::string text = u32_to_utf8(cps);
    if (key.empty() || text.empty()) return;
    auto& list = entries_[key];
    auto it = std::find_if(list.begin(), list.end(), [&](const LexEntry& e) { return e.burmese == text; });
    if (it != list.end()) { if (cost < it->cost) it->cost = cost; }
    else list.push_back({text, cost});
    maxKeyLength_ = std::max(maxKeyLength_, key.size());
}

const std::vector<LexEntry>& Lexicon::lookup(const std::string& key) const {
    static const std::vector<LexEntry> empty;
    auto it = entries_.find(key);
    return it == entries_.end() ? empty : it->second;
}

size_t Lexicon::size() const {
    size_t n = 0;
    for (auto& kv : entries_) n += kv.second.size();
    return n;
}

void Lexicon::loadTSV(const std::string& text, float defaultCost) {
    for (const auto& raw : splitLines(text)) {
        std::u32string line = trim(raw);
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::u32string> parts;
        for (auto& p : splitTabs(line)) parts.push_back(trim(p));
        if (parts.size() < 2) {
            size_t k = 0;
            while (k < line.size() && line[k] < 0x80 && !is_unicode_whitespace(line[k])) ++k;
            parts = {line.substr(0, k), trim(line.substr(k))};
        }
        if (parts.size() < 2 || parts[0].empty() || parts[1].empty()) continue;
        float cost = defaultCost;
        if (parts.size() >= 3) {
            float v;
            if (parseFloat(u32_to_utf8(parts[2]), v)) cost = v;
        }
        add(u32_to_utf8(parts[0]), u32_to_utf8(parts[1]), cost);
    }
}

void LearnedStore::record(const std::string& key, const std::string& burmese, float weight) {
    if (key.empty() || burmese.empty() || key.find(' ') != std::string::npos) return;
    auto& forKey = weights_[key];
    forKey[burmese] += weight;
    for (auto& kv : forKey) if (kv.first != burmese) kv.second = kv.second * 0.8f;
    for (auto it = forKey.begin(); it != forKey.end();) {
        if (it->second < 0.05f) it = forKey.erase(it); else ++it;
    }
    maxKeyLength_ = std::max(maxKeyLength_, key.size());
}

std::vector<std::pair<std::string, float>> LearnedStore::entries(const std::string& key) const {
    std::vector<std::pair<std::string, float>> out;
    auto it = weights_.find(key);
    if (it != weights_.end()) for (auto& kv : it->second) out.emplace_back(kv.first, kv.second);
    return out;
}

size_t LearnedStore::count() const {
    size_t n = 0;
    for (auto& kv : weights_) n += kv.second.size();
    return n;
}

std::string LearnedStore::serialized() const {
    std::vector<std::string> keys;
    for (auto& kv : weights_) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    std::string out;
    char buf[32];
    for (auto& key : keys) {
        std::vector<std::pair<std::string, float>> items(weights_.at(key).begin(), weights_.at(key).end());
        std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (auto& it : items) {
            std::snprintf(buf, sizeof buf, "%.2f", it.second);
            out += key + "\t" + it.first + "\t" + buf + "\n";
        }
    }
    return out.empty() ? "\n" : out;
}

LearnedStore LearnedStore::parse(const std::string& text) {
    LearnedStore store;
    for (const auto& line : splitLines(text)) {
        std::vector<std::u32string> parts = splitTabs(line);
        float w;
        if (parts.size() != 3 || !parseFloat(u32_to_utf8(parts[2]), w)) continue;
        std::string key = u32_to_utf8(parts[0]);
        store.weights_[key][u32_to_utf8(parts[1])] = w;
        store.maxKeyLength_ = std::max(store.maxKeyLength_, key.size());
    }
    return store;
}

}  // namespace pyaw
