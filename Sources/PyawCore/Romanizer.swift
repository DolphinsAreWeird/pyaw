import Foundation

/// One way of typing a Burmese syllable in Myanglish, with a cost (lower = more typical).
public struct RomanVariant: Hashable, Sendable {
    public let text: String
    public let cost: Float
}

/// Generates the Myanglish spellings people commonly use for a Burmese syllable.
///
/// Costs behave like -ln(probability of typing it this way). The tables cover spelling-based
/// forms (`kyay`, `par`, `tal`), pronunciation-based forms (`jay`, `ba`, `deh`) and chat
/// abbreviations that drop the vowel (`pr`, `tl`, `ny`, `kg`, `ll`).
public enum Romanizer {
    public static let maxCost: Float = 3.6

    public static func variants(of text: String) -> [RomanVariant] {
        guard let syl = Syllable.parse(text) else { return [] }
        return variants(of: syl)
    }

    public static func variants(of syl: Syllable) -> [RomanVariant] {
        if let symbol = syl.symbol {
            return (specialTable[symbol] ?? []).map { RomanVariant(text: $0.0, cost: $0.1) }
        }
        var best: [String: Float] = [:]
        let onsets = onsetVariants(syl)
        func combine(_ rhymes: [(String, Float, Bool)], extra: Float) {
            for (o, oc) in onsets {
                for (r, rc, abbreviated) in rhymes {
                    // A vowel-initial syllable (အ...) must spell its vowel.
                    if o.isEmpty && (abbreviated || r.isEmpty) { continue }
                    let text = o + r
                    let cost = oc + rc + extra
                    if cost > maxCost { continue }
                    if let old = best[text], old <= cost { continue }
                    best[text] = cost
                }
            }
        }
        var rhymes = rhymeVariants(syl.rhyme, tone: syl.tone)
        if syl.trailingL {
            rhymes = rhymes.flatMap { r in r.2 ? [(r.0, r.1 + 1.0, true)] : [(r.0 + "l", r.1 + 0.2, false), (r.0, r.1 + 0.8, false)] }
        }
        combine(rhymes, extra: 0)
        // Stacked codas are often not pronounced: ကမ္ဘာ is typed "kabar" as well as "kambar".
        if syl.stacked && !syl.isKinzi, let open = droppedCoda[syl.rhyme] {
            combine(rhymeVariants(open, tone: .creaky), extra: 0.6)
        }
        return best.map { RomanVariant(text: $0.key, cost: $0.value) }.sorted { $0.cost < $1.cost }
    }

    // MARK: Onsets

    static let baseOnsets: [UInt32: [(String, Float)]] = [
        0x1000: [("k", 0.1), ("g", 1.0)],                                   // က
        0x1001: [("kh", 0.3), ("k", 0.7), ("g", 1.5), ("hk", 1.6)],          // ခ
        0x1002: [("g", 0.1), ("k", 2.2)],                                   // ဂ
        0x1003: [("g", 0.2), ("gh", 1.6)],                                  // ဃ
        0x1004: [("ng", 0.1)],                                              // င
        0x1005: [("s", 0.1), ("z", 1.2), ("c", 2.2)],                        // စ
        0x1006: [("s", 0.3), ("hs", 0.6), ("z", 1.5), ("ss", 2.0), ("sh", 2.2)], // ဆ
        0x1007: [("z", 0.1), ("j", 2.0)],                                   // ဇ
        0x1008: [("z", 0.2), ("zh", 2.0), ("j", 2.0)],                      // ဈ
        0x1009: [("ny", 0.2), ("n", 1.6)],                                  // ဉ
        0x100A: [("ny", 0.1), ("n", 1.6)],                                  // ည
        0x100B: [("t", 0.1), ("d", 1.5)],                                   // ဋ
        0x100C: [("ht", 0.4), ("th", 0.6), ("t", 1.0), ("d", 1.5)],          // ဌ
        0x100D: [("d", 0.1)],                                               // ဍ
        0x100E: [("d", 0.1), ("dh", 1.6)],                                  // ဎ
        0x100F: [("n", 0.1)],                                               // ဏ
        0x1010: [("t", 0.1), ("d", 1.0)],                                   // တ
        0x1011: [("ht", 0.3), ("th", 0.5), ("t", 0.9), ("d", 1.4)],          // ထ
        0x1012: [("d", 0.1)],                                               // ဒ
        0x1013: [("d", 0.1), ("dh", 1.6)],                                  // ဓ
        0x1014: [("n", 0.1)],                                               // န
        0x1015: [("p", 0.1), ("b", 1.0)],                                   // ပ
        0x1016: [("ph", 0.3), ("p", 0.8), ("hp", 1.0), ("b", 1.4), ("f", 2.0)], // ဖ
        0x1017: [("b", 0.1), ("v", 1.6)],                                   // ဗ
        0x1018: [("b", 0.1), ("bh", 2.0)],                                  // ဘ
        0x1019: [("m", 0.1)],                                               // မ
        0x101A: [("y", 0.1)],                                               // ယ
        0x101B: [("y", 0.3), ("r", 0.9)],                                   // ရ
        0x101C: [("l", 0.1)],                                               // လ
        0x101D: [("w", 0.1)],                                               // ဝ
        0x101E: [("th", 0.1), ("t", 1.0), ("s", 2.2)],                       // သ
        0x101F: [("h", 0.1)],                                               // ဟ
        0x1020: [("l", 0.1)],                                               // ဠ
        0x1021: [("", 0.0)],                                                // အ
        MM.greatSa: [("th", 0.5), ("tth", 1.6)],                           // ဿ
    ]

    static func base(_ c: UInt32) -> [(String, Float)] { baseOnsets[c] ?? [] }

    /// Onsets with ျ or ြ.
    static func palatalOnsets(_ c: UInt32, r: Bool) -> [(String, Float)] {
        switch c {
        case 0x1000: return [("ky", 0.3), ("ch", 0.8), ("gy", 1.0), ("j", 1.4)] + (r ? [("kr", 1.8)] : [])
        case 0x1001: return [("ch", 0.3), ("ky", 1.1), ("khy", 1.2), ("hky", 1.6), ("chy", 1.6), ("j", 1.8), ("gy", 1.8)]
        case 0x1002: return [("gy", 0.2), ("j", 0.8), ("ky", 1.6)] + (r ? [("gr", 2.0)] : [])
        case 0x1003: return [("gy", 0.3), ("j", 1.0)]
        case 0x1004: return [("ny", 0.3), ("ngr", 2.0), ("ngy", 2.0)]
        case 0x1015: return [("py", 0.2), ("by", 1.4), ("p", 2.2)] + (r ? [("pr", 1.4)] : [])
        case 0x1016: return [("phy", 0.3), ("py", 0.8), ("hpy", 1.0), ("by", 1.8), ("fy", 2.2)] + (r ? [("phr", 1.8)] : [])
        case 0x1017: return [("by", 0.2)] + (r ? [("br", 1.8)] : [])
        case 0x1018: return [("by", 0.2)] + (r ? [("bhr", 2.4)] : [])
        case 0x1019: return [("my", 0.2), ("m", 2.0)] + (r ? [("mr", 1.4)] : [])
        case 0x101C: return [("ly", 0.3), ("y", 1.2)]
        case 0x1005: return [("z", 0.3), ("s", 0.4), ("zy", 1.5), ("sy", 1.5)]   // စျေး "zay"
        case 0x101E: return r ? [("thy", 1.2), ("", 1.5)] : [("thy", 0.5)]
        default: return base(c).map { ($0.0 + (r ? "r" : "y"), $0.1 + 0.3) }
        }
    }

    /// Onsets with ှ (optionally combined with ျ/ြ).
    static func aspiratedOnsets(_ c: UInt32, palatal: Bool) -> [(String, Float)] {
        if palatal {
            switch c {
            case 0x101C, 0x101E: return [("sh", 0.1), ("hly", 2.0), ("ly", 2.2)]  // လျှ သျှ
            case 0x1019: return [("hmy", 0.4), ("my", 0.8), ("mhy", 1.0)]         // မျှ မြှ
            case 0x1014, 0x1004: return [("hny", 0.5), ("ny", 1.0)]
            default: return palatalOnsets(c, r: false).map { ("h" + $0.0, $0.1 + 0.5) }
            }
        }
        switch c {
        case 0x1019: return [("hm", 0.4), ("mh", 0.5), ("m", 0.9)]          // မှ
        case 0x1014: return [("hn", 0.4), ("nh", 0.5), ("n", 0.9)]          // နှ
        case 0x100A, 0x1009: return [("hny", 0.4), ("ny", 0.9), ("nyh", 1.6)] // ညှ
        case 0x1004: return [("hng", 0.4), ("ng", 0.9), ("ngh", 1.6)]       // ငှ
        case 0x101C: return [("hl", 0.4), ("lh", 0.6), ("l", 0.9)]          // လှ
        case 0x101D: return [("hw", 0.5), ("wh", 0.6), ("w", 0.9)]          // ဝှ
        case 0x101B: return [("sh", 0.1), ("rh", 2.0), ("hr", 2.0)]         // ရှ
        case 0x101A: return [("sh", 0.2), ("hy", 1.4)]                      // ယှ
        default: return base(c).map { ("h" + $0.0, $0.1 + 1.0) } + base(c).map { ($0.0 + "h", $0.1 + 1.0) }
        }
    }

    static func onsetVariants(_ s: Syllable) -> [(String, Float)] {
        let palatal = s.medialY || s.medialR
        if s.medialH { return aspiratedOnsets(s.onset, palatal: palatal) }
        if palatal { return palatalOnsets(s.onset, r: s.medialR) }
        return base(s.onset)
    }

    // MARK: Rhymes

    /// (spelling, cost, isAbbreviation). Abbreviations drop the vowel letters and need an onset.
    static let rhymeTable: [Rhyme: [(String, Float, Bool)]] = [
        .a: [("a", 0.2, false), ("", 1.4, true), ("ah", 2.2, false), ("ar", 2.4, false)],
        .aa: [("ar", 0.3, false), ("a", 0.7, false), ("r", 0.7, true), ("rr", 1.6, true), ("aa", 2.0, false), ("ah", 2.0, false)],
        .i: [("i", 0.2, false), ("e", 1.6, false), ("ee", 1.6, false), ("ih", 2.2, false)],
        .ii: [("i", 0.3, false), ("ee", 0.7, false), ("e", 1.6, false), ("ii", 1.8, false)],
        .u: [("u", 0.2, false), ("oo", 1.2, false), ("o", 2.2, false)],
        .uu: [("u", 0.3, false), ("oo", 0.7, false), ("uu", 1.8, false)],
        .e: [("ay", 0.3, false), ("y", 0.8, true), ("e", 1.2, false), ("ei", 1.2, false), ("yy", 1.6, true),
             ("ayy", 1.6, false), ("ey", 2.0, false), ("ae", 2.2, false)],
        .ai: [("al", 0.5, false), ("ae", 0.5, false), ("l", 0.7, true), ("e", 1.0, false), ("el", 1.0, false),
              ("eh", 1.2, false), ("ll", 1.8, true), ("ai", 2.4, false), ("ay", 2.6, false)],
        .aw: [("aw", 0.3, false), ("w", 1.2, true), ("or", 1.2, false), ("o", 1.6, false), ("au", 2.0, false), ("oh", 2.2, false)],
        .o: [("o", 0.3, false), ("oe", 0.8, false), ("oh", 1.2, false)],
        .an: [("an", 0.3, false), ("n", 1.2, true), ("am", 1.6, false), ("un", 2.4, false), ("en", 2.4, false)],
        .at: [("at", 0.3, false), ("t", 1.5, true), ("et", 1.5, false), ("ut", 2.4, false)],
        .et: [("et", 0.3, false), ("ek", 1.0, false), ("k", 1.5, true), ("t", 1.6, true), ("ak", 2.0, false), ("at", 2.2, false)],
        .in: [("in", 0.3, false), ("n", 1.3, true), ("ing", 1.4, false), ("inn", 1.8, false)],
        .it: [("it", 0.3, false), ("t", 1.5, true), ("eit", 2.2, false), ("eet", 2.4, false)],
        .ie: [("i", 0.5, false), ("ee", 1.0, false), ("e", 1.0, false), ("ay", 1.5, false), ("ae", 1.6, false),
              ("ei", 1.6, false), ("al", 1.8, false), ("ih", 2.0, false)],
        .ein: [("ein", 0.3, false), ("ain", 1.2, false), ("en", 1.6, false), ("n", 2.0, true), ("eim", 2.0, false)],
        .eik: [("eik", 0.4, false), ("ate", 0.6, false), ("eit", 1.0, false), ("ait", 1.3, false), ("ake", 1.6, false),
               ("k", 2.0, true), ("t", 2.2, true)],
        .oun: [("on", 0.5, false), ("one", 0.5, false), ("oun", 0.6, false), ("own", 1.2, false), ("ohn", 1.5, false),
               ("om", 1.6, false), ("ome", 1.6, false), ("un", 2.0, false), ("n", 2.0, true), ("oon", 2.2, false)],
        .ok: [("ote", 0.4, false), ("ok", 0.6, false), ("oke", 0.8, false), ("oat", 1.2, false), ("t", 2.0, true),
              ("oak", 2.0, false), ("out", 2.6, false)],
        .aing: [("aing", 0.5, false), ("ai", 0.6, false), ("ine", 0.7, false), ("ain", 1.0, false), ("ing", 2.0, false)],
        .aik: [("ike", 0.5, false), ("aik", 0.5, false), ("ite", 1.0, false), ("ait", 1.0, false), ("ik", 1.6, false),
               ("k", 2.0, true)],
        .aung: [("aung", 0.3, false), ("g", 0.6, true), ("aun", 1.5, false), ("ng", 1.6, true), ("ong", 1.8, false),
                ("owng", 2.2, false)],
        .auk: [("auk", 0.4, false), ("out", 0.6, false), ("aut", 1.0, false), ("ouk", 1.5, false), ("k", 1.8, true),
               ("awk", 2.2, false)],
        .wa: [("wa", 0.3, false), ("w", 1.5, true)],
        .waa: [("war", 0.3, false), ("wa", 0.8, false), ("wr", 0.8, true), ("wrr", 1.6, true), ("waa", 2.0, false)],
        .wi: [("wi", 0.5, false), ("wee", 1.2, false)],
        .we: [("way", 0.3, false), ("we", 0.8, false), ("wy", 1.0, true), ("wei", 1.5, false)],
        .wai: [("wal", 0.5, false), ("wae", 0.5, false), ("wl", 1.0, true), ("wel", 1.0, false), ("we", 1.2, false),
               ("weh", 1.6, false)],
        .un: [("un", 0.4, false), ("wan", 1.0, false), ("wun", 1.0, false), ("oon", 1.5, false), ("wn", 1.5, true)],
        .ut: [("ut", 0.4, false), ("wut", 1.0, false), ("wat", 1.0, false), ("oot", 1.6, false), ("wt", 1.6, true)],
        .wet: [("wet", 0.4, false), ("wat", 1.0, false), ("wek", 1.2, false), ("wt", 1.6, true)],
        .win: [("win", 0.4, false), ("wn", 1.6, true)],
        .wit: [("wit", 0.4, false), ("wt", 1.8, true)],
    ]

    /// Tone-specific spellings, e.g. chat writers mark creaky tone with a trailing t (တော့ "tot").
    static let toneExtras: [Rhyme: [Tone: [(String, Float, Bool)]]] = [
        .aa: [.high: [("rr", 1.0, true), ("arr", 1.6, false)]],
        .ii: [.high: [("ee", 0.5, false)]],
        .uu: [.high: [("oo", 0.5, false)]],
        .o: [.high: [("oe", 0.6, false)], .creaky: [("ot", 1.8, false)]],
        .e: [.high: [("ayy", 1.4, false)]],
        .ai: [.high: [("ae", 0.4, false)], .creaky: [("ae", 0.4, false), ("e", 0.8, false), ("eh", 1.0, false),
                                                     ("et", 1.6, false), ("ek", 1.8, false)]],
        .aw: [.creaky: [("ot", 1.4, false), ("awt", 1.8, false)], .low: [("or", 0.9, false)]],
        .aung: [.creaky: [("ount", 1.0, false), ("aunt", 1.0, false)]],
        .in: [.creaky: [("int", 0.9, false)], .high: [("inn", 1.4, false)]],
        .an: [.creaky: [("ant", 1.4, false)]],
    ]

    static func rhymeVariants(_ rhyme: Rhyme, tone: Tone) -> [(String, Float, Bool)] {
        var list = rhymeTable[rhyme] ?? []
        if let extra = toneExtras[rhyme]?[tone] {
            for e in extra {
                if let idx = list.firstIndex(where: { $0.0 == e.0 }) {
                    if e.1 < list[idx].1 { list[idx] = e }
                } else {
                    list.append(e)
                }
            }
        }
        return list
    }

    static let droppedCoda: [Rhyme: Rhyme] = [
        .an: .a, .at: .a, .et: .a, .it: .i, .ein: .i, .eik: .i, .oun: .u, .ok: .u, .un: .wa, .ut: .wa,
    ]

    static let specialTable: [String: [(String, Float)]] = [
        "၌": [("hnaik", 0.3), ("naik", 0.8), ("nike", 0.8), ("hnike", 0.8)],
        "၍": [("ywe", 0.3), ("ywae", 0.5), ("ywal", 0.8), ("yway", 1.0)],
        "၏": [("i", 0.6), ("ei", 0.8), ("e", 1.0)],
        "၎င်း": [("lagaung", 0.3), ("lakaung", 0.8), ("lingaung", 1.2)],
        "ယောက်ျား": [("yaukkyar", 0.4), ("youtkyar", 0.5), ("yautkyar", 0.8), ("youkyar", 0.8), ("yaukyar", 0.8),
                    ("yaukjar", 1.2)],
        "ကျွန်ုပ်": [("kyanote", 0.4), ("kyunote", 0.6), ("kyunoke", 0.6), ("kyanoke", 0.7), ("kyanok", 0.8), ("kyunok", 1.0)],
        "လက်ျာ": [("letyar", 0.4), ("letya", 0.8), ("lakyar", 1.0), ("letkyar", 1.0)],
    ]
}
