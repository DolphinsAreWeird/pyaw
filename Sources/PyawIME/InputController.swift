import Carbon
import Cocoa
import InputMethodKit
import PyawCore

/// One instance per text-input client. Maps key events onto the Composer and renders the
/// composition inline (as marked text) plus the candidate panel.
@objc(PyawInputController)
final class PyawInputController: IMKInputController {
    private lazy var composer: Composer = {
        let c = Composer(engine: { EngineHost.shared.engine })
        c.onLearn = { segment, burmese, explicit in
            EngineHost.shared.learn(segment: segment, burmese: burmese, explicit: explicit)
        }
        return c
    }()

    private var hasMarkedText = false
    private var englishMode = false
    private var shiftIsDown = false
    private var shiftChorded = false
    private var shiftDownAt = Date.distantPast
    private var lastCaret = NSRect.zero

    // MARK: Lifecycle

    override func activateServer(_ sender: Any!) {
        super.activateServer(sender)
        composer.settings = EngineHost.shared.settings
        composer.context = []
        EngineHost.shared.reloadUserWordsIfChanged()
    }

    override func deactivateServer(_ sender: Any!) {
        finishComposition(sender as? IMKTextInput)
        CandidatePanel.shared.hide()
        super.deactivateServer(sender)
    }

    /// The system asks us to end the composition (e.g. the user clicked elsewhere).
    override func commitComposition(_ sender: Any!) {
        finishComposition(sender as? IMKTextInput)
    }

    private func finishComposition(_ sender: IMKTextInput?) {
        guard composer.isComposing, let client = sender ?? (self.client() as IMKTextInput?) else { return }
        let out = composer.handle(.enter)
        insert(out.commit, into: client)
        CandidatePanel.shared.hide()
    }

    override func recognizedEvents(_ sender: Any!) -> Int {
        Int(NSEvent.EventTypeMask([.keyDown, .flagsChanged]).rawValue)
    }

    // MARK: Events

    override func handle(_ event: NSEvent!, client sender: Any!) -> Bool {
        guard let event, let client = sender as? IMKTextInput else { return false }
        if event.type == .flagsChanged { handleFlags(event, client); return false }
        guard event.type == .keyDown else { return false }
        if shiftIsDown { shiftChorded = true }

        let mods = event.modifierFlags.intersection(.deviceIndependentFlagsMask)
        if mods.contains(.command) || mods.contains(.control) {
            // Shortcuts belong to the app; don't lose what was being typed.
            if composer.isComposing { finishComposition(client) }
            return false
        }
        if englishMode { return false }
        if mods.contains(.capsLock) && !composer.isComposing { return false }

        guard let key = composerKey(for: event, mods: mods) else {
            return composer.isComposing  // swallow unrelated keys (F-keys, etc.) mid-composition
        }
        let out = composer.handle(key)
        if !out.commit.isEmpty { insert(out.commit, into: client) }
        render(client)
        return out.handled
    }

    private func composerKey(for event: NSEvent, mods: NSEvent.ModifierFlags) -> ComposerKey? {
        let composing = composer.isComposing
        switch Int(event.keyCode) {
        case kVK_Return, kVK_ANSI_KeypadEnter:
            return mods.contains(.shift) || mods.contains(.option) ? .rawEnter : .enter
        case kVK_Delete: return .backspace
        case kVK_Escape: return .escape
        case kVK_Space: return .space
        case kVK_Tab: return composing ? (mods.contains(.shift) ? .backTab : .tab) : nil
        case kVK_UpArrow: return .up
        case kVK_DownArrow: return .down
        case kVK_LeftArrow: return .left
        case kVK_RightArrow: return .right
        case kVK_PageUp: return .pageUp
        case kVK_PageDown: return .pageDown
        case kVK_ForwardDelete, kVK_Home, kVK_End: return composing ? nil : .punctuation("\u{0}")
        default: break
        }
        guard let chars = event.characters, let c = chars.first, chars.count == 1 else { return nil }
        if mods.contains(.option) { return .punctuation(c) }
        if c.isASCII && c.isLetter { return .letter(c) }  // case is kept for Shift+Return
        if let d = c.wholeNumberValue, c.isASCII { return .digit(d) }
        if composing {
            switch c {
            case "'", ":": return .toneMark(c)
            case "-": return .pageUp
            case "=": return .pageDown
            default: break
            }
        }
        return .punctuation(c)
    }

    /// Shift pressed and released on its own toggles English (pass-through) mode.
    private func handleFlags(_ event: NSEvent, _ client: IMKTextInput) {
        let isShift = event.keyCode == UInt16(kVK_Shift) || event.keyCode == UInt16(kVK_RightShift)
        let shiftNow = event.modifierFlags.contains(.shift)
        if isShift && shiftNow && !shiftIsDown {
            shiftIsDown = true; shiftChorded = false; shiftDownAt = Date()
        } else if isShift && !shiftNow && shiftIsDown {
            shiftIsDown = false
            if !shiftChorded && Date().timeIntervalSince(shiftDownAt) < 0.5 && EngineHost.shared.shiftTogglesEnglish {
                toggleEnglish(client)
            }
        } else if !isShift {
            shiftChorded = true
        }
    }

    private func toggleEnglish(_ client: IMKTextInput) {
        if composer.isComposing { finishComposition(client) }
        englishMode.toggle()
        composer.context = []
        ModeBadge.shared.flash(englishMode ? "English" : "မြန်မာ", burmese: !englishMode, near: caretRect(client))
    }

    // MARK: Rendering

    private func insert(_ text: String, into client: IMKTextInput) {
        guard !text.isEmpty else {
            if hasMarkedText { clearMarkedText(client) }
            return
        }
        client.insertText(text, replacementRange: NSRange(location: NSNotFound, length: 0))
        hasMarkedText = false
    }

    private func clearMarkedText(_ client: IMKTextInput) {
        client.setMarkedText("", selectionRange: NSRange(location: 0, length: 0),
                             replacementRange: NSRange(location: NSNotFound, length: 0))
        hasMarkedText = false
    }

    private func markAttributes(_ style: Int, _ range: NSRange) -> [NSAttributedString.Key: Any] {
        var result: [NSAttributedString.Key: Any] = [:]
        if let dict = mark(forStyle: style, at: range) {
            for (k, v) in dict {
                if let key = k as? NSAttributedString.Key { result[key] = v } else if let s = k as? String { result[NSAttributedString.Key(s)] = v }
            }
        }
        if result.isEmpty { result[.underlineStyle] = NSUnderlineStyle.single.rawValue }
        return result
    }

    private func render(_ client: IMKTextInput) {
        guard composer.isComposing else {
            if hasMarkedText { clearMarkedText(client) }
            CandidatePanel.shared.hide()
            return
        }
        let preview = composer.preview
        let marked = NSMutableAttributedString(string: preview)
        let full = NSRange(location: 0, length: marked.length)
        marked.addAttributes(markAttributes(kTSMHiliteConvertedText, full), range: full)
        let ranges = composer.outputRanges
        if ranges.count > 1, composer.focusedSegment < ranges.count {
            let r = ranges[composer.focusedSegment]
            let nr = NSRange(location: r.lowerBound, length: r.count)
            if nr.length > 0 && NSMaxRange(nr) <= marked.length {
                marked.addAttributes(markAttributes(kTSMHiliteSelectedConvertedText, nr), range: nr)
            }
        }
        client.setMarkedText(marked, selectionRange: NSRange(location: marked.length, length: 0),
                             replacementRange: NSRange(location: NSNotFound, length: 0))
        hasMarkedText = true
        showCandidates(client)
    }

    private func caretRect(_ client: IMKTextInput) -> NSRect {
        var rect = NSRect.zero
        _ = client.attributes(forCharacterIndex: 0, lineHeightRectangle: &rect)
        if rect.width == 0 && rect.height == 0 { return lastCaret }
        lastCaret = rect
        return rect
    }

    private func showCandidates(_ client: IMKTextInput) {
        let result = composer.result
        let normalized = result?.normalized ?? composer.raw
        var focus: Range<Int>? = nil
        let phraseMode = composer.listMode == .phrase
        if let r = result, !phraseMode, composer.focusedSegment < r.segments.count, r.segments.count > 1 {
            focus = r.segments[composer.focusedSegment].range
        }
        var hint: String? = nil
        if phraseMode {
            hint = "whole phrase · ⇥ next · esc back"
        } else if (result?.segments.count ?? 0) > 1 {
            hint = "⇥ whole phrase"
        }
        let pageStart = composer.page * composer.settings.pageSize
        let content = CandidatePanel.Content(
            input: normalized,
            focus: focus,
            candidates: composer.pageCandidates.map { $0.text },
            highlighted: composer.highlighted - pageStart,
            page: composer.page,
            pageCount: composer.pageCount,
            loading: EngineHost.shared.engine == nil,
            hint: hint)
        CandidatePanel.shared.onSelect = { [weak self, weak client] index in
            guard let self, let client else { return }
            _ = self.composer.handle(.digit(index + 1))
            self.render(client)
        }
        CandidatePanel.shared.show(content, caret: caretRect(client))
    }

    // MARK: Menu

    override func menu() -> NSMenu! {
        let menu = NSMenu(title: "Pyaw")
        let s = EngineHost.shared.settings
        func item(_ title: String, _ action: Selector, on: Bool? = nil) {
            let it = NSMenuItem(title: title, action: action, keyEquivalent: "")
            it.target = self
            if let on { it.state = on ? .on : .off }
            menu.addItem(it)
        }
        item("Space separates syllables · Return confirms", #selector(useSeparateMode(_:)), on: s.spaceMode == .separate)
        item("Space confirms each word (Pinyin style)", #selector(useCommitMode(_:)), on: s.spaceMode == .commit)
        item("Return inserts and sends (one press)", #selector(toggleReturnSends(_:)), on: s.returnAlsoSends)
        menu.addItem(.separator())
        item("Burmese punctuation ( .→။  ,→၊ )", #selector(togglePunctuation(_:)), on: s.burmesePunctuation)
        item("Burmese digits (၀–၉)", #selector(toggleDigits(_:)), on: s.burmeseDigits)
        item("Tap Shift to switch English ↔ Burmese", #selector(toggleShift(_:)), on: EngineHost.shared.shiftTogglesEnglish)
        menu.addItem(.separator())
        item("Edit My Words…", #selector(editMyWords(_:)))
        item("Forget Learned Words…", #selector(forgetLearned(_:)))
        item("How to Use Pyaw", #selector(showHelp(_:)))
        return menu
    }

    @objc private func useSeparateMode(_ sender: Any?) { EngineHost.shared.set(spaceMode: .separate); composer.settings = EngineHost.shared.settings }
    @objc private func useCommitMode(_ sender: Any?) { EngineHost.shared.set(spaceMode: .commit); composer.settings = EngineHost.shared.settings }
    @objc private func togglePunctuation(_ sender: Any?) {
        EngineHost.shared.set(burmesePunctuation: !EngineHost.shared.settings.burmesePunctuation)
        composer.settings = EngineHost.shared.settings
    }
    @objc private func toggleDigits(_ sender: Any?) {
        EngineHost.shared.set(burmeseDigits: !EngineHost.shared.settings.burmeseDigits)
        composer.settings = EngineHost.shared.settings
    }
    @objc private func toggleShift(_ sender: Any?) { EngineHost.shared.shiftTogglesEnglish.toggle() }
    @objc private func toggleReturnSends(_ sender: Any?) {
        EngineHost.shared.set(returnAlsoSends: !EngineHost.shared.settings.returnAlsoSends)
        composer.settings = EngineHost.shared.settings
    }

    @objc private func editMyWords(_ sender: Any?) {
        EngineHost.shared.ensureUserWordsFile()
        let textEdit = URL(fileURLWithPath: "/System/Applications/TextEdit.app")
        NSWorkspace.shared.open([EngineHost.shared.userWordsURL], withApplicationAt: textEdit,
                                configuration: NSWorkspace.OpenConfiguration())
    }

    @objc private func forgetLearned(_ sender: Any?) {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.messageText = "Forget learned words?"
        alert.informativeText = "Pyaw will forget the choices it learned from your typing. Your “My Words” list is kept."
        alert.addButton(withTitle: "Forget")
        alert.addButton(withTitle: "Cancel")
        if alert.runModal() == .alertFirstButtonReturn { EngineHost.shared.forgetLearned() }
    }

    @objc private func showHelp(_ sender: Any?) {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.messageText = "Pyaw (ပြော): Myanglish → မြန်မာ"
        alert.informativeText = """
        Type Burmese the way you write Myanglish, e.g. “br lote ny ll” → ဘာလုပ်နေလဲ.

        Space — separate words · Return — insert the Burmese
        (Return again gives a new line / sends, as usual)
        Double Space — insert and add a space
        1–9 or click — pick a suggestion for the highlighted word
        ↑ ↓ — browse suggestions · ← → — move between words
        Tab / Shift+Tab — other readings of the whole phrase (Esc to go back)
        Shift+Return — insert what you typed in English letters
        Esc — cancel · Tap Shift — English ↔ Burmese
        . and , — ။ and ၊ after Burmese text
        Add “ : ” after a syllable for း, or “ ' ” for ့ (e.g. “tha:” → သား)

        Pyaw learns from the suggestions you pick. Add your own shortcuts with “Edit My Words…”.
        """
        alert.runModal()
    }
}
