import Foundation
import Testing
@testable import PyawCore

/// These tests need the built model (run scripts/build_model.sh first); they are skipped otherwise.
let modelDirectory: URL = {
    URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
        .deletingLastPathComponent().appendingPathComponent("build/model")
}()
let sharedEngine: Engine? = try? Engine(resources: modelDirectory)

@Suite("Decoding", .enabled(if: sharedEngine != nil, "model not built"))
struct EngineTests {
    let engine = sharedEngine!

    @Test(arguments: [
        ("br lote ny ll", "ဘာလုပ်နေလဲ"),
        ("min ny kg lr", "မင်းနေကောင်းလား"),
        ("kyayzu tin pr tl", "ကျေးဇူးတင်ပါတယ်"),
        ("chit tl", "ချစ်တယ်"),
        ("sr p p lr", "စားပြီးပြီလား"),
        ("mingalarpar", "မင်္ဂလာပါ"),
        ("kyay zu tin par tal", "ကျေးဇူးတင်ပါတယ်"),
        ("kya naw", "ကျွန်တော်"),
        ("hma:", "မှား"),
    ])
    func decodes(_ input: String, _ expected: String) {
        #expect(engine.decode(input).text == expected)
    }

    @Test func keepsOutputPerSegment() {
        let r = engine.decode("br lote ny ll")
        #expect(r.outputs == ["ဘာ", "လုပ်", "နေ", "လဲ"])
    }

    @Test func offersAlternativesForASegment() {
        let r = engine.decode("ma thi bu")
        let cands = engine.candidates(for: 1, in: r).map(\.text)
        #expect(cands.first == "သိ")
        #expect(cands.contains("ထိ"))
    }

    @Test func phraseOptionsStartWithTheBestReading() {
        let options = engine.phraseOptions("ma thwar lar")
        #expect(options.first?.text == engine.decode("ma thwar lar").text)
        #expect(Set(options.map(\.text)).count == options.count)
        // Every word gets an alternative, so whichever word is wrong can be fixed from the list.
        #expect(Set(options.compactMap(\.segment)) == [0, 1, 2])
        #expect(options.map(\.text).contains("မသွားလား"))
    }

    @Test func spacesMeanSyllableBreaks() {
        // "lo at" is two syllables, so the one-syllable dictionary word လုပ် (loat) must not match.
        #expect(engine.decode("lo at").text != "လုပ်")
    }

    @Test func pinsOverrideTheDecoder() {
        let r = engine.decode("br lote ny ll", pins: [3: "လယ်"])
        #expect(r.text == "ဘာလုပ်နေလယ်")
    }
}

@Suite("Composer keys", .enabled(if: sharedEngine != nil, "model not built"))
struct ComposerTests {
    func composer() -> Composer { Composer(engine: { sharedEngine }) }

    func type(_ c: Composer, _ text: String) -> String {
        var out = ""
        for ch in text {
            let key: ComposerKey = ch == " " ? .space : .letter(ch)
            out += c.handle(key).commit
        }
        return out
    }

    @Test func spaceSeparatesAndReturnCommits() {
        let c = composer()
        #expect(type(c, "br lote ny ll") == "")
        #expect(c.preview == "ဘာလုပ်နေလဲ")
        let out = c.handle(.enter)
        #expect(out.commit == "ဘာလုပ်နေလဲ" && out.handled)
        #expect(!c.isComposing)
        // With nothing being composed, Return goes to the app (new line / send).
        #expect(c.handle(.enter).handled == false)
    }

    @Test func doubleSpaceCommitsWithASpace() {
        let c = composer()
        _ = type(c, "hote kae")
        #expect(c.handle(.space).commit == "")
        #expect(c.handle(.space).commit == "ဟုတ်ကဲ့ ")
    }

    @Test func digitPicksACandidateForTheFocusedWord() throws {
        let c = composer()
        _ = type(c, "ma thi bu")
        _ = c.handle(.left)                       // focus "thi"
        #expect(c.focusedSegment == 1)
        let idx = try #require(c.candidates.firstIndex { $0.text == "ထိ" })
        _ = c.handle(.digit(idx % 9 + 1 + 0))    // candidates on first page
        if idx < 9 { #expect(c.preview.contains("ထိ")) }
    }

    @Test func pickingAWordLeavesTheOtherWordsAlone() throws {
        let c = composer()
        _ = type(c, "ma thwar lar")
        let before = try #require(c.result?.outputs)
        #expect(c.focusedSegment == 2)
        _ = c.handle(.digit(2))
        let after = try #require(c.result?.outputs)
        #expect(after[0] == before[0] && after[1] == before[1] && after[2] != before[2])
    }

    @Test func punctuationCommitsAndMapsToBurmese() {
        let c = composer()
        _ = type(c, "thwar ml")
        #expect(c.handle(.punctuation(".")).commit == "သွားမယ်။")
    }

    @Test func shiftReturnKeepsLatin() {
        let c = composer()
        _ = type(c, "OK")
        #expect(!c.preview.contains("O"))           // shown as Burmese while typing…
        #expect(c.handle(.rawEnter).commit == "OK")  // …but Shift+Return keeps the letters as typed
    }

    @Test func pinyinModeCommitsOnSpace() {
        let c = composer()
        c.settings.spaceMode = .commit
        _ = type(c, "chit")
        #expect(c.handle(.space).commit == "ချစ်")
    }

    @Test func returnCanAlsoSend() {
        let c = composer()
        c.settings.returnAlsoSends = true
        _ = type(c, "hote")
        let out = c.handle(.enter)
        #expect(out.commit == "ဟုတ်" && out.handled == false)
    }

    @Test func tabCyclesWholePhraseReadingsAndEscapeGoesBack() {
        let c = composer()
        _ = type(c, "ma thwar lar")
        let original = c.preview
        _ = c.handle(.tab)
        #expect(c.listMode == .phrase)
        #expect(c.preview == c.phraseOptions[1].text && c.preview != original)
        #expect(c.highlighted == 1)
        _ = c.handle(.backTab)
        #expect(c.preview == original)
        _ = c.handle(.tab)
        _ = c.handle(.escape)                     // back to the original reading, still typing
        #expect(c.listMode == .word && c.isComposing && c.preview == original)
    }

    @Test func pickingAWholePhraseReadingWithADigit() throws {
        let c = composer()
        _ = type(c, "ma thwar lar")
        _ = c.handle(.tab)
        let i = try #require(c.phraseOptions.firstIndex { $0.text == "မသွားလား" })
        _ = c.handle(.digit(i + 1))
        #expect(c.preview == "မသွားလား")
        #expect(c.handle(.enter).commit == "မသွားလား")
    }

    @Test func typingAfterTabKeepsTheChosenWord() throws {
        let c = composer()
        _ = type(c, "ma thwar")
        _ = c.handle(.tab)
        let option = c.phraseOptions[1]
        let segment = try #require(option.segment), word = try #require(option.word)
        _ = type(c, " lar")
        #expect(c.listMode == .word)
        #expect(c.result?.outputs[segment] == word)
    }

    @Test func tabOnASingleWordBrowsesItsOptions() {
        let c = composer()
        _ = type(c, "nin")
        let first = c.preview
        _ = c.handle(.tab)
        #expect(c.listMode == .word && c.highlighted == 1 && c.preview != first)
    }

    @Test func backspaceEditsAndEscapeCancels() {
        let c = composer()
        _ = type(c, "chitt")
        _ = c.handle(.backspace)
        #expect(c.raw == "chit")
        _ = c.handle(.escape)
        #expect(!c.isComposing)
    }
}
