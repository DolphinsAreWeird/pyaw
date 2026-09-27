import Foundation
import PyawCore

/// Cleans raw Burmese corpus lines: detects legacy Zawgyi encoding, converts it with ICU,
/// normalizes, splits into syllables and keeps only well-formed Unicode syllable runs.
enum Cleaner {
    static let zawgyiToUnicode = StringTransform(rawValue: "Zawgyi-my")

    struct Reading {
        var runs: [[String]]
        var valid: [[Bool]]
        var bad: Int
        var total: Int
    }

    static func assess(_ text: String) -> Reading {
        let runs = BurmeseText.syllableRuns(text)
        var valid: [[Bool]] = []
        var bad = 0, total = 0
        for run in runs {
            var mask = [Bool](repeating: true, count: run.count)
            for (k, syl) in run.enumerated() {
                total += 1
                guard let p = Syllable.parse(syl) else { mask[k] = false; bad += 1; continue }
                if p.stacked {
                    var ok = false
                    if k + 1 < run.count, let next = run[k + 1].unicodeScalars.first?.value {
                        ok = p.isKinzi ? MM.isConsonant(next) : BurmeseText.isPlausibleStack(upper: p.finalConsonant, lower: next)
                    }
                    if !ok { mask[k] = false; bad += 1 }
                }
            }
            valid.append(mask)
        }
        for u in text.unicodeScalars where MM.isZawgyiRange(u.value) { bad += 2 }
        return Reading(runs: runs, valid: valid, bad: bad, total: total)
    }

    enum Classification {
        case empty
        /// Clearly Unicode: valid as written, broken when read as Zawgyi.
        case definite(Reading)
        /// Needs a frequency model to decide between the text as written and its Zawgyi reading.
        case ambiguous(original: Reading, zawgyi: Reading)
    }

    static func classify(_ raw: String) -> Classification {
        guard raw.unicodeScalars.contains(where: { $0.value >= 0x1000 && $0.value <= 0x109F }) else { return .empty }
        let normalized = BurmeseText.normalize(raw)
        let a = assess(normalized)
        if a.total == 0 { return .empty }
        let zText = BurmeseText.normalize(raw.applyingTransform(zawgyiToUnicode, reverse: false) ?? raw)
        if zText == normalized { return .definite(a) }
        let b = assess(zText)
        if a.bad == 0 && b.bad > 0 { return .definite(a) }
        return .ambiguous(original: a, zawgyi: b)
    }

    enum Outcome: Hashable { case unicode, keptOriginal, converted, repaired, dropped, empty }

    /// Keeps a reading if it is (mostly) valid, cutting runs at broken syllables.
    static func finalize(_ r: Reading, outcome: Outcome) -> (runs: [[String]], outcome: Outcome) {
        if r.bad == 0 { return (r.runs, outcome) }
        guard r.total >= 8 && Double(r.bad) <= Double(r.total) * 0.08 else { return ([], .dropped) }
        var out: [[String]] = []
        for (run, mask) in zip(r.runs, r.valid) {
            var cur: [String] = []
            func keep() {
                // Single syllables left between cuts are usually debris of the broken part.
                if cur.count >= 2 || (cur.count == 1 && mask.count == 1) { out.append(cur) }
                cur = []
            }
            for (syl, ok) in zip(run, mask) {
                if ok { cur.append(syl) } else { keep() }
            }
            keep()
        }
        return (out, .repaired)
    }
}

/// Unigram syllable model used to decide which reading of an ambiguous line is real Burmese.
struct SyllablePrior {
    private(set) var counts: [String: Int] = [:]
    private(set) var total = 0

    mutating func add(_ r: Cleaner.Reading) {
        for (run, mask) in zip(r.runs, r.valid) {
            for (syl, ok) in zip(run, mask) where ok { counts[syl, default: 0] += 1; total += 1 }
        }
    }

    func logLikelihood(_ r: Cleaner.Reading) -> Double {
        let denom = Double(total) + 0.5 * Double(max(counts.count, 1)) + 1
        var ll = 0.0
        for (run, mask) in zip(r.runs, r.valid) {
            for (syl, ok) in zip(run, mask) {
                if ok { ll += log((Double(counts[syl] ?? 0) + 0.1) / denom) }
            }
        }
        // A broken syllable costs about as much as a very rare one: a single typo in real
        // Unicode text must not make a wholesale (wrong) Zawgyi reading look better.
        return ll + Double(r.bad) * log(1e-7)
    }

    func resolve(_ c: Cleaner.Classification) -> (runs: [[String]], outcome: Cleaner.Outcome) {
        switch c {
        case .empty: return ([], .empty)
        case .definite(let r): return Cleaner.finalize(r, outcome: .unicode)
        case .ambiguous(let a, let z):
            // The text as written gets the benefit of the doubt: short lines give weak evidence.
            return logLikelihood(z) > logLikelihood(a) + 4.0
                ? Cleaner.finalize(z, outcome: .converted)
                : Cleaner.finalize(a, outcome: .keptOriginal)
        }
    }
}

/// Runs `transform` on a batch of lines in parallel, preserving order.
func parallelMap<R>(_ lines: [String], _ transform: (String) -> R) -> [R] {
    let workers = ProcessInfo.processInfo.activeProcessorCount
    var results = [R?](repeating: nil, count: lines.count)
    results.withUnsafeMutableBufferPointer { buf in
        let chunk = (lines.count + workers - 1) / workers
        DispatchQueue.concurrentPerform(iterations: workers) { w in
            let lo = w * chunk, hi = min(lines.count, lo + chunk)
            guard lo < hi else { return }
            for i in lo..<hi { buf[i] = transform(lines[i]) }
        }
    }
    return results.map { $0! }
}

func readBatch(_ size: Int) -> [String] {
    var batch: [String] = []
    batch.reserveCapacity(min(size, 100_000))
    while batch.count < size, let line = readLine(strippingNewline: true) { batch.append(line) }
    return batch
}

/// Buffered writer for stdout.
final class OutputBuffer {
    private var buffer = ""
    private let handle: FileHandle
    init(_ handle: FileHandle = .standardOutput) { self.handle = handle }
    func write(_ s: String) {
        buffer += s
        if buffer.utf8.count > 1 << 20 { flush() }
    }
    func flush() {
        if !buffer.isEmpty { handle.write(buffer.data(using: .utf8)!); buffer = "" }
    }
}

func log(_ s: String) {
    FileHandle.standardError.write((s + "\n").data(using: .utf8)!)
}
