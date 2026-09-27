// Display attributes for the composition: a thin underline for the preview, a thick one for the
// word currently being edited.
#pragma once

#include "globals.h"

namespace pyawwin {

class DisplayAttributeInfo final : public ITfDisplayAttributeInfo {
public:
    DisplayAttributeInfo(const GUID& guid, const TF_DISPLAYATTRIBUTE& attr, const wchar_t* description);

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP GetGUID(GUID* pguid) override;
    STDMETHODIMP GetDescription(BSTR* pbstrDesc) override;
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override;
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) override;
    STDMETHODIMP Reset() override;

    static DisplayAttributeInfo* CreateInput();
    static DisplayAttributeInfo* CreateFocused();

private:
    ~DisplayAttributeInfo();
    LONG refs_ = 1;
    GUID guid_;
    TF_DISPLAYATTRIBUTE attr_;
    const wchar_t* description_;
};

class EnumDisplayAttributeInfo final : public IEnumTfDisplayAttributeInfo {
public:
    EnumDisplayAttributeInfo();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Skip(ULONG ulCount) override;

private:
    ~EnumDisplayAttributeInfo();
    LONG refs_ = 1;
    ULONG index_ = 0;
};

}  // namespace pyawwin
