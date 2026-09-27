#include "text_service.h"

#include <optional>

#include "display_attribute.h"
#include "host.h"

namespace pyawwin {

namespace {

using pyaw::ComposerKey;

bool Down(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }
bool CapsLockOn() { return (GetKeyState(VK_CAPITAL) & 1) != 0; }

/// The character a punctuation / number key produces on a US layout (Pyaw substitutes it).
wchar_t UsPunctuation(WPARAM vk, bool shift) {
    switch (vk) {
        case VK_OEM_1: return shift ? L':' : L';';
        case VK_OEM_PLUS: return shift ? L'+' : L'=';
        case VK_OEM_COMMA: return shift ? L'<' : L',';
        case VK_OEM_MINUS: return shift ? L'_' : L'-';
        case VK_OEM_PERIOD: return shift ? L'>' : L'.';
        case VK_OEM_2: return shift ? L'?' : L'/';
        case VK_OEM_3: return shift ? L'~' : L'`';
        case VK_OEM_4: return shift ? L'{' : L'[';
        case VK_OEM_5: return shift ? L'|' : L'\\';
        case VK_OEM_6: return shift ? L'}' : L']';
        case VK_OEM_7: return shift ? L'"' : L'\'';
        case VK_MULTIPLY: return L'*';
        case VK_ADD: return L'+';
        case VK_SUBTRACT: return L'-';
        case VK_DECIMAL: return L'.';
        case VK_DIVIDE: return L'/';
        default: return 0;
    }
}

enum class KeyClass { Mapped, Swallow, PassThrough };

/// Maps a virtual key to a composer key. `Swallow` means: eat it while composing, ignore it.
KeyClass MapKey(WPARAM vk, bool composing, ComposerKey& out) {
    const bool shift = Down(VK_SHIFT);
    switch (vk) {
        case VK_RETURN: out = ComposerKey::of(shift ? ComposerKey::RawEnter : ComposerKey::Enter); return KeyClass::Mapped;
        case VK_BACK: out = ComposerKey::of(ComposerKey::Backspace); return KeyClass::Mapped;
        case VK_ESCAPE: out = ComposerKey::of(ComposerKey::Escape); return KeyClass::Mapped;
        case VK_SPACE: out = ComposerKey::of(ComposerKey::Space); return KeyClass::Mapped;
        case VK_TAB:
            if (!composing) return KeyClass::PassThrough;
            out = ComposerKey::of(shift ? ComposerKey::BackTab : ComposerKey::Tab);
            return KeyClass::Mapped;
        case VK_UP: out = ComposerKey::of(ComposerKey::Up); return KeyClass::Mapped;
        case VK_DOWN: out = ComposerKey::of(ComposerKey::Down); return KeyClass::Mapped;
        case VK_LEFT: out = ComposerKey::of(ComposerKey::Left); return KeyClass::Mapped;
        case VK_RIGHT: out = ComposerKey::of(ComposerKey::Right); return KeyClass::Mapped;
        case VK_PRIOR: out = ComposerKey::of(ComposerKey::PageUp); return KeyClass::Mapped;
        case VK_NEXT: out = ComposerKey::of(ComposerKey::PageDown); return KeyClass::Mapped;
        case VK_DELETE: case VK_HOME: case VK_END: case VK_INSERT:
            return KeyClass::Swallow;
        default: break;
    }
    if (vk >= 'A' && vk <= 'Z') {
        bool upper = shift != CapsLockOn();
        out = ComposerKey::letter(static_cast<char32_t>(upper ? vk : vk - 'A' + 'a'));
        return KeyClass::Mapped;
    }
    if (vk >= '0' && vk <= '9') {
        if (shift) {
            static const wchar_t shifted[] = L")!@#$%^&*(";
            out = ComposerKey::punctuation(shifted[vk - '0']);
        } else {
            out = ComposerKey::number(static_cast<int>(vk - '0'));
        }
        return KeyClass::Mapped;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        out = ComposerKey::number(static_cast<int>(vk - VK_NUMPAD0));
        return KeyClass::Mapped;
    }
    if (wchar_t c = UsPunctuation(vk, shift)) {
        if (composing && (c == L'\'' || c == L':')) out = ComposerKey::toneMark(c);
        else if (composing && c == L'-') out = ComposerKey::of(ComposerKey::PageUp);
        else if (composing && c == L'=') out = ComposerKey::of(ComposerKey::PageDown);
        else out = ComposerKey::punctuation(c);
        return KeyClass::Mapped;
    }
    return KeyClass::Swallow;
}

class EditSession final : public ITfEditSession {
public:
    EditSession(TextService* service, ITfContext* context, std::wstring commit)
        : service_(service), context_(context), commit_(std::move(commit)) {
        service_->AddRef();
        context_->AddRef();
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) *ppv = static_cast<ITfEditSession*>(this);
        if (!*ppv) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return n;
    }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        service_->DoUpdate(ec, context_, commit_);
        return S_OK;
    }

private:
    ~EditSession() {
        context_->Release();
        service_->Release();
    }
    LONG refs_ = 1;
    TextService* service_;
    ITfContext* context_;
    std::wstring commit_;
};

void CollapseSelectionToEnd(TfEditCookie ec, ITfContext* context, ITfRange* range) {
    ITfRange* sel = nullptr;
    if (FAILED(range->Clone(&sel)) || !sel) return;
    sel->Collapse(ec, TF_ANCHOR_END);
    TF_SELECTION ts;
    ts.range = sel;
    ts.style.ase = TF_AE_NONE;
    ts.style.fInterimChar = FALSE;
    context->SetSelection(ec, 1, &ts);
    sel->Release();
}

void ClearAttributes(TfEditCookie ec, ITfContext* context, ITfRange* range) {
    ITfProperty* prop = nullptr;
    if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &prop)) && prop) {
        prop->Clear(ec, range);
        prop->Release();
    }
}

}  // namespace

// --- Lifetime -------------------------------------------------------------------------------

TextService::TextService() { DllAddRef(); }
TextService::~TextService() { DllRelease(); }

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor) ||
        IsEqualIID(riid, IID_TextInputProcessorEx))
        *ppv = static_cast<ITfTextInputProcessorEx*>(this);
    else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink)) *ppv = static_cast<ITfThreadMgrEventSink*>(this);
    else if (IsEqualIID(riid, IID_ITfKeyEventSink)) *ppv = static_cast<ITfKeyEventSink*>(this);
    else if (IsEqualIID(riid, IID_ITfCompositionSink)) *ppv = static_cast<ITfCompositionSink*>(this);
    else if (IsEqualIID(riid, IID_DisplayAttributeProvider)) *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
    if (!*ppv) return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef() { return InterlockedIncrement(&refs_); }

STDMETHODIMP_(ULONG) TextService::Release() {
    LONG n = InterlockedDecrement(&refs_);
    if (n == 0) delete this;
    return n;
}

// --- Activation -----------------------------------------------------------------------------

STDMETHODIMP TextService::Activate(ITfThreadMgr* ptim, TfClientId tid) { return ActivateEx(ptim, tid, 0); }

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD) {
    threadMgr_ = ptim;
    threadMgr_->AddRef();
    clientId_ = tid;

    ITfSource* source = nullptr;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source))) && source) {
        source->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &threadMgrCookie_);
        source->Release();
    }
    ITfKeystrokeMgr* keys = nullptr;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&keys))) && keys) {
        keySinkAdvised_ = SUCCEEDED(keys->AdviseKeyEventSink(clientId_, static_cast<ITfKeyEventSink*>(this), TRUE));
        keys->Release();
    }
    ITfCategoryMgr* cats = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                   reinterpret_cast<void**>(&cats))) && cats) {
        cats->RegisterGUID(GUID_PyawDisplayAttributeInput, &inputAtom_);
        cats->RegisterGUID(GUID_PyawDisplayAttributeFocused, &focusedAtom_);
        cats->Release();
    }

    Host& host = Host::Instance();
    host.Engine();  // load the model now rather than on the first keystroke
    composer_.reset(new pyaw::Composer([] { return static_cast<const pyaw::Engine*>(Host::Instance().Engine()); }));
    composer_->onLearn = [](const std::string& key, const std::string& burmese, bool isExplicit) {
        Host::Instance().Learn(key, burmese, isExplicit);
    };
    Settings s = Host::LoadSettings();
    composer_->settings = s.composer;
    shiftToggles_ = s.shiftToggles;
    englishMode_ = false;
    candidates_.SetClickHandler([this](int i) { OnCandidateClick(i); });
    return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
    // TSF ends our composition itself; the preview text stays in the document as typed.
    ResetComposition();
    candidates_.Destroy();
    if (threadMgr_) {
        ITfSource* source = nullptr;
        if (threadMgrCookie_ != TF_INVALID_COOKIE &&
            SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source))) && source) {
            source->UnadviseSink(threadMgrCookie_);
            source->Release();
        }
        threadMgrCookie_ = TF_INVALID_COOKIE;
        ITfKeystrokeMgr* keys = nullptr;
        if (keySinkAdvised_ && SUCCEEDED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&keys))) && keys) {
            keys->UnadviseKeyEventSink(clientId_);
            keys->Release();
        }
        keySinkAdvised_ = false;
        threadMgr_->Release();
        threadMgr_ = nullptr;
    }
    if (lastContext_) { lastContext_->Release(); lastContext_ = nullptr; }
    composer_.reset();
    clientId_ = TF_CLIENTID_NULL;
    return S_OK;
}

// --- Focus ----------------------------------------------------------------------------------

STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr*) { return S_OK; }
STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr*) { return S_OK; }
STDMETHODIMP TextService::OnPushContext(ITfContext*) { return S_OK; }
STDMETHODIMP TextService::OnPopContext(ITfContext*) { return S_OK; }

STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr*, ITfDocumentMgr*) {
    // Another text field got focus: drop what was in progress (TSF ends that composition) and
    // pick up setting changes or words learned in other apps.
    ResetComposition();
    if (lastContext_) { lastContext_->Release(); lastContext_ = nullptr; }
    if (composer_) {
        composer_->context.clear();
        Settings s = Host::LoadSettings();
        composer_->settings = s.composer;
        shiftToggles_ = s.shiftToggles;
    }
    Host::Instance().RefreshUserFiles();
    return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(BOOL) { return S_OK; }

void TextService::RememberContext(ITfContext* context) {
    if (context == lastContext_) return;
    if (lastContext_) lastContext_->Release();
    lastContext_ = context;
    if (lastContext_) lastContext_->AddRef();
}

void TextService::ResetComposition() {
    if (composition_) { composition_->Release(); composition_ = nullptr; }
    if (composer_) composer_->reset();
    candidates_.Hide();
}

// --- Keys -----------------------------------------------------------------------------------

STDMETHODIMP TextService::OnTestKeyDown(ITfContext*, WPARAM wParam, LPARAM, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    if (!composer_) return S_OK;
    if (wParam == VK_SHIFT) {
        if (!shiftDown_) { shiftDown_ = true; shiftChorded_ = false; shiftDownAt_ = GetTickCount(); }
        return S_OK;
    }
    if (shiftDown_) shiftChorded_ = true;
    if (englishMode_ || wParam == VK_CONTROL || wParam == VK_MENU || wParam == VK_LWIN || wParam == VK_RWIN) return S_OK;
    const bool composing = composer_->isComposing();
    if (Down(VK_CONTROL) || Down(VK_MENU)) { *pfEaten = composing; return S_OK; }  // commit, then pass on
    if (!composing && CapsLockOn()) return S_OK;
    ComposerKey key = ComposerKey::of(ComposerKey::Escape);
    KeyClass kc = MapKey(wParam, composing, key);
    if (kc == KeyClass::PassThrough) return S_OK;
    if (kc == KeyClass::Swallow) { *pfEaten = composing; return S_OK; }
    *pfEaten = composer_->wouldHandle(key);
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    if (!composer_ || !pic || wParam == VK_SHIFT || englishMode_) return S_OK;
    if (wParam == VK_CONTROL || wParam == VK_MENU || wParam == VK_LWIN || wParam == VK_RWIN) return S_OK;
    RememberContext(pic);
    const bool composing = composer_->isComposing();
    if (Down(VK_CONTROL) || Down(VK_MENU)) {
        // Shortcuts belong to the app; don't lose what was being typed.
        if (composing) Update(pic, composer_->handle(ComposerKey::of(ComposerKey::Enter)).commit);
        return S_OK;
    }
    if (!composing && CapsLockOn()) return S_OK;
    ComposerKey key = ComposerKey::of(ComposerKey::Escape);
    KeyClass kc = MapKey(wParam, composing, key);
    if (kc == KeyClass::PassThrough) return S_OK;
    if (kc == KeyClass::Swallow) { *pfEaten = composing; return S_OK; }
    if (!composer_->wouldHandle(key)) return S_OK;
    auto out = composer_->handle(key);
    Update(pic, out.commit);
    *pfEaten = out.handled;
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    if (wParam == VK_SHIFT && shiftDown_) {
        shiftDown_ = false;
        if (!shiftChorded_ && GetTickCount() - shiftDownAt_ < 500 && shiftToggles_ && composer_) ToggleEnglish(pic);
    }
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

void TextService::ToggleEnglish(ITfContext* context) {
    if (composer_->isComposing() && context) Update(context, composer_->handle(ComposerKey::of(ComposerKey::Enter)).commit);
    englishMode_ = !englishMode_;
    composer_->context.clear();
    RECT anchor = textRect_;
    if (anchor.right == anchor.left) anchor = CaretRect();
    candidates_.Flash(englishMode_ ? L"English" : L"\x1019\x103C\x1014\x103A\x1019\x102C", anchor);
}

void TextService::OnCandidateClick(int index) {
    if (!composer_ || !lastContext_ || !composer_->isComposing()) return;
    auto out = composer_->handle(ComposerKey::number(index + 1));
    Update(lastContext_, out.commit);
}

// --- Composition ----------------------------------------------------------------------------

void TextService::Update(ITfContext* context, const std::string& commitUtf8) {
    auto* session = new EditSession(this, context, Widen(commitUtf8));
    HRESULT hr = S_OK;
    context->RequestEditSession(clientId_, session, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
    session->Release();
}

void TextService::InsertAtSelection(TfEditCookie ec, ITfContext* context, const std::wstring& text) {
    ITfInsertAtSelection* ins = nullptr;
    if (FAILED(context->QueryInterface(IID_ITfInsertAtSelection, reinterpret_cast<void**>(&ins))) || !ins) return;
    ITfRange* range = nullptr;
    if (SUCCEEDED(ins->InsertTextAtSelection(ec, 0, text.c_str(), static_cast<LONG>(text.size()), &range)) && range) {
        CollapseSelectionToEnd(ec, context, range);
        range->Release();
    }
    ins->Release();
}

void TextService::StartComposition(TfEditCookie ec, ITfContext* context) {
    ITfInsertAtSelection* ins = nullptr;
    ITfContextComposition* cc = nullptr;
    ITfRange* range = nullptr;
    if (SUCCEEDED(context->QueryInterface(IID_ITfInsertAtSelection, reinterpret_cast<void**>(&ins))) && ins &&
        SUCCEEDED(ins->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &range)) && range &&
        SUCCEEDED(context->QueryInterface(IID_ITfContextComposition, reinterpret_cast<void**>(&cc))) && cc) {
        ITfComposition* comp = nullptr;
        if (SUCCEEDED(cc->StartComposition(ec, range, static_cast<ITfCompositionSink*>(this), &comp)) && comp)
            composition_ = comp;
    }
    if (cc) cc->Release();
    if (range) range->Release();
    if (ins) ins->Release();
}

void TextService::SetAttributes(TfEditCookie ec, ITfContext* context, ITfRange* range) {
    ITfProperty* prop = nullptr;
    if (FAILED(context->GetProperty(GUID_PROP_ATTRIBUTE, &prop)) || !prop) return;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = static_cast<LONG>(inputAtom_);
    prop->SetValue(ec, range, &v);
    // Thick underline on the word being edited when the phrase has several.
    auto ranges = composer_->outputRangesUtf16();
    int f = composer_->focusedSegment();
    if (composer_->listMode() == pyaw::Composer::ListMode::Word && ranges.size() > 1 && f >= 0 &&
        f < static_cast<int>(ranges.size()) && ranges[f].second > ranges[f].first) {
        ITfRange* sub = nullptr;
        if (SUCCEEDED(range->Clone(&sub)) && sub) {
            LONG moved = 0;
            sub->Collapse(ec, TF_ANCHOR_START);
            sub->ShiftEnd(ec, ranges[f].second, &moved, nullptr);
            sub->ShiftStart(ec, ranges[f].first, &moved, nullptr);
            v.lVal = static_cast<LONG>(focusedAtom_);
            prop->SetValue(ec, sub, &v);
            sub->Release();
        }
    }
    prop->Release();
}

void TextService::DoUpdate(TfEditCookie ec, ITfContext* context, const std::wstring& commit) {
    if (!commit.empty()) {
        if (composition_) {
            ITfRange* range = nullptr;
            if (SUCCEEDED(composition_->GetRange(&range)) && range) {
                range->SetText(ec, 0, commit.c_str(), static_cast<LONG>(commit.size()));
                ClearAttributes(ec, context, range);
                CollapseSelectionToEnd(ec, context, range);
                range->Release();
            }
            composition_->EndComposition(ec);
            composition_->Release();
            composition_ = nullptr;
        } else {
            InsertAtSelection(ec, context, commit);
        }
    }
    const bool composing = composer_ && composer_->isComposing();
    if (composing) {
        std::wstring preview = Widen(composer_->preview());
        if (!composition_) StartComposition(ec, context);
        if (composition_) {
            ITfRange* range = nullptr;
            if (SUCCEEDED(composition_->GetRange(&range)) && range) {
                range->SetText(ec, 0, preview.c_str(), static_cast<LONG>(preview.size()));
                SetAttributes(ec, context, range);
                CollapseSelectionToEnd(ec, context, range);
                ITfContextView* view = nullptr;
                if (SUCCEEDED(context->GetActiveView(&view)) && view) {
                    RECT rc;
                    BOOL clipped = FALSE;
                    if (SUCCEEDED(view->GetTextExt(ec, range, &rc, &clipped)) && (rc.bottom > rc.top || rc.right > rc.left))
                        textRect_ = rc;
                    view->Release();
                }
                range->Release();
            }
        }
    } else if (composition_) {
        // Everything was deleted or cancelled.
        ITfRange* range = nullptr;
        if (SUCCEEDED(composition_->GetRange(&range)) && range) {
            range->SetText(ec, 0, L"", 0);
            range->Release();
        }
        composition_->EndComposition(ec);
        composition_->Release();
        composition_ = nullptr;
    }
    ShowCandidates();
}

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie, ITfComposition* pComposition) {
    // The app ended the composition (e.g. the user clicked elsewhere): the preview text stays.
    if (composition_ && composition_ == pComposition) { composition_->Release(); composition_ = nullptr; }
    if (composer_) composer_->reset();
    candidates_.Hide();
    return S_OK;
}

RECT TextService::CaretRect() const {
    GUITHREADINFO gti = {sizeof gti};
    RECT r = {0, 0, 0, 0};
    if (GetGUIThreadInfo(0, &gti) && gti.hwndCaret) {
        r = gti.rcCaret;
        POINT tl = {r.left, r.top}, br = {r.right, r.bottom};
        ClientToScreen(gti.hwndCaret, &tl);
        ClientToScreen(gti.hwndCaret, &br);
        r = {tl.x, tl.y, br.x, br.y};
    } else {
        POINT p;
        GetCursorPos(&p);
        r = {p.x, p.y, p.x, p.y + 16};
    }
    return r;
}

void TextService::ShowCandidates() {
    if (!composer_ || !composer_->isComposing()) { candidates_.Hide(); return; }
    CandidateWindow::Content c;
    const pyaw::DecodeResult* r = composer_->result();
    c.input = Widen(r ? r->normalized : composer_->raw());
    const bool phrase = composer_->listMode() == pyaw::Composer::ListMode::Phrase;
    const int focused = composer_->focusedSegment();
    if (r && !phrase && r->segments.size() > 1 && focused >= 0 && focused < static_cast<int>(r->segments.size())) {
        const auto& seg = r->segments[static_cast<size_t>(focused)];
        c.focusStart = seg.start;
        c.focusEnd = seg.end;
    }
    for (auto& cand : composer_->pageCandidates()) c.items.push_back(Widen(cand.text));
    c.highlighted = composer_->highlighted() - composer_->page() * composer_->settings.pageSize;
    c.page = composer_->page();
    c.pageCount = composer_->pageCount();
    if (!Host::Instance().Engine()) c.hint = L"Pyaw: model files missing, reinstall";
    else if (phrase) c.hint = L"whole phrase \x00B7 Tab next \x00B7 Esc back";
    else if (r && r->segments.size() > 1) c.hint = L"Tab: whole phrase";
    RECT rect = textRect_;
    if (rect.right == rect.left && rect.bottom == rect.top) rect = CaretRect();
    candidates_.Show(c, rect);
}

// --- Display attributes ---------------------------------------------------------------------

STDMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    *ppEnum = new pyawwin::EnumDisplayAttributeInfo();
    return S_OK;
}

STDMETHODIMP TextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) {
    if (!ppInfo) return E_INVALIDARG;
    *ppInfo = nullptr;
    if (IsEqualGUID(guid, GUID_PyawDisplayAttributeInput)) *ppInfo = DisplayAttributeInfo::CreateInput();
    else if (IsEqualGUID(guid, GUID_PyawDisplayAttributeFocused)) *ppInfo = DisplayAttributeInfo::CreateFocused();
    else return E_INVALIDARG;
    return S_OK;
}

}  // namespace pyawwin
