// Shared declarations for the Pyaw Windows text service (TSF input method).
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <msctf.h>
#include <olectl.h>

#include <string>

// --- Declarations that MinGW-w64's msctf.h lacks (the Windows SDK has them) ---------------
#ifndef __ITfTextInputProcessorEx_INTERFACE_DEFINED__
#define __ITfTextInputProcessorEx_INTERFACE_DEFINED__
struct ITfTextInputProcessorEx : public ITfTextInputProcessor {
    virtual HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) = 0;
};
#endif
#ifndef __ITfDisplayAttributeProvider_INTERFACE_DEFINED__
#define __ITfDisplayAttributeProvider_INTERFACE_DEFINED__
struct ITfDisplayAttributeProvider : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) = 0;
};
#endif

#ifndef TF_CLIENTID_NULL
#define TF_CLIENTID_NULL 0
#endif
#ifndef TF_INVALID_GUIDATOM
#define TF_INVALID_GUIDATOM 0
#endif

namespace pyawwin {

// Interface IDs and TIP capability categories, defined here so both SDKs agree.
extern const IID IID_TextInputProcessorEx;       // {6E4E2102-F9CD-433D-B496-303CE03A6507}
extern const IID IID_DisplayAttributeProvider;   // {FEE47777-163C-4769-996A-6E9C50AD8F54}
extern const GUID CAT_TIPCAP_IMMERSIVESUPPORT;   // {13A016DF-560B-46CD-947A-4C3AF1E0E35D}
extern const GUID CAT_TIPCAP_SYSTRAYSUPPORT;     // {25504FB4-7BAB-4BC1-9C69-CF81890F0EF5}
extern const GUID CAT_TIPCAP_COMLESS;            // {364215D9-75BC-11D7-A6EF-00065B84435C}

// Pyaw's own identifiers.
extern const CLSID CLSID_PyawTextService;           // {E05EC474-487B-4788-AD78-D238B56D2833}
extern const GUID GUID_PyawProfile;                 // {5080D84A-8835-4182-9601-296F428F17FC}
extern const GUID GUID_PyawDisplayAttributeInput;   // {F43B8F97-CC83-4771-B144-483B939E093A}
extern const GUID GUID_PyawDisplayAttributeFocused; // {E3C7A6C1-B4D3-4ED4-816D-7729C72435C9}

/// Burmese (Myanmar).
constexpr LANGID kLangId = 0x0455;
constexpr int kIconResourceId = 101;

extern HINSTANCE g_module;

void DllAddRef();
void DllRelease();
LONG DllRefCount();

/// Full path of this DLL, and the directory it lives in (with a trailing backslash).
std::wstring ModulePath();
std::wstring ModuleDirectory();

std::wstring Widen(const std::string& utf8);
std::string Narrow(const std::wstring& wide);

}  // namespace pyawwin
