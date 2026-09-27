import Cocoa
import PyawCore

/// Owns the shared engine (loaded once in the background), the user's learned choices and
/// custom words, and the settings shown in the input menu.
final class EngineHost {
    static let shared = EngineHost()

    private let queue = DispatchQueue(label: "pyaw.engine", qos: .userInitiated)
    private let lock = NSLock()
    private var _engine: Engine?
    private var saveScheduled = false
    private var userWordsModified: Date?

    var engine: Engine? {
        lock.lock(); defer { lock.unlock() }
        return _engine
    }

    // MARK: Files

    let supportDirectory: URL = {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let dir = base.appendingPathComponent("Pyaw", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }()

    var learnedURL: URL { supportDirectory.appendingPathComponent("learned.tsv") }
    var userWordsURL: URL { supportDirectory.appendingPathComponent("my-words.txt") }

    // MARK: Settings

    private let defaults = UserDefaults.standard

    var settings: ComposerSettings {
        var s = ComposerSettings()
        s.spaceMode = SpaceMode(rawValue: defaults.string(forKey: "spaceMode") ?? "") ?? .separate
        s.burmesePunctuation = defaults.object(forKey: "burmesePunctuation") as? Bool ?? true
        s.burmeseDigits = defaults.object(forKey: "burmeseDigits") as? Bool ?? false
        s.returnAlsoSends = defaults.object(forKey: "returnAlsoSends") as? Bool ?? false
        return s
    }

    func set(spaceMode: SpaceMode) { defaults.set(spaceMode.rawValue, forKey: "spaceMode") }
    func set(burmesePunctuation: Bool) { defaults.set(burmesePunctuation, forKey: "burmesePunctuation") }
    func set(burmeseDigits: Bool) { defaults.set(burmeseDigits, forKey: "burmeseDigits") }
    func set(returnAlsoSends: Bool) { defaults.set(returnAlsoSends, forKey: "returnAlsoSends") }

    var shiftTogglesEnglish: Bool {
        get { defaults.object(forKey: "shiftTogglesEnglish") as? Bool ?? true }
        set { defaults.set(newValue, forKey: "shiftTogglesEnglish") }
    }

    // MARK: Loading

    func loadInBackground() {
        queue.async {
            guard let resources = Bundle.main.resourceURL else { return }
            do {
                let start = Date()
                let engine = try Engine(resources: resources)
                if let text = try? String(contentsOf: self.learnedURL, encoding: .utf8) {
                    engine.learned = LearnedStore.parse(text)
                }
                self.loadUserWords(into: engine)
                self.lock.lock(); self._engine = engine; self.lock.unlock()
                NSLog("Pyaw: engine ready in %.2fs (%d syllables)", Date().timeIntervalSince(start), engine.lm.vocabSize)
            } catch {
                NSLog("Pyaw: failed to load engine: \(error)")
            }
        }
    }

    private func loadUserWords(into engine: Engine) {
        ensureUserWordsFile()
        let attrs = try? FileManager.default.attributesOfItem(atPath: userWordsURL.path)
        userWordsModified = attrs?[.modificationDate] as? Date
        var lex = Lexicon()
        if let text = try? String(contentsOf: userWordsURL, encoding: .utf8) { lex.load(tsv: text, defaultCost: 0) }
        engine.userLexicon = lex
    }

    /// Reloads my-words.txt if it was edited since it was last read.
    func reloadUserWordsIfChanged() {
        guard let engine else { return }
        let attrs = try? FileManager.default.attributesOfItem(atPath: userWordsURL.path)
        let modified = attrs?[.modificationDate] as? Date
        if modified != userWordsModified { loadUserWords(into: engine) }
    }

    func ensureUserWordsFile() {
        guard !FileManager.default.fileExists(atPath: userWordsURL.path) else { return }
        let template = """
        # My words for Pyaw — one per line: what you type, then the Burmese.
        # Saved changes are picked up automatically. Examples:
        #   mgmg      မောင်မောင်
        #   ygn       ရန်ကုန်
        #   tks       ကျေးဇူးတင်ပါတယ်

        """
        try? template.write(to: userWordsURL, atomically: true, encoding: .utf8)
    }

    // MARK: Learning

    /// Remembers a word the user explicitly picked. Accepted defaults are not recorded: the model
    /// already prefers them, and reinforcing them would make context-dependent choices rigid.
    func learn(segment: String, burmese: String, explicit: Bool) {
        guard let engine, explicit else { return }
        engine.learned.record(key: segment, burmese: burmese, weight: 2.0)
        scheduleSave()
    }

    func forgetLearned() {
        engine?.learned.removeAll()
        try? FileManager.default.removeItem(at: learnedURL)
    }

    private func scheduleSave() {
        guard !saveScheduled else { return }
        saveScheduled = true
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self] in
            guard let self else { return }
            self.saveScheduled = false
            guard let engine = self.engine else { return }
            let text = engine.learned.serialized()
            let url = self.learnedURL
            self.queue.async { try? text.write(to: url, atomically: true, encoding: .utf8) }
        }
    }
}
