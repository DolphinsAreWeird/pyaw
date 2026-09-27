// Myanglish → Burmese decoder. A C++ port of Sources/PyawCore/Engine.swift; for the same model
// and input it must produce the same output.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "language_model.hpp"
#include "lexicon.hpp"

namespace pyaw {

struct Candidate {
    std::string text;
    std::vector<Token> tokens;
    float score = 0;
};

/// A space-separated piece of the typed input; [start, end) offsets into `normalized`.
struct Segment {
    int start = 0, end = 0;
    std::string text;
};

using Pins = std::map<int, std::string>;

struct DecodeResult {
    std::string normalized;
    std::vector<Segment> segments;
    std::vector<std::string> outputs;
    std::vector<std::vector<Token>> tokens;
    float score = 0;
    Token startU = LanguageModel::bos, startV = LanguageModel::bos;
    Pins pins;

    std::string text() const;
    std::vector<Token> allTokens() const;
};

struct PhraseOption {
    std::string text;
    std::vector<std::string> outputs;
    int segment = -1;  // the changed word (-1 for the best reading)
    std::string word;
};

struct EngineConfig {
    float matchWeight = 2.0f;
    int beamWidth = 20;
    int candidateBeam = 48;
    int maxHitsPerKey = 40;
    float endWeight = 1.0f;
    float candidateWindow = 12;
    float edgePenalty = 0.8f;
};

class Engine {
public:
    /// Loads model.bin and lexicon.tsv from a directory; nullptr on failure.
    static std::unique_ptr<Engine> load(const std::string& resourceDir);
    Engine(std::unique_ptr<LanguageModel> lm, Lexicon lexicon);

    EngineConfig config;
    Lexicon lexicon;
    Lexicon userLexicon;
    LearnedStore learned;

    const LanguageModel& lm() const { return *lm_; }
    std::vector<Token> tokensFor(const std::string& burmese) const;
    std::vector<std::pair<std::string, float>> romanHits(const std::string& key) const;

    /// Lowercase ASCII letters, tone marks and single spaces.
    static std::string normalizeInput(const std::string& input);
    static std::vector<Segment> segmentsOf(const std::string& chars);

    DecodeResult decode(const std::string& input, const std::vector<Token>& context = {}, const Pins& pins = {}) const;
    std::vector<Candidate> candidates(int segmentIndex, const DecodeResult& result, int limit = 18) const;
    std::vector<PhraseOption> phraseOptions(const std::string& input, const std::vector<Token>& context = {},
                                            const Pins& pins = {}, int limit = 6) const;

private:
    struct Hit { Token token; float cost; };
    struct Piece { std::string text; std::vector<Token> tokens; };
    struct Edge {
        int start, end;
        std::string text;
        std::vector<Token> tokens;
        float cost;
        std::vector<Piece> pieces;  // non-empty for multi-segment dictionary words split per segment
    };

    void buildLattice(const std::string& chars, const std::vector<Segment>& segments, const Pins& pins,
                      std::vector<Edge>& edges, std::vector<std::vector<int>>& from) const;
    float joinCost(Token v, Token t) const;
    float endCost(Token u, Token v) const;

    std::unique_ptr<LanguageModel> lm_;
    std::unordered_map<std::string, std::vector<Hit>> romanIndex_;
    size_t maxRomanLength_ = 0;
    std::vector<unsigned char> highTone_, creakyTone_;
    std::vector<char32_t> stackCoda_, onsetOf_;
    mutable std::unordered_map<std::string, std::vector<Token>> tokenCache_;
    mutable std::mutex cacheMutex_;
};

}  // namespace pyaw
