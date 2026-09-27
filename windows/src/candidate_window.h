// The floating candidate window drawn under the composition (never takes focus).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "globals.h"

namespace pyawwin {

class CandidateWindow {
public:
    struct Content {
        std::wstring input;              // what was typed
        int focusStart = -1, focusEnd = -1;  // highlighted part of `input`
        std::vector<std::wstring> items; // candidates on this page
        int highlighted = -1;
        int page = 0, pageCount = 1;
        std::wstring hint;
    };

    ~CandidateWindow();
    void SetClickHandler(std::function<void(int)> handler) { onClick_ = std::move(handler); }
    /// Shows the window below `textRect` (screen coordinates of the composed text).
    void Show(const Content& content, const RECT& textRect);
    void Hide();
    /// Shows a short message (e.g. "English") for a moment.
    void Flash(const std::wstring& text, const RECT& anchor);
    void Destroy();

private:
    bool EnsureWindow();
    void Measure(SIZE& size);
    void Paint(HDC dc);
    void Place(const RECT& textRect, SIZE size);
    void UpdateFonts();
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    HWND hwnd_ = nullptr;
    Content content_;
    std::function<void(int)> onClick_;
    std::vector<RECT> rowRects_;
    HFONT burmeseFont_ = nullptr, uiFont_ = nullptr, uiBold_ = nullptr;
    UINT fontDpi_ = 0;
    int rowHeight_ = 0, headerHeight_ = 0, pad_ = 0;
    bool dark_ = false;
    bool flashing_ = false;
};

}  // namespace pyawwin
