import Testing
@testable import PyawCore

@Suite("Burmese text handling")
struct BurmeseTextTests {
    @Test func splitsSyllablesIncludingStacks() {
        #expect(BurmeseText.syllables("မင်္ဂလာပါ") == ["မင်္", "ဂ", "လာ", "ပါ"])
        #expect(BurmeseText.syllables("ကမ္ဘာ") == ["ကမ္", "ဘာ"])
        #expect(BurmeseText.syllables("ကျေးဇူးတင်ပါတယ်") == ["ကျေး", "ဇူး", "တင်", "ပါ", "တယ်"])
        #expect(BurmeseText.syllables("ယောက်ျား") == ["ယောက်ျား"])
    }

    @Test func normalizesMarkOrderAndLookalikes() {
        // ု typed before ိ, and ၀ (zero) used for ဝ
        #expect(BurmeseText.normalize("ကုိ") == "ကို")
        #expect(BurmeseText.normalize("၀ယ်") == "ဝယ်")
        #expect(BurmeseText.normalize("ကျွန်ုပ်") == "ကျွန်ုပ်")  // irregular spelling kept
    }

    @Test func parsesRhymesAndTones() throws {
        let kaung = try #require(Syllable.parse("ကောင်း"))
        #expect(kaung.rhyme == .aung && kaung.tone == .high)
        let tal = try #require(Syllable.parse("တယ်"))
        #expect(tal.rhyme == .ai && tal.tone == .low)
        let nae = try #require(Syllable.parse("နဲ့"))
        #expect(nae.rhyme == .ai && nae.tone == .creaky)
        let thwar = try #require(Syllable.parse("သွား"))
        #expect(thwar.medialW && thwar.rhyme == .waa)
        #expect(Syllable.parse("ကိုယ်")?.rhyme == .o)
        #expect(Syllable.parse("စျေး") != nil)
    }

    @Test func rejectsZawgyiAndImpossibleSpellings() {
        #expect(Syllable.parse("ေမ") == nil)   // Zawgyi-style vowel-first
        #expect(Syllable.parse("တျ") == nil)   // ya-pin cannot go on တ
        #expect(Syllable.parse("ကေါ") == nil)  // tall aa is only for ခ ဂ င ဒ ပ ဝ
        #expect(Syllable.parse("ပး") == nil)
    }

    @Test func romanizesChatAbbreviations() {
        func spellings(_ s: String) -> Set<String> { Set(Romanizer.variants(of: s).map(\.text)) }
        #expect(spellings("ကောင်း").isSuperset(of: ["kaung", "kg"]))
        #expect(spellings("ပါ").isSuperset(of: ["par", "pa", "pr"]))
        #expect(spellings("တယ်").isSuperset(of: ["tal", "tae", "tl", "te"]))
        #expect(spellings("နေ").isSuperset(of: ["nay", "ny", "ne"]))
        #expect(spellings("လဲ").isSuperset(of: ["lal", "lae", "ll", "le"]))
        #expect(spellings("ကျေး").isSuperset(of: ["kyay", "chay"]))
        #expect(spellings("လုပ်").isSuperset(of: ["lote", "lok"]))
        #expect(!spellings("အေး").contains("y"))  // a vowel-initial syllable can't be abbreviated
    }
}
