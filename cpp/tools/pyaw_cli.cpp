// Command-line harness for the C++ engine (mirrors Sources/PyawCLI):
//   pyaw-cli-cpp [--model DIR] "br lote ny ll" ...   decode and show candidates
//   pyaw-cli-cpp [--model DIR] --eval FILE.tsv       exact-match accuracy
//   pyaw-cli-cpp [--model DIR] --dump < inputs.txt   machine-readable output for comparing engines
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../pyaw/burmese.hpp"
#include "../pyaw/engine.hpp"

using namespace pyaw;

static std::string strip(const std::string& s) {
    // Normalized, without whitespace or punctuation, for comparing outputs.
    std::string n = normalize_burmese(s), out;
    static const std::vector<std::string> drop = {" ", "\t", "\xE1\x81\x8B", "\xE1\x81\x8A", ".", ",", "?", "!"};
    size_t i = 0;
    while (i < n.size()) {
        bool skipped = false;
        for (auto& d : drop)
            if (n.compare(i, d.size(), d) == 0) { i += d.size(); skipped = true; break; }
        if (!skipped) out.push_back(n[i++]);
    }
    return out;
}

static std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) { if (i) s += sep; s += v[i]; }
    return s;
}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string modelDir = "build/model";
    bool useLexicon = true;
    for (size_t i = 0; i < args.size();) {
        if (args[i] == "--model" && i + 1 < args.size()) { modelDir = args[i + 1]; args.erase(args.begin() + i, args.begin() + i + 2); }
        else if (args[i] == "--no-lexicon") { useLexicon = false; args.erase(args.begin() + i); }
        else ++i;
    }
    auto t0 = std::chrono::steady_clock::now();
    auto engine = Engine::load(modelDir);
    if (!engine) { std::fprintf(stderr, "cannot load model from %s\n", modelDir.c_str()); return 1; }
    if (!useLexicon) engine->lexicon = Lexicon();
    double loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "loaded %zu syllables, %zu bigrams, %zu trigrams in %.0f ms\n", engine->lm().vocabSize(),
                 engine->lm().bigramCount(), engine->lm().trigramCount(), loadMs);

    if (!args.empty() && args[0] == "--dump") {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            DecodeResult r = engine->decode(line);
            std::vector<std::string> segs;
            for (int k = 0; k < static_cast<int>(r.segments.size()); ++k) {
                std::vector<std::string> texts;
                for (auto& c : engine->candidates(k, r, 5)) texts.push_back(c.text);
                segs.push_back(join(texts, ","));
            }
            std::vector<std::string> phrases;
            for (auto& o : engine->phraseOptions(line)) phrases.push_back(o.text);
            std::cout << line << "\t" << r.text() << "\t" << join(segs, " | ") << "\t" << join(phrases, ",") << "\n";
        }
        return 0;
    }
    if (!args.empty() && args[0] == "--eval") {
        int total = 0, exact = 0;
        for (size_t f = 1; f < args.size(); ++f) {
            std::ifstream in(args[f]);
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty() || line[0] == '#') continue;
                auto tab = line.find('\t');
                if (tab == std::string::npos) continue;
                std::string input = line.substr(0, tab), expected = line.substr(tab + 1);
                if (input.find('=') != std::string::npos) continue;
                std::string got = engine->decode(input).text();
                ++total;
                bool ok = false;
                std::stringstream alts(expected);
                std::string alt;
                while (std::getline(alts, alt, '|')) if (strip(alt) == strip(got)) ok = true;
                if (ok) ++exact;
                else std::cout << "  " << input << "\n      want " << strip(expected) << "\n      got  " << got << "\n";
            }
        }
        std::printf("exact: %d/%d = %.1f%%\n", exact, total, total ? 100.0 * exact / total : 0.0);
        return 0;
    }
    for (auto& input : args) {
        auto s = std::chrono::steady_clock::now();
        DecodeResult r = engine->decode(input);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count();
        std::printf("%s  →  %s   (%.1f ms)\n", input.c_str(), r.text().c_str(), ms);
        for (int k = 0; k < static_cast<int>(r.segments.size()); ++k) {
            std::printf("   [%s]", r.segments[k].text.c_str());
            for (auto& c : engine->candidates(k, r, 8)) std::printf(" %s %.1f |", c.text.c_str(), c.score);
            std::printf("\n");
        }
    }
    return 0;
}
