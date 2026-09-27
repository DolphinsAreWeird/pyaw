import Foundation

/// Myanmar (Burmese) Unicode constants and character classes.
public enum MM {
    public static let eVowel: UInt32 = 0x1031    // ေ
    public static let iVowel: UInt32 = 0x102D    // ိ
    public static let iiVowel: UInt32 = 0x102E   // ီ
    public static let uVowel: UInt32 = 0x102F    // ု
    public static let uuVowel: UInt32 = 0x1030   // ူ
    public static let aiVowel: UInt32 = 0x1032   // ဲ
    public static let tallAa: UInt32 = 0x102B    // ါ
    public static let aa: UInt32 = 0x102C        // ာ
    public static let anusvara: UInt32 = 0x1036  // ံ
    public static let dotBelow: UInt32 = 0x1037  // ့
    public static let visarga: UInt32 = 0x1038   // း
    public static let virama: UInt32 = 0x1039    // ္  (stacking)
    public static let asat: UInt32 = 0x103A      // ်
    public static let medialYa: UInt32 = 0x103B  // ျ
    public static let medialRa: UInt32 = 0x103C  // ြ
    public static let medialWa: UInt32 = 0x103D  // ွ
    public static let medialHa: UInt32 = 0x103E  // ှ
    public static let greatSa: UInt32 = 0x103F   // ဿ
    public static let letterA: UInt32 = 0x1021   // အ
    public static let nga: UInt32 = 0x1004       // င

    @inline(__always) public static func isConsonant(_ c: UInt32) -> Bool { c >= 0x1000 && c <= 0x1021 }
    /// ဣ ဤ ဥ ဦ ဧ ဩ ဪ (U+1028 is Mon and excluded).
    @inline(__always) public static func isIndependentVowel(_ c: UInt32) -> Bool {
        c >= 0x1023 && c <= 0x102A && c != 0x1028
    }
    /// ၌ ၍ ၎ ၏
    @inline(__always) public static func isSymbolSyllable(_ c: UInt32) -> Bool { c >= 0x104C && c <= 0x104F }
    /// Dependent signs that attach to a syllable (virama is handled separately).
    @inline(__always) public static func isMark(_ c: UInt32) -> Bool {
        c >= 0x102B && c <= 0x103E && c != virama
    }
    @inline(__always) public static func isStarter(_ c: UInt32) -> Bool {
        isConsonant(c) || isIndependentVowel(c) || c == greatSa || isSymbolSyllable(c)
    }
    @inline(__always) public static func isDigit(_ c: UInt32) -> Bool { c >= 0x1040 && c <= 0x1049 }
    /// Code points outside basic Burmese that legacy Zawgyi text uses for its own glyph variants.
    @inline(__always) public static func isZawgyiRange(_ c: UInt32) -> Bool { c >= 0x1050 && c <= 0x109F }

    static func string(_ scalars: [UInt32]) -> String {
        var s = String.UnicodeScalarView()
        for v in scalars { if let u = Unicode.Scalar(v) { s.append(u) } }
        return String(s)
    }
}

// MARK: - Normalization

public enum BurmeseText {
    /// Canonical ordering rank for dependent marks within a mark run (UTN #11 order).
    @inline(__always) static func markRank(_ c: UInt32) -> Int {
        switch c {
        case 0x103B: return 1
        case 0x103C: return 2
        case 0x103D: return 3
        case 0x103E: return 4
        case 0x1031: return 5
        case 0x102D, 0x102E, 0x1032: return 6
        case 0x102F, 0x1030: return 7
        case 0x102B, 0x102C: return 8
        case 0x1036: return 9
        case 0x1037: return 10
        case 0x103A: return 11
        case 0x1038: return 12
        default: return 20
        }
    }

    /// Normalizes Unicode Burmese text: NFC, removes invisible joiners, fixes common look-alike
    /// substitutions (၀→ဝ, ၇→ရ, ဥ်→ဉ်, ၄င်း→၎င်း) and reorders dependent marks into canonical order.
    public static func normalize(_ input: String) -> String {
        let nfc = input.precomposedStringWithCanonicalMapping
        var s: [UInt32] = []
        s.reserveCapacity(nfc.unicodeScalars.count)
        for u in nfc.unicodeScalars {
            switch u.value {
            case 0x200B, 0x200C, 0x200D, 0xFEFF, 0x00AD: continue
            default: s.append(u.value)
            }
        }
        let n = s.count
        for i in 0..<n {
            let next = i + 1 < n ? s[i + 1] : 0
            let prev = i > 0 ? s[i - 1] : 0
            switch s[i] {
            case 0x1040: // ၀ used for ဝ (digits never take vowel signs; ၁၀ယောက် stays a number)
                if MM.isMark(next) {
                    s[i] = 0x101D
                } else if !MM.isDigit(prev) && !MM.isDigit(next)
                            && (MM.isConsonant(next) || MM.isConsonant(prev) || MM.isMark(prev)) {
                    s[i] = 0x101D
                }
            case 0x1047: // ၇ used for ရ
                if MM.isMark(next) { s[i] = 0x101B }
            case 0x1025: // ဥ် used for ဉ်
                if next == MM.asat { s[i] = 0x1009 }
            case 0x1044: // ၄င်း used for ၎င်း
                if next == MM.nga, i + 2 < n, s[i + 2] == MM.asat { s[i] = 0x104E }
            default: break
            }
        }
        // Reorder each run of dependent marks and drop exact duplicates.
        var out: [UInt32] = []
        out.reserveCapacity(n)
        var i = 0
        while i < n {
            if MM.isMark(s[i]) {
                var j = i
                while j < n && MM.isMark(s[j]) { j += 1 }
                // Marks after an asat other than း/့ are deliberate irregular spellings
                // (ယောက်ျား, ကျွန်ုပ်, လက်ျာ): keep those runs exactly as written.
                let irregular = s[i..<j].firstIndex(of: MM.asat).map { a in
                    s[(a + 1)..<j].contains { $0 != MM.visarga && $0 != MM.dotBelow }
                } ?? false
                if j - i == 1 || irregular {
                    out.append(contentsOf: s[i..<j])
                } else {
                    let run = s[i..<j].enumerated().sorted {
                        let a = markRank($0.element), b = markRank($1.element)
                        return a != b ? a < b : $0.offset < $1.offset
                    }.map { $0.element }
                    var last: UInt32 = 0
                    for c in run where c != last { out.append(c); last = c }
                }
                i = j
            } else {
                out.append(s[i])
                i += 1
            }
        }
        return MM.string(out)
    }

    /// Splits normalized text into runs of orthographic-phonological syllables.
    ///
    /// Stacked consonants are split at the virama so that each piece is one spoken syllable:
    /// `မင်္ဂလာ` → `မင်္` `ဂ` `လာ`, `ကမ္ဘာ` → `ကမ္` `ဘာ`. Spaces keep a run going (Burmese spaces
    /// separate phrases, not syllables); any other non-Burmese character ends the run.
    public static func syllableRuns(_ text: String) -> [[String]] {
        let s = text.unicodeScalars.map { $0.value }
        let n = s.count
        var runs: [[String]] = []
        var run: [String] = []
        var syl: [UInt32] = []
        @inline(__always) func flushSyllable() {
            if !syl.isEmpty { run.append(MM.string(syl)); syl.removeAll(keepingCapacity: true) }
        }
        @inline(__always) func flushRun() {
            flushSyllable()
            if !run.isEmpty { runs.append(run); run = [] }
        }
        var i = 0
        while i < n {
            let c = s[i]
            if MM.isStarter(c) {
                let n1 = i + 1 < n ? s[i + 1] : 0
                let n2 = i + 2 < n ? s[i + 2] : 0
                let closesSyllable = n1 == MM.asat || (n1 == MM.dotBelow && n2 == MM.asat) || n1 == MM.virama
                let afterVirama = i > 0 && s[i - 1] == MM.virama
                if closesSyllable && !syl.isEmpty && !afterVirama && MM.isConsonant(c) {
                    syl.append(c) // final consonant of the current syllable
                } else {
                    flushSyllable()
                    syl.append(c)
                }
            } else if MM.isMark(c) || c == MM.virama {
                if syl.isEmpty {
                    // Orphan mark: keep it as its own (invalid) syllable so validation can see it.
                    syl.append(c)
                    flushSyllable()
                } else {
                    syl.append(c)
                }
            } else if c == 0x20 || c == 0xA0 || c == 0x3000 {
                flushSyllable()
            } else {
                flushRun()
            }
            i += 1
        }
        flushRun()
        return runs
    }

    /// Splits text into syllables ignoring run boundaries (non-Burmese characters are dropped).
    public static func syllables(_ text: String) -> [String] {
        syllableRuns(normalize(text)).flatMap { $0 }
    }

    // MARK: Stacks

    private static let stackPairs: Set<UInt64> = {
        var set = Set<UInt64>()
        func add(_ a: UInt32, _ b: UInt32) { set.insert(UInt64(a) << 32 | UInt64(b)) }
        let rows: [[UInt32]] = [
            [0x1000, 0x1001, 0x1002, 0x1003, 0x1004],          // က ခ ဂ ဃ င
            [0x1005, 0x1006, 0x1007, 0x1008, 0x1009, 0x100A],  // စ ဆ ဇ ဈ ဉ ည
            [0x100B, 0x100C, 0x100D, 0x100E, 0x100F],          // ဋ ဌ ဍ ဎ ဏ
            [0x1010, 0x1011, 0x1012, 0x1013, 0x1014],          // တ ထ ဒ ဓ န
            [0x1015, 0x1016, 0x1017, 0x1018, 0x1019],          // ပ ဖ ဗ ဘ မ
        ]
        for r in rows {
            add(r[0], r[0]); add(r[0], r[1]); add(r[2], r[2]); add(r[2], r[3])
            for nasal in r[4...] { for x in r[0..<4] { add(nasal, x) }; add(nasal, nasal) }
        }
        add(0x101C, 0x101C)  // လ္လ
        add(0x101E, 0x101E)  // သ္သ
        add(0x101F, 0x1019)  // ဟ္မ (ဗြဟ္မာ)
        add(0x101A, 0x101A)  // ယ္ယ
        add(0x1020, 0x1020)  // ဠ္ဠ
        return set
    }()

    /// Whether `lower` may be stacked under a syllable whose coda is `upper` (e.g. န္တ, မ္ဘ).
    /// Legacy Zawgyi text uses U+1039 as a plain "killer", which produces implausible stacks.
    public static func isPlausibleStack(upper: UInt32, lower: UInt32) -> Bool {
        stackPairs.contains(UInt64(upper) << 32 | UInt64(lower))
    }
}
