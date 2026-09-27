# Contributing to Pyaw

Thank you for helping! Everyone writes Myanglish a little differently, so the most useful
contribution is simply telling us where Pyaw guesses wrong.

## Report a wrong conversion (no coding needed)

Open an issue with the **Wrong conversion** form: what you typed, what Pyaw gave, and what you
expected. Each report becomes a test case, so once it's fixed it stays fixed.

## Add words

[`data/lexicon/community.tsv`](data/lexicon/community.tsv) is a plain list of
`Myanglish<TAB>Burmese`. Add abbreviations, names and slang that people really type, then open a
pull request. Keep entries to spellings you've seen in use, not personal shortcuts. Personal
shortcuts belong in each user's own **My Words** list.

## Add test cases

[`data/eval/chat_style.tsv`](data/eval/chat_style.tsv) holds `input<TAB>expected` phrases
(use `|` for several accepted spellings). Please check how your change affects the score:

```bash
.build/release/pyaw-cli --eval data/eval/chat_style.tsv
```

## Development

Requires macOS 13+ and Swift 6 (the Xcode Command Line Tools are enough).

```bash
./scripts/download_data.sh      # CC-100 + chat dictionary (~50 MB)
./scripts/prepare_corpus.sh     # clean and convert the corpus (~4 min)
./scripts/build_model.sh        # build the model into build/model (~1 min)
./scripts/test.sh               # unit tests
./scripts/install.sh            # build the app and install it for yourself
.build/release/pyaw-cli -v "br lote ny ll"      # decode with candidates
.build/release/pyaw-cli --phrases "ma thwar lar" # whole-phrase readings (Tab)
```

Where things live:

| Path | What |
|---|---|
| `Sources/PyawCore/Burmese.swift`, `Syllable.swift` | Unicode normalization, syllable splitting and parsing |
| `Sources/PyawCore/Romanizer.swift` | the Myanglish spelling tables and their costs |
| `Sources/PyawCore/Engine.swift` | decoder, candidates, whole-phrase readings |
| `Sources/PyawCore/Composer.swift` | key handling (tested without the UI) |
| `Sources/PyawIME/` | the macOS input method: controller, candidate window |
| `Sources/BuildModel/` | corpus cleaning (Zawgyi→Unicode) and model building |

Changes to the spelling tables or decoder should keep, or improve, the `--eval` score, and pass
`./scripts/test.sh`.
