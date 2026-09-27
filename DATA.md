# Data sources

The MIT license covers Pyaw's source code. This file covers the data the language model and
lexicon are built from.

The released model (`model.bin`) holds **syllable statistics**: probabilities of up to three
consecutive Burmese syllables. It does not contain the texts themselves.

## In the released model

| Data | Used for | Terms |
|---|---|---|
| [CC-100](https://data.statmt.org/cc-100/) Burmese | language model | The CC-100 maintainers release their packaging under CC0 and claim no rights to the underlying web text, which comes from [Common Crawl](https://commoncrawl.org/terms-of-use). |
| [myanglish_mapping.json](https://github.com/AUNGSWANOOgit/myanglish) by AUNGSWANOOgit | chat-spelling lexicon (`kg` → ကောင်း, `woo` → ဘူး …) | Its README invites people to "use and modify freely for your own project". |
| [`data/lexicon/community.tsv`](data/lexicon/community.tsv) | extra words from Pyaw contributors | MIT, like the code. |

CC-100 references: A. Conneau et al., *Unsupervised Cross-lingual Representation Learning at
Scale*, ACL 2020; G. Wenzek et al., *CCNet: Extracting High Quality Monolingual Datasets from
Web Crawl Data*, LREC 2020.

## Optional, for personal builds only

`scripts/download_data.sh --with-subtitles` adds the OPUS OpenSubtitles Burmese corpus
(P. Lison and J. Tiedemann, *OpenSubtitles2016*, LREC 2016). Its everyday dialogue makes
chat-style typing noticeably more accurate. The subtitles themselves are copyrighted and the
corpus has no clear license, so models built with it are not published here.
