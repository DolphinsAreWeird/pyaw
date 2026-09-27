// End-to-end test of the installed (regsvr32-registered) Pyaw text service: opens a window with
// a TSF-enabled rich edit box, switches this process to Pyaw, types with SendInput and checks
// the Burmese that arrives.   typing_test.exe   (exit code 0 = all passed)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <msctf.h>
#include <richedit.h>

// Not in MinGW-w64's msctf.h.
#ifndef TF_IPPMF_FORPROCESS
#define TF_IPPMF_FORPROCESS 0x10000000
#endif
#ifndef TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE
#define TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE 0x00000004
#endif

#include <cstdio>
#include <string>
#include <vector>

namespace {

const CLSID kClsid = {0xe05ec474, 0x487b, 0x4788, {0xad, 0x78, 0xd2, 0x38, 0xb5, 0x6d, 0x28, 0x33}};
const GUID kProfile = {0x5080d84a, 0x8835, 0x4182, {0x96, 0x01, 0x29, 0x6f, 0x42, 0x8f, 0x17, 0xfc}};

HWND g_edit = nullptr;

void Pump(DWORD ms) {
    const DWORD end = GetTickCount() + ms;
    MSG msg;
    while (GetTickCount() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
}

void Key(WORD vk, bool shift = false) {
    std::vector<INPUT> in;
    auto add = [&](WORD k, bool up) {
        INPUT i = {};
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = k;
        i.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        in.push_back(i);
    };
    if (shift) add(VK_SHIFT, false);
    add(vk, false);
    add(vk, true);
    if (shift) add(VK_SHIFT, true);
    SendInput(static_cast<UINT>(in.size()), in.data(), sizeof(INPUT));
    Pump(40);
}

void Type(const char* text) {
    for (const char* p = text; *p; ++p) {
        char c = *p;
        if (c == ' ') Key(VK_SPACE);
        else if (c >= 'a' && c <= 'z') Key(static_cast<WORD>('A' + (c - 'a')));
        else if (c >= '1' && c <= '9') Key(static_cast<WORD>(c));
        else if (c == '.') Key(VK_OEM_PERIOD);
        else if (c == '\n') Key(VK_RETURN);
        else if (c == '\t') Key(VK_TAB);
    }
}

std::wstring Text() {
    int n = GetWindowTextLengthW(g_edit);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(g_edit, &s[0], n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

void Clear() {
    SetWindowTextW(g_edit, L"");
    Pump(100);
}

std::string Hex(const std::wstring& s) {
    std::string out;
    char buf[8];
    for (wchar_t c : s) { std::snprintf(buf, sizeof buf, "%04X ", static_cast<unsigned>(c)); out += buf; }
    return out;
}

int g_failures = 0;

void Expect(const char* name, const std::wstring& want) {
    std::wstring got = Text();
    bool ok = got == want;
    std::printf("%s %s\n   got:  %s\n", ok ? "PASS" : "FAIL", name, Hex(got).c_str());
    if (!ok) { std::printf("   want: %s\n", Hex(want).c_str()); ++g_failures; }
    std::fflush(stdout);
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProcW(h, m, w, l);
}

}  // namespace

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LoadLibraryW(L"Msftedit.dll");
    WNDCLASSW wc = {};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PyawTypingTest";
    RegisterClassW(&wc);
    HWND main = CreateWindowExW(0, wc.lpszClassName, L"Pyaw typing test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100,
                                640, 240, nullptr, nullptr, wc.hInstance, nullptr);
    g_edit = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE, 10, 10, 600, 160,
                             main, nullptr, wc.hInstance, nullptr);
    SendMessageW(g_edit, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);
    ShowWindow(main, SW_SHOW);
    SetForegroundWindow(main);
    SetFocus(g_edit);
    Pump(300);

    ITfInputProcessorProfileMgr* mgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&mgr));
    if (SUCCEEDED(hr) && mgr) {
        TF_INPUTPROCESSORPROFILE info = {};
        HRESULT got = mgr->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0455, kClsid, kProfile, nullptr, &info);
        std::printf("GetProfile: 0x%08lX (flags 0x%lX, hkl %p)\n", static_cast<unsigned long>(got),
                    static_cast<unsigned long>(info.dwFlags), static_cast<void*>(info.hkl));
        hr = mgr->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0455, kClsid, kProfile, nullptr,
                                  TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
        std::printf("ActivateProfile: 0x%08lX\n", static_cast<unsigned long>(hr));
        mgr->Release();
    }
    if (FAILED(hr)) {
        // Older API: enable for this user, switch the thread's language, activate.
        ITfInputProcessorProfiles* old = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(&old))) && old) {
            HRESULT e = old->EnableLanguageProfile(kClsid, 0x0455, kProfile, TRUE);
            HRESULT c = old->ChangeCurrentLanguage(0x0455);
            hr = old->ActivateLanguageProfile(kClsid, 0x0455, kProfile);
            std::printf("EnableLanguageProfile 0x%08lX, ChangeCurrentLanguage 0x%08lX, ActivateLanguageProfile 0x%08lX\n",
                        static_cast<unsigned long>(e), static_cast<unsigned long>(c), static_cast<unsigned long>(hr));
            old->Release();
        }
    }
    std::printf("foreground ok: %d\n", GetForegroundWindow() == main);
    std::fflush(stdout);
    if (FAILED(hr)) return 2;
    SetFocus(g_edit);
    Pump(500);

    Type("br lote ny ll\n");
    Pump(300);
    Expect("br lote ny ll + Return", L"\x1018\x102C\x101C\x102F\x1015\x103A\x1014\x1031\x101C\x1032");

    Clear();
    Type("thwar ml.");
    Pump(300);
    Expect("thwar ml + . (punctuation commits)", L"\x101E\x103D\x102C\x1038\x1019\x101A\x103A\x104B");

    Clear();
    Type("ma thwar lar\t\n");
    Pump(300);
    std::wstring tabbed = Text();
    std::printf("%s Tab gives another reading: %s\n", tabbed.size() > 3 ? "PASS" : "FAIL", Hex(tabbed).c_str());
    if (tabbed.size() <= 3) ++g_failures;

    Clear();
    Type("hote kae  ok");
    Key(VK_RETURN, true);  // Shift+Return keeps Latin
    Pump(300);
    Expect("double space commits with a space, Shift+Return keeps Latin",
           L"\x101F\x102F\x1010\x103A\x1000\x1032\x1037 ok");

    std::printf("%s\n", g_failures ? "SOME TESTS FAILED" : "ALL TYPING TESTS PASSED");
    DestroyWindow(main);
    CoUninitialize();
    return g_failures ? 1 : 0;
}
