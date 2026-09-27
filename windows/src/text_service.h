// The Pyaw TSF text service: one instance per UI thread that uses the input method.
#pragma once

#include <memory>
#include <string>

#include "../../cpp/pyaw/composer.hpp"
#include "candidate_window.h"
#include "globals.h"

namespace pyawwin {

class TextService final : public ITfTextInputProcessorEx,
                          public ITfThreadMgrEventSink,
                          public ITfKeyEventSink,
                          public ITfCompositionSink,
                          public ITfDisplayAttributeProvider {
public:
    TextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor / Ex
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) override;
    STDMETHODIMP Deactivate() override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pdim) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pdim) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext* pic) override;
    STDMETHODIMP OnPopContext(ITfContext* pic) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) override;

    /// Runs inside an edit session: inserts `commit`, then shows the current composition.
    void DoUpdate(TfEditCookie ec, ITfContext* context, const std::wstring& commit);

private:
    ~TextService();

    /// Applies a composer result to the document through an edit session.
    void Update(ITfContext* context, const std::string& commitUtf8);
    void RememberContext(ITfContext* context);
    void ResetComposition();
    void ToggleEnglish(ITfContext* context);
    void ShowCandidates();
    void OnCandidateClick(int index);
    RECT CaretRect() const;

    void StartComposition(TfEditCookie ec, ITfContext* context);
    void InsertAtSelection(TfEditCookie ec, ITfContext* context, const std::wstring& text);
    void SetAttributes(TfEditCookie ec, ITfContext* context, ITfRange* range);

    LONG refs_ = 1;
    ITfThreadMgr* threadMgr_ = nullptr;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    DWORD threadMgrCookie_ = TF_INVALID_COOKIE;
    bool keySinkAdvised_ = false;
    TfGuidAtom inputAtom_ = TF_INVALID_GUIDATOM;
    TfGuidAtom focusedAtom_ = TF_INVALID_GUIDATOM;

    ITfComposition* composition_ = nullptr;
    ITfContext* lastContext_ = nullptr;
    std::unique_ptr<pyaw::Composer> composer_;
    CandidateWindow candidates_;
    RECT textRect_ = {0, 0, 0, 0};

    bool englishMode_ = false;
    bool shiftToggles_ = true;
    bool shiftDown_ = false;
    bool shiftChorded_ = false;
    DWORD shiftDownAt_ = 0;
};

}  // namespace pyawwin
