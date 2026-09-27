#include "display_attribute.h"

namespace pyawwin {

namespace {

TF_DISPLAYATTRIBUTE MakeAttribute(BOOL bold) {
    TF_DISPLAYATTRIBUTE a = {};
    a.crText.type = TF_CT_NONE;
    a.crBk.type = TF_CT_NONE;
    a.lsStyle = TF_LS_SOLID;
    a.fBoldLine = bold;
    a.crLine.type = TF_CT_NONE;
    a.bAttr = TF_ATTR_INPUT;
    return a;
}

}  // namespace

DisplayAttributeInfo::DisplayAttributeInfo(const GUID& guid, const TF_DISPLAYATTRIBUTE& attr, const wchar_t* description)
    : guid_(guid), attr_(attr), description_(description) {
    DllAddRef();
}

DisplayAttributeInfo::~DisplayAttributeInfo() { DllRelease(); }

DisplayAttributeInfo* DisplayAttributeInfo::CreateInput() {
    return new DisplayAttributeInfo(GUID_PyawDisplayAttributeInput, MakeAttribute(FALSE), L"Pyaw composition");
}

DisplayAttributeInfo* DisplayAttributeInfo::CreateFocused() {
    return new DisplayAttributeInfo(GUID_PyawDisplayAttributeFocused, MakeAttribute(TRUE), L"Pyaw selected word");
}

STDMETHODIMP DisplayAttributeInfo::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
    if (!*ppv) return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) DisplayAttributeInfo::AddRef() { return InterlockedIncrement(&refs_); }
STDMETHODIMP_(ULONG) DisplayAttributeInfo::Release() {
    LONG n = InterlockedDecrement(&refs_);
    if (n == 0) delete this;
    return n;
}

STDMETHODIMP DisplayAttributeInfo::GetGUID(GUID* pguid) {
    if (!pguid) return E_INVALIDARG;
    *pguid = guid_;
    return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::GetDescription(BSTR* pbstrDesc) {
    if (!pbstrDesc) return E_INVALIDARG;
    *pbstrDesc = SysAllocString(description_);
    return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP DisplayAttributeInfo::GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) {
    if (!pda) return E_INVALIDARG;
    *pda = attr_;
    return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) { return E_NOTIMPL; }
STDMETHODIMP DisplayAttributeInfo::Reset() { return S_OK; }

EnumDisplayAttributeInfo::EnumDisplayAttributeInfo() { DllAddRef(); }
EnumDisplayAttributeInfo::~EnumDisplayAttributeInfo() { DllRelease(); }

STDMETHODIMP EnumDisplayAttributeInfo::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo))
        *ppv = static_cast<IEnumTfDisplayAttributeInfo*>(this);
    if (!*ppv) return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) EnumDisplayAttributeInfo::AddRef() { return InterlockedIncrement(&refs_); }
STDMETHODIMP_(ULONG) EnumDisplayAttributeInfo::Release() {
    LONG n = InterlockedDecrement(&refs_);
    if (n == 0) delete this;
    return n;
}

STDMETHODIMP EnumDisplayAttributeInfo::Clone(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    auto* clone = new EnumDisplayAttributeInfo();
    clone->index_ = index_;
    *ppEnum = clone;
    return S_OK;
}

STDMETHODIMP EnumDisplayAttributeInfo::Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) {
    if (!rgInfo) return E_INVALIDARG;
    ULONG fetched = 0;
    while (fetched < ulCount && index_ < 2) {
        rgInfo[fetched++] = index_ == 0 ? DisplayAttributeInfo::CreateInput() : DisplayAttributeInfo::CreateFocused();
        ++index_;
    }
    if (pcFetched) *pcFetched = fetched;
    return fetched == ulCount ? S_OK : S_FALSE;
}

STDMETHODIMP EnumDisplayAttributeInfo::Reset() { index_ = 0; return S_OK; }

STDMETHODIMP EnumDisplayAttributeInfo::Skip(ULONG ulCount) {
    index_ += ulCount;
    return index_ <= 2 ? S_OK : S_FALSE;
}

}  // namespace pyawwin
