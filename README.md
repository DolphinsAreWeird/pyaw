# Pyaw (ပြော)

**Type Burmese on your Mac or Windows PC the way you already write Myanglish.** Pyaw is a native
keyboard (input method) that turns Latin-letter Burmese into Unicode Burmese as you type, the
way Pinyin works for Chinese. It sits in the system's keyboard menu like any other keyboard.

```
br lote ny ll        →  ဘာလုပ်နေလဲ
min ny kg lr         →  မင်းနေကောင်းလား
kyayzu tin pr tl     →  ကျေးဇူးတင်ပါတယ်
mingalarpar          →  မင်္ဂလာပါ
```

There's no fixed spelling to learn. Full spellings (`bar lote nay lal`), spellings that follow
the sound (`ba lok ne le`) and chat abbreviations (`pr` `tl` `ny` `kg` `ll` `lr` `woo` `p`) all
work, mixed freely. A language model trained on millions of Burmese sentences picks the most
likely reading from context, and you can fix any word with a number key.

## မြန်မာလို

**ပြော (Pyaw)** က Mac နဲ့ Windows ကွန်ပျူတာတွေအတွက် မြန်မာစာ ရိုက်စနစ်တစ်ခုပါ။ Myanglish ကို
သင်ရိုက်နေကျအတိုင်း ရိုက်ရုံနဲ့ Unicode မြန်မာစာ ဖြစ်သွားပါမယ်။

**ထည့်သွင်းနည်း (Mac)** — Terminal ကိုဖွင့်ပြီး အောက်က command ကို ရိုက်ထည့်ပါ။ ပြီးရင်
System Settings → Keyboard → Text Input → Edit… → + → Burmese → Pyaw ကို ထည့်ပါ။

**ထည့်သွင်းနည်း (Windows)** — [Releases](https://github.com/DolphinsAreWeird/pyaw/releases) ကနေ
`Pyaw-…-Windows-Setup.exe` ကို download လုပ်ပြီး run ပါ။ ပြီးရင် Windows key + Space နဲ့ Pyaw ကို ရွေးပါ။

**အသုံးပြုနည်း**
- Space — စကားလုံးခွဲရန် · Return — မြန်မာစာ ထည့်ရန်
- 1–9 — ရွေးစရာထဲက ရွေးရန် · Tab — စာကြောင်းတစ်ကြောင်းလုံးအတွက် အခြားရွေးစရာများ
- Shift+Return — ရိုက်ထားတဲ့ အင်္ဂလိပ်စာလုံးအတိုင်း ထည့်ရန်

မှားနေတဲ့ စကားလုံးတွေ တွေ့ရင် [Issue](https://github.com/DolphinsAreWeird/pyaw/issues/new?template=wrong-conversion.yml)
မှာ ပြောပြပေးပါ။ သင်ရိုက်တာနဲ့ လိုချင်တဲ့ မြန်မာစာကို ရေးပေးရုံပါပဲ။

## Install on Mac

Open **Terminal** and paste:

```bash
curl -fsSL https://raw.githubusercontent.com/DolphinsAreWeird/pyaw/main/scripts/get.sh | bash
```

Then, once: **System Settings → Keyboard → Text Input → Input Sources: Edit… → + → Burmese →
Pyaw → Add**. Switch to it with Control-Space, the Globe key, or the **မ** icon in the menu bar.
Running the same command again updates Pyaw.

Pyaw works on macOS 13 or later, on both Apple Silicon and Intel Macs.

<details>
<summary>Installing from the downloaded zip instead</summary>

Download `Pyaw-…-macOS.zip` from [Releases](https://github.com/DolphinsAreWeird/pyaw/releases),
unzip it, and move `Pyaw.app` into `~/Library/Input Methods` (in Finder: Go → Go to Folder…).
Pyaw isn't signed with a paid Apple developer certificate, so macOS blocks it after a browser
download. Clear that once in Terminal:

```bash
xattr -dr com.apple.quarantine ~/Library/Input\ Methods/Pyaw.app
~/Library/Input\ Methods/Pyaw.app/Contents/MacOS/Pyaw --register
```
</details>

## Install on Windows

Download **`Pyaw-…-Windows-Setup.exe`** from [Releases](https://github.com/DolphinsAreWeird/pyaw/releases)
and run it. Keep **"Add Pyaw to my keyboards"** checked. Then press **Windows key + Space**, or
click the language indicator on the taskbar, and choose **Burmese – Pyaw**.

The installer isn't signed with a paid code-signing certificate yet, so Windows may show
"Windows protected your PC". Click **More info → Run anyway**. Apps that were already open may
need a restart before they see the new keyboard.

Options, My Words and help: Start menu → **Pyaw Settings**. Pyaw works on Windows 10 and 11
(64-bit), in both 64- and 32-bit apps.

## Typing

| Key | While typing (underlined text) | Otherwise |
|---|---|---|
| letters | type Myanglish; the Burmese preview updates live | start typing |
| Space | separate words | normal space |
| Space Space | insert the Burmese and a space | |
| Return | insert the Burmese | normal Return (new line / send) |
| Shift-Return | insert what you typed, as Latin letters | |
| 1–9 or click | pick a suggestion for the highlighted word | |
| ↑ ↓ | browse suggestions (the preview follows) | |
| ← → | move between words of the phrase | |
| Tab / Shift-Tab | other readings of the whole phrase (1–9 picks, Esc goes back) | normal Tab |
| − = | previous / next page of suggestions | |
| . , | insert, then ။ / ၊ | ။ / ၊ right after Burmese text |
| Esc | cancel | |
| tap Shift | switch English ↔ Burmese | |

Tone hints: add `:` after a syllable for း and `'` for ့ (`hma:` → မှား, `nga'` → ငါ့).

Options are in the input menu on Mac and in **Pyaw Settings** (Start menu) on Windows:
Pinyin-style Space (Space confirms each word), "Return inserts and sends" (one press in chat
apps), Burmese digits, Burmese punctuation, and the Shift toggle.

**It learns.** When you pick a suggestion, Pyaw remembers that choice for what you typed.
**My Words** (Edit My Words… in the menu or in Pyaw Settings) is a text file for your own
shortcuts, e.g. `ygn ရန်ကုန်`. It's picked up as soon as you save. Both live in
`~/Library/Application Support/Pyaw/` on Mac and `%APPDATA%\Pyaw\` on Windows.

## Accuracy

Exact whole-phrase matches of the released model (`pyaw-cli --eval`):

| Test set | Accuracy |
|---|---|
| [`data/eval/chat_style.tsv`](data/eval/chat_style.tsv): 92 everyday chat phrases | 95.7% |
| 195 test cases shipped with the myanglish mapping | 85.6% |

When a guess is wrong, the right word is usually one number key or one Tab away. Every
[reported mistake](https://github.com/DolphinsAreWeird/pyaw/issues/new?template=wrong-conversion.yml)
becomes a test case.

## How it works

- **Burmese text** (`Sources/PyawCore/Burmese.swift`, `Syllable.swift`): Unicode
  normalization, syllable splitting (stacked consonants are split, so `ကမ္ဘာ` is two spoken
  syllables), and a strict parser into onset + medials + rhyme + tone.
- **Romanizer** (`Romanizer.swift`): for every Burmese syllable, the Myanglish spellings people
  use, each with a cost for how typical it is. That covers spellings that follow the letters
  (`kyay`), the sound (`jay`), voiced forms (`ba` for ပါ) and chat abbreviations that drop the
  vowel (`kg` for ကောင်း).
- **Language model** (`LanguageModel.swift`): a trigram model over syllables with Kneser-Ney
  smoothing, memory-mapped from `model.bin`.
- **Lexicon** (`Lexicon.swift`): direct chat mappings such as `woo` → ဘူး and `kya naw` →
  ကျွန်တော်, including words typed with spaces between their syllables.
- **Decoder** (`Engine.swift`): a beam search over the possible readings, weighing how typical
  the spelling is against how likely the Burmese is. It also produces ranked alternatives for
  each word, and whole-phrase readings for Tab.
- **Composer** (`Composer.swift`): the key-handling state machine, testable without AppKit.
- **Input method, Mac** (`Sources/PyawIME`): InputMethodKit controller, inline marked text, and
  the candidate window.
- **Windows** (`cpp/`, `windows/`): a C++ port of the engine that reads the same `model.bin`.
  It's checked against the Swift engine on thousands of inputs for identical output. On top of it
  sits a TSF text service with a candidate window, a settings window, and an Inno Setup
  installer. GitHub Actions builds it with MSVC and runs an end-to-end typing test on Windows.

The corpus cleaner converts legacy **Zawgyi** text to Unicode with macOS's ICU transform. When a
line reads as valid in both encodings, a syllable-frequency model decides which one it is.

## Building from source

Requires the Xcode Command Line Tools (`xcode-select --install`).

```bash
./scripts/download_data.sh    # training data (~50 MB)
./scripts/prepare_corpus.sh   # clean and convert (~4 min)
./scripts/build_model.sh      # build the model into build/model (~1 min)
./scripts/test.sh             # unit tests
./scripts/install.sh          # build the app and install it
```

On Windows (Visual Studio 2022 with C++, CMake, Inno Setup), with the model files in
`windows/installer/model/`:

```bat
cmake -S windows -B build/win-msvc/x64 -A x64 -DPYAW_SUFFIX=64
cmake --build build/win-msvc/x64 --config Release
cmake -S windows -B build/win-msvc/x86 -A Win32 -DPYAW_SUFFIX=32
cmake --build build/win-msvc/x86 --config Release --target pyaw
iscc windows\installer\pyaw.iss
```

`download_data.sh --with-subtitles` followed by `build_model.sh --with-subtitles` builds a
more chat-accurate model that also uses movie subtitles (98.9% on the chat set). That one is for
personal use only; see [DATA.md](DATA.md).

## Uninstall

**Mac:** remove Pyaw in System Settings → Keyboard → Text Input, then delete
`~/Library/Input Methods/Pyaw.app` (and `~/Library/Application Support/Pyaw` for your learned
words).

**Windows:** Settings → Apps → Installed apps → Pyaw → Uninstall.

## Contributing

Reports of wrong conversions are the most valuable help, and no coding is needed. See
[CONTRIBUTING.md](CONTRIBUTING.md).

## License and credits

Code: [MIT](LICENSE). Data sources and their terms: [DATA.md](DATA.md). The chat-spelling lexicon
builds on [myanglish](https://github.com/AUNGSWANOOgit/myanglish) by AUNGSWANOOgit.
