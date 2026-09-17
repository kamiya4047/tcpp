#include "server.h"
#include "candidate_window.h"
#include <inputscope.h>
#include <functional>
#include <new>

namespace ime::windows {
namespace {
constexpr GUID kInputScopeProperty = {0x1713dd5a,0x68e7,0x4a5b,{0x9a,0xf6,0x59,0x2a,0x59,0x5c,0x77,0x8d}};

class EditSession final : public ITfEditSession {
public:
    EditSession(IUnknown* owner, std::function<HRESULT(TfEditCookie)> edit) : owner_(owner), edit_(std::move(edit)) { ++live_objects; }
    ~EditSession() { --live_objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfEditSession) return E_NOINTERFACE;
        *out = static_cast<ITfEditSession*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override {
        try { return edit_(cookie); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_UNEXPECTED; }
    }
private:
    std::atomic<ULONG> refs_{1};
    ComPtr<IUnknown> owner_;
    std::function<HRESULT(TfEditCookie)> edit_;
};
enum class Action { Key, Finish, Select, Reconvert, Paste, Position };

bool compartment_flag(ITfContext* context, REFGUID guid) noexcept {
    ComPtr<ITfCompartmentMgr> manager;
    if (FAILED(context->QueryInterface(IID_ITfCompartmentMgr, reinterpret_cast<void**>(manager.put())))) return false;
    ComPtr<ITfCompartment> compartment;
    if (FAILED(manager->GetCompartment(guid, compartment.put()))) return false;
    VARIANT value; VariantInit(&value);
    const bool set = SUCCEEDED(compartment->GetValue(&value)) && value.vt == VT_I4 && value.lVal != 0;
    VariantClear(&value);
    return set;
}
bool disabled(ITfContext* context) noexcept {
    if (!context) return true;
    TF_STATUS status{};
    if (SUCCEEDED(context->GetStatus(&status)) && (status.dwDynamicFlags & TF_SD_READONLY)) return true;
    if (compartment_flag(context, GUID_COMPARTMENT_KEYBOARD_DISABLED) || compartment_flag(context, GUID_COMPARTMENT_EMPTYCONTEXT)) return true;
    ComPtr<ITfContextView> view;
    HWND window{};
    if (SUCCEEDED(context->GetActiveView(view.put())) && SUCCEEDED(view->GetWnd(&window)) && window) {
        wchar_t name[32]{};
        GetClassNameW(window, name, 32);
        if (_wcsicmp(name,L"Edit") == 0 && (GetWindowLongPtrW(window,GWL_STYLE) & ES_PASSWORD)) return true;
    }
    return false;
}
bool sensitive_range(ITfContext* context, TfEditCookie cookie, ITfRange* range) noexcept {
    ComPtr<ITfProperty> property;
    if (FAILED(context->GetProperty(kInputScopeProperty, property.put()))) return false;
    VARIANT value; VariantInit(&value);
    bool sensitive{};
    if (SUCCEEDED(property->GetValue(cookie, range, &value)) && value.vt == VT_UNKNOWN && value.punkVal) {
        ComPtr<ITfInputScope> scope;
        if (SUCCEEDED(value.punkVal->QueryInterface(IID_ITfInputScope, reinterpret_cast<void**>(scope.put())))) {
            InputScope* scopes{}; UINT count{};
            if (SUCCEEDED(scope->GetInputScopes(&scopes, &count))) {
                for (UINT i = 0; i < count; ++i) if (scopes[i] == IS_PASSWORD || scopes[i] == IS_NUMERIC_PIN ||
                                    scopes[i] == IS_ALPHANUMERIC_PIN || scopes[i] == IS_ALPHANUMERIC_PIN_SET) sensitive = true;
                CoTaskMemFree(scopes);
            }
        }
    }
    VariantClear(&value);
    return sensitive;
}

class TextService final : public ITfTextInputProcessorEx, public ITfKeyEventSink, public ITfCompositionSink,
    public ITfThreadMgrEventSink, public ITfThreadFocusSink, public ITfDisplayAttributeProvider, public ITfTextLayoutSink {
public:
    TextService() { ++live_objects; }
    ~TextService() { candidate_.hide(); --live_objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITfTextInputProcessor || iid == IID_ITfTextInputProcessorEx) *out = static_cast<ITfTextInputProcessorEx*>(this);
        else if (iid == IID_ITfKeyEventSink) *out = static_cast<ITfKeyEventSink*>(this);
        else if (iid == IID_ITfCompositionSink) *out = static_cast<ITfCompositionSink*>(this);
        else if (iid == IID_ITfThreadMgrEventSink) *out = static_cast<ITfThreadMgrEventSink*>(this);
        else if (iid == IID_ITfThreadFocusSink) *out = static_cast<ITfThreadFocusSink*>(this);
        else if (iid == IID_ITfDisplayAttributeProvider) *out = static_cast<ITfDisplayAttributeProvider*>(this);
        else if (iid == IID_ITfTextLayoutSink) *out = static_cast<ITfTextLayoutSink*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr* manager, TfClientId client) override { return ActivateEx(manager,client,0); }
    HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr* manager, TfClientId client, DWORD flags) override {
        if (!manager) return E_INVALIDARG;
        if (manager_) return E_UNEXPECTED;
        try {
            manager_ = ComPtr<ITfThreadMgr>(manager);
            client_ = client;
            secure_ = (flags & TF_TMAE_SECUREMODE) != 0;
            engine_.set_settings(read_user_settings());
            const auto settings_file = settings_path();
            if (!settings_file.empty()) {
                const auto dictionary = (settings_file.parent_path() / L"user-dictionary.tsv").u8string();
                engine_.set_user_dictionary_path(std::string(dictionary.begin(), dictionary.end()));
            }
            candidate_.set_selection_handler([this](std::size_t index) { if (context_) request(context_.get(),Action::Select,static_cast<WPARAM>(index)); });
            candidate_.set_navigation_handler([this](WPARAM key) { if (context_) request(context_.get(), Action::Key, key); });
            ComPtr<ITfKeystrokeMgr> keys;
            HRESULT hr = manager->QueryInterface(IID_ITfKeystrokeMgr,reinterpret_cast<void**>(keys.put()));
            if (SUCCEEDED(hr)) hr = keys->AdviseKeyEventSink(client_,static_cast<ITfKeyEventSink*>(this),TRUE);
            if (FAILED(hr)) { Deactivate(); return hr; }
            advised_keys_ = true;
            ComPtr<ITfSource> source;
            hr = manager->QueryInterface(IID_ITfSource,reinterpret_cast<void**>(source.put()));
            if (SUCCEEDED(hr)) hr = source->AdviseSink(IID_ITfThreadMgrEventSink,static_cast<ITfThreadMgrEventSink*>(this),&manager_cookie_);
            if (SUCCEEDED(hr)) hr = source->AdviseSink(IID_ITfThreadFocusSink,static_cast<ITfThreadFocusSink*>(this),&focus_cookie_);
            if (FAILED(hr)) { Deactivate(); return hr; }
            ComPtr<ITfCategoryMgr> categories;
            if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_ITfCategoryMgr,reinterpret_cast<void**>(categories.put())))) {
                categories->RegisterGUID(kInputAttribute,&input_atom_);
                categories->RegisterGUID(kSelectedAttribute,&selected_atom_);
            }
            OutputDebugStringW(L"Taiwan Bopomofo: text service activated.\n");
            return S_OK;
        } catch (...) { Deactivate(); return E_UNEXPECTED; }
    }
    HRESULT STDMETHODCALLTYPE Deactivate() override {
        try {
            finish_focus();
            candidate_.hide();
            if (manager_) {
                ComPtr<ITfKeystrokeMgr> keys;
                if (advised_keys_ && SUCCEEDED(manager_->QueryInterface(IID_ITfKeystrokeMgr,reinterpret_cast<void**>(keys.put())))) keys->UnadviseKeyEventSink(client_);
                advised_keys_ = false;
                ComPtr<ITfSource> source;
                if (SUCCEEDED(manager_->QueryInterface(IID_ITfSource,reinterpret_cast<void**>(source.put())))) {
                    if (manager_cookie_ != TF_INVALID_COOKIE) source->UnadviseSink(manager_cookie_);
                    if (focus_cookie_ != TF_INVALID_COOKIE) source->UnadviseSink(focus_cookie_);
                }
            }
            manager_cookie_ = focus_cookie_ = TF_INVALID_COOKIE;
            manager_.reset();
            client_ = TF_CLIENTID_NULL;
            for (auto& key : eaten_keys_) key = false;
            return S_OK;
        } catch (...) { return E_UNEXPECTED; }
    }
    HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL foreground) override {
        if (!foreground) { finish_focus(); for (auto& key : eaten_keys_) key = false; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext* context, WPARAM key, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = want_key(context,key); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext*, WPARAM key, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = key < 256 && eaten_keys_[key]; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext* context, WPARAM key, LPARAM lparam, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE;
        try {
            if (!want_key(context,key)) return S_OK;
            if (context_ && context_.get() != context) { finish_focus(); if (context_) return S_OK; }
            const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (shift && key == VK_SPACE && !control) {
                finish_focus();
                // Keep the current mode if the host still owns an unfinished edit.
                if (!composition_) { latin_ = !latin_; *eaten = TRUE; }
            } else {
                const Action action = control && shift && key == 'R' ? Action::Reconvert : control && key == 'V' ? Action::Paste : Action::Key;
                const HRESULT hr = request(context,action,key,lparam);
                *eaten = hr == S_OK;
                if (FAILED(hr)) OutputDebugStringW(L"Taiwan Bopomofo: host declined edit; key passed through.\n");
            }
            if (key < 256) eaten_keys_[key] = *eaten != FALSE;
            return S_OK;
        } catch (...) { return E_UNEXPECTED; }
    }
    HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext*, WPARAM key, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = key < 256 && eaten_keys_[key]; if (key < 256) eaten_keys_[key] = false; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) override { if (!eaten) return E_POINTER; *eaten = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnCompositionTerminated(TfEditCookie, ITfComposition* composition) override {
        if (composition_.get() == composition) clear_composition();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnSetFocus(ITfDocumentMgr* focused, ITfDocumentMgr*) override {
        ComPtr<ITfContext> context;
        if (focused) focused->GetTop(context.put());
        if (context_ && context_.get() != context.get()) finish_focus();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPushContext(ITfContext*) override { finish_focus(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPopContext(ITfContext* context) override { if (context == context_.get()) finish_focus(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnSetThreadFocus() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnKillThreadFocus() override { finish_focus(); for (auto& key : eaten_keys_) key = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** out) override { return enumerate_attributes(out); }
    HRESULT STDMETHODCALLTYPE GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** out) override { return create_attribute(guid,out); }
    HRESULT STDMETHODCALLTYPE OnLayoutChange(ITfContext* context, TfLayoutCode code, ITfContextView*) override {
        if (code == TF_LC_DESTROY) candidate_.hide();
        else if (context == context_.get() && composition_) request(context,Action::Position);
        return S_OK;
    }
private:
    bool want_key(ITfContext* context, WPARAM key) const noexcept {
        if (!manager_ || secure_ || disabled(context)) return false;
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (!control && !alt && shift && key == VK_SPACE) return true;
        if (latin_ || (GetKeyState(VK_CAPITAL) & 1)) return false;
        if (!control && !alt && !shift && key == VK_SPACE && !composition_ && engine_.state().mode == Mode::Empty) return true;
        if (control && !alt && shift && key == 'R' && !composition_) return true;
        if (control && !alt && !shift && key == 'V' && engine_.settings().smart_paste && !composition_) return true;
        return accepts_key(engine_,key,control,alt,shift);
    }
    HRESULT request(ITfContext* context, Action action, WPARAM key = 0, LPARAM lparam = 0, bool asynchronous = false) noexcept {
        if (!context || client_ == TF_CLIENTID_NULL) return E_UNEXPECTED;
        try {
            const ComPtr<ITfContext> held(context);
            const ComPtr<ITfComposition> expected = composition_;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const char text = action == Action::Key ? translated_key(key,lparam) : 0;
            auto* session = new EditSession(static_cast<ITfTextInputProcessorEx*>(this),[this,held,expected,action,key,shift,text](TfEditCookie cookie) {
                // A queued focus edit can only finish the composition that scheduled it.
                if (action == Action::Finish && expected.get() != composition_.get()) return S_FALSE;
                return edit(held.get(),cookie,action,key,shift,text);
            });
            HRESULT result = E_FAIL;
            const DWORD flags = (asynchronous ? TF_ES_ASYNC : TF_ES_SYNC) | (action == Action::Position ? TF_ES_READ : TF_ES_READWRITE);
            const HRESULT hr = context->RequestEditSession(client_,session,flags,&result);
            session->Release();
            return FAILED(hr) ? hr : result;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_UNEXPECTED; }
    }
    void finish_focus() noexcept {
        candidate_.hide();
        if (!context_ || !composition_) return;
        const ComPtr<ITfContext> context = context_;
        if (FAILED(request(context.get(),Action::Finish))) request(context.get(),Action::Finish,0,0,true);
    }
    void clear_composition() noexcept {
        if (context_ && layout_cookie_ != TF_INVALID_COOKIE) {
            ComPtr<ITfSource> source;
            if (SUCCEEDED(context_->QueryInterface(IID_ITfSource,reinterpret_cast<void**>(source.put())))) source->UnadviseSink(layout_cookie_);
        }
        layout_cookie_ = TF_INVALID_COOKIE;
        composition_.reset(); context_.reset(); original_.clear(); pending_finish_ = false;
        engine_.cancel(); candidate_.hide();
    }
    HRESULT start(ITfContext* context, TfEditCookie cookie, ITfRange* selection) {
        ComPtr<ITfContextComposition> compositions;
        HRESULT hr = context->QueryInterface(IID_ITfContextComposition,reinterpret_cast<void**>(compositions.put()));
        if (FAILED(hr)) return hr;
        wchar_t old[4096]{}; ULONG length{};
        hr = selection->GetText(cookie,0,old,4096,&length);
        if (FAILED(hr)) return hr;
        // Avoid replacing a huge selection which could not be restored by Escape.
        if (length == 4096) return S_FALSE;
        const std::wstring original(old,length);
        hr = compositions->StartComposition(cookie,selection,static_cast<ITfCompositionSink*>(this),composition_.put());
        if (FAILED(hr) || !composition_) return FAILED(hr) ? hr : E_FAIL;
        context_ = ComPtr<ITfContext>(context);
        original_ = original;
        ComPtr<ITfSource> source;
        if (SUCCEEDED(context->QueryInterface(IID_ITfSource,reinterpret_cast<void**>(source.put())))) source->AdviseSink(IID_ITfTextLayoutSink,static_cast<ITfTextLayoutSink*>(this),&layout_cookie_);
        return S_OK;
    }
    HRESULT edit(ITfContext* context, TfEditCookie cookie, Action action, WPARAM key, bool shift, char text) {
        if (action == Action::Position) { position(context,cookie); return S_OK; }
        if (action == Action::Finish || pending_finish_) {
            if (!composition_ || context != context_.get()) return S_FALSE;
            ComPtr<ITfRange> range;
            HRESULT hr = composition_->GetRange(range.put());
            if (FAILED(hr)) return hr;
            clear_attributes(context,cookie,range.get());
            Engine finished = engine_;
            const auto raw = finished.state().raw_keys;
            const auto surface = finished.commit(false);
            const ComPtr<ITfComposition> composition = composition_;
            hr = composition->EndComposition(cookie);
            if (SUCCEEDED(hr)) {
                learn_after_edit(finished,raw,surface);
                engine_ = std::move(finished);
                clear_composition();
            }
            if (FAILED(hr) || action == Action::Finish) return hr;
        }
        // The field may have changed since OnTestKeyDown or the request was queued.
        if (secure_ || disabled(context)) { candidate_.hide(); return S_FALSE; }
        TF_SELECTION selection{}; ULONG fetched{};
        HRESULT hr = context->GetSelection(cookie,TF_DEFAULT_SELECTION,1,&selection,&fetched);
        if (FAILED(hr) || fetched != 1) return FAILED(hr) ? hr : E_FAIL;
        ComPtr<ITfRange> selected;
        *selected.put() = selection.range;
        if (!selected) return E_FAIL;
        if (sensitive_range(context,cookie,selected.get())) { candidate_.hide(); return S_FALSE; }
        // Never acquire clipboard data before the host grants the edit and the
        // selected field's input scope has been checked for passwords and PINs.
        const auto paste = action == Action::Paste ? clipboard_text(nullptr) : std::u32string{};
        if (action == Action::Paste && paste.empty()) return S_FALSE;
        Engine next = engine_;
        if (!composition_) {
            next.set_settings(read_user_settings());
            next.reload_user_dictionary();
            // Read a bounded context only in fields that explicitly permit ordinary input.
            ComPtr<ITfRange> preceding;
            if (SUCCEEDED(selected->Clone(preceding.put()))) {
                preceding->Collapse(cookie,TF_ANCHOR_START);
                LONG moved{};
                preceding->ShiftStart(cookie,-64,&moved,nullptr);
                wchar_t previous[64]{}; ULONG length{};
                if (SUCCEEDED(preceding->GetText(cookie,0,previous,64,&length))) next.set_context(utf32(std::wstring_view(previous,length)));
            }
        }
        bool commit{};
        std::u32string committed;
        const auto committed_raw = next.state().raw_keys;
        const auto committed_segments = next.state().segments;
        // Space on a non-empty selection without an active composition triggers reconversion,
        // mirroring MS Japanese IME behavior.
        const bool try_reconvert = action == Action::Reconvert ||
            (action == Action::Key && key == VK_SPACE && !composition_);
        if (try_reconvert) {
            wchar_t content[4096]{}; ULONG length{};
            const bool have_selection = SUCCEEDED(selected->GetText(cookie,0,content,4096,&length)) && length > 0 && length < 4096;
            if (have_selection && next.reconvert(utf32(std::wstring_view(content,length)))) {
                /* reconversion accepted — fall through to composition setup */
            } else if (action == Action::Reconvert) {
                return S_FALSE;
            } else {
                committed = handle_key(next,key,text,shift,commit,false);
            }
        } else if (action == Action::Paste) next.paste(paste);
        else if (action == Action::Select) next.confirm_candidate(static_cast<std::size_t>(key));
        else committed = handle_key(next,key,text,shift,commit,false);
        if (!composition_ && next.state().mode == Mode::Empty) return S_FALSE;
        if (!composition_) { hr = start(context,cookie,selected.get()); if (hr != S_OK) return hr; }
        ComPtr<ITfRange> range;
        hr = composition_->GetRange(range.put());
        if (FAILED(hr)) return hr;
        const bool cancel = !commit && next.state().mode == Mode::Empty;
        const auto surface = cancel ? original_ : wide(commit ? committed : next.preedit());
        hr = range->SetText(cookie,0,surface.c_str(),static_cast<LONG>(surface.size()));
        if (FAILED(hr)) return hr;
        engine_ = std::move(next);
        if (commit) {
            if(committed_segments.empty()) learn_after_edit(engine_,committed_raw,committed);
            else for(const auto& segment:committed_segments)
                learn_after_edit(engine_,segment.raw_keys,segment.surface);
        }
        ComPtr<ITfRange> caret;
        if (SUCCEEDED(range->Clone(caret.put()))) {
            caret->Collapse(cookie,TF_ANCHOR_END);
            TF_SELECTION after{caret.get(),{TF_AE_NONE,FALSE}};
            context->SetSelection(cookie,1,&after);
        }
        if (commit || cancel) {
            clear_attributes(context,cookie,range.get());
            const ComPtr<ITfComposition> composition = composition_;
            hr = composition->EndComposition(cookie);
            if (SUCCEEDED(hr)) clear_composition();
            else {
                // SetText already applied this key. Keep ownership and retry the
                // end later, but do not pass an already-written key to the host.
                pending_finish_ = true;
                candidate_.hide();
                OutputDebugStringW(L"Taiwan Bopomofo: host deferred composition completion.\n");
            }
            return S_OK;
        }
        try {
            attributes(context,cookie,range.get());
            position(context,cookie);
        } catch (...) {
            // The text transaction succeeded even if optional presentation failed.
            candidate_.hide();
        }
        return S_OK;
    }
    void learn_after_edit(Engine& engine, std::string_view raw, std::u32string_view text) noexcept {
        try { engine.learn_user_phrase(raw,text); }
        catch (...) { OutputDebugStringW(L"Taiwan Bopomofo: phrase learning unavailable; text preserved.\n"); }
    }
    void clear_attributes(ITfContext* context, TfEditCookie cookie, ITfRange* range) noexcept {
        ComPtr<ITfProperty> property;
        if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE,property.put()))) property->Clear(cookie,range);
    }
    void attributes(ITfContext* context, TfEditCookie cookie, ITfRange* range) {
        ComPtr<ITfProperty> property;
        if (FAILED(context->GetProperty(GUID_PROP_ATTRIBUTE,property.put()))) return;
        VARIANT attribute; VariantInit(&attribute); attribute.vt = VT_I4; attribute.lVal = static_cast<LONG>(input_atom_);
        property->SetValue(cookie,range,&attribute);
        const auto& state = engine_.state();
        if (state.mode != Mode::Converted || state.active_segment >= state.segments.size()) return;
        std::size_t begin{};
        for (std::size_t i=0;i<state.active_segment;++i) begin += wide(state.segments[i].surface).size();
        const auto length = wide(state.segments[state.active_segment].surface).size();
        ComPtr<ITfRange> active;
        if (SUCCEEDED(range->Clone(active.put()))) {
            active->Collapse(cookie,TF_ANCHOR_START);
            LONG moved{};
            active->ShiftEnd(cookie,static_cast<LONG>(begin+length),&moved,nullptr);
            active->ShiftStart(cookie,static_cast<LONG>(begin),&moved,nullptr);
            attribute.lVal = static_cast<LONG>(selected_atom_);
            property->SetValue(cookie,active.get(),&attribute);
        }
    }
    void position(ITfContext* context, TfEditCookie cookie) {
        if (!composition_ || context != context_.get()) return;
        if (pending_finish_ || secure_ || disabled(context)) { candidate_.hide(); return; }
        ComPtr<ITfRange> range;
        ComPtr<ITfContextView> view;
        if (FAILED(composition_->GetRange(range.put())) || FAILED(context->GetActiveView(view.put()))) return;
        if (sensitive_range(context,cookie,range.get())) { candidate_.hide(); return; }
        range->Collapse(cookie,TF_ANCHOR_END);
        RECT caret{}; BOOL clipped{};
        if (FAILED(view->GetTextExt(cookie,range.get(),&caret,&clipped))) { candidate_.hide(); return; }
        candidate_.show(engine_,caret);
    }
    std::atomic<ULONG> refs_{1};
    ComPtr<ITfThreadMgr> manager_;
    ComPtr<ITfContext> context_;
    ComPtr<ITfComposition> composition_;
    TfClientId client_{TF_CLIENTID_NULL};
    DWORD manager_cookie_{TF_INVALID_COOKIE}, focus_cookie_{TF_INVALID_COOKIE}, layout_cookie_{TF_INVALID_COOKIE};
    TfGuidAtom input_atom_{TF_INVALID_GUIDATOM}, selected_atom_{TF_INVALID_GUIDATOM};
    Engine engine_;
    CandidateWindow candidate_;
    std::wstring original_;
    bool advised_keys_{}, secure_{}, latin_{}, pending_finish_{};
    bool eaten_keys_[256]{};
};
}
HRESULT create_text_service(REFIID iid, void** result) noexcept {
    if (!result) return E_POINTER;
    *result = nullptr;
    try {
        auto* service = new TextService;
        const HRESULT hr = service->QueryInterface(iid,result); service->Release(); return hr;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_UNEXPECTED; }
}
}
