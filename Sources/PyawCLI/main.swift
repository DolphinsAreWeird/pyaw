import Foundation
import PyawCore

// Command-line harness for the Pyaw engine.
//   pyaw-cli [--model DIR] "br lote ny ll" ...     decode phrases and show candidates
//   pyaw-cli [--model DIR] --eval FILE.tsv [...]   accuracy on input<TAB>expected lines
//   pyaw-cli [--model DIR] --variants ကောင်း        romanizations of a syllable
//   pyaw-cli [--model DIR] --repl                  interactive

var args = Array(CommandLine.arguments.dropFirst())
var modelDir = URL(fileURLWithPath: "build/model")
if let i = args.firstIndex(of: "--model"), i + 1 < args.count {
    modelDir = URL(fileURLWithPath: args[i + 1]); args.removeSubrange(i...(i + 1))
}
var useLexicon = true
if let i = args.firstIndex(of: "--no-lexicon") { useLexicon = false; args.remove(at: i) }
var verbose = false
if let i = args.firstIndex(of: "-v") { verbose = true; args.remove(at: i) }
var endWeight: Float? = nil
if let i = args.firstIndex(of: "--end"), i + 1 < args.count { endWeight = Float(args[i + 1]); args.removeSubrange(i...(i + 1)) }
var matchWeight: Float? = nil
if let i = args.firstIndex(of: "--match"), i + 1 < args.count { matchWeight = Float(args[i + 1]); args.removeSubrange(i...(i + 1)) }

func stderr(_ s: String) { FileHandle.standardError.write((s + "\n").data(using: .utf8)!) }

let loadStart = Date()
let engine: Engine
do {
    engine = try Engine(resources: modelDir)
    if !useLexicon { engine.lexicon = Lexicon() }
    if let endWeight { engine.config.endWeight = endWeight }
    if let matchWeight { engine.config.matchWeight = matchWeight }
} catch {
    stderr("cannot load model from \(modelDir.path): \(error)"); exit(1)
}
stderr(String(format: "loaded %d syllables, %d bigrams, %d trigrams in %.2fs",
              engine.lm.vocabSize, engine.lm.bigramCount, engine.lm.trigramCount, Date().timeIntervalSince(loadStart)))

func show(_ input: String) {
    let t0 = Date()
    let r = engine.decode(input)
    let ms = Date().timeIntervalSince(t0) * 1000
    print("\(input)  →  \(r.text)   (\(String(format: "%.1f", ms)) ms)")
    guard verbose else { return }
    for (i, seg) in r.segments.enumerated() {
        let cands = engine.candidates(for: i, in: r, limit: 8)
        print("   [\(seg.text)] " + cands.map { "\($0.text) \(String(format: "%.1f", $0.score))" }.joined(separator: " | "))
    }
}

func stripSpaces(_ s: String) -> String {
    BurmeseText.normalize(s).filter { !$0.isWhitespace && !"။၊.,?!".contains($0) }
}

if args.contains("--dump") {
    // Machine-readable output for comparing engine implementations (see cpp/tools/pyaw_cli.cpp).
    while let line = readLine() {
        if line.isEmpty { continue }
        let r = engine.decode(line)
        let segs = r.segments.indices.map { k in engine.candidates(for: k, in: r, limit: 5).map(\.text).joined(separator: ",") }
        let phrases = engine.phraseOptions(line).map(\.text)
        print("\(line)\t\(r.text)\t\(segs.joined(separator: " | "))\t\(phrases.joined(separator: ","))")
    }
} else if let i = args.firstIndex(of: "--gen"), i + 2 < args.count {
    // Test inputs: real Burmese runs from a cleaned corpus, romanized with random typical spellings.
    //   --gen CORPUS.txt COUNT [SEED]
    var rng: UInt64 = i + 3 < args.count ? UInt64(args[i + 3]) ?? 1 : 1
    func next() -> UInt64 {
        rng &+= 0x9E37_79B9_7F4A_7C15
        var z = rng
        z = (z ^ (z >> 30)) &* 0xBF58_476D_1CE4_E5B9
        z = (z ^ (z >> 27)) &* 0x94D0_49BB_1331_11EB
        return z ^ (z >> 31)
    }
    func uniform() -> Double { Double(next() >> 11) / Double(1 << 53) }
    let handle = FileHandle(forReadingAtPath: args[i + 1])!
    let text = String(decoding: handle.readData(ofLength: 30 << 20), as: UTF8.self)
    let lines = text.split(separator: "\n").map { $0.split(separator: " ").map(String.init) }.filter { !$0.isEmpty }
    let count = Int(args[i + 2]) ?? 1000
    var made = 0
    while made < count {
        let line = lines[Int(next() % UInt64(lines.count))]
        let len = min(line.count, 1 + Int(next() % 5))
        let start = Int(next() % UInt64(line.count - len + 1))
        var out = ""
        var ok = true
        for (k, syl) in line[start..<(start + len)].enumerated() {
            let vs = Romanizer.variants(of: syl)
            if vs.isEmpty { ok = false; break }
            let weights = vs.map { exp(-Double($0.cost)) }
            var pick = uniform() * weights.reduce(0, +)
            var chosen = vs[0].text
            for (v, w) in zip(vs, weights) { pick -= w; if pick <= 0 { chosen = v.text; break } }
            if k > 0 && uniform() < 0.65 { out += " " }
            out += chosen
        }
        if ok { print(out); made += 1 }
    }
} else if let i = args.firstIndex(of: "--phrases") {
    for input in args[(i + 1)...] {
        let t0 = Date()
        let options = engine.phraseOptions(input)
        let ms = Date().timeIntervalSince(t0) * 1000
        print("\(input)   (\(String(format: "%.0f", ms)) ms)")
        for (n, o) in options.enumerated() {
            let why = o.segment.map { "   ← word \($0 + 1) → \(o.word ?? "")" } ?? ""
            print("  \(n + 1). \(o.text)\(why)")
        }
    }
} else if let i = args.firstIndex(of: "--variants") {
    for syl in args[(i + 1)...] {
        let v = Romanizer.variants(of: BurmeseText.normalize(syl))
        print(syl, v.prefix(30).map { "\($0.text):\(String(format: "%.1f", $0.cost))" }.joined(separator: " "))
    }
} else if let i = args.firstIndex(of: "--hits") {
    for key in args[(i + 1)...] {
        print(key, engine.romanHits(key).prefix(30).map { "\($0.0):\(String(format: "%.1f", $0.1))" }.joined(separator: " "))
    }
} else if let i = args.firstIndex(of: "--eval") {
    var total = 0, exact = 0
    var failures: [String] = []
    for path in args[(i + 1)...] {
        guard let text = try? String(contentsOfFile: path, encoding: .utf8) else { stderr("cannot read \(path)"); continue }
        for line in text.split(whereSeparator: \.isNewline) {
            let parts = line.split(separator: "\t")
            guard parts.count >= 2, !line.hasPrefix("#"), !parts[0].contains("=") else { continue }
            let got = engine.decode(String(parts[0])).text
            total += 1
            let accepted = parts[1].split(separator: "|").map { stripSpaces(String($0)) }
            if accepted.contains(stripSpaces(got)) { exact += 1 } else {
                failures.append("  \(parts[0])\n      want \(stripSpaces(String(parts[1])))\n      got  \(got)")
            }
        }
    }
    if verbose || failures.count <= 40 { failures.forEach { print($0) } } else { failures.prefix(40).forEach { print($0) } }
    print(String(format: "exact: %d/%d = %.1f%%", exact, total, 100 * Double(exact) / Double(max(total, 1))))
} else if args.contains("--repl") {
    verbose = true
    while let line = readLine() { if !line.isEmpty { show(line) } }
} else {
    verbose = true
    for a in args { show(a) }
}
