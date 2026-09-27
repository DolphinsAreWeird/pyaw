import Foundation

/// Remembers which Burmese the user chose for what they typed, so their personal spelling
/// habits win over time. Keys are normalized segment strings ("ll", "kyayzu").
public struct LearnedStore: Sendable {
    public private(set) var weights: [String: [String: Float]] = [:]
    public private(set) var maxKeyLength = 0

    public init() {}

    public mutating func record(key: String, burmese: String, weight: Float) {
        guard !key.isEmpty, !burmese.isEmpty, !key.contains(" ") else { return }
        var forKey = weights[key] ?? [:]
        forKey[burmese, default: 0] += weight
        // Other outputs for the same spelling fade a little, so a correction sticks.
        for (other, w) in forKey where other != burmese { forKey[other] = w * 0.8 }
        forKey = forKey.filter { $0.value >= 0.05 }
        weights[key] = forKey
        maxKeyLength = max(maxKeyLength, key.count)
    }

    public func entries(for key: String) -> [(String, Float)] {
        guard let forKey = weights[key] else { return [] }
        return forKey.map { ($0.key, $0.value) }
    }

    public mutating func removeAll() { weights = [:]; maxKeyLength = 0 }

    public var count: Int { weights.values.reduce(0) { $0 + $1.count } }

    // MARK: Persistence (tab-separated: key, burmese, weight)

    public func serialized() -> String {
        var lines: [String] = []
        for key in weights.keys.sorted() {
            for (b, w) in weights[key]!.sorted(by: { $0.value > $1.value }) {
                lines.append("\(key)\t\(b)\t\(String(format: "%.2f", w))")
            }
        }
        return lines.joined(separator: "\n") + "\n"
    }

    public static func parse(_ text: String) -> LearnedStore {
        var store = LearnedStore()
        for line in text.split(whereSeparator: \.isNewline) {
            let parts = line.split(separator: "\t")
            guard parts.count == 3, let w = Float(parts[2]) else { continue }
            let key = String(parts[0])
            store.weights[key, default: [:]][String(parts[1])] = w
            store.maxKeyLength = max(store.maxKeyLength, key.count)
        }
        return store
    }
}
