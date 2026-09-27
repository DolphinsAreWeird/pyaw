#include "host.h"

#include <shlobj.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace pyawwin {

namespace {

const wchar_t kSettingsKey[] = L"Software\\Pyaw";

DWORD ReadDword(const wchar_t* name, DWORD fallback) {
    DWORD value = 0, size = sizeof value;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
        return value;
    return fallback;
}

// Last-write time and size combined, to notice when a file changed.
ULONGLONG FileStamp(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return 0;
    ULONGLONG t = (static_cast<ULONGLONG>(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
    return t ^ (static_cast<ULONGLONG>(info.nFileSizeLow) << 1);
}

bool ReadUtf8(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(f, &size) || size.QuadPart > (64 << 20)) { CloseHandle(f); return false; }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    BOOL ok = out.empty() || ReadFile(f, &out[0], static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(f);
    out.resize(read);
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF && static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF)
        out.erase(0, 3);  // Notepad may save a BOM
    return ok != FALSE;
}

bool AppendUtf8(const std::wstring& path, const std::string& text) {
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(f);
    return ok != FALSE;
}

}  // namespace

Host& Host::Instance() {
    static Host host;
    return host;
}

pyaw::Engine* Host::Engine() {
    if (!attempted_) {
        attempted_ = true;
        engine_ = pyaw::Engine::load(Narrow(ModuleDirectory()));
        if (engine_) {
            LoadUserWords();
            LoadLearned();
        }
    }
    return engine_.get();
}

Settings Host::LoadSettings() {
    Settings s;
    s.composer.spaceMode = ReadDword(L"SpaceConfirmsWord", 0) ? pyaw::SpaceMode::Commit : pyaw::SpaceMode::Separate;
    s.composer.burmesePunctuation = ReadDword(L"BurmesePunctuation", 1) != 0;
    s.composer.burmeseDigits = ReadDword(L"BurmeseDigits", 0) != 0;
    s.composer.returnAlsoSends = ReadDword(L"ReturnAlsoSends", 0) != 0;
    s.shiftToggles = ReadDword(L"ShiftToggles", 1) != 0;
    return s;
}

void Host::SaveSetting(const wchar_t* name, DWORD value) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof value);
        RegCloseKey(key);
    }
}

std::wstring Host::DataDirectory() {
    PWSTR base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &base))) {
        dir = std::wstring(base) + L"\\Pyaw\\";
        CoTaskMemFree(base);
        CreateDirectoryW(dir.c_str(), nullptr);
    }
    return dir;
}

std::wstring Host::MyWordsPath() { return DataDirectory() + L"my-words.txt"; }
std::wstring Host::LearnedLogPath() { return DataDirectory() + L"learned.log"; }

void Host::EnsureMyWordsFile() {
    std::wstring path = MyWordsPath();
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    const char* text =
        "# My words for Pyaw: one per line, what you type, then the Burmese.\r\n"
        "# Saved changes are picked up automatically. Examples:\r\n"
        "#   mgmg      \xE1\x80\x99\xE1\x80\xB1\xE1\x80\xAC\xE1\x80\x84\xE1\x80\xBA\xE1\x80\x99\xE1\x80\xB1\xE1\x80\xAC\xE1\x80\x84\xE1\x80\xBA\r\n"
        "#   ygn       \xE1\x80\x9B\xE1\x80\x94\xE1\x80\xBA\xE1\x80\x80\xE1\x80\xAF\xE1\x80\x94\xE1\x80\xBA\r\n\r\n";
    AppendUtf8(path, text);
}

void Host::ForgetLearned() {
    DeleteFileW(LearnedLogPath().c_str());
    Host& h = Instance();
    if (h.engine_) h.engine_->learned.clear();
    h.learnedStamp_ = 0;
}

void Host::LoadUserWords() {
    std::wstring path = MyWordsPath();
    myWordsStamp_ = FileStamp(path);
    pyaw::Lexicon lex;
    std::string text;
    if (ReadUtf8(path, text)) lex.loadTSV(text, 0.0f);
    engine_->userLexicon = std::move(lex);
}

void Host::LoadLearned() {
    std::wstring path = LearnedLogPath();
    learnedStamp_ = FileStamp(path);
    engine_->learned.clear();
    std::string text;
    if (!ReadUtf8(path, text)) return;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos) continue;
        float w = std::strtof(line.c_str() + b + 1, nullptr);
        if (w > 0) engine_->learned.record(line.substr(0, a), line.substr(a + 1, b - a - 1), w);
    }
}

void Host::Learn(const std::string& key, const std::string& burmese, bool isExplicit) {
    if (!isExplicit || !engine_) return;
    engine_->learned.record(key, burmese, 2.0f);
    if (AppendUtf8(LearnedLogPath(), key + "\t" + burmese + "\t2.0\n")) learnedStamp_ = FileStamp(LearnedLogPath());
}

void Host::RefreshUserFiles() {
    if (!engine_) return;
    if (FileStamp(MyWordsPath()) != myWordsStamp_) LoadUserWords();
    if (FileStamp(LearnedLogPath()) != learnedStamp_) LoadLearned();
}

}  // namespace pyawwin
