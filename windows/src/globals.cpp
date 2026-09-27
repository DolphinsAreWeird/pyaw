#include "globals.h"

#include "../../cpp/pyaw/unicode.hpp"

namespace pyawwin {

const IID IID_TextInputProcessorEx = {0x6e4e2102, 0xf9cd, 0x433d, {0xb4, 0x96, 0x30, 0x3c, 0xe0, 0x3a, 0x65, 0x07}};
const IID IID_DisplayAttributeProvider = {0xfee47777, 0x163c, 0x4769, {0x99, 0x6a, 0x6e, 0x9c, 0x50, 0xad, 0x8f, 0x54}};
const GUID CAT_TIPCAP_IMMERSIVESUPPORT = {0x13a016df, 0x560b, 0x46cd, {0x94, 0x7a, 0x4c, 0x3a, 0xf1, 0xe0, 0xe3, 0x5d}};
const GUID CAT_TIPCAP_SYSTRAYSUPPORT = {0x25504fb4, 0x7bab, 0x4bc1, {0x9c, 0x69, 0xcf, 0x81, 0x89, 0x0f, 0x0e, 0xf5}};
const GUID CAT_TIPCAP_COMLESS = {0x364215d9, 0x75bc, 0x11d7, {0xa6, 0xef, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};

const CLSID CLSID_PyawTextService = {0xe05ec474, 0x487b, 0x4788, {0xad, 0x78, 0xd2, 0x38, 0xb5, 0x6d, 0x28, 0x33}};
const GUID GUID_PyawProfile = {0x5080d84a, 0x8835, 0x4182, {0x96, 0x01, 0x29, 0x6f, 0x42, 0x8f, 0x17, 0xfc}};
const GUID GUID_PyawDisplayAttributeInput = {0xf43b8f97, 0xcc83, 0x4771, {0xb1, 0x44, 0x48, 0x3b, 0x93, 0x9e, 0x09, 0x3a}};
const GUID GUID_PyawDisplayAttributeFocused = {0xe3c7a6c1, 0xb4d3, 0x4ed4, {0x81, 0x6d, 0x77, 0x29, 0xc7, 0x24, 0x35, 0xc9}};

HINSTANCE g_module = nullptr;
static LONG g_refs = 0;

void DllAddRef() { InterlockedIncrement(&g_refs); }
void DllRelease() { InterlockedDecrement(&g_refs); }
LONG DllRefCount() { return g_refs; }

std::wstring ModulePath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(g_module, buf, static_cast<DWORD>(sizeof buf / sizeof buf[0]));
    return std::wstring(buf, n);
}

std::wstring ModuleDirectory() {
    std::wstring path = ModulePath();
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : path.substr(0, slash + 1);
}

std::wstring Widen(const std::string& utf8) {
    std::u16string s = pyaw::utf8_to_utf16(utf8);
    return std::wstring(s.begin(), s.end());
}

std::string Narrow(const std::wstring& wide) {
    return pyaw::utf16_to_utf8(std::u16string(wide.begin(), wide.end()));
}

}  // namespace pyawwin
