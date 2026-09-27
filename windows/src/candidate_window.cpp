#include "candidate_window.h"

#include <dwmapi.h>

#include <algorithm>

namespace pyawwin {

namespace {

const wchar_t kClassName[] = L"PyawCandidateWindow";
constexpr UINT_PTR kFlashTimer = 1;

UINT DpiFor(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (fn && hwnd) {
        UINT dpi = fn(hwnd);
        if (dpi) return dpi;
    }
    HDC dc = GetDC(nullptr);
    UINT dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSY));
    ReleaseDC(nullptr, dc);
    return dpi ? dpi : 96;
}

bool SystemUsesDarkMode() {
    DWORD value = 1, size = sizeof value;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}

SIZE TextSize(HDC dc, HFONT font, const std::wstring& text) {
    HGDIOBJ old = SelectObject(dc, font);
    RECT r = {0, 0, 0, 0};
    DrawTextW(dc, text.empty() ? L" " : text.c_str(), -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
    return {r.right - r.left, r.bottom - r.top};
}

}  // namespace

CandidateWindow::~CandidateWindow() { Destroy(); }

void CandidateWindow::Destroy() {
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    for (HFONT* f : {&burmeseFont_, &uiFont_, &uiBold_}) {
        if (*f) { DeleteObject(*f); *f = nullptr; }
    }
    fontDpi_ = 0;
}

bool CandidateWindow::EnsureWindow() {
    if (hwnd_) return true;
    WNDCLASSEXW wc = {sizeof wc};
    if (!GetClassInfoExW(g_module, kClassName, &wc)) {
        wc = {sizeof wc};
        wc.style = CS_DROPSHADOW | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = g_module;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
    }
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"", WS_POPUP, 0, 0, 10, 10,
                            nullptr, nullptr, g_module, this);
    if (!hwnd_) return false;
    const DWORD round = 2;  // DWMWCP_ROUND (Windows 11; ignored elsewhere)
    DwmSetWindowAttribute(hwnd_, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof round);
    return true;
}

void CandidateWindow::UpdateFonts() {
    UINT dpi = DpiFor(hwnd_);
    if (dpi == fontDpi_ && burmeseFont_) return;
    for (HFONT* f : {&burmeseFont_, &uiFont_, &uiBold_}) {
        if (*f) { DeleteObject(*f); *f = nullptr; }
    }
    fontDpi_ = dpi;
    auto make = [&](int pt, int weight, const wchar_t* face) {
        return CreateFontW(-MulDiv(pt, static_cast<int>(dpi), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    };
    burmeseFont_ = make(14, FW_NORMAL, L"Myanmar Text");
    uiFont_ = make(9, FW_NORMAL, L"Segoe UI");
    uiBold_ = make(9, FW_SEMIBOLD, L"Segoe UI");
    HDC dc = GetDC(hwnd_);
    TEXTMETRICW tm;
    HGDIOBJ old = SelectObject(dc, burmeseFont_);
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    pad_ = MulDiv(8, static_cast<int>(dpi), 96);
    rowHeight_ = tm.tmHeight + MulDiv(4, static_cast<int>(dpi), 96);
    headerHeight_ = MulDiv(24, static_cast<int>(dpi), 96);
}

void CandidateWindow::Measure(SIZE& size) {
    HDC dc = GetDC(hwnd_);
    int width = TextSize(dc, uiFont_, content_.input).cx;
    const int numberWidth = MulDiv(22, static_cast<int>(fontDpi_), 96);
    for (auto& item : content_.items) width = std::max<int>(width, numberWidth + TextSize(dc, burmeseFont_, item).cx);
    bool footer = content_.pageCount > 1 || !content_.hint.empty();
    if (footer) width = std::max<int>(width, TextSize(dc, uiFont_, content_.hint).cx + MulDiv(50, static_cast<int>(fontDpi_), 96));
    ReleaseDC(hwnd_, dc);
    width = std::max(width, MulDiv(120, static_cast<int>(fontDpi_), 96));
    size.cx = width + pad_ * 2;
    size.cy = headerHeight_ + static_cast<int>(content_.items.size()) * rowHeight_ + (footer ? headerHeight_ - pad_ / 2 : pad_ / 2);
}

void CandidateWindow::Place(const RECT& textRect, SIZE size) {
    HMONITOR mon = MonitorFromRect(&textRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(mon, &mi);
    const RECT work = mi.rcWork;
    int x = textRect.left - pad_;
    int y = textRect.bottom + MulDiv(4, static_cast<int>(fontDpi_), 96);
    if (y + size.cy > work.bottom) y = textRect.top - size.cy - MulDiv(4, static_cast<int>(fontDpi_), 96);
    x = std::max<int>(work.left, std::min<int>(x, work.right - size.cx));
    y = std::max<int>(work.top, std::min<int>(y, work.bottom - size.cy));
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, size.cx, size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CandidateWindow::Show(const Content& content, const RECT& textRect) {
    if (!EnsureWindow()) return;
    KillTimer(hwnd_, kFlashTimer);
    flashing_ = false;
    content_ = content;
    dark_ = SystemUsesDarkMode();
    UpdateFonts();
    SIZE size;
    Measure(size);
    Place(textRect, size);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CandidateWindow::Hide() {
    if (hwnd_ && !flashing_) ShowWindow(hwnd_, SW_HIDE);
}

void CandidateWindow::Flash(const std::wstring& text, const RECT& anchor) {
    Content c;
    c.input = text;
    Show(c, anchor);
    flashing_ = true;
    SetTimer(hwnd_, kFlashTimer, 900, nullptr);
}

void CandidateWindow::Paint(HDC target) {
    RECT client;
    GetClientRect(hwnd_, &client);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    const COLORREF bg = dark_ ? RGB(0x2B, 0x2B, 0x2B) : RGB(0xFF, 0xFF, 0xFF);
    const COLORREF fg = dark_ ? RGB(0xF2, 0xF2, 0xF2) : RGB(0x1A, 0x1A, 0x1A);
    const COLORREF dim = dark_ ? RGB(0x9A, 0x9A, 0x9A) : RGB(0x80, 0x80, 0x80);
    const COLORREF line = dark_ ? RGB(0x44, 0x44, 0x44) : RGB(0xE0, 0xE0, 0xE0);
    const COLORREF accent = GetSysColor(COLOR_HIGHLIGHT);
    HBRUSH bgBrush = CreateSolidBrush(bg);
    FillRect(dc, &client, bgBrush);
    DeleteObject(bgBrush);
    SetBkMode(dc, TRANSPARENT);

    // Typed input, with the focused word emphasized.
    RECT header = {pad_, 0, client.right - pad_, headerHeight_};
    int x = pad_;
    auto drawPart = [&](const std::wstring& s, HFONT font, COLORREF color) {
        if (s.empty()) return;
        SelectObject(dc, font);
        SetTextColor(dc, color);
        RECT r = {x, header.top, header.right, header.bottom};
        DrawTextW(dc, s.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        x += TextSize(dc, font, s).cx;
    };
    const std::wstring& in = content_.input;
    if (content_.focusStart >= 0 && content_.focusEnd <= static_cast<int>(in.size()) && content_.focusStart < content_.focusEnd) {
        drawPart(in.substr(0, content_.focusStart), uiFont_, dim);
        drawPart(in.substr(content_.focusStart, content_.focusEnd - content_.focusStart), uiBold_, fg);
        drawPart(in.substr(content_.focusEnd), uiFont_, dim);
    } else {
        drawPart(in, uiFont_, content_.items.empty() ? fg : dim);
    }

    rowRects_.clear();
    if (!content_.items.empty()) {
        HPEN pen = CreatePen(PS_SOLID, 1, line);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, pad_, headerHeight_ - 2, nullptr);
        LineTo(dc, client.right - pad_, headerHeight_ - 2);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    const int numberWidth = MulDiv(22, static_cast<int>(fontDpi_), 96);
    for (size_t i = 0; i < content_.items.size(); ++i) {
        RECT row = {pad_ / 2, headerHeight_ + static_cast<int>(i) * rowHeight_, client.right - pad_ / 2,
                    headerHeight_ + static_cast<int>(i + 1) * rowHeight_};
        rowRects_.push_back(row);
        const bool hl = static_cast<int>(i) == content_.highlighted;
        if (hl) {
            HBRUSH b = CreateSolidBrush(accent);
            HGDIOBJ oldBrush = SelectObject(dc, b);
            HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            const int radius = MulDiv(6, static_cast<int>(fontDpi_), 96);
            RoundRect(dc, row.left, row.top, row.right, row.bottom, radius, radius);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(b);
        }
        wchar_t number[4];
        wsprintfW(number, L"%d", static_cast<int>(i) + 1);
        SelectObject(dc, uiFont_);
        SetTextColor(dc, hl ? RGB(0xFF, 0xFF, 0xFF) : dim);
        RECT nr = {pad_, row.top, pad_ + numberWidth, row.bottom};
        DrawTextW(dc, number, -1, &nr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(dc, burmeseFont_);
        SetTextColor(dc, hl ? RGB(0xFF, 0xFF, 0xFF) : fg);
        RECT tr = {pad_ + numberWidth, row.top, row.right, row.bottom};
        DrawTextW(dc, content_.items[i].c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    if (content_.pageCount > 1 || !content_.hint.empty()) {
        const int top = headerHeight_ + static_cast<int>(content_.items.size()) * rowHeight_;
        RECT fr = {pad_, top, client.right - pad_, client.bottom};
        SelectObject(dc, uiFont_);
        SetTextColor(dc, dim);
        DrawTextW(dc, content_.hint.c_str(), -1, &fr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_LEFT);
        if (content_.pageCount > 1) {
            wchar_t pageText[32];
            wsprintfW(pageText, L"%d/%d", content_.page + 1, content_.pageCount);
            DrawTextW(dc, pageText, -1, &fr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_RIGHT);
        }
    }
    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

LRESULT CALLBACK CandidateWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (self) self->Paint(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (self && self->onClick_) {
                POINT p = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
                for (size_t i = 0; i < self->rowRects_.size(); ++i) {
                    if (PtInRect(&self->rowRects_[i], p)) { self->onClick_(static_cast<int>(i)); break; }
                }
            }
            return 0;
        case WM_TIMER:
            if (wp == kFlashTimer && self) {
                KillTimer(hwnd, kFlashTimer);
                self->flashing_ = false;
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace pyawwin
