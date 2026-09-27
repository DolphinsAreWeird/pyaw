import Foundation
import PyawCore

/// Reads a text file line by line without loading it all into memory.
struct LineReader: Sequence, IteratorProtocol {
    private let handle: FileHandle
    private var buffer: [UInt8] = []
    private var pos = 0
    private var eof = false

    init?(path: String) {
        guard let h = FileHandle(forReadingAtPath: path) else { return nil }
        handle = h
    }

    mutating func next() -> String? {
        while true {
            if let nl = buffer[pos...].firstIndex(of: 0x0A) {
                let line = String(decoding: buffer[pos..<nl], as: UTF8.self)
                pos = nl + 1
                return line
            }
            if eof {
                guard pos < buffer.count else { return nil }
                let line = String(decoding: buffer[pos...], as: UTF8.self)
                pos = buffer.count
                return line
            }
            // Compact, then refill.
            buffer.removeFirst(pos)
            pos = 0
            let chunk = handle.readData(ofLength: 4 << 20)
            if chunk.isEmpty { eof = true } else { buffer.append(contentsOf: chunk) }
        }
    }
}

/// Builds the trigram syllable model from cleaned corpora (one syllable run per line,
/// syllables separated by spaces).
enum LMBuilder {
    typealias Token = LanguageModel.Token
    static let bos = LanguageModel.bos, eos = LanguageModel.eos, unk = LanguageModel.unk

    struct Corpus { let path: String; let weight: Int }

    static func countSyllables(_ corpora: [Corpus]) -> [String: Int] {
        var counts: [String: Int] = [:]
        for c in corpora {
            guard let reader = LineReader(path: c.path) else { log("cannot read \(c.path)"); continue }
            for line in reader {
                for syl in line.split(separator: " ") { counts[String(syl), default: 0] += c.weight }
            }
        }
        return counts
    }

    /// Trigram counts for one corpus. Each run is BOS BOS w1 ... wn EOS.
    static func trigramCounts(_ corpus: Corpus, index: [String: Token]) -> (keys: [UInt64], counts: [UInt32]) {
        var keys: [UInt64] = []
        guard let reader = LineReader(path: corpus.path) else { return ([], []) }
        var tokens = 0
        for line in reader {
            var u = bos, v = bos
            let syls = line.split(separator: " ")
            if syls.isEmpty { continue }
            for s in syls {
                let w = index[String(s)] ?? unk
                keys.append(UInt64(u) << 32 | UInt64(v) << 16 | UInt64(w))
                u = v; v = w
            }
            keys.append(UInt64(u) << 32 | UInt64(v) << 16 | UInt64(eos))
            tokens += syls.count
        }
        let result = uniqueCounts(&keys)
        log("  \(corpus.path): \(tokens) syllables, \(result.keys.count) trigram types, weight \(corpus.weight)")
        return (result.keys, result.counts.map { $0 * UInt32(corpus.weight) })
    }

    /// Merges sorted (key, count) lists, summing counts of equal keys.
    static func merge(_ a: (keys: [UInt64], counts: [UInt32]), _ b: (keys: [UInt64], counts: [UInt32])) -> (keys: [UInt64], counts: [UInt32]) {
        var keys: [UInt64] = [], counts: [UInt32] = []
        keys.reserveCapacity(a.keys.count + b.keys.count)
        counts.reserveCapacity(a.keys.count + b.keys.count)
        var i = 0, j = 0
        while i < a.keys.count || j < b.keys.count {
            if j >= b.keys.count || (i < a.keys.count && a.keys[i] < b.keys[j]) {
                keys.append(a.keys[i]); counts.append(a.counts[i]); i += 1
            } else if i >= a.keys.count || b.keys[j] < a.keys[i] {
                keys.append(b.keys[j]); counts.append(b.counts[j]); j += 1
            } else {
                keys.append(a.keys[i]); counts.append(a.counts[i] + b.counts[j]); i += 1; j += 1
            }
        }
        return (keys, counts)
    }

    /// Sorted unique keys with their counts.
    static func uniqueCounts<K: Comparable>(_ keys: inout [K]) -> (keys: [K], counts: [UInt32]) {
        keys.sort()
        var outK: [K] = [], outC: [UInt32] = []
        var i = 0
        while i < keys.count {
            var j = i + 1
            while j < keys.count && keys[j] == keys[i] { j += 1 }
            outK.append(keys[i]); outC.append(UInt32(j - i))
            i = j
        }
        keys = []
        return (outK, outC)
    }

    static func discount(_ counts: [UInt32]) -> Float {
        var n1 = 0, n2 = 0
        for c in counts { if c == 1 { n1 += 1 } else if c == 2 { n2 += 1 } }
        guard n1 + 2 * n2 > 0 else { return 0.5 }
        return min(0.95, max(0.1, Float(n1) / Float(n1 + 2 * n2)))
    }

    static func build(vocab: [String], trigrams: (keys: [UInt64], counts: [UInt32]), minBigram: UInt32, minTrigram: UInt32) -> LanguageModel.Tables {
        let V = vocab.count
        var tri = trigrams
        log("  trigram types: \(tri.keys.count)")

        // --- Bigram raw counts and continuation counts N1+(• v w), marginalized from trigrams
        var pairs: [UInt64] = []
        pairs.reserveCapacity(tri.keys.count)
        for (k, c) in zip(tri.keys, tri.counts) { pairs.append((k & 0xFFFF_FFFF) << 32 | UInt64(c)) }
        pairs.sort()
        var biKeys: [UInt32] = [], biRaw: [UInt32] = [], biCont: [UInt32] = []
        var i = 0
        while i < pairs.count {
            let key = UInt32(pairs[i] >> 32)
            var raw: UInt32 = 0, cont: UInt32 = 0
            while i < pairs.count && UInt32(pairs[i] >> 32) == key { raw += UInt32(pairs[i] & 0xFFFF_FFFF); cont += 1; i += 1 }
            biKeys.append(key); biRaw.append(raw); biCont.append(cont)
        }
        pairs = []
        log("  bigram types: \(biKeys.count)")

        // KN count for bigrams: continuation counts, except after BOS where raw counts are used.
        var biKN = biCont
        for j in 0..<biKeys.count where Token(biKeys[j] >> 16) == bos { biKN[j] = biRaw[j] }

        // --- Unigram continuation counts N1+(• w)
        var uniCont = [Double](repeating: 0, count: V)
        for k in biKeys { uniCont[Int(k & 0xFFFF)] += 1 }
        let uniTotal = uniCont.reduce(0, +)
        var p1 = [Double](repeating: 0, count: V)
        for w in 0..<V { p1[w] = 0.99 * uniCont[w] / uniTotal + 0.01 / Double(V) }
        p1[Int(bos)] = 1e-12

        // --- Interpolated bigram probabilities (for all bigram types)
        let d2 = Double(discount(biKN))
        var p2 = [Double](repeating: 0, count: biKeys.count)
        var g = 0
        while g < biKeys.count {
            let v = biKeys[g] >> 16
            var e = g, total = 0.0
            while e < biKeys.count && biKeys[e] >> 16 == v { total += Double(biKN[e]); e += 1 }
            let gamma = d2 * Double(e - g) / total
            for j in g..<e {
                let w = Int(biKeys[j] & 0xFFFF)
                p2[j] = max(Double(biKN[j]) - d2, 0) / total + gamma * p1[w]
            }
            g = e
        }

        func bigramIndex(_ key: UInt32) -> Int? {
            var lo = 0, hi = biKeys.count
            while lo < hi {
                let mid = (lo + hi) >> 1
                if biKeys[mid] < key { lo = mid + 1 } else if biKeys[mid] > key { hi = mid } else { return mid }
            }
            return nil
        }

        // --- Interpolated trigram probabilities, pruned
        let d3 = Double(discount(tri.counts))
        log(String(format: "  discounts: D2=%.3f D3=%.3f", d2, d3))
        var keptTriKeys: [UInt64] = [], keptTriP: [Double] = []
        // Per-context sums for backoff weights: key (u<<16|v) → (Σ P3 kept, Σ P2 kept)
        var ctxSums: [UInt32: (Double, Double)] = [:]
        g = 0
        while g < tri.keys.count {
            let ctx = tri.keys[g] >> 16
            var e = g, total = 0.0
            while e < tri.keys.count && tri.keys[e] >> 16 == ctx { total += Double(tri.counts[e]); e += 1 }
            let gamma = d3 * Double(e - g) / total
            let v = UInt32(ctx & 0xFFFF)
            for j in g..<e where tri.counts[j] >= minTrigram {
                let w = UInt32(tri.keys[j] & 0xFFFF)
                let lower = bigramIndex(v << 16 | w).map { p2[$0] } ?? 1e-9
                let p = max(Double(tri.counts[j]) - d3, 0) / total + gamma * lower
                keptTriKeys.append(tri.keys[j]); keptTriP.append(p)
                let c = UInt32(ctx & 0xFFFF_FFFF)
                let s = ctxSums[c] ?? (0, 0)
                ctxSums[c] = (s.0 + p, s.1 + lower)
            }
            g = e
        }
        tri = ([], [])
        log("  trigrams kept: \(keptTriKeys.count)")

        // --- Prune bigrams; always keep bigrams that are contexts of kept trigrams.
        var keep = [Bool](repeating: false, count: biKeys.count)
        for j in 0..<biKeys.count where biRaw[j] >= minBigram { keep[j] = true }
        for c in ctxSums.keys { if let j = bigramIndex(c) { keep[j] = true } }
        // Unigram backoff weights from kept bigrams.
        var sumP2 = [Double](repeating: 0, count: V), sumP1 = [Double](repeating: 0, count: V)
        for j in 0..<biKeys.count where keep[j] {
            let v = Int(biKeys[j] >> 16), w = Int(biKeys[j] & 0xFFFF)
            sumP2[v] += p2[j]; sumP1[v] += p1[w]
        }
        var uniBackoff = [Float](repeating: 0, count: V)
        for v in 0..<V {
            let num = max(1 - sumP2[v], 1e-6), den = max(1 - sumP1[v], 1e-6)
            uniBackoff[v] = Float(log(num / den))
        }

        var outBiKeys: [UInt32] = [], outBiP: [Float] = [], outBiB: [Float] = []
        // The BOS BOS context is not a real bigram but needs a backoff weight.
        let bosCtx = UInt32(bos) << 16 | UInt32(bos)
        var extra: [(UInt32, Float, Float)] = []
        if let s = ctxSums[bosCtx] {
            extra.append((bosCtx, -99, Float(log(max(1 - s.0, 1e-6) / max(1 - s.1, 1e-6)))))
        }
        var e = 0
        for j in 0..<biKeys.count where keep[j] {
            while e < extra.count && extra[e].0 < biKeys[j] { outBiKeys.append(extra[e].0); outBiP.append(extra[e].1); outBiB.append(extra[e].2); e += 1 }
            outBiKeys.append(biKeys[j]); outBiP.append(Float(log(p2[j])))
            if let s = ctxSums[biKeys[j]] {
                outBiB.append(Float(log(max(1 - s.0, 1e-6) / max(1 - s.1, 1e-6))))
            } else {
                outBiB.append(0)
            }
        }
        while e < extra.count { outBiKeys.append(extra[e].0); outBiP.append(extra[e].1); outBiB.append(extra[e].2); e += 1 }
        log("  bigrams kept: \(outBiKeys.count)")

        return LanguageModel.Tables(
            vocab: vocab,
            uniLogProb: p1.map { Float(log($0)) },
            uniBackoff: uniBackoff,
            biKeys: outBiKeys, biLogProb: outBiP, biBackoff: outBiB,
            triKeys: keptTriKeys, triLogProb: keptTriP.map { Float(log($0)) })
    }
}
