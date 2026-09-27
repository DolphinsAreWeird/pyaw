// Direct Myanglish → Burmese mappings and the store of learned choices.
// C++ ports of Sources/PyawCore/Lexicon.swift and LearnedStore.swift (same file formats).
#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pyaw {

struct LexEntry {
    std::string burmese;
    float cost;
};

class Lexicon {
public:
    /// Lowercase; keeps only a–z, ' and :.
    static std::string normalizeKey(const std::string& s);
    void add(const std::string& roman, const std::string& burmese, float cost);
    /// Entries for a key in insertion order (empty if none).
    const std::vector<LexEntry>& lookup(const std::string& key) const;
    /// `roman<TAB>burmese[<TAB>cost]` lines; `#` comments; a space may separate the first two.
    void loadTSV(const std::string& text, float defaultCost);
    size_t maxKeyLength() const { return maxKeyLength_; }
    size_t size() const;

private:
    std::unordered_map<std::string, std::vector<LexEntry>> entries_;
    size_t maxKeyLength_ = 0;
};

class LearnedStore {
public:
    void record(const std::string& key, const std::string& burmese, float weight);
    std::vector<std::pair<std::string, float>> entries(const std::string& key) const;
    void clear() { weights_.clear(); maxKeyLength_ = 0; }
    size_t count() const;
    size_t maxKeyLength() const { return maxKeyLength_; }
    std::string serialized() const;
    static LearnedStore parse(const std::string& text);

private:
    std::unordered_map<std::string, std::map<std::string, float>> weights_;
    size_t maxKeyLength_ = 0;
};

}  // namespace pyaw
