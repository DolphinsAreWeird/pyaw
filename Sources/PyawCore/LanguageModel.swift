import Foundation

/// Binary trigram syllable language model (interpolated Kneser-Ney, stored in backoff form).
///
/// File layout (little-endian, every section 8-byte aligned):
///   header: "MYLM", version, V, bigramCount, trigramCount, 3 × reserved      (8 × UInt32)
///   vocab:  UInt32 offsets[V + 1], then UTF-8 bytes
///   unigrams: Float32 logProb[V], Float32 backoff[V]
///   bigrams:  UInt32 key[B] (w1 << 16 | w2, sorted), Float32 logProb[B], Float32 backoff[B]
///   trigrams: UInt64 key[T] (w1 << 32 | w2 << 16 | w3, sorted), Float32 logProb[T]
/// All probabilities are natural logs.
public final class LanguageModel: @unchecked Sendable {
    public typealias Token = UInt16
    public static let bos: Token = 0
    public static let eos: Token = 1
    public static let unk: Token = 2
    public static let magic: UInt32 = 0x4D4C_594D  // "MYLM"

    public let vocab: [String]
    public let index: [String: Token]

    private let data: Data
    private let uniLogProb: UnsafeBufferPointer<Float>
    private let uniBackoff: UnsafeBufferPointer<Float>
    private let biKeys: UnsafeBufferPointer<UInt32>
    private let biLogProb: UnsafeBufferPointer<Float>
    private let biBackoff: UnsafeBufferPointer<Float>
    private let triKeys: UnsafeBufferPointer<UInt64>
    private let triLogProb: UnsafeBufferPointer<Float>

    public var vocabSize: Int { vocab.count }
    public var bigramCount: Int { biKeys.count }
    public var trigramCount: Int { triKeys.count }

    public enum LoadError: Error { case badFormat(String) }

    public convenience init(contentsOf url: URL) throws {
        try self.init(data: try Data(contentsOf: url, options: .alwaysMapped))
    }

    public init(data: Data) throws {
        self.data = data
        let base = data.withUnsafeBytes { $0.baseAddress! }
        var offset = 0
        func u32(_ at: Int) -> UInt32 { base.loadUnaligned(fromByteOffset: at, as: UInt32.self) }
        guard data.count >= 32, u32(0) == Self.magic, u32(4) == 1 else { throw LoadError.badFormat("header") }
        let v = Int(u32(8)), b = Int(u32(12)), t = Int(u32(16))
        offset = 32
        func align() { offset = (offset + 7) & ~7 }
        func buffer<T>(_ type: T.Type, _ count: Int) throws -> UnsafeBufferPointer<T> {
            align()
            let size = MemoryLayout<T>.stride * count
            guard offset + size <= data.count else { throw LoadError.badFormat("truncated") }
            let p = UnsafeBufferPointer(start: (base + offset).assumingMemoryBound(to: T.self), count: count)
            offset += size
            return p
        }
        let offsets = try buffer(UInt32.self, v + 1)
        let textStart = offset
        var words: [String] = []
        words.reserveCapacity(v)
        for i in 0..<v {
            let lo = textStart + Int(offsets[i]), hi = textStart + Int(offsets[i + 1])
            words.append(String(decoding: UnsafeRawBufferPointer(start: base + lo, count: hi - lo), as: UTF8.self))
        }
        offset = textStart + Int(offsets[v])
        vocab = words
        var idx: [String: Token] = [:]
        for (i, w) in words.enumerated() { idx[w] = Token(i) }
        index = idx
        uniLogProb = try buffer(Float.self, v)
        uniBackoff = try buffer(Float.self, v)
        biKeys = try buffer(UInt32.self, b)
        biLogProb = try buffer(Float.self, b)
        biBackoff = try buffer(Float.self, b)
        triKeys = try buffer(UInt64.self, t)
        triLogProb = try buffer(Float.self, t)
    }

    @inline(__always) private static func search<K: Comparable>(_ keys: UnsafeBufferPointer<K>, _ key: K) -> Int? {
        var lo = 0, hi = keys.count
        while lo < hi {
            let mid = (lo + hi) >> 1
            let k = keys[mid]
            if k < key { lo = mid + 1 } else if k > key { hi = mid } else { return mid }
        }
        return nil
    }

    public func unigramLogProb(_ w: Token) -> Float { uniLogProb[Int(w)] }

    public func bigramLogProb(_ w: Token, after v: Token) -> Float {
        if let i = Self.search(biKeys, UInt32(v) << 16 | UInt32(w)) { return biLogProb[i] }
        return uniBackoff[Int(v)] + uniLogProb[Int(w)]
    }

    /// ln P(w | u v)
    public func logProb(_ w: Token, _ u: Token, _ v: Token) -> Float {
        if let i = Self.search(triKeys, UInt64(u) << 32 | UInt64(v) << 16 | UInt64(w)) { return triLogProb[i] }
        var backoff: Float = 0
        if let j = Self.search(biKeys, UInt32(u) << 16 | UInt32(v)) { backoff = biBackoff[j] }
        return backoff + bigramLogProb(w, after: v)
    }

    public func token(_ syllable: String) -> Token? { index[syllable] }

    // MARK: Writing

    public struct Tables {
        public var vocab: [String]
        public var uniLogProb: [Float]
        public var uniBackoff: [Float]
        public var biKeys: [UInt32]
        public var biLogProb: [Float]
        public var biBackoff: [Float]
        public var triKeys: [UInt64]
        public var triLogProb: [Float]
        public init(vocab: [String], uniLogProb: [Float], uniBackoff: [Float], biKeys: [UInt32], biLogProb: [Float],
                    biBackoff: [Float], triKeys: [UInt64], triLogProb: [Float]) {
            self.vocab = vocab; self.uniLogProb = uniLogProb; self.uniBackoff = uniBackoff
            self.biKeys = biKeys; self.biLogProb = biLogProb; self.biBackoff = biBackoff
            self.triKeys = triKeys; self.triLogProb = triLogProb
        }
    }

    public static func serialize(_ t: Tables) -> Data {
        var out = Data()
        func append<T>(_ values: [T]) {
            while out.count % 8 != 0 { out.append(0) }
            values.withUnsafeBytes { out.append(contentsOf: $0) }
        }
        let header: [UInt32] = [magic, 1, UInt32(t.vocab.count), UInt32(t.biKeys.count), UInt32(t.triKeys.count), 0, 0, 0]
        append(header)
        var offsets: [UInt32] = [0]
        var text = Data()
        for w in t.vocab { text.append(contentsOf: Array(w.utf8)); offsets.append(UInt32(text.count)) }
        append(offsets)
        out.append(text)
        append(t.uniLogProb); append(t.uniBackoff)
        append(t.biKeys); append(t.biLogProb); append(t.biBackoff)
        append(t.triKeys); append(t.triLogProb)
        return out
    }
}
