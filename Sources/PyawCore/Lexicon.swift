import Foundation

/// Direct Myanglish → Burmese word mappings (chat abbreviations like `kg` → ကောင်း, `woo` → ဘူး,
/// and user-defined words). Keys are lowercase with no spaces.
public struct Lexicon: Sendable {
    public struct Entry: Hashable, Sendable {
        public let burmese: String
        public let cost: Float
    }

    public private(set) var entries: [String: [Entry]] = [:]
    public private(set) var maxKeyLength = 0

    public init() {}

    public static func normalizeKey(_ s: String) -> String {
        String(s.lowercased().unicodeScalars.filter { ($0.value >= 0x61 && $0.value <= 0x7A) || $0 == "'" || $0 == ":" }
            .map(Character.init))
    }

    public mutating func add(roman: String, burmese: String, cost: Float) {
        let key = Self.normalizeKey(roman)
        let text = BurmeseText.normalize(burmese).filter { !$0.isWhitespace }
        guard !key.isEmpty, !text.isEmpty else { return }
        var list = entries[key] ?? []
        if let i = list.firstIndex(where: { $0.burmese == text }) {
            if cost < list[i].cost { list[i] = Entry(burmese: text, cost: cost) }
        } else {
            list.append(Entry(burmese: text, cost: cost))
        }
        entries[key] = list
        maxKeyLength = max(maxKeyLength, key.count)
    }

    public func lookup(_ key: String) -> [Entry] { entries[key] ?? [] }

    /// Loads `roman<TAB>burmese[<TAB>cost]` lines; `#` starts a comment. Spaces are accepted as the
    /// separator too, so a hand-written user file can be `kg ကောင်း`.
    public mutating func load(tsv text: String, defaultCost: Float) {
        for raw in text.split(whereSeparator: \.isNewline) {
            let line = raw.trimmingCharacters(in: .whitespaces)
            if line.isEmpty || line.hasPrefix("#") { continue }
            var parts = line.split(separator: "\t").map { $0.trimmingCharacters(in: .whitespaces) }
            if parts.count < 2 {
                // "roman burmese..." with a space: the roman part is the leading Latin run.
                let latin = line.prefix { $0.isASCII && !$0.isWhitespace }
                let rest = line.dropFirst(latin.count).trimmingCharacters(in: .whitespaces)
                parts = [String(latin), rest]
            }
            guard parts.count >= 2, !parts[0].isEmpty, !parts[1].isEmpty else { continue }
            let cost = parts.count >= 3 ? Float(parts[2]) ?? defaultCost : defaultCost
            add(roman: parts[0], burmese: parts[1], cost: cost)
        }
    }

    public func serialized() -> String {
        var lines: [String] = []
        for key in entries.keys.sorted() {
            for e in entries[key]! { lines.append("\(key)\t\(e.burmese)\t\(String(format: "%.2f", e.cost))") }
        }
        return lines.joined(separator: "\n") + "\n"
    }
}
