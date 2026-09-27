import Foundation
import PyawCore

let args = CommandLine.arguments
let usage = """
usage:
  build-model clean [priorLines]         < raw.txt  > clean.txt     (Zawgyi-aware cleaning)
  build-model diagnose [N]               < raw.txt                  (show classification samples)
  build-model build <outDir> <mapping.json> [--lexicon extra.tsv] <clean.txt[:weight]>...
"""

guard args.count >= 2 else { log(usage); exit(2) }

switch args[1] {
case "clean":
    // First batch builds the syllable prior from clearly-Unicode lines, then everything is resolved.
    let priorLines = args.count > 2 ? Int(args[2]) ?? 300_000 : 300_000
    let start = Date()
    var prior = SyllablePrior()
    var pending = parallelMap(readBatch(priorLines), Cleaner.classify)
    for c in pending { if case .definite(let r) = c { prior.add(r) } }
    log("prior: \(prior.counts.count) syllable types from \(prior.total) tokens")

    var counts: [Cleaner.Outcome: Int] = [:]
    var lines = 0, syllables = 0
    let out = OutputBuffer()
    while true {
        for c in pending {
            let (runs, outcome) = prior.resolve(c)
            counts[outcome, default: 0] += 1
            for run in runs where !run.isEmpty {
                syllables += run.count
                out.write(run.joined(separator: " "))
                out.write("\n")
            }
        }
        lines += pending.count
        let batch = readBatch(50_000)
        if batch.isEmpty { break }
        pending = parallelMap(batch, Cleaner.classify)
    }
    out.flush()
    let secs = Int(Date().timeIntervalSince(start))
    log("lines=\(lines) unicode=\(counts[.unicode, default: 0]) keptOriginal=\(counts[.keptOriginal, default: 0]) " +
        "converted=\(counts[.converted, default: 0]) repaired=\(counts[.repaired, default: 0]) " +
        "dropped=\(counts[.dropped, default: 0]) empty=\(counts[.empty, default: 0]) syllables=\(syllables) (\(secs)s)")

case "diagnose":
    let limit = args.count > 2 ? Int(args[2]) ?? 20 : 20
    let lines = readBatch(Int.max)
    let classes = parallelMap(lines, Cleaner.classify)
    var prior = SyllablePrior()
    for c in classes { if case .definite(let r) = c { prior.add(r) } }
    var shown: [Cleaner.Outcome: Int] = [:]
    for (line, c) in zip(lines, classes) {
        guard case .ambiguous = c else { continue }
        let (runs, outcome) = prior.resolve(c)
        if shown[outcome, default: 0] < limit {
            shown[outcome, default: 0] += 1
            log("\(outcome): \(line)\n   -> \(runs.map { $0.joined() }.joined(separator: " | "))")
        }
    }

case "build":
    guard args.count >= 5 else { log(usage); exit(2) }
    let outDir = URL(fileURLWithPath: args[2])
    try FileManager.default.createDirectory(at: outDir, withIntermediateDirectories: true)
    let start = Date()

    log("== lexicon")
    var (lexicon, tests) = try LexiconBuilder.build(path: args[3])
    // Community word list (roman<TAB>burmese[<TAB>cost]) contributed by users.
    var corpusArgs = Array(args[4...])
    if let i = corpusArgs.firstIndex(of: "--lexicon"), i + 1 < corpusArgs.count {
        let text = try String(contentsOfFile: corpusArgs[i + 1], encoding: .utf8)
        let before = lexicon.entries.values.reduce(0) { $0 + $1.count }
        lexicon.load(tsv: text, defaultCost: 0.3)
        log("  + \(lexicon.entries.values.reduce(0) { $0 + $1.count } - before) entries from \(corpusArgs[i + 1])")
        corpusArgs.removeSubrange(i...(i + 1))
    }

    let corpora = corpusArgs.map { spec -> LMBuilder.Corpus in
        let parts = spec.split(separator: ":")
        return LMBuilder.Corpus(path: String(parts[0]), weight: parts.count > 1 ? Int(parts[1]) ?? 1 : 1)
    }
    log("== vocabulary")
    let counts = LMBuilder.countSyllables(corpora)
    var forced = Set<String>()
    for entries in lexicon.entries.values {
        for e in entries { for s in BurmeseText.syllables(e.burmese) { forced.insert(s) } }
    }
    let minCount = 4
    var words = counts.filter { ($0.value >= minCount || forced.contains($0.key)) && Syllable.parse($0.key) != nil }
        .sorted { $0.value != $1.value ? $0.value > $1.value : $0.key < $1.key }
        .map { $0.key }
    for f in forced.sorted() where counts[f] == nil && Syllable.parse(f) != nil { words.append(f) }
    if words.count > 65_000 { words = Array(words.prefix(65_000)) }
    let vocab = ["<s>", "</s>", "<unk>"] + words
    var index: [String: LanguageModel.Token] = [:]
    for (i, w) in vocab.enumerated() { index[w] = LanguageModel.Token(i) }
    log("  \(counts.count) syllable types seen, vocabulary \(vocab.count) (min count \(minCount))")

    log("== counting trigrams")
    var merged: (keys: [UInt64], counts: [UInt32]) = ([], [])
    for c in corpora { merged = LMBuilder.merge(merged, LMBuilder.trigramCounts(c, index: index)) }

    log("== smoothing")
    let tables = LMBuilder.build(vocab: vocab, trigrams: merged, minBigram: 2, minTrigram: 3)
    merged = ([], [])
    let modelData = LanguageModel.serialize(tables)
    try modelData.write(to: outDir.appendingPathComponent("model.bin"))
    try lexicon.serialized().write(to: outDir.appendingPathComponent("lexicon.tsv"), atomically: true, encoding: .utf8)
    let testText = tests.map { "\($0.input)\t\($0.expected)" }.joined(separator: "\n") + "\n"
    try testText.write(to: outDir.appendingPathComponent("tests.tsv"), atomically: true, encoding: .utf8)
    log(String(format: "== wrote model.bin (%.1f MB), lexicon.tsv, tests.tsv in %ds",
               Double(modelData.count) / 1e6, Int(Date().timeIntervalSince(start))))

default:
    log(usage); exit(2)
}
