#include "composer.hpp"

#include <algorithm>

#include "unicode.hpp"

namespace pyaw {

namespace {

bool isAsciiLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

std::string trimSpaces(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

}  // namespace

std::string Composer::preview() const {
    if (!hasResult_) return raw_;
    std::string t = result_.text();
    return t.empty() ? raw_ : t;
}

int Composer::pageCount() const {
    int n = static_cast<int>(candidates_.size());
    return std::max(1, (n + settings.pageSize - 1) / settings.pageSize);
}

std::vector<Candidate> Composer::pageCandidates() const {
    int n = static_cast<int>(candidates_.size());
    int lo = std::min(page() * settings.pageSize, n), hi = std::min(lo + settings.pageSize, n);
    return std::vector<Candidate>(candidates_.begin() + lo, candidates_.begin() + hi);
}

std::vector<std::pair<int, int>> Composer::outputRangesUtf16() const {
    std::vector<std::pair<int, int>> ranges;
    if (!hasResult_) return ranges;
    int pos = 0;
    for (auto& o : result_.outputs) {
        int n = static_cast<int>(utf16_length(o));
        ranges.emplace_back(pos, pos + n);
        pos += n;
    }
    return ranges;
}

void Composer::reset() {
    raw_.clear();
    result_ = DecodeResult();
    hasResult_ = false;
    candidates_.clear();
    pins_.clear();
    focusedSegment_ = 0;
    highlighted_ = 0;
    focusIsExplicit_ = false;
    listMode_ = ListMode::Word;
    phraseOptions_.clear();
    phrasePin_ = -1;
}

Composer::Output Composer::handle(const ComposerKey& key) {
    if (!isComposing()) return handleIdle(key);
    if (listMode_ == ListMode::Phrase) {
        Output out;
        if (handlePhraseMode(key, out)) return out;
    }
    switch (key.kind) {
        case ComposerKey::Letter:
            raw_ += u32_to_utf8(key.ch);
            focusIsExplicit_ = false;
            refresh();
            break;
        case ComposerKey::ToneMark:
            if (!raw_.empty() && isAsciiLetter(raw_.back())) { raw_ += u32_to_utf8(key.ch); refresh(); }
            break;
        case ComposerKey::Space:
            if (settings.spaceMode == SpaceMode::Commit) return commit();
            if (!raw_.empty() && raw_.back() == ' ') { Output out = commit(); out.commit += " "; return out; }
            raw_ += ' ';
            focusIsExplicit_ = false;
            refresh();
            break;
        case ComposerKey::Enter: {
            Output out = commit();
            if (settings.returnAlsoSends) out.handled = false;
            return out;
        }
        case ComposerKey::RawEnter: {
            std::string text = trimSpaces(raw_);
            reset();
            lastCommitWasBurmese_ = false;
            context.clear();
            return Output{text, true};
        }
        case ComposerKey::Backspace:
            raw_.pop_back();
            if (raw_.empty()) reset();
            else { focusIsExplicit_ = false; refresh(); }
            break;
        case ComposerKey::Escape: reset(); break;
        case ComposerKey::Up: moveHighlight(-1); break;
        case ComposerKey::Down: moveHighlight(1); break;
        case ComposerKey::PageUp: moveHighlight(-settings.pageSize); break;
        case ComposerKey::PageDown: moveHighlight(settings.pageSize); break;
        case ComposerKey::Left: moveFocus(-1); break;
        case ComposerKey::Right: moveFocus(1); break;
        case ComposerKey::Tab:
        case ComposerKey::BackTab: {
            bool forward = key.kind == ComposerKey::Tab;
            if (hasResult_ && result_.segments.size() > 1) enterPhraseMode(forward ? 1 : -1);
            else moveHighlight(forward ? 1 : -1);
            break;
        }
        case ComposerKey::Digit: {
            if (key.digit < 1) return Output{};
            int i = page() * settings.pageSize + key.digit - 1;
            if (i >= static_cast<int>(candidates_.size())) return Output{};
            choose(i, true);
            break;
        }
        case ComposerKey::Punctuation: {
            Output out = commit();
            std::string p = punctuation(key.ch);
            out.commit += p.empty() ? u32_to_utf8(key.ch) : p;
            lastCommitWasBurmese_ = false;
            context.clear();
            return out;
        }
    }
    return Output{};
}

bool Composer::handlePhraseMode(const ComposerKey& key, Output& out) {
    out = Output{};
    switch (key.kind) {
        case ComposerKey::Tab: selectPhrase(highlighted_ + 1, true); return true;
        case ComposerKey::BackTab: selectPhrase(highlighted_ - 1, true); return true;
        case ComposerKey::Down: selectPhrase(highlighted_ + 1, false); return true;
        case ComposerKey::Up: selectPhrase(highlighted_ - 1, false); return true;
        case ComposerKey::PageDown: selectPhrase(highlighted_ + settings.pageSize, false); return true;
        case ComposerKey::PageUp: selectPhrase(highlighted_ - settings.pageSize, false); return true;
        case ComposerKey::Digit: {
            int i = page() * settings.pageSize + key.digit - 1;
            if (key.digit >= 1 && i < static_cast<int>(phraseOptions_.size())) selectPhrase(i, false);
            return true;
        }
        case ComposerKey::Escape:
            selectPhrase(0, false);
            leavePhraseMode();
            return true;
        default:
            leavePhraseMode();
            return false;
    }
}

void Composer::enterPhraseMode(int step) {
    const Engine* engine = engine_();
    if (!engine) return;
    auto options = engine->phraseOptions(raw_, context, pinTexts(true), settings.pageSize);
    if (options.size() <= 1) return;
    phraseOptions_ = std::move(options);
    listMode_ = ListMode::Phrase;
    phrasePin_ = -1;
    selectPhrase(step, true);
}

void Composer::selectPhrase(int index, bool wrap) {
    int n = static_cast<int>(phraseOptions_.size());
    if (n == 0 || !hasResult_) return;
    int i = wrap ? ((index % n) + n) % n : std::max(0, std::min(n - 1, index));
    const PhraseOption option = phraseOptions_[i];
    const DecodeResult r = result_;
    for (size_t k = 0; k < r.segments.size() && k < option.outputs.size(); ++k) {
        auto it = pins_.find(static_cast<int>(k));
        if (it != pins_.end() && it->second.isExplicit && static_cast<int>(k) != phrasePin_) continue;
        pins_[static_cast<int>(k)] = {r.segments[k].text, option.outputs[k], static_cast<int>(k) == option.segment};
    }
    phrasePin_ = option.segment;
    decode();
    candidates_.clear();
    for (auto& o : phraseOptions_) candidates_.push_back({o.text, {}, 0});
    highlighted_ = i;
}

void Composer::leavePhraseMode() {
    listMode_ = ListMode::Word;
    phraseOptions_.clear();
    phrasePin_ = -1;
    loadCandidates();
}

bool Composer::wouldHandle(const ComposerKey& key) const {
    if (isComposing()) return true;
    switch (key.kind) {
        case ComposerKey::Letter: return true;
        case ComposerKey::Punctuation: return lastCommitWasBurmese_ && !punctuation(key.ch).empty();
        case ComposerKey::Digit: return settings.burmeseDigits;
        default: return false;
    }
}

Composer::Output Composer::handleIdle(const ComposerKey& key) {
    switch (key.kind) {
        case ComposerKey::Letter:
            raw_ = u32_to_utf8(key.ch);
            focusIsExplicit_ = false;
            refresh();
            return Output{};
        case ComposerKey::Punctuation:
            if (lastCommitWasBurmese_) {
                std::string p = punctuation(key.ch);
                if (!p.empty()) {
                    lastCommitWasBurmese_ = false;
                    context.clear();
                    return Output{p, true};
                }
            }
            break;
        case ComposerKey::Digit:
            if (settings.burmeseDigits) {
                lastCommitWasBurmese_ = false;
                return Output{u32_to_utf8(static_cast<char32_t>(0x1040 + key.digit)), true};
            }
            break;
        default: break;
    }
    lastCommitWasBurmese_ = false;
    if (key.kind != ComposerKey::Space) context.clear();
    return Output{"", false};
}

std::string Composer::punctuation(char32_t c) const {
    if (!settings.burmesePunctuation) return "";
    if (c == '.') return "\xE1\x81\x8B";  // ။
    if (c == ',') return "\xE1\x81\x8A";  // ၊
    return "";
}

void Composer::moveHighlight(int delta) {
    if (candidates_.empty()) return;
    int i = std::max(0, std::min(static_cast<int>(candidates_.size()) - 1, highlighted_ + delta));
    if (i != highlighted_) choose(i, true, true);
}

void Composer::moveFocus(int delta) {
    if (!hasResult_ || result_.segments.empty()) return;
    int f = std::max(0, std::min(static_cast<int>(result_.segments.size()) - 1, focusedSegment_ + delta));
    if (f == focusedSegment_) return;
    focusedSegment_ = f;
    focusIsExplicit_ = true;
    loadCandidates();
}

void Composer::choose(int index, bool isExplicit, bool keepCandidates) {
    if (!hasResult_ || focusedSegment_ >= static_cast<int>(result_.segments.size()) ||
        index >= static_cast<int>(candidates_.size()))
        return;
    const DecodeResult r = result_;
    // The other words stay exactly as shown; only the picked one changes.
    for (size_t k = 0; k < r.segments.size(); ++k)
        if (!pins_.count(static_cast<int>(k))) pins_[static_cast<int>(k)] = {r.segments[k].text, r.outputs[k], false};
    pins_[focusedSegment_] = {r.segments[focusedSegment_].text, candidates_[index].text, isExplicit};
    highlighted_ = index;
    decode();
    if (!keepCandidates) loadCandidates();
}

void Composer::refresh() {
    for (auto it = pins_.begin(); it != pins_.end();) {
        if (!it->second.isExplicit) it = pins_.erase(it); else ++it;
    }
    decode();
    if (hasResult_ && (!focusIsExplicit_ || focusedSegment_ >= static_cast<int>(result_.segments.size())))
        focusedSegment_ = std::max(0, static_cast<int>(result_.segments.size()) - 1);
    loadCandidates();
}

Pins Composer::pinTexts(bool explicitOnly) const {
    Pins p;
    for (auto& kv : pins_) if (!explicitOnly || kv.second.isExplicit) p[kv.first] = kv.second.text;
    return p;
}

void Composer::decode() {
    const Engine* engine = engine_();
    if (!engine) { hasResult_ = false; return; }
    auto segs = Engine::segmentsOf(Engine::normalizeInput(raw_));
    for (auto it = pins_.begin(); it != pins_.end();) {
        bool valid = it->first < static_cast<int>(segs.size()) && segs[it->first].text == it->second.key;
        if (!valid) it = pins_.erase(it); else ++it;
    }
    result_ = engine->decode(raw_, context, pinTexts(false));
    hasResult_ = true;
}

void Composer::loadCandidates() {
    const Engine* engine = engine_();
    if (!engine || !hasResult_ || focusedSegment_ >= static_cast<int>(result_.segments.size())) {
        candidates_.clear();
        highlighted_ = 0;
        return;
    }
    auto list = engine->candidates(focusedSegment_, result_, settings.pageSize * 3);
    const std::string& current = result_.outputs[focusedSegment_];
    auto it = std::find_if(list.begin(), list.end(), [&](const Candidate& c) { return c.text == current; });
    if (it != list.end()) {
        highlighted_ = static_cast<int>(it - list.begin());
    } else {
        list.insert(list.begin(), Candidate{current, result_.tokens[focusedSegment_], 0});
        highlighted_ = 0;
    }
    candidates_ = std::move(list);
}

Composer::Output Composer::commit() {
    if (!hasResult_) {
        std::string text = trimSpaces(raw_);
        reset();
        return Output{text, true};
    }
    std::string text = result_.text();
    for (size_t i = 0; i < result_.segments.size(); ++i) {
        const std::string& out = result_.outputs[i];
        bool ascii = std::any_of(out.begin(), out.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
        if (out.empty() || ascii) continue;
        auto pin = pins_.find(static_cast<int>(i));
        if (onLearn) onLearn(result_.segments[i].text, out, pin != pins_.end() && pin->second.isExplicit);
    }
    auto all = result_.allTokens();
    context.assign(all.end() - std::min<size_t>(2, all.size()), all.end());
    lastCommitWasBurmese_ = !text.empty();
    reset();
    return Output{text, true};
}

}  // namespace pyaw
