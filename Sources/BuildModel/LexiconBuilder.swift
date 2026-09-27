import Foundation
import PyawCore

/// Turns myanglish_mapping.json (github.com/AUNGSWANOOgit/myanglish) into word-level lexicon
/// entries, and extracts its test cases for evaluation.
enum LexiconBuilder {
    struct TestCase { let input: String; let expected: String }

    /// Burmese for the particle labels used in the mapping file.
    static let particleBurmese: [String: String] = [
        "TL": "တယ်", "LRR": "လား", "ML": "မယ်", "LL": "လဲ", "BU": "ဘူး", "NY": "နေ", "FIN": "ပြီ",
        "PP": "ပြီးပြီ", "PYI": "ပြီး", "CHIN": "ချင်", "PR": "ပါ", "TR": "တာ", "NE": "နဲ့", "THAY": "သေး",
        "LO": "လို့", "NAI": "နိုင်", "PAY": "ပေး", "LITE": "လိုက်", "NAW": "နော်", "BR": "ဘာ", "BL": "ဘယ်",
        "AYAN": "အရမ်း", "KHAE": "ခဲ့", "TOT": "တော့", "OWN": "ဦး", "SOT": "စို့", "TAT": "တတ်",
        "PHOE": "ဖို့", "YAE": "ရဲ့", "SAY": "စေ", "THWAR": "သွား",
    ]

    static func confidenceCost(_ c: Any?) -> Float {
        switch c as? String {
        case "high": return 0.3
        case "medium": return 0.6
        default: return 0.8
        }
    }

    /// (roman phrase, burmese, cost) pairs straight from the file, plus test cases.
    static func load(path: String) throws -> (pairs: [(String, String, Float)], phrases: [(String, String)], tests: [TestCase]) {
        let data = try Data(contentsOf: URL(fileURLWithPath: path))
        guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any] else { return ([], [], []) }
        var pairs: [(String, String, Float)] = []
        var phrases: [(String, String)] = []
        for item in root["token_map"] as? [[String: Any]] ?? [] {
            guard let burmese = item["burmese"] as? String else { continue }
            let cost = confidenceCost(item["confidence"])
            for v in item["variants"] as? [String] ?? [] { pairs.append((v, burmese, cost)) }
        }
        for item in root["predicates"] as? [[String: Any]] ?? [] {
            guard let burmese = item["burmese"] as? String else { continue }
            for v in item["latin"] as? [String] ?? [] { pairs.append((v, burmese, 0.4)) }
        }
        for (label, variants) in root["particles"] as? [String: [String]] ?? [:] {
            guard let burmese = particleBurmese[label] else { continue }
            for v in variants {
                if v.contains(" ") { phrases.append((v, burmese)) } else { pairs.append((v, burmese, 0.3)) }
            }
        }
        for item in root["phrase_map"] as? [[String: Any]] ?? [] {
            guard let burmese = item["burmese"] as? String else { continue }
            for v in item["myanglish"] as? [String] ?? [] { phrases.append((v, burmese)) }
        }
        var tests: [TestCase] = []
        for item in root["test_cases"] as? [[String: Any]] ?? [] {
            if let i = item["input"] as? String, let e = item["expected_burmese"] as? String {
                tests.append(TestCase(input: i, expected: e))
            }
        }
        return (pairs, phrases, tests)
    }

    /// Cost of spelling `word` as the syllables `syls` using the romanization rules, if possible.
    static func ruleCost(_ word: [UInt8], _ syls: ArraySlice<[RomanVariant]>) -> Float? {
        // dp[position in word] after consuming syllables in order
        var dp: [Int: Float] = [0: 0]
        for variants in syls {
            var next: [Int: Float] = [:]
            for (pos, cost) in dp {
                for v in variants {
                    let b = Array(v.text.utf8)
                    if pos + b.count <= word.count && Array(word[pos..<(pos + b.count)]) == b {
                        let c = cost + v.cost
                        if c < next[pos + b.count, default: .infinity] { next[pos + b.count] = c }
                    }
                }
            }
            dp = next
            if dp.isEmpty { return nil }
        }
        return dp[word.count]
    }

    /// Aligns a roman phrase with its Burmese syllables; words that no rule or known entry can
    /// explain become new lexicon entries (e.g. "ek" → အဲ့ from "ek lo" → အဲ့လို).
    static func extract(phrase: String, burmese: String, known: Lexicon) -> [(String, String)] {
        let words = phrase.lowercased().split(separator: " ").map { Lexicon.normalizeKey(String($0)) }.filter { !$0.isEmpty }
        let syls = BurmeseText.syllables(burmese)
        guard !words.isEmpty, !syls.isEmpty, words.count <= 8, syls.count <= 16 else { return [] }
        let variants = syls.map { Romanizer.variants(of: $0) }
        let m = words.count, n = syls.count
        let unknownCost: Float = 6
        // best[k][i]: min cost aligning words[0..<k] to syls[0..<i]
        var best = [[Float]](repeating: [Float](repeating: .infinity, count: n + 1), count: m + 1)
        var back = [[(Int, Bool)]](repeating: [(Int, Bool)](repeating: (-1, false), count: n + 1), count: m + 1)
        best[0][0] = 0
        for k in 0..<m {
            let w = Array(words[k].utf8)
            for i in 0..<n where best[k][i] < .infinity {
                for j in (i + 1)...min(n, i + 4) {
                    let piece = syls[i..<j].joined()
                    var c: Float = .infinity
                    var unknown = false
                    if known.lookup(words[k]).contains(where: { $0.burmese == piece }) { c = 0.1 }
                    if let r = ruleCost(w, variants[i..<j]) { c = min(c, r) }
                    if c == .infinity && j - i <= 2 { c = unknownCost * Float(j - i); unknown = true }
                    if best[k][i] + c < best[k + 1][j] { best[k + 1][j] = best[k][i] + c; back[k + 1][j] = (i, unknown) }
                }
            }
        }
        guard best[m][n] < unknownCost * 2.5 else { return [] }  // at most ~2 unexplained words
        var out: [(String, String)] = []
        var j = n
        for k in stride(from: m, to: 0, by: -1) {
            let (i, unknown) = back[k][j]
            if unknown && soundsAlike(words[k - 1], variants[i]) { out.append((words[k - 1], syls[i..<j].joined())) }
            j = i
        }
        return out
    }

    /// Rejects alignments where Burmese word order differs from the Myanglish (e.g. "ma sate"
    /// vs စိတ်မ...): the word must start with the same sound as the first syllable.
    static func soundsAlike(_ word: String, _ firstSyllable: [RomanVariant]) -> Bool {
        guard let w = word.first else { return false }
        let vowels: Set<Character> = ["a", "e", "i", "o", "u"]
        let pairs: [Character: Character] = ["p": "b", "b": "p", "t": "d", "d": "t", "k": "g", "g": "k", "s": "z", "z": "s"]
        for v in firstSyllable {
            guard let f = v.text.first else { continue }
            if f == w || pairs[f] == w || (vowels.contains(f) && vowels.contains(w)) { return true }
        }
        return false
    }

    static func build(path: String) throws -> (lexicon: Lexicon, tests: [TestCase]) {
        let (pairs, phrases, tests) = try load(path: path)
        var lex = Lexicon()
        for (r, b, c) in pairs { lex.add(roman: r, burmese: b, cost: c) }
        // Extractions that are right inside one phrase but wrong in general.
        let rejected: Set<String> = ["mae→မ", "may→မ", "kat→က", "pyat→ပြ", "tha→သူ", "tha→သီ"]
        // Right only in some contexts ("ml ll" = မလဲ): kept, but the language model must agree.
        let demoted: Set<String> = ["ml→မ", "mal→မ"]
        var extracted = 0
        for (phrase, burmese) in phrases {
            // The whole phrase is matched even when typed with spaces between its words.
            lex.add(roman: phrase, burmese: burmese, cost: 0.3)
            for (w, b) in extract(phrase: phrase, burmese: burmese, known: lex) where !rejected.contains("\(w)→\(b)") {
                lex.add(roman: w, burmese: b, cost: demoted.contains("\(w)→\(b)") ? 1.2 : 0.5)
                extracted += 1
                log("    extracted: \(w) → \(b)   (from \"\(phrase)\")")
            }
        }
        log("  lexicon: \(pairs.count) direct entries, \(extracted) extracted from \(phrases.count) phrases, \(tests.count) test cases")
        return (lex, tests)
    }
}
