// Registration as a COM server and as a Burmese keyboard text service (regsvr32).
#include "globals.h"

using namespace pyawwin;

namespace {

const wchar_t kDescription[] = L"Pyaw";

std::wstring ClsidString() {
    wchar_t buf[64];
    StringFromGUID2(CLSID_PyawTextService, buf, 64);
    return buf;
}

bool SetString(HKEY root, const std::wstring& subkey, const wchar_t* name, const std::wstring& value) {
    HKEY key;
    if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

const GUID* const kCategories[] = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    &CAT_TIPCAP_IMMERSIVESUPPORT,  // Store / packaged apps such as the new Notepad
    &CAT_TIPCAP_COMLESS,
};

HRESULT RegisterProfile() {
    ITfInputProcessorProfileMgr* mgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&mgr));
    if (FAILED(hr) || !mgr) return FAILED(hr) ? hr : E_FAIL;
    const std::wstring icon = ModulePath();
    // Typing uses Latin keys, so the US layout sits underneath instead of the Myanmar layout.
    HKL us = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x04090409));
    hr = mgr->RegisterProfile(CLSID_PyawTextService, kLangId, GUID_PyawProfile, kDescription,
                              static_cast<ULONG>(wcslen(kDescription)), icon.c_str(), static_cast<ULONG>(icon.size()),
                              static_cast<UINT>(-kIconResourceId), us, 0, TRUE, 0);
    mgr->Release();
    return hr;
}

HRESULT RegisterCategories(bool add) {
    ITfCategoryMgr* cats = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                  reinterpret_cast<void**>(&cats));
    if (FAILED(hr) || !cats) return FAILED(hr) ? hr : E_FAIL;
    for (const GUID* cat : kCategories) {
        if (add) hr = cats->RegisterCategory(CLSID_PyawTextService, *cat, CLSID_PyawTextService);
        else cats->UnregisterCategory(CLSID_PyawTextService, *cat, CLSID_PyawTextService);
        if (FAILED(hr)) break;
    }
    cats->Release();
    return add ? hr : S_OK;
}

}  // namespace

STDAPI DllRegisterServer() {
    const std::wstring key = L"CLSID\\" + ClsidString();
    if (!SetString(HKEY_CLASSES_ROOT, key, nullptr, kDescription) ||
        !SetString(HKEY_CLASSES_ROOT, key + L"\\InprocServer32", nullptr, ModulePath()) ||
        !SetString(HKEY_CLASSES_ROOT, key + L"\\InprocServer32", L"ThreadingModel", L"Apartment"))
        return SELFREG_E_CLASS;
    const bool needUninit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    HRESULT hr = RegisterProfile();
    if (SUCCEEDED(hr)) hr = RegisterCategories(true);
    if (needUninit) CoUninitialize();
    return SUCCEEDED(hr) ? S_OK : SELFREG_E_CLASS;
}

STDAPI DllUnregisterServer() {
    const bool needUninit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    ITfInputProcessorProfileMgr* mgr = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&mgr))) && mgr) {
        mgr->UnregisterProfile(CLSID_PyawTextService, kLangId, GUID_PyawProfile, 0);
        mgr->Release();
    }
    RegisterCategories(false);
    if (needUninit) CoUninitialize();
    RegDeleteTreeW(HKEY_CLASSES_ROOT, (L"CLSID\\" + ClsidString()).c_str());
    return S_OK;
}
