// Per-process state shared by all Pyaw text-service instances: the engine, settings and the
// user's files (%APPDATA%\Pyaw).
#pragma once

#include <memory>
#include <string>

#include "../../cpp/pyaw/composer.hpp"
#include "../../cpp/pyaw/engine.hpp"
#include "globals.h"

namespace pyawwin {

struct Settings {
    pyaw::ComposerSettings composer;
    bool shiftToggles = true;
};

class Host {
public:
    static Host& Instance();

    /// Loads model.bin / lexicon.tsv from the DLL's folder on first use; nullptr if missing.
    pyaw::Engine* Engine();

    static Settings LoadSettings();
    static void SaveSetting(const wchar_t* name, DWORD value);

    /// Remembers an explicitly picked word (appended to learned.log).
    void Learn(const std::string& key, const std::string& burmese, bool isExplicit);
    /// Reloads my-words.txt and learned.log if another process or the user changed them.
    void RefreshUserFiles();

    static std::wstring DataDirectory();
    static std::wstring MyWordsPath();
    static std::wstring LearnedLogPath();
    static void EnsureMyWordsFile();
    static void ForgetLearned();

private:
    Host() = default;
    void LoadUserWords();
    void LoadLearned();

    std::unique_ptr<pyaw::Engine> engine_;
    bool attempted_ = false;
    ULONGLONG myWordsStamp_ = 0;
    ULONGLONG learnedStamp_ = 0;
};

}  // namespace pyawwin
