import Foundation

/// Keys the composer understands (the input controller maps NSEvents onto these).
public enum ComposerKey: Equatable, Sendable {
    case letter(Character)       // a–z or A–Z (decoding ignores case; Shift+Return keeps it)
    case toneMark(Character)     // ' (creaky ့) or : (high း)
    case space
    case enter
    case rawEnter                // Shift/Option+Return: commit the Latin text as typed
    case backspace
    case escape
    case up, down, left, right
    case tab, backTab            // cycle whole-phrase readings
    case pageUp, pageDown
    case digit(Int)
    case punctuation(Character)  // any other printable character
}

public enum SpaceMode: String, Sendable, CaseIterable {
    /// Space separates syllables/words; Enter commits (double space commits and adds a space).
    case separate
    /// Pinyin style: Space commits the current word.
    case commit
}

public struct ComposerSettings: Sendable {
    public var spaceMode: SpaceMode = .separate
    public var burmesePunctuation = true
    public var burmeseDigits = false
    /// Return inserts the Burmese and is then passed on to the app (new line / send in chat).
    public var returnAlsoSends = false
    public var pageSize = 9
    public init() {}
}

/// State machine for one text-input session: collects Myanglish keystrokes, keeps the decoded
/// Burmese preview and per-word candidates up to date, and decides what to commit.
public final class Composer {
    public typealias Token = LanguageModel.Token

    public struct Output: Sendable {
        /// Text to insert into the document (may be empty).
        public var commit = ""
        /// False when the key should be passed through to the application.
        public var handled = true
    }

    public var settings = ComposerSettings()
    /// Tokens committed right before this composition, used as language-model context.
    public var context: [Token] = []
    /// Called when a composition is committed: (segment text, chosen Burmese, was explicitly picked).
    public var onLearn: ((String, String, Bool) -> Void)?

    /// What the suggestion list shows: options for the focused word, or whole-phrase readings.
    public enum ListMode: Sendable { case word, phrase }

    public private(set) var raw = ""
    public private(set) var result: DecodeResult?
    /// The list shown in the panel (word options or, in phrase mode, whole-phrase readings).
    public private(set) var candidates: [Candidate] = []
    public private(set) var focusedSegment = 0
    public private(set) var highlighted = 0
    public private(set) var listMode = ListMode.word
    public private(set) var phraseOptions: [Engine.PhraseOption] = []

    private let engineProvider: () -> Engine?
    /// Fixed outputs per segment. Explicit pins are the user's choices (kept, and learned);
    /// the others only hold words steady after a pick and are dropped when the text changes.
    private var pins: [Int: (key: String, text: String, explicit: Bool)] = [:]
    private var focusIsExplicit = false
    private var lastCommitWasBurmese = false
    /// Segment pinned by the currently selected whole-phrase reading.
    private var phrasePin: Int?

    public init(engine: @escaping () -> Engine?) { engineProvider = engine }

    public var isComposing: Bool { !raw.isEmpty }
    /// What to show inline while composing.
    public var preview: String { result.map { $0.text.isEmpty ? raw : $0.text } ?? raw }
    public var segments: [Segment] { result?.segments ?? [] }
    public var page: Int { highlighted / settings.pageSize }
    public var pageCount: Int { max(1, (candidates.count + settings.pageSize - 1) / settings.pageSize) }
    public var pageCandidates: ArraySlice<Candidate> {
        let lo = page * settings.pageSize
        return candidates[min(lo, candidates.count)..<min(lo + settings.pageSize, candidates.count)]
    }

    /// Character range of each segment's output within `preview` (for inline highlighting).
    public var outputRanges: [Range<Int>] {
        guard let r = result else { return [] }
        var ranges: [Range<Int>] = []
        var pos = 0
        for o in r.outputs { let n = o.utf16.count; ranges.append(pos..<(pos + n)); pos += n }
        return ranges
    }

    public func reset() {
        raw = ""; result = nil; candidates = []; pins = [:]
        focusedSegment = 0; highlighted = 0; focusIsExplicit = false
        listMode = .word; phraseOptions = []; phrasePin = nil
    }

    // MARK: Key handling

    public func handle(_ key: ComposerKey) -> Output {
        if !isComposing { return handleIdle(key) }
        if listMode == .phrase, let out = handlePhraseMode(key) { return out }
        switch key {
        case .letter(let c):
            raw.append(c)
            focusIsExplicit = false
            refresh()
        case .toneMark(let c):
            if let last = raw.last, last.isLetter { raw.append(c); refresh() }
        case .space:
            if settings.spaceMode == .commit { return commit() }
            if raw.last == " " { var out = commit(); out.commit += " "; return out }
            raw.append(" ")
            focusIsExplicit = false
            refresh()
        case .enter:
            var out = commit()
            if settings.returnAlsoSends { out.handled = false }
            return out
        case .rawEnter:
            let text = raw.trimmingCharacters(in: .whitespaces)
            reset()
            lastCommitWasBurmese = false
            context = []
            return Output(commit: text)
        case .backspace:
            raw.removeLast()
            if raw.isEmpty { reset() } else { focusIsExplicit = false; refresh() }
        case .escape:
            reset()
        case .up:
            moveHighlight(by: -1)
        case .down:
            moveHighlight(by: 1)
        case .pageUp:
            moveHighlight(by: -settings.pageSize)
        case .pageDown:
            moveHighlight(by: settings.pageSize)
        case .left:
            moveFocus(by: -1)
        case .right:
            moveFocus(by: 1)
        case .tab, .backTab:
            let forward = key == .tab
            if segments.count > 1 {
                enterPhraseMode(selecting: forward ? 1 : -1)
            } else {
                moveHighlight(by: forward ? 1 : -1)   // one word: its options are the phrase options
            }
        case .digit(let d):
            guard d >= 1 else { return Output() }
            let i = page * settings.pageSize + d - 1
            guard i < candidates.count else { return Output() }
            choose(i, explicit: true)
        case .punctuation(let c):
            var out = commit()
            out.commit += punctuation(c) ?? String(c)
            lastCommitWasBurmese = false
            context = []
            return out
        }
        return Output()
    }

    /// Keys while whole-phrase readings are listed. Returns nil to fall through to normal handling
    /// (after leaving phrase mode with the chosen reading kept).
    private func handlePhraseMode(_ key: ComposerKey) -> Output? {
        switch key {
        case .tab: selectPhrase(highlighted + 1, wrap: true)
        case .backTab: selectPhrase(highlighted - 1, wrap: true)
        case .down: selectPhrase(highlighted + 1, wrap: false)
        case .up: selectPhrase(highlighted - 1, wrap: false)
        case .pageDown: selectPhrase(highlighted + settings.pageSize, wrap: false)
        case .pageUp: selectPhrase(highlighted - settings.pageSize, wrap: false)
        case .digit(let d):
            let i = page * settings.pageSize + d - 1
            if d >= 1 && i < phraseOptions.count { selectPhrase(i, wrap: false) }
        case .escape:
            selectPhrase(0, wrap: false)   // back to the original reading
            leavePhraseMode()
        default:
            leavePhraseMode()
            return nil
        }
        return Output()
    }

    private func enterPhraseMode(selecting step: Int) {
        guard let engine = engineProvider() else { return }
        let userPins = pins.filter { $0.value.explicit }
        let options = engine.phraseOptions(raw, context: context, pins: userPins.mapValues { $0.text },
                                           limit: settings.pageSize)
        guard options.count > 1 else { return }
        phraseOptions = options
        listMode = .phrase
        phrasePin = nil
        selectPhrase(step, wrap: true)
    }

    private func selectPhrase(_ index: Int, wrap: Bool) {
        let n = phraseOptions.count
        guard n > 0, let r = result else { return }
        let i = wrap ? ((index % n) + n) % n : max(0, min(n - 1, index))
        let option = phraseOptions[i]
        // Show exactly this reading: the changed word is the user's choice, the rest held steady.
        for (k, seg) in r.segments.enumerated() where k < option.outputs.count {
            if let p = pins[k], p.explicit, k != phrasePin { continue }
            pins[k] = (seg.text, option.outputs[k], k == option.segment)
        }
        phrasePin = option.segment
        decode()
        candidates = phraseOptions.map { Candidate(text: $0.text, tokens: [], score: 0) }
        highlighted = i
    }

    /// Returns to the per-word list; the chosen reading stays.
    private func leavePhraseMode() {
        listMode = .word
        phraseOptions = []
        phrasePin = nil
        loadCandidates()
    }

    private func handleIdle(_ key: ComposerKey) -> Output {
        switch key {
        case .letter(let c):
            raw = String(c)
            focusIsExplicit = false
            refresh()
            return Output()
        case .punctuation(let c) where lastCommitWasBurmese:
            if let p = punctuation(c) {
                lastCommitWasBurmese = false
                context = []
                return Output(commit: p)
            }
        case .digit(let d) where settings.burmeseDigits:
            lastCommitWasBurmese = false
            return Output(commit: String(UnicodeScalar(0x1040 + UInt32(d))!))
        default:
            break
        }
        lastCommitWasBurmese = false
        if key != .space { context = [] }
        return Output(handled: false)
    }

    private func punctuation(_ c: Character) -> String? {
        guard settings.burmesePunctuation else { return nil }
        switch c {
        case ".": return "။"
        case ",": return "၊"
        default: return nil
        }
    }

    // MARK: Candidates and focus

    private func moveHighlight(by delta: Int) {
        guard !candidates.isEmpty else { return }
        let i = max(0, min(candidates.count - 1, highlighted + delta))
        if i != highlighted { choose(i, explicit: true, keepCandidates: true) }
    }

    private func moveFocus(by delta: Int) {
        guard let r = result, !r.segments.isEmpty else { return }
        let f = max(0, min(r.segments.count - 1, focusedSegment + delta))
        guard f != focusedSegment else { return }
        focusedSegment = f
        focusIsExplicit = true
        loadCandidates()
    }

    private func choose(_ index: Int, explicit: Bool, keepCandidates: Bool = false) {
        guard let r = result, focusedSegment < r.segments.count, index < candidates.count else { return }
        // The other words stay exactly as shown; only the picked one changes.
        for (k, seg) in r.segments.enumerated() where pins[k] == nil { pins[k] = (seg.text, r.outputs[k], false) }
        pins[focusedSegment] = (r.segments[focusedSegment].text, candidates[index].text, explicit)
        highlighted = index
        decode()
        if !keepCandidates { loadCandidates(keepHighlight: true) }
    }

    /// Re-decodes after the raw text changed. Words held steady after a pick are released.
    private func refresh() {
        pins = pins.filter { $0.value.explicit }
        decode()
        if let r = result, !focusIsExplicit || focusedSegment >= r.segments.count {
            focusedSegment = max(0, r.segments.count - 1)
        }
        loadCandidates()
    }

    private func decode() {
        guard let engine = engineProvider() else { result = nil; return }
        // Drop pins whose segment text changed.
        let segs = Engine.segments(of: Engine.normalizeInput(raw))
        pins = pins.filter { $0.key < segs.count && segs[$0.key].text == $0.value.key }
        result = engine.decode(raw, context: context, pins: pins.mapValues { $0.text })
    }

    private func loadCandidates(keepHighlight: Bool = false) {
        guard let engine = engineProvider(), let r = result, focusedSegment < r.segments.count else {
            candidates = []; highlighted = 0; return
        }
        var list = engine.candidates(for: focusedSegment, in: r, limit: settings.pageSize * 3)
        let current = r.outputs[focusedSegment]
        if let i = list.firstIndex(where: { $0.text == current }) {
            highlighted = i
        } else {
            list.insert(Candidate(text: current, tokens: r.tokens[focusedSegment], score: 0), at: 0)
            highlighted = 0
        }
        candidates = list
        _ = keepHighlight
    }

    // MARK: Commit

    private func commit() -> Output {
        guard let r = result else {
            let text = raw.trimmingCharacters(in: .whitespaces)
            reset()
            return Output(commit: text)
        }
        let text = r.text
        for (i, seg) in r.segments.enumerated() {
            let out = r.outputs[i]
            // Only learn real Burmese (not letters kept as typed).
            guard !out.isEmpty, !out.unicodeScalars.contains(where: { $0.isASCII }) else { continue }
            onLearn?(seg.text, out, pins[i]?.explicit ?? false)
        }
        context = Array(r.allTokens.suffix(2))
        lastCommitWasBurmese = !text.isEmpty
        reset()
        return Output(commit: text)
    }
}
