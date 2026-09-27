// "Pyaw Settings" window, opened from the Start menu through:
//   rundll32.exe "C:\Program Files\Pyaw\pyaw64.dll",ShowSettings
#include "globals.h"
#include "host.h"

#include <shellapi.h>

using namespace pyawwin;

namespace {

struct Option {
    int id;
    const wchar_t* label;
    const wchar_t* setting;
    DWORD fallback;
};

const Option kOptions[] = {
    {101, L"Space confirms each word (Pinyin style)", L"SpaceConfirmsWord", 0},
    {102, L"Return inserts the Burmese and sends (one press in chat apps)", L"ReturnAlsoSends", 0},
    {103, L"Burmese punctuation   ( .  \x2192  \x104B     ,  \x2192  \x104A )", L"BurmesePunctuation", 1},
    {104, L"Burmese digits (\x1040\x2013\x1049)", L"BurmeseDigits", 0},
    {105, L"Tap Shift to switch English \x2194 Burmese", L"ShiftToggles", 1},
};
constexpr int kEditWords = 201, kForget = 202, kClose = 203;

const wchar_t kHelp[] =
    L"Type Burmese the way you write Myanglish:  br lote ny ll  \x2192  \x1018\x102C\x101C\x102F\x1015\x103A\x1014\x1031\x101C\x1032\n"
    L"Space separates words \x00B7 Return inserts \x00B7 Shift+Return keeps the Latin letters\n"
    L"1\x2013" L"9 picks a suggestion \x00B7 \x2191\x2193 browse \x00B7 \x2190\x2192 move between words\n"
    L"Tab: other readings of the whole phrase \x00B7 Esc cancels";

HFONT g_font = nullptr;
UINT g_dpi = 96;

int Scale(int v) { return MulDiv(v, static_cast<int>(g_dpi), 96); }

DWORD ReadSetting(const wchar_t* name, DWORD fallback) {
    DWORD value = 0, size = sizeof value;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Pyaw", name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
        return value;
    return fallback;
}

HWND Add(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, Scale(x), Scale(y), Scale(w), Scale(h), parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_module, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    return c;
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            int y = 14;
            for (const Option& o : kOptions) {
                HWND box = Add(hwnd, L"BUTTON", o.label, BS_AUTOCHECKBOX | WS_TABSTOP, 16, y, 440, 24, o.id);
                SendMessageW(box, BM_SETCHECK, ReadSetting(o.setting, o.fallback) ? BST_CHECKED : BST_UNCHECKED, 0);
                y += 28;
            }
            y += 8;
            Add(hwnd, L"STATIC", kHelp, SS_LEFT, 16, y, 460, 76, 0);
            y += 88;
            Add(hwnd, L"BUTTON", L"Edit My Words\x2026", BS_PUSHBUTTON | WS_TABSTOP, 16, y, 130, 30, kEditWords);
            Add(hwnd, L"BUTTON", L"Forget Learned Words\x2026", BS_PUSHBUTTON | WS_TABSTOP, 156, y, 170, 30, kForget);
            Add(hwnd, L"BUTTON", L"Close", BS_DEFPUSHBUTTON | WS_TABSTOP, 376, y, 90, 30, kClose);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            for (const Option& o : kOptions) {
                if (id == o.id) {
                    bool on = SendMessageW(reinterpret_cast<HWND>(lp), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    Host::SaveSetting(o.setting, on ? 1 : 0);
                    return 0;
                }
            }
            if (id == kEditWords) {
                Host::EnsureMyWordsFile();
                ShellExecuteW(hwnd, L"open", L"notepad.exe", Host::MyWordsPath().c_str(), nullptr, SW_SHOWNORMAL);
            } else if (id == kForget) {
                if (MessageBoxW(hwnd, L"Forget the choices Pyaw learned from your typing? Your My Words list is kept.",
                                L"Pyaw", MB_OKCANCEL | MB_ICONQUESTION) == IDOK)
                    Host::ForgetLearned();
            } else if (id == kClose) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

}  // namespace

extern "C" void CALLBACK ShowSettings(HWND, HINSTANCE, LPSTR, int) {
    using SetContextFn = BOOL(WINAPI*)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (auto set = reinterpret_cast<SetContextFn>(reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))))
        set(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));  // per-monitor v2
    HDC screen = GetDC(nullptr);
    g_dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSY));
    ReleaseDC(nullptr, screen);
    g_font = CreateFontW(-MulDiv(9, static_cast<int>(g_dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    WNDCLASSEXW wc = {sizeof wc};
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = g_module;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIconW(g_module, MAKEINTRESOURCEW(kIconResourceId));
    wc.lpszClassName = L"PyawSettings";
    RegisterClassExW(&wc);

    RECT r = {0, 0, Scale(490), Scale(330)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectEx(&r, style, FALSE, 0);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Pyaw Settings", style, CW_USEDEFAULT, CW_USEDEFAULT,
                                r.right - r.left, r.bottom - r.top, nullptr, nullptr, g_module, nullptr);
    if (!hwnd) return;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DeleteObject(g_font);
}
