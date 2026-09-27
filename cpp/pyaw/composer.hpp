// Typing-session state machine. A C++ port of Sources/PyawCore/Composer.swift.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "engine.hpp"

namespace pyaw {

struct ComposerKey {
    enum Kind {
        Letter, ToneMark, Space, Enter, RawEnter, Backspace, Escape,
        Up, Down, Left, Right, Tab, BackTab, PageUp, PageDown, Digit, Punctuation,
    };
    Kind kind;
    char32_t ch = 0;  // Letter / ToneMark / Punctuation
    int digit = 0;    // Digit

    static ComposerKey letter(char32_t c) { return {Letter, c, 0}; }
    static ComposerKey toneMark(char32_t c) { return {ToneMark, c, 0}; }
    static ComposerKey punctuation(char32_t c) { return {Punctuation, c, 0}; }
    static ComposerKey number(int d) { return {Digit, 0, d}; }
    static ComposerKey of(Kind k) { return {k, 0, 0}; }
};

enum class SpaceMode { Separate, Commit };

struct ComposerSettings {
    SpaceMode spaceMode = SpaceMode::Separate;
    bool burmesePunctuation = true;
    bool burmeseDigits = false;
    bool returnAlsoSends = false;
    int pageSize = 9;
};

class Composer {
public:
    struct Output {
        std::string commit;   // UTF-8 text to insert
        bool handled = true;  // false: pass the key on to the application
    };
    enum class ListMode { Word, Phrase };

    /// `engine` may return nullptr while the engine is still loading.
    explicit Composer(std::function<const Engine*()> engine) : engine_(std::move(engine)) {}

    ComposerSettings settings;
    std::vector<Token> context;
    /// (segment text, chosen Burmese, explicitly picked)
    std::function<void(const std::string&, const std::string&, bool)> onLearn;

    Output handle(const ComposerKey& key);
    /// Whether handle(key) would consume the key, without changing any state (for TSF's
    /// OnTestKeyDown). While composing every key is consumed.
    bool wouldHandle(const ComposerKey& key) const;
    void reset();

    bool isComposing() const { return !raw_.empty(); }
    const std::string& raw() const { return raw_; }
    std::string preview() const;
    const DecodeResult* result() const { return hasResult_ ? &result_ : nullptr; }
    const std::vector<Candidate>& candidates() const { return candidates_; }
    int focusedSegment() const { return focusedSegment_; }
    int highlighted() const { return highlighted_; }
    ListMode listMode() const { return listMode_; }
    const std::vector<PhraseOption>& phraseOptions() const { return phraseOptions_; }
    int page() const { return highlighted_ / settings.pageSize; }
    int pageCount() const;
    std::vector<Candidate> pageCandidates() const;
    /// [start, end) of each segment's output within preview(), in UTF-16 code units.
    std::vector<std::pair<int, int>> outputRangesUtf16() const;

private:
    struct Pin { std::string key, text; bool isExplicit; };

    Output handleIdle(const ComposerKey& key);
    bool handlePhraseMode(const ComposerKey& key, Output& out);
    std::string punctuation(char32_t c) const;
    void moveHighlight(int delta);
    void moveFocus(int delta);
    void choose(int index, bool isExplicit, bool keepCandidates = false);
    void refresh();
    void decode();
    void loadCandidates();
    void enterPhraseMode(int step);
    void selectPhrase(int index, bool wrap);
    void leavePhraseMode();
    Output commit();
    Pins pinTexts(bool explicitOnly) const;

    std::function<const Engine*()> engine_;
    std::string raw_;
    DecodeResult result_;
    bool hasResult_ = false;
    std::vector<Candidate> candidates_;
    int focusedSegment_ = 0;
    int highlighted_ = 0;
    ListMode listMode_ = ListMode::Word;
    std::vector<PhraseOption> phraseOptions_;
    std::map<int, Pin> pins_;
    bool focusIsExplicit_ = false;
    bool lastCommitWasBurmese_ = false;
    int phrasePin_ = -1;
};

}  // namespace pyaw
