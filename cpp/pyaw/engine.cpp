#include "engine.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

#include "burmese.hpp"
#include "romanizer.hpp"
#include "syllable.hpp"
#include "unicode.hpp"

namespace pyaw {

std::string DecodeResult::text() const {
    std::string s;
    for (auto& o : outputs) s += o;
    return s;
}

std::vector<Token> DecodeResult::allTokens() const {
    std::vector<Token> all;
    for (auto& t : tokens) all.insert(all.end(), t.begin(), t.end());
    return all;
}

namespace {

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool isToneMark(char c) { return c == '\'' || c == ':'; }

struct State {
    float score;
    Token u, v;
    int node;
};

struct Node {
    int edge, parent;
};

void contextPair(const std::vector<Token>& context, Token& u, Token& v) {
    const Token bos = LanguageModel::bos;
    if (context.empty()) { u = bos; v = bos; }
    else if (context.size() == 1) { u = bos; v = context[0]; }
    else { u = context[context.size() - 2]; v = context[context.size() - 1]; }
}

}  // namespace

std::unique_ptr<Engine> Engine::load(const std::string& resourceDir) {
    std::string dir = resourceDir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
    auto lm = LanguageModel::load(dir + "model.bin");
    if (!lm) return nullptr;
    Lexicon lex;
    std::string text;
    if (readFile(dir + "lexicon.tsv", text)) lex.loadTSV(text, 0.4f);
    return std::unique_ptr<Engine>(new Engine(std::move(lm), std::move(lex)));
}

Engine::Engine(std::unique_ptr<LanguageModel> model, Lexicon lex) : lexicon(std::move(lex)), lm_(std::move(model)) {
    const size_t V = lm_->vocabSize();
    highTone_.assign(V, 0);
    creakyTone_.assign(V, 0);
    stackCoda_.assign(V, 0);
    onsetOf_.assign(V, 0);
    for (size_t i = LanguageModel::unk + 1; i < V; ++i) {
        std::u32string syl = utf8_to_u32(lm_->vocab()[i]);
        auto parsed = parse_syllable(syl);
        if (!parsed) continue;
        highTone_[i] = parsed->tone == Tone::high;
        creakyTone_[i] = parsed->tone == Tone::creaky;
        onsetOf_[i] = syl.empty() ? 0 : syl[0];
        if (parsed->stacked) stackCoda_[i] = parsed->isKinzi ? 1 : parsed->finalConsonant;
        for (auto& v : romanize(*parsed)) {
            romanIndex_[v.text].push_back({static_cast<Token>(i), v.cost});
            maxRomanLength_ = std::max(maxRomanLength_, v.text.size());
        }
    }
    for (auto& kv : romanIndex_)
        std::stable_sort(kv.second.begin(), kv.second.end(), [](const Hit& a, const Hit& b) { return a.cost < b.cost; });
}

std::vector<Token> Engine::tokensFor(const std::string& burmese) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = tokenCache_.find(burmese);
    if (it != tokenCache_.end()) return it->second;
    std::vector<Token> t;
    for (auto& syl : syllables(burmese)) {
        int tok = lm_->token(syl);
        t.push_back(tok < 0 ? LanguageModel::unk : static_cast<Token>(tok));
    }
    tokenCache_[burmese] = t;
    return t;
}

std::vector<std::pair<std::string, float>> Engine::romanHits(const std::string& key) const {
    std::vector<std::pair<std::string, float>> out;
    auto it = romanIndex_.find(key);
    if (it != romanIndex_.end()) for (auto& h : it->second) out.emplace_back(lm_->vocab()[h.token], h.cost);
    return out;
}

std::string Engine::normalizeInput(const std::string& input) {
    std::string out;
    for (unsigned char b : input) {
        if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b - 'A' + 'a');
        bool isLetter = b >= 'a' && b <= 'z';
        if (isLetter || isToneMark(static_cast<char>(b))) {
            if (isLetter && out.size() >= 2 && out[out.size() - 1] == static_cast<char>(b) && out[out.size() - 2] == static_cast<char>(b))
                continue;
            out.push_back(static_cast<char>(b));
        } else if (b == ' ' || b == '-' || b == '_') {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::vector<Segment> Engine::segmentsOf(const std::string& chars) {
    std::vector<Segment> result;
    int start = 0;
    const int n = static_cast<int>(chars.size());
    for (int i = 0; i <= n; ++i) {
        if (i == n || chars[i] == ' ') {
            if (i > start) result.push_back({start, i, chars.substr(start, i - start)});
            start = i + 1;
        }
    }
    return result;
}

void Engine::buildLattice(const std::string& chars, const std::vector<Segment>& segments, const Pins& pins,
                          std::vector<Edge>& edges, std::vector<std::vector<int>>& from) const {
    edges.clear();
    from.assign(chars.size() + 1, {});
    auto add = [&](Edge e) { from[e.start].push_back(static_cast<int>(edges.size())); edges.push_back(std::move(e)); };
    const size_t maxLen = std::max({maxRomanLength_, lexicon.maxKeyLength(), userLexicon.maxKeyLength(), learned.maxKeyLength()});

    auto toneAdjusted = [&](float cost, const std::vector<Token>& toks, char mark) -> float {
        if (toks.empty() || toks.back() >= highTone_.size()) return cost + 1.0f;
        Token t = toks.back();
        bool ok = mark == ':' ? highTone_[t] != 0 : creakyTone_[t] != 0;
        return ok ? cost - 0.3f : cost + 6.0f;
    };

    for (size_t si = 0; si < segments.size(); ++si) {
        const int lo = segments[si].start, hi = segments[si].end;
        auto pin = pins.find(static_cast<int>(si));
        if (pin != pins.end()) {
            add({lo, hi, pin->second, tokensFor(pin->second), -2.0f, {}});
            continue;
        }
        for (int p = lo; p < hi; ++p) {
            if (isToneMark(chars[p])) {
                add({p, p + 1, "", {}, 6.0f, {}});  // stray tone mark
                continue;
            }
            std::string key;
            int q = p;
            while (q < hi && static_cast<size_t>(q - p) < maxLen && !isToneMark(chars[q])) {
                key.push_back(chars[q]);
                ++q;
                char mark = (q < hi && isToneMark(chars[q])) ? chars[q] : 0;
                auto emit = [&](const std::string& text, const std::vector<Token>& toks, float cost) {
                    add({p, q, text, toks, cost, {}});
                    if (mark) add({p, q + 1, text, toks, toneAdjusted(cost, toks, mark), {}});
                };
                auto hits = romanIndex_.find(key);
                if (hits != romanIndex_.end() && !hits->second.empty()) {
                    const float limit = hits->second[0].cost + 2.5f;
                    size_t count = std::min(hits->second.size(), static_cast<size_t>(config.maxHitsPerKey));
                    for (size_t h = 0; h < count; ++h) {
                        const Hit& hit = hits->second[h];
                        if (hit.cost <= limit) emit(lm_->vocab()[hit.token], {hit.token}, hit.cost);
                    }
                }
                for (auto& e : lexicon.lookup(key)) emit(e.burmese, tokensFor(e.burmese), e.cost);
                for (auto& e : userLexicon.lookup(key)) emit(e.burmese, tokensFor(e.burmese), e.cost - 0.5f);
                for (auto& lw : learned.entries(key))
                    emit(lw.first, tokensFor(lw.first), std::max(-1.0f, 0.3f - 0.5f * std::log(1.0f + lw.second)));
            }
            // Repeated trailing letters are emphasis ("lrrr", "nawww").
            if (p > lo && chars[p] == chars[p - 1]) {
                bool allSame = true;
                for (int k = p; k < hi; ++k) if (chars[k] != chars[p]) { allSame = false; break; }
                if (allSame) add({p, hi, "", {}, 1.2f, {}});
            }
            // Fallback so decoding never dead-ends: keep the letter as typed.
            add({p, p + 1, std::string(1, chars[p]), {LanguageModel::unk}, 7.0f, {}});
        }
    }
    // Dictionary words typed with spaces between their syllables ("kya naw").
    for (size_t si = 0; si < segments.size(); ++si) {
        if (pins.count(static_cast<int>(si))) continue;
        std::string key = segments[si].text;
        const size_t lastSeg = std::min(segments.size(), si + 6);
        for (size_t sj = si + 1; sj < lastSeg; ++sj) {
            if (pins.count(static_cast<int>(sj))) break;
            key += segments[sj].text;
            if (key.find_first_of("':") != std::string::npos) break;
            std::vector<std::pair<std::string, float>> found;
            for (auto& e : lexicon.lookup(key)) found.emplace_back(e.burmese, e.cost);
            for (auto& e : userLexicon.lookup(key)) found.emplace_back(e.burmese, e.cost - 0.5f);
            for (auto& f : found) {
                std::vector<Token> toks = tokensFor(f.first);
                std::vector<std::string> syls = syllables(f.first);
                const size_t count = sj - si + 1;
                if (syls.size() < count) continue;  // a typed space is a syllable break
                Edge e{segments[si].start, segments[sj].end, f.first, toks, f.second, {}};
                if (syls.size() == count && toks.size() == count)
                    for (size_t k = 0; k < count; ++k) e.pieces.push_back({syls[k], {toks[k]}});
                add(std::move(e));
            }
        }
    }
}

float Engine::joinCost(Token v, Token t) const {
    char32_t coda = v < stackCoda_.size() ? stackCoda_[v] : 0;
    if (coda == 0) return 0;
    char32_t onset = t < onsetOf_.size() ? onsetOf_[t] : 0;
    if (coda == 1) return mm::isConsonant(onset) ? 0.0f : 20.0f;
    return is_plausible_stack(coda, onset) ? 0.0f : 20.0f;
}

float Engine::endCost(Token u, Token v) const {
    float c = 0;
    if (v < stackCoda_.size() && stackCoda_[v] != 0) c += 20;
    if (config.endWeight > 0) c -= config.endWeight * lm_->logProb(LanguageModel::eos, u, v);
    return c;
}

DecodeResult Engine::decode(const std::string& input, const std::vector<Token>& context, const Pins& pins) const {
    DecodeResult r;
    const std::string chars = normalizeInput(input);
    r.normalized = chars;
    r.segments = segmentsOf(chars);
    r.pins = pins;
    contextPair(context, r.startU, r.startV);
    if (r.segments.empty()) return r;

    std::vector<Edge> edges;
    std::vector<std::vector<int>> from;
    buildLattice(chars, r.segments, pins, edges, from);
    const int n = static_cast<int>(chars.size());
    std::vector<std::vector<State>> beams(n + 1);
    std::vector<std::unordered_map<uint32_t, int>> slots(n + 1);
    std::vector<Node> nodes;
    std::unordered_map<uint64_t, float> lmCache;

    auto lmCost = [&](Token t, Token u, Token v) {
        uint64_t key = static_cast<uint64_t>(u) << 32 | static_cast<uint64_t>(v) << 16 | t;
        auto it = lmCache.find(key);
        if (it != lmCache.end()) return it->second;
        float c = -lm_->logProb(t, u, v);
        lmCache.emplace(key, c);
        return c;
    };
    auto push = [&](const State& s, int pos) {
        uint32_t key = static_cast<uint32_t>(s.u) << 16 | s.v;
        auto it = slots[pos].find(key);
        if (it != slots[pos].end()) {
            if (s.score < beams[pos][it->second].score) beams[pos][it->second] = s;
        } else {
            slots[pos].emplace(key, static_cast<int>(beams[pos].size()));
            beams[pos].push_back(s);
        }
    };

    push({0.0f, r.startU, r.startV, -1}, 0);
    for (int p = 0; p < n; ++p) {
        if (beams[p].empty()) continue;
        std::vector<State> states = beams[p];
        if (static_cast<int>(states.size()) > config.beamWidth) {
            std::stable_sort(states.begin(), states.end(), [](const State& a, const State& b) { return a.score < b.score; });
            states.resize(config.beamWidth);
        }
        if (chars[p] == ' ') {
            for (auto& s : states) push(s, p + 1);
            continue;
        }
        for (int ei : from[p]) {
            const Edge& e = edges[ei];
            for (auto& s : states) {
                float score = s.score + config.matchWeight * e.cost + config.edgePenalty;
                Token u = s.u, v = s.v;
                for (Token t : e.tokens) { score += lmCost(t, u, v) + joinCost(v, t); u = v; v = t; }
                nodes.push_back({ei, s.node});
                push({score, u, v, static_cast<int>(nodes.size()) - 1}, e.end);
            }
        }
    }
    for (auto& s : beams[n]) s.score += endCost(s.u, s.v);
    if (beams[n].empty()) {
        for (auto& seg : r.segments) { r.outputs.push_back(seg.text); r.tokens.push_back({}); }
        r.score = std::numeric_limits<float>::infinity();
        return r;
    }
    const State best = *std::min_element(beams[n].begin(), beams[n].end(),
                                         [](const State& a, const State& b) { return a.score < b.score; });
    std::vector<const Edge*> path;
    for (int node = best.node; node >= 0; node = nodes[node].parent) path.push_back(&edges[nodes[node].edge]);
    std::reverse(path.begin(), path.end());

    r.outputs.assign(r.segments.size(), "");
    r.tokens.assign(r.segments.size(), {});
    size_t si = 0;
    for (const Edge* e : path) {
        while (si < r.segments.size() && e->start >= r.segments[si].end) ++si;
        if (si >= r.segments.size()) break;
        if (!e->pieces.empty()) {
            for (size_t k = 0; k < e->pieces.size() && si + k < r.segments.size(); ++k) {
                r.outputs[si + k] += e->pieces[k].text;
                r.tokens[si + k].insert(r.tokens[si + k].end(), e->pieces[k].tokens.begin(), e->pieces[k].tokens.end());
            }
        } else {
            r.outputs[si] += e->text;
            r.tokens[si].insert(r.tokens[si].end(), e->tokens.begin(), e->tokens.end());
        }
    }
    r.score = best.score;
    return r;
}

std::vector<Candidate> Engine::candidates(int segmentIndex, const DecodeResult& result, int limit) const {
    if (segmentIndex < 0 || segmentIndex >= static_cast<int>(result.segments.size())) return {};
    const std::string& chars = result.normalized;
    Pins pins = result.pins;
    pins.erase(segmentIndex);
    std::vector<Edge> edges;
    std::vector<std::vector<int>> from;
    buildLattice(chars, result.segments, pins, edges, from);
    const Segment& seg = result.segments[segmentIndex];

    Token u0 = result.startU, v0 = result.startV;
    for (int k = 0; k < segmentIndex; ++k) for (Token t : result.tokens[k]) { u0 = v0; v0 = t; }
    std::vector<Token> right;
    for (size_t k = segmentIndex + 1; k < result.tokens.size() && right.size() < 2; ++k)
        for (Token t : result.tokens[k]) { if (right.size() < 2) right.push_back(t); }

    struct Partial { float score; Token u, v; std::string text; std::vector<Token> tokens; };
    // Partials per position, keyed by text, in insertion order.
    struct Bucket {
        std::vector<Partial> items;
        std::unordered_map<std::string, size_t> index;
    };
    std::vector<Bucket> at(chars.size() + 1);
    auto put = [&](int pos, Partial p) {
        Bucket& b = at[pos];
        auto it = b.index.find(p.text);
        if (it != b.index.end()) {
            if (b.items[it->second].score <= p.score) return;
            b.items[it->second] = std::move(p);
        } else {
            b.index.emplace(p.text, b.items.size());
            b.items.push_back(std::move(p));
        }
    };
    put(seg.start, {0.0f, u0, v0, "", {}});
    for (int p = seg.start; p < seg.end; ++p) {
        if (at[p].items.empty()) continue;
        std::vector<Partial> partials = at[p].items;
        std::stable_sort(partials.begin(), partials.end(), [](const Partial& a, const Partial& b) { return a.score < b.score; });
        if (static_cast<int>(partials.size()) > config.candidateBeam) partials.resize(config.candidateBeam);
        for (int ei : from[p]) {
            const Edge& e = edges[ei];
            if (e.end > seg.end) continue;
            for (auto& s : partials) {
                float score = s.score + config.matchWeight * e.cost + config.edgePenalty;
                Token u = s.u, v = s.v;
                for (Token t : e.tokens) { score += -lm_->logProb(t, u, v) + joinCost(v, t); u = v; v = t; }
                Partial np{score, u, v, s.text + e.text, s.tokens};
                np.tokens.insert(np.tokens.end(), e.tokens.begin(), e.tokens.end());
                put(e.end, std::move(np));
            }
        }
    }
    std::vector<Candidate> finals;
    for (auto& s : at[seg.end].items) {
        if (s.text.empty()) continue;
        float score = s.score;
        Token u = s.u, v = s.v;
        if (right.empty()) {
            score += endCost(u, v);
        } else {
            for (Token t : right) { score += -lm_->logProb(t, u, v) + joinCost(v, t); u = v; v = t; }
        }
        finals.push_back({s.text, s.tokens, score});
    }
    std::stable_sort(finals.begin(), finals.end(), [](const Candidate& a, const Candidate& b) { return a.score < b.score; });
    if (finals.empty()) return {};
    const float window = finals[0].score + config.candidateWindow;
    std::vector<Candidate> kept;
    for (size_t i = 0; i < finals.size() && static_cast<int>(i) < limit; ++i)
        if (finals[i].score <= window) kept.push_back(finals[i]);
    if (kept.size() < 3) kept.assign(finals.begin(), finals.begin() + std::min(finals.size(), static_cast<size_t>(std::min(3, limit))));
    return kept;
}

std::vector<PhraseOption> Engine::phraseOptions(const std::string& input, const std::vector<Token>& context,
                                                const Pins& pins, int limit) const {
    DecodeResult best = decode(input, context, pins);
    if (best.segments.empty()) return {};
    struct Proposal { int segment; std::string word; float delta; };
    std::vector<Proposal> proposals;
    for (int k = 0; k < static_cast<int>(best.segments.size()); ++k) {
        if (pins.count(k)) continue;
        auto cands = candidates(k, best, 5);
        const Candidate* base = nullptr;
        for (auto& c : cands) if (c.text == best.outputs[k]) { base = &c; break; }
        if (!base && !cands.empty()) base = &cands[0];
        if (!base) continue;
        const float baseScore = base->score;
        for (auto& c : cands) if (c.text != best.outputs[k]) proposals.push_back({k, c.text, c.score - baseScore});
    }
    std::stable_sort(proposals.begin(), proposals.end(), [](const Proposal& a, const Proposal& b) { return a.delta < b.delta; });
    std::vector<Proposal> ordered, rest;
    std::set<int> covered;
    for (auto& p : proposals) { if (covered.insert(p.segment).second) ordered.push_back(p); else rest.push_back(p); }
    ordered.insert(ordered.end(), rest.begin(), rest.end());

    std::vector<PhraseOption> options{{best.text(), best.outputs, -1, ""}};
    std::set<std::string> seen{best.text()};
    auto offer = [&](const std::vector<std::string>& outputs, const Proposal& p) {
        std::string text;
        for (auto& o : outputs) text += o;
        if (static_cast<int>(options.size()) < limit && seen.insert(text).second)
            options.push_back({text, outputs, p.segment, p.word});
    };
    const size_t tries = std::min(ordered.size(), static_cast<size_t>(limit + 8));
    for (size_t i = 0; i < tries && static_cast<int>(options.size()) < limit; ++i) {
        const Proposal& p = ordered[i];
        std::vector<std::string> minimal = best.outputs;
        minimal[p.segment] = p.word;
        offer(minimal, p);
        Pins withPin = pins;
        withPin[p.segment] = p.word;
        offer(decode(input, context, withPin).outputs, p);
    }
    return options;
}

}  // namespace pyaw
