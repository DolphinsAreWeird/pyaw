import Foundation

/// Phonological rhyme class of a Burmese syllable (vowel + final), independent of spelling.
public enum Rhyme: UInt8, CaseIterable, Sendable {
    // Open syllables
    case a      // က (inherent)
    case aa     // ကာ ကား
    case i      // ကိ
    case ii     // ကီ ကီး
    case u      // ကု
    case uu     // ကူ ကူး
    case e      // ကေ ကေး ကေ့
    case ai     // ကယ် ကဲ ကဲ့
    case aw     // ကော ကော် ကော့
    case o      // ကို ကိုး ကို့
    // Closed syllables
    case an     // ကန် ကမ် ကံ
    case at     // ကတ် ကပ်
    case et     // ကက်
    case `in`   // ကင် ကဉ်
    case it     // ကစ်
    case ie     // ကည် (pronounced i/e)
    case ein    // ကိန် ကိမ်
    case eik    // ကိတ် ကိပ်
    case oun    // ကုန် ကုမ် ကုံ
    case ok     // ကုတ် ကုပ်
    case aing   // ကိုင်
    case aik    // ကိုက်
    case aung   // ကောင်
    case auk    // ကောက်
    // With medial ွ
    case wa     // ကွ
    case waa    // ကွာ ကွား
    case wi     // ကွိ ကွီ
    case we     // ကွေ
    case wai    // ကွယ် ကွဲ
    case un     // ကွန် ကွမ် ကွံ
    case ut     // ကွတ် ကွပ်
    case wet    // ကွက်
    case win    // ကွင်
    case wit    // ကွစ်
}

public enum Tone: UInt8, Sendable {
    case low      // plain (ကာ, ကယ်, ကော်)
    case high     // း or the unmarked high forms ဲ / ော
    case creaky   // ့ or inherent short vowel
    case checked  // stop finals
}

/// A parsed Burmese syllable.
public struct Syllable: Hashable, Sendable {
    public var onset: UInt32          // consonant; independent vowels are folded into အ + vowel
    public var medialY = false        // ျ
    public var medialR = false        // ြ
    public var medialW = false        // ွ
    public var medialH = false        // ှ
    public var rhyme: Rhyme = .a
    public var tone: Tone = .creaky
    public var finalConsonant: UInt32 = 0
    /// True when the syllable's coda is stacked onto the next syllable (virama or kinzi).
    public var stacked = false
    public var isKinzi = false
    /// Loanword ending written as a final လ် after a vowel (မေးလ် "mail", ဖိုင်လ်).
    public var trailingL = false
    /// Symbol syllables (၌ ၍ ၏ ၎င်း) are romanized from a fixed table.
    public var symbol: String? = nil
}

extension Syllable {
    private enum Vowel { case none, aa, i, ii, u, uu, e, ai, aw, o }

    private enum FinalClass { case none, velar, ng, palatal, nyaSmall, nyaBig, stop, nasal, ya, la, other }

    // Burmese phonotactics: which consonants can carry which medials. Rejecting impossible
    // combinations (တျ, ငျ, ကြှ ...) catches text that was mangled by double Zawgyi conversion.
    // က ခ ဂ ဃ ပ ဖ ဗ ဘ မ လ, plus န (loanwords: နျူး), သ (သျှ) and စ (စျေး)
    private static let takesYa: Set<UInt32> = [0x1000, 0x1001, 0x1002, 0x1003, 0x1015, 0x1016, 0x1017, 0x1018, 0x1019, 0x101C, 0x1014, 0x101E, 0x1005]
    // က ခ ဂ ဃ င ပ ဖ ဗ ဘ မ သ(သြ)
    private static let takesRa: Set<UInt32> = [0x1000, 0x1001, 0x1002, 0x1003, 0x1004, 0x1015, 0x1016, 0x1017, 0x1018, 0x1019, 0x101E]
    // Sonorants: င ည ဉ ဏ န မ ယ ရ လ ဝ (and သ only in သျှ)
    private static let takesHa: Set<UInt32> = [0x1004, 0x100A, 0x1009, 0x100F, 0x1014, 0x1019, 0x101A, 0x101B, 0x101C, 0x101D]

    // ါ is written after ခ ဂ င ဒ ပ ဝ (when they have no ျ ြ ွ medial), ာ everywhere else.
    private static let takesTallAa: Set<UInt32> = [0x1001, 0x1002, 0x1004, 0x1012, 0x1015, 0x101D]

    private static func aaSpellingOK(_ s: Syllable, tall: Bool) -> Bool {
        let wantsTall = takesTallAa.contains(s.onset) && !s.medialY && !s.medialR && !s.medialW
        return tall == wantsTall
    }

    private static func medialsAllowed(_ s: Syllable) -> Bool {
        let c = s.onset
        if s.medialY {
            if !takesYa.contains(c) { return false }
            if c == 0x101E && !s.medialH { return false }
        }
        if s.medialR && !takesRa.contains(c) { return false }
        if s.medialH && !(takesHa.contains(c) || (c == 0x101E && s.medialY)) { return false }
        if s.medialW && (c == 0x101D || c == MM.letterA || c == MM.greatSa) { return false }
        return true
    }

    private static func finalClass(_ c: UInt32) -> FinalClass {
        switch c {
        case 0: return .none
        case 0x1000...0x1003: return .velar
        case 0x1004: return .ng
        case 0x1005...0x1008: return .palatal
        case 0x1009: return .nyaSmall     // ဉ
        case 0x100A: return .nyaBig       // ည
        case 0x100B...0x100E, 0x1010...0x1013, 0x1015...0x1018: return .stop
        case 0x100F, 0x1014, 0x1019: return .nasal
        case 0x101A: return .ya
        case 0x101C: return .la
        default: return .other
        }
    }

    /// Symbols and irregular spellings that are romanized from a fixed table.
    public static let specialSpellings: Set<String> = ["၌", "၍", "၏", "၎င်း", "ယောက်ျား", "ကျွန်ုပ်", "လက်ျာ"]

    /// Parses one syllable produced by `BurmeseText.syllableRuns`. Returns nil for anything that
    /// is not a well-formed Unicode Burmese syllable (which is how legacy Zawgyi text is detected).
    public static func parse(_ text: String) -> Syllable? {
        let s = text.unicodeScalars.map { $0.value }
        guard let first = s.first else { return nil }
        if specialSpellings.contains(text) { return Syllable(onset: first, symbol: text) }
        if MM.isSymbolSyllable(first) { return nil }

        var syl = Syllable(onset: first)
        var implied: Vowel? = nil
        if MM.isIndependentVowel(first) {
            syl.onset = MM.letterA
            switch first {
            case 0x1023: implied = .i    // ဣ
            case 0x1024: implied = .ii   // ဤ
            case 0x1025: implied = .u    // ဥ
            case 0x1026: implied = .uu   // ဦ
            case 0x1027: implied = .e    // ဧ
            case 0x1029: implied = .aw   // ဩ
            case 0x102A: implied = .aw   // ဪ (low tone)
            default: return nil
            }
        } else if !(MM.isConsonant(first) || first == MM.greatSa) {
            return nil
        }

        let n = s.count
        var i = 1
        // Medials in canonical order.
        var lastMedial: UInt32 = 0
        while i < n, s[i] >= MM.medialYa, s[i] <= MM.medialHa {
            if s[i] <= lastMedial || implied != nil { return nil }
            switch s[i] {
            case MM.medialYa: syl.medialY = true
            case MM.medialRa: syl.medialR = true
            case MM.medialWa: syl.medialW = true
            default: syl.medialH = true
            }
            lastMedial = s[i]
            i += 1
        }
        if syl.medialY && syl.medialR { return nil }
        if !medialsAllowed(syl) { return nil }

        func take(_ c: UInt32) -> Bool {
            if i < n && s[i] == c { i += 1; return true }
            return false
        }
        func takeAny(_ cs: ClosedRange<UInt32>) -> UInt32? {
            if i < n && cs.contains(s[i]) { i += 1; return s[i - 1] }
            return nil
        }

        let hasE = take(MM.eVowel)
        var upper: UInt32 = 0
        if i < n, s[i] == MM.iVowel || s[i] == MM.iiVowel || s[i] == MM.aiVowel { upper = s[i]; i += 1 }
        let lower = takeAny(0x102F...0x1030) ?? 0
        let aaSign = takeAny(0x102B...0x102C)
        let hasAa = aaSign != nil
        if let aaSign, !aaSpellingOK(syl, tall: aaSign == MM.tallAa) { return nil }
        let anus = take(MM.anusvara)
        var dot = take(MM.dotBelow)
        let vowelAsat = take(MM.asat)
        var vis = take(MM.visarga)
        let toneOnVowel = dot || vis

        var fin: UInt32 = 0
        if i < n, MM.isConsonant(s[i]) {
            fin = s[i]; i += 1
            if take(MM.dotBelow) { dot = true }
            if take(MM.asat) {
                if take(MM.virama) {
                    guard fin == MM.nga else { return nil }
                    syl.stacked = true; syl.isKinzi = true
                }
            } else if take(MM.virama) {
                syl.stacked = true
            } else {
                return nil
            }
            if take(MM.dotBelow) { dot = true }
            if take(MM.visarga) { vis = true }
        }
        guard i == n else { return nil }
        if anus && fin != 0 { return nil }
        if dot && vis { return nil }

        // Resolve the written vowel.
        var vowel: Vowel
        switch (hasE, upper, lower, hasAa) {
        case (false, 0, 0, false): vowel = .none
        case (false, 0, 0, true): vowel = .aa
        case (false, MM.iVowel, 0, false): vowel = .i
        case (false, MM.iiVowel, 0, false): vowel = .ii
        case (false, 0, MM.uVowel, false): vowel = .u
        case (false, 0, MM.uuVowel, false): vowel = .uu
        case (true, 0, 0, false): vowel = .e
        case (true, 0, 0, true): vowel = .aw
        case (false, MM.aiVowel, 0, false): vowel = .ai
        case (false, MM.iVowel, MM.uVowel, false): vowel = .o
        default: return nil
        }
        if let implied {
            guard vowel == .none else { return nil }
            vowel = implied
        }
        if vowelAsat {
            // Asat directly on a vowel only occurs in ော် / ေါ်.
            guard vowel == .aw, fin == 0 else { return nil }
        }
        if syl.medialW, vowel == .u || vowel == .uu || vowel == .o || vowel == .aw { return nil }

        syl.finalConsonant = fin
        var fc = finalClass(fin)
        let w = syl.medialW
        // Loanwords write ါ/ာ before a final (နံပါတ် "number"); it is pronounced like the plain final.
        if fc == .la && !syl.stacked && vowel != .o {
            // English "-l" ending on an open vowel: keep the vowel, remember the l.
            syl.trailingL = true
            fc = .none
        }
        if vowel == .aa && fc != .none {
            guard !toneOnVowel else { return nil }
            vowel = .none
        }
        // A dot or visarga on the bare inherent vowel (ပ့ ပး) is not standard spelling.
        if vowel == .none && fc == .none && !anus && (dot || vis) && implied == nil { return nil }
        var rhyme: Rhyme
        switch (vowel, fc, anus) {
        case (.none, .none, false): rhyme = w ? .wa : .a
        case (.aa, .none, false): rhyme = w ? .waa : .aa
        case (.i, .none, false): rhyme = w ? .wi : .i
        case (.ii, .none, false): rhyme = w ? .wi : .ii
        case (.u, .none, false): rhyme = .u
        case (.uu, .none, false): rhyme = .uu
        case (.e, .none, false): rhyme = w ? .we : .e
        case (.ai, .none, false), (.none, .ya, false): rhyme = w ? .wai : .ai
        case (.aw, .none, false): rhyme = .aw
        case (.o, .none, false), (.o, .la, false), (.o, .ya, false), (.o, .other, false):
            rhyme = .o   // ကို ဗိုလ် ကိုယ် ဂြိုဟ်
        case (.none, .nasal, false), (.none, .none, true): rhyme = w ? .un : .an
        case (.none, .stop, false): rhyme = w ? .ut : .at
        case (.none, .velar, false): rhyme = w ? .wet : .et
        case (.none, .ng, false), (.none, .nyaSmall, false): rhyme = w ? .win : .in
        case (.none, .palatal, false): rhyme = w ? .wit : .it
        case (.none, .nyaBig, false): rhyme = .ie
        case (.i, .nasal, false), (.i, .none, true): rhyme = .ein
        case (.i, .stop, false), (.i, .velar, false), (.i, .palatal, false): rhyme = .eik
        case (.u, .nasal, false), (.u, .none, true): rhyme = .oun
        case (.u, .stop, false), (.u, .velar, false): rhyme = .ok
        case (.o, .ng, false): rhyme = .aing
        case (.o, .velar, false): rhyme = .aik
        case (.aw, .ng, false): rhyme = .aung
        case (.aw, .velar, false): rhyme = .auk
        case (.none, .la, false) where syl.stacked: rhyme = .an
        case (.none, .other, false) where syl.stacked: rhyme = .at
        default: return nil
        }
        syl.rhyme = rhyme

        // Tone.
        switch rhyme {
        case .at, .et, .it, .eik, .ok, .aik, .auk, .ut, .wet, .wit:
            syl.tone = .checked
        case .a, .wa:
            syl.tone = vis ? .high : .creaky
        case .aw:
            if dot { syl.tone = .creaky } else if vowelAsat || first == 0x102A { syl.tone = .low } else { syl.tone = .high }
        case .ai, .wai:
            if dot { syl.tone = .creaky } else if fin == 0x101A { syl.tone = .low } else { syl.tone = .high }
        default:
            syl.tone = dot ? .creaky : (vis ? .high : .low)
        }
        return syl
    }
}
