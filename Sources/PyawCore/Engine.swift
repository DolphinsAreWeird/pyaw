import Foundation

/// A suggestion for one typed segment.
public struct Candidate: Hashable, Sendable {
    public let text: String
    public let tokens: [LanguageModel.Token]
    public let score: Float
}

/// A space-separated piece of what the user typed.
public struct Segment: Hashable, Sendable {
    public let range: Range<Int>   // offsets into `DecodeResult.normalized`
    public let text: String
}

public struct DecodeResult: Sendable {
    public let normalized: String
    public let segments: [Segment]
    /// Best Burmese text for each segment.
    public let outputs: [String]
    public let tokens: [[LanguageModel.Token]]
    public let score: Float
    let startContext: (LanguageModel.Token, LanguageModel.Token)
    let pins: [Int: String]

    public var text: String { outputs.joined() }
    public var allTokens: [LanguageModel.Token] { tokens.flatMap { $0 } }
}

/// Converts Myanglish input into Burmese using the romanization tables, lexicons and the
/// syllable language model (noisy-channel decoding with a beam search).
public final class Engine: @unchecked Sendable {
    public typealias Token = LanguageModel.Token

    public struct Config: Sendable {
        /// Weight of spelling (match) costs relative to language-model costs.
        public var matchWeight: Float = 2.0
        public var beamWidth = 20
        public var candidateBeam = 48
        public var maxHitsPerKey = 40
        /// How strongly the end of the typed phrase is treated as a sentence end (0 = not at all).
        public var endWeight: Float = 1.0
        /// Candidates scoring this much worse than the best are not shown.
        public var candidateWindow: Float = 12
        /// Cost per lattice edge: prefers reading a typed word as fewer pieces.
        public var edgePenalty: Float = 0.8
        public init() {}
    }

    public let lm: LanguageModel
    public var config = Config()
    public var lexicon: Lexicon
    public var userLexicon = Lexicon()
    public var learned = LearnedStore()

    private struct Hit { let token: Token; let cost: Float }
    private var romanIndex: [String: [Hit]] = [:]
    private var maxRomanLength = 0
    private var tokenCache: [String: [Token]] = [:]
    private let cacheLock = NSLock()
    private var highTone: [Bool] = []
    private var creakyTone: [Bool] = []
    /// Coda consonant for tokens that stack onto the next syllable (0 = not stacked, 1 = kinzi).
    private var stackCoda: [UInt32] = []
    private var onsetOf: [UInt32] = []

    public init(model: LanguageModel, lexicon: Lexicon) {
        lm = model
        self.lexicon = lexicon
        var index: [String: [Hit]] = [:]
        highTone = [Bool](repeating: false, count: model.vocabSize)
        creakyTone = [Bool](repeating: false, count: model.vocabSize)
        stackCoda = [UInt32](repeating: 0, count: model.vocabSize)
        onsetOf = [UInt32](repeating: 0, count: model.vocabSize)
        for (i, syl) in model.vocab.enumerated() where i > Int(LanguageModel.unk) {
            guard let parsed = Syllable.parse(syl) else { continue }
            highTone[i] = parsed.tone == .high
            creakyTone[i] = parsed.tone == .creaky
            onsetOf[i] = syl.unicodeScalars.first?.value ?? 0
            if parsed.stacked { stackCoda[i] = parsed.isKinzi ? 1 : parsed.finalConsonant }
            for v in Romanizer.variants(of: parsed) {
                index[v.text, default: []].append(Hit(token: Token(i), cost: v.cost))
                maxRomanLength = max(maxRomanLength, v.text.count)
            }
        }
        for k in index.keys { index[k]!.sort { $0.cost < $1.cost } }
        romanIndex = index
    }

    public convenience init(resources: URL) throws {
        let model = try LanguageModel(contentsOf: resources.appendingPathComponent("model.bin"))
        var lex = Lexicon()
        if let text = try? String(contentsOf: resources.appendingPathComponent("lexicon.tsv"), encoding: .utf8) {
            lex.load(tsv: text, defaultCost: 0.4)
        }
        self.init(model: model, lexicon: lex)
    }

    /// LM tokens for a Burmese string (unknown syllables map to <unk>).
    public func tokens(for burmese: String) -> [Token] {
        cacheLock.lock(); defer { cacheLock.unlock() }
        if let t = tokenCache[burmese] { return t }
        let t = BurmeseText.syllables(burmese).map { lm.token($0) ?? LanguageModel.unk }
        tokenCache[burmese] = t
        return t
    }

    /// The romanization variants the engine knows for a syllable (for debugging/tests).
    public func romanHits(_ key: String) -> [(String, Float)] {
        (romanIndex[key] ?? []).map { (lm.vocab[Int($0.token)], $0.cost) }
    }

    // MARK: Input normalization

    static func isToneMark(_ c: UInt8) -> Bool { c == UInt8(ascii: "'") || c == UInt8(ascii: ":") }

    /// Lowercases, keeps letters/tone marks, collapses spaces and 3+ repeated letters.
    public static func normalizeInput(_ input: String) -> [UInt8] {
        var out: [UInt8] = []
        for b in input.lowercased().utf8 {
            let isLetter = b >= 0x61 && b <= 0x7A
            if isLetter || isToneMark(b) {
                if isLetter, out.count >= 2, out[out.count - 1] == b, out[out.count - 2] == b { continue }
                out.append(b)
            } else if b == 0x20 || b == UInt8(ascii: "-") || b == UInt8(ascii: "_") {
                if let last = out.last, last != 0x20 { out.append(0x20) }
            }
        }
        while out.last == 0x20 { out.removeLast() }
        return out
    }

    // MARK: Lattice

    private struct Edge {
        let start: Int
        let end: Int
        let text: String
        let tokens: [Token]
        let cost: Float
        /// For dictionary entries spanning several typed segments ("kya naw" → ကျွန်တော်):
        /// the output per segment, if it can be split syllable-for-syllable.
        var pieces: [(text: String, tokens: [Token])]? = nil
    }

    /// Longest run of segments a dictionary entry may cover.
    private let maxSegmentsPerEntry = 6

    private func buildLattice(_ chars: [UInt8], _ segments: [Segment], pins: [Int: String]) -> (edges: [Edge], from: [[Int]]) {
        var edges: [Edge] = []
        var from = [[Int]](repeating: [], count: chars.count + 1)
        func add(_ e: Edge) { from[e.start].append(edges.count); edges.append(e) }
        let maxLen = max(maxRomanLength, lexicon.maxKeyLength, userLexicon.maxKeyLength, learned.maxKeyLength)

        func toneAdjusted(_ cost: Float, lastToken: Token?, mark: UInt8) -> Float {
            guard let t = lastToken, Int(t) < highTone.count else { return cost + 1.0 }
            let ok = mark == UInt8(ascii: ":") ? highTone[Int(t)] : creakyTone[Int(t)]
            return ok ? cost - 0.3 : cost + 6.0
        }

        for (si, seg) in segments.enumerated() {
            let lo = seg.range.lowerBound, hi = seg.range.upperBound
            if let pinned = pins[si] {
                add(Edge(start: lo, end: hi, text: pinned, tokens: tokens(for: pinned), cost: -2))
                continue
            }
            for p in lo..<hi {
                if Self.isToneMark(chars[p]) {
                    add(Edge(start: p, end: p + 1, text: "", tokens: [], cost: 6.0))  // stray tone mark
                    continue
                }
                var key = ""
                var q = p
                while q < hi && q - p < maxLen && !Self.isToneMark(chars[q]) {
                    key.unicodeScalars.append(Unicode.Scalar(chars[q]))
                    q += 1
                    let mark: UInt8? = q < hi && Self.isToneMark(chars[q]) ? chars[q] : nil
                    func emit(_ text: String, _ toks: [Token], _ cost: Float) {
                        add(Edge(start: p, end: q, text: text, tokens: toks, cost: cost))
                        if let m = mark {
                            add(Edge(start: p, end: q + 1, text: text, tokens: toks,
                                     cost: toneAdjusted(cost, lastToken: toks.last, mark: m)))
                        }
                    }
                    if let hits = romanIndex[key] {
                        let limit = hits[0].cost + 2.5
                        for h in hits.prefix(config.maxHitsPerKey) where h.cost <= limit {
                            emit(lm.vocab[Int(h.token)], [h.token], h.cost)
                        }
                    }
                    for e in lexicon.lookup(key) { emit(e.burmese, tokens(for: e.burmese), e.cost) }
                    for e in userLexicon.lookup(key) { emit(e.burmese, tokens(for: e.burmese), e.cost - 0.5) }
                    for (text, weight) in learned.entries(for: key) {
                        emit(text, tokens(for: text), max(-1.0, 0.3 - 0.5 * log(1 + weight)))
                    }
                }
                // Repeated trailing letters are emphasis ("lrrr", "nawww").
                if p > lo, chars[p] == chars[p - 1], chars[p..<hi].allSatisfy({ $0 == chars[p] }) {
                    add(Edge(start: p, end: hi, text: "", tokens: [], cost: 1.2))
                }
                // Fallback so decoding never dead-ends: keep the letter as typed.
                add(Edge(start: p, end: p + 1, text: String(UnicodeScalar(chars[p])),
                         tokens: [LanguageModel.unk], cost: 7.0))
            }
        }
        // Dictionary words typed with spaces between their syllables ("kya naw", "ma nar lal bu").
        for si in segments.indices where pins[si] == nil {
            var key = segments[si].text
            for sj in (si + 1)..<min(segments.count, si + maxSegmentsPerEntry) {
                if pins[sj] != nil { break }
                key += segments[sj].text
                if key.contains(where: { $0 == "'" || $0 == ":" }) { break }
                var found: [(String, Float)] = lexicon.lookup(key).map { ($0.burmese, $0.cost) }
                found += userLexicon.lookup(key).map { ($0.burmese, $0.cost - 0.5) }
                for (text, cost) in found {
                    let toks = tokens(for: text)
                    let syls = BurmeseText.syllables(text)
                    let count = sj - si + 1
                    // Typing a space means a syllable break: "lo at" is not လုပ်.
                    if syls.count < count { continue }
                    var pieces: [(text: String, tokens: [Token])]? = nil
                    if syls.count == count && toks.count == count {
                        pieces = (0..<count).map { (syls[$0], [toks[$0]]) }
                    }
                    var e = Edge(start: segments[si].range.lowerBound, end: segments[sj].range.upperBound,
                                 text: text, tokens: toks, cost: cost)
                    e.pieces = pieces
                    add(e)
                }
            }
        }
        return (edges, from)
    }

    /// Extra cost for putting token `t` after `v`: a stacked coda (ကမ္ in ကမ္ဘာ) needs a
    /// matching consonant next.
    @inline(__always) private func joinCost(_ v: Token, _ t: Token) -> Float {
        let coda = Int(v) < stackCoda.count ? stackCoda[Int(v)] : 0
        if coda == 0 { return 0 }
        let onset = Int(t) < onsetOf.count ? onsetOf[Int(t)] : 0
        if coda == 1 { return MM.isConsonant(onset) ? 0 : 20 }
        return BurmeseText.isPlausibleStack(upper: coda, lower: onset) ? 0 : 20
    }

    /// Cost of ending the phrase after (u, v).
    @inline(__always) private func endCost(_ u: Token, _ v: Token) -> Float {
        var c: Float = 0
        if Int(v) < stackCoda.count, stackCoda[Int(v)] != 0 { c += 20 }  // dangling stack
        if config.endWeight > 0 { c -= config.endWeight * lm.logProb(LanguageModel.eos, u, v) }
        return c
    }

    // MARK: Decoding

    private struct State {
        var score: Float
        var u: Token
        var v: Token
        var node: Int32
    }

    private struct Node { let edge: Int32; let parent: Int32 }

    /// Splits normalized input into segments at spaces.
    public static func segments(of chars: [UInt8]) -> [Segment] {
        var result: [Segment] = []
        var start = 0
        for i in 0...chars.count where i == chars.count || chars[i] == 0x20 {
            if i > start {
                result.append(Segment(range: start..<i, text: String(decoding: chars[start..<i], as: UTF8.self)))
            }
            start = i + 1
        }
        return result
    }

    static func contextPair(_ context: [Token]) -> (Token, Token) {
        let bos = LanguageModel.bos
        switch context.count {
        case 0: return (bos, bos)
        case 1: return (bos, context[0])
        default: return (context[context.count - 2], context[context.count - 1])
        }
    }

    /// Decodes the whole input. `context` holds tokens committed just before (for better guesses),
    /// `pins` fixes the output of specific segments (index → Burmese text).
    public func decode(_ input: String, context: [Token] = [], pins: [Int: String] = [:]) -> DecodeResult {
        let chars = Self.normalizeInput(input)
        let normalized = String(decoding: chars, as: UTF8.self)
        let segments = Self.segments(of: chars)
        let start = Self.contextPair(context)
        guard !segments.isEmpty else {
            return DecodeResult(normalized: normalized, segments: [], outputs: [], tokens: [], score: 0,
                                startContext: start, pins: pins)
        }
        let (edges, from) = buildLattice(chars, segments, pins: pins)
        let n = chars.count
        var beams = [[State]](repeating: [], count: n + 1)
        var slots = [[UInt32: Int]](repeating: [:], count: n + 1)
        var nodes: [Node] = []
        var lmCache: [UInt64: Float] = [:]

        func lmCost(_ t: Token, _ u: Token, _ v: Token) -> Float {
            let key = UInt64(u) << 32 | UInt64(v) << 16 | UInt64(t)
            if let c = lmCache[key] { return c }
            let c = -lm.logProb(t, u, v)
            lmCache[key] = c
            return c
        }
        func push(_ s: State, at pos: Int) {
            let key = UInt32(s.u) << 16 | UInt32(s.v)
            if let i = slots[pos][key] {
                if s.score < beams[pos][i].score { beams[pos][i] = s }
            } else {
                slots[pos][key] = beams[pos].count
                beams[pos].append(s)
            }
        }

        push(State(score: 0, u: start.0, v: start.1, node: -1), at: 0)
        for p in 0..<n {
            if beams[p].isEmpty { continue }
            var states = beams[p]
            if states.count > config.beamWidth {
                states.sort { $0.score < $1.score }
                states.removeLast(states.count - config.beamWidth)
            }
            if chars[p] == 0x20 {
                for s in states { push(s, at: p + 1) }
                continue
            }
            for ei in from[p] {
                let e = edges[ei]
                for s in states {
                    var score = s.score + config.matchWeight * e.cost + config.edgePenalty
                    var u = s.u, v = s.v
                    for t in e.tokens { score += lmCost(t, u, v) + joinCost(v, t); u = v; v = t }
                    nodes.append(Node(edge: Int32(ei), parent: s.node))
                    push(State(score: score, u: u, v: v, node: Int32(nodes.count - 1)), at: e.end)
                }
            }
        }
        for i in beams[n].indices { beams[n][i].score += endCost(beams[n][i].u, beams[n][i].v) }
        guard let best = beams[n].min(by: { $0.score < $1.score }) else {
            return DecodeResult(normalized: normalized, segments: segments, outputs: segments.map { $0.text },
                                tokens: segments.map { _ in [] }, score: .infinity, startContext: start, pins: pins)
        }
        var path: [Edge] = []
        var node = best.node
        while node >= 0 { path.append(edges[Int(nodes[Int(node)].edge)]); node = nodes[Int(node)].parent }
        path.reverse()

        var outputs = [String](repeating: "", count: segments.count)
        var toks = [[Token]](repeating: [], count: segments.count)
        var si = 0
        for e in path {
            while si < segments.count && e.start >= segments[si].range.upperBound { si += 1 }
            guard si < segments.count else { break }
            if let pieces = e.pieces {
                for (k, piece) in pieces.enumerated() where si + k < segments.count {
                    outputs[si + k] += piece.text
                    toks[si + k] += piece.tokens
                }
            } else {
                outputs[si] += e.text
                toks[si] += e.tokens
            }
        }
        return DecodeResult(normalized: normalized, segments: segments, outputs: outputs, tokens: toks,
                            score: best.score, startContext: start, pins: pins)
    }

    /// A whole-phrase reading: the best one, or the result of changing one uncertain word.
    public struct PhraseOption: Hashable, Sendable {
        public let text: String
        /// Output for each typed segment.
        public let outputs: [String]
        /// The word change that produces this reading (nil for the best reading).
        public let segment: Int?
        public let word: String?
    }

    /// Alternative readings of the whole input, best first. Words fixed in `pins` stay.
    /// For each uncertain word there is the minimal change (only that word differs) and, if it
    /// differs, the re-decoded reading where neighbouring words re-adjust around it.
    public func phraseOptions(_ input: String, context: [Token] = [], pins: [Int: String] = [:],
                              limit: Int = 6) -> [PhraseOption] {
        let best = decode(input, context: context, pins: pins)
        guard !best.segments.isEmpty else { return [] }
        var proposals: [(segment: Int, word: String, delta: Float)] = []
        for k in best.segments.indices where pins[k] == nil {
            let cands = candidates(for: k, in: best, limit: 5)
            guard let base = cands.first(where: { $0.text == best.outputs[k] })?.score ?? cands.first?.score else { continue }
            for c in cands where c.text != best.outputs[k] { proposals.append((k, c.text, c.score - base)) }
        }
        proposals.sort { $0.delta < $1.delta }
        // Each word's best alternative comes first, so any single wrong word can be fixed from
        // this list; then the remaining alternatives by score.
        var firstPerWord: [(segment: Int, word: String, delta: Float)] = [], rest = firstPerWord
        var covered = Set<Int>()
        for p in proposals { if covered.insert(p.segment).inserted { firstPerWord.append(p) } else { rest.append(p) } }

        var options = [PhraseOption(text: best.text, outputs: best.outputs, segment: nil, word: nil)]
        var seen: Set<String> = [best.text]
        func offer(_ outputs: [String], _ p: (segment: Int, word: String, delta: Float)) {
            let text = outputs.joined()
            if options.count < limit, seen.insert(text).inserted {
                options.append(PhraseOption(text: text, outputs: outputs, segment: p.segment, word: p.word))
            }
        }
        for p in (firstPerWord + rest).prefix(limit + 8) where options.count < limit {
            var minimal = best.outputs
            minimal[p.segment] = p.word
            offer(minimal, p)
            var withPin = pins
            withPin[p.segment] = p.word
            offer(decode(input, context: context, pins: withPin).outputs, p)
        }
        return options
    }

    /// Ranked alternatives for one segment, given the rest of the best decoding as context.
    public func candidates(for segmentIndex: Int, in result: DecodeResult, limit: Int = 18) -> [Candidate] {
        guard segmentIndex < result.segments.count else { return [] }
        let chars = Array(result.normalized.utf8)
        var pins = result.pins
        pins[segmentIndex] = nil
        let (edges, from) = buildLattice(chars, result.segments, pins: pins)
        let seg = result.segments[segmentIndex]

        var (u0, v0) = result.startContext
        for k in 0..<segmentIndex { for t in result.tokens[k] { u0 = v0; v0 = t } }
        let right = Array(result.tokens.dropFirst(segmentIndex + 1).joined().prefix(2))

        struct Partial { var score: Float; var u: Token; var v: Token; var text: String; var tokens: [Token] }
        var at = [[String: Partial]](repeating: [:], count: chars.count + 1)
        at[seg.range.lowerBound][""] = Partial(score: 0, u: u0, v: v0, text: "", tokens: [])
        for p in seg.range {
            if at[p].isEmpty { continue }
            let partials = at[p].values.sorted { $0.score < $1.score }.prefix(config.candidateBeam)
            for ei in from[p] where edges[ei].end <= seg.range.upperBound {
                let e = edges[ei]
                for s in partials {
                    var score = s.score + config.matchWeight * e.cost + config.edgePenalty
                    var u = s.u, v = s.v
                    for t in e.tokens { score += -lm.logProb(t, u, v) + joinCost(v, t); u = v; v = t }
                    let text = s.text + e.text
                    if let old = at[e.end][text], old.score <= score { continue }
                    at[e.end][text] = Partial(score: score, u: u, v: v, text: text, tokens: s.tokens + e.tokens)
                }
            }
        }
        var finals: [Candidate] = []
        for s in at[seg.range.upperBound].values where !s.text.isEmpty {
            var score = s.score, u = s.u, v = s.v
            if right.isEmpty {
                score += endCost(u, v)
            } else {
                for t in right { score += -lm.logProb(t, u, v) + joinCost(v, t); u = v; v = t }
            }
            finals.append(Candidate(text: s.text, tokens: s.tokens, score: score))
        }
        finals.sort { $0.score < $1.score }
        guard let bestScore = finals.first?.score else { return [] }
        let window = bestScore + config.candidateWindow
        var kept = finals.prefix(limit).filter { $0.score <= window }
        if kept.count < 3 { kept = Array(finals.prefix(min(3, limit))) }
        return kept
    }
}
