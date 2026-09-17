#include "common.h"
#include "candidate_window.h"
#include <iostream>

namespace {
int failures{};
void check(bool success,const char* expression){if(!success){std::cerr<<"FAILED: "<<expression<<'\n';++failures;}}
#define VERIFY(expression) check((expression),#expression)

// A host which refuses edit locks must receive the original key, with no
// composition created and no selection/context content requested.
class RefusingContext final : public ITfContext {
public:
    ULONG requests{}, selections{};
    bool readonly{}, reject_request{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(iid!=IID_IUnknown&&iid!=IID_ITfContext)return E_NOINTERFACE;
        *out=static_cast<ITfContext*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override {return --refs_;}
    HRESULT STDMETHODCALLTYPE RequestEditSession(TfClientId,ITfEditSession*,DWORD,HRESULT* result) override {
        ++requests;*result=TF_E_NOLOCK;return reject_request?E_ACCESSDENIED:S_OK;
    }
    HRESULT STDMETHODCALLTYPE InWriteSession(TfClientId,BOOL*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetSelection(TfEditCookie,ULONG,ULONG,TF_SELECTION*,ULONG*) override {++selections;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetSelection(TfEditCookie,ULONG,const TF_SELECTION*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetStart(TfEditCookie,ITfRange**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetEnd(TfEditCookie,ITfRange**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetActiveView(ITfContextView**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumViews(IEnumTfContextViews**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetStatus(TF_STATUS* status) override {*status={};status->dwDynamicFlags=readonly?TF_SD_READONLY:0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID,ITfProperty**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetAppProperty(REFGUID,ITfReadOnlyProperty**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE TrackProperties(const GUID**,ULONG,const GUID**,ULONG,ITfReadOnlyProperty**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumProperties(IEnumTfProperties**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateRangeBackup(TfEditCookie,ITfRange*,ITfRangeBackup**) override {return E_NOTIMPL;}
private:
    ULONG refs_{1};
};

void verify_refused_edits(ITfTextInputProcessorEx* service,ITfKeyEventSink* keys) {
    using ime::windows::ComPtr;
    ComPtr<ITfThreadMgr> manager;
    const HRESULT created=CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_ITfThreadMgr,reinterpret_cast<void**>(manager.put()));
    VERIFY(SUCCEEDED(created));
    if(FAILED(created))return;
    TfClientId client{};
    const HRESULT activated=manager->Activate(&client);
    VERIFY(SUCCEEDED(activated));
    if(FAILED(activated))return;
    BYTE original[256]{},neutral[256]{};
    const BOOL saved=GetKeyboardState(original);
    VERIFY(saved!=FALSE);
    if(saved)VERIFY(SetKeyboardState(neutral)!=FALSE);
    const HRESULT attached=service->ActivateEx(manager.get(),client,0);
    if(attached==E_INVALIDARG) {
        std::cout<<"SKIPPED: synthetic foreground TSF key sink is unavailable\n";
        if(saved)VERIFY(SetKeyboardState(original)!=FALSE);
        VERIFY(manager->Deactivate()==S_OK);
        return;
    }
    VERIFY(SUCCEEDED(attached));
    if(FAILED(attached))return;
    if(SUCCEEDED(attached)) {
        RefusingContext context;
        BOOL eaten=FALSE;
        VERIFY(keys->OnTestKeyDown(&context,'A',0,&eaten)==S_OK);
        VERIFY(eaten==TRUE);
        VERIFY(keys->OnKeyDown(&context,'A',0,&eaten)==S_OK);
        VERIFY(eaten==FALSE);
        VERIFY(context.requests==1);
        VERIFY(context.selections==0);
        context.reject_request=true;
        VERIFY(keys->OnKeyDown(&context,'A',0,&eaten)==S_OK);
        VERIFY(eaten==FALSE);
        VERIFY(context.requests==2);
        VERIFY(keys->OnTestKeyDown(&context,VK_RETURN,0,&eaten)==S_OK);
        VERIFY(eaten==FALSE);
        VERIFY(keys->OnKeyUp(&context,'A',0,&eaten)==S_OK);
        VERIFY(eaten==FALSE);
        context.readonly=true;
        VERIFY(keys->OnKeyDown(&context,'A',0,&eaten)==S_OK);
        VERIFY(eaten==FALSE);
        VERIFY(context.requests==2);
        VERIFY(service->Deactivate()==S_OK);
        VERIFY(service->ActivateEx(manager.get(),client,TF_TMAE_SECUREMODE)==S_OK);
        context.readonly=false;
        VERIFY(keys->OnKeyDown(&context,'A',0,&eaten)==S_OK);
        VERIFY(eaten==FALSE);
        VERIFY(context.requests==2);
        VERIFY(service->Deactivate()==S_OK);
    }
    if(saved)VERIFY(SetKeyboardState(original)!=FALSE);
    VERIFY(manager->Deactivate()==S_OK);
}

void verify_candidate_columns() {
    ime::Settings settings;
    settings.explanations = false;
    ime::Engine engine(settings);
    // Synthetic view data deliberately isolates layout from dictionary contents/ranking.
    auto& state = const_cast<ime::CompositionState&>(engine.state());
    state.mode = ime::Mode::Converted;
    state.candidates.resize(20);
    for (auto& candidate : state.candidates) candidate.text = U"雞";
    ime::windows::CandidateWindow window;
    std::size_t clicked = SIZE_MAX;
    window.set_selection_handler([&](std::size_t index) { clicked = index; });
    const RECT caret{20,20,20,40};
    window.show(engine, caret);
    const auto hwnd = window.handle();
    VERIFY(hwnd != nullptr);
    if (!hwnd) return;
    const UINT dpi = GetDpiForWindow(hwnd);
    const auto scaled = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    RECT compact{};
    GetClientRect(hwnd, &compact);
    VERIFY(compact.right < scaled(160));
    // Definitions must not add a footer or force a wider candidate menu.
    settings.explanations = true;
    engine.set_settings(settings);
    state.mode = ime::Mode::Converted;
    state.candidates.resize(20);
    for (auto& candidate : state.candidates) {
        candidate.text = U"雞";
        candidate.explanation = U"家禽，這段釋義不應出現在候選選單底部。";
    }
    window.show(engine, caret);
    RECT with_definition{};
    GetClientRect(hwnd, &with_definition);
    VERIFY(with_definition.right == compact.right);
    VERIFY(with_definition.bottom == compact.bottom);
    VERIFY(compact.bottom == 2 * scaled(6) + 9 * scaled(30) + scaled(28));
    // Paging into the next column expands the list without any footer control.
    state.selected = 9;
    state.candidate_menu_expanded = true;
    window.show(engine, caret);
    RECT expanded{};
    GetClientRect(hwnd, &expanded);
    VERIFY(expanded.right == compact.right * 3);
    VERIFY(expanded.bottom == compact.bottom);
    SendMessageW(hwnd, WM_LBUTTONDOWN, 0,
                 MAKELPARAM(compact.right + scaled(20), scaled(6) + scaled(15)));
    VERIFY(clicked == 9);
    SendMessageW(hwnd, WM_LBUTTONDOWN, 0,
                 MAKELPARAM(compact.right * 2 + scaled(20), scaled(6) + scaled(45)));
    VERIFY(clicked == 19);
    clicked = SIZE_MAX;
    SendMessageW(hwnd, WM_LBUTTONDOWN, 0,
                 MAKELPARAM(compact.right * 2 + scaled(20), scaled(6) + scaled(75)));
    VERIFY(clicked == SIZE_MAX); // Empty rows in the last column aren't selectable.
    state.selected = 10;
    state.candidate_menu_expanded = true;
    window.show(engine, caret);
    RECT moved{};
    GetClientRect(hwnd, &moved);
    VERIFY(moved.right == expanded.right);
    // The footer controls must not select a candidate when no navigation handler is attached.
    clicked = SIZE_MAX;
    SendMessageW(hwnd, WM_LBUTTONDOWN, 0, MAKELPARAM(scaled(20), compact.bottom + scaled(6)));
    GetClientRect(hwnd, &moved);
    VERIFY(moved.right == expanded.right);
    VERIFY(clicked == SIZE_MAX);
    SendMessageW(hwnd, WM_LBUTTONDOWN, 0, MAKELPARAM(scaled(20), scaled(21)));
    VERIFY(clicked == 0);
    window.hide();
    state.selected = 0;
    state.candidates.resize(1);
    state.candidates[0].text = U"這是一個比較長的候選詞";
    window.show(engine, caret);
    GetClientRect(hwnd, &moved);
    VERIFY(moved.right > compact.right);
    window.hide();

    state.candidates.resize(200);
    for (auto& candidate : state.candidates) candidate.text = U"雞";
    state.selected = 199;
    MONITORINFO monitor{sizeof(monitor), {}, {}, 0};
    GetMonitorInfoW(MonitorFromRect(&caret, MONITOR_DEFAULTTONEAREST), &monitor);
    RECT edge{monitor.rcWork.right - 5, monitor.rcWork.bottom - 20,
              monitor.rcWork.right - 5, monitor.rcWork.bottom - 5};
    window.show(engine, edge);
    RECT bounds{};
    GetWindowRect(hwnd, &bounds);
    VERIFY(bounds.left >= monitor.rcWork.left);
    VERIFY(bounds.right <= monitor.rcWork.right);
    VERIFY(bounds.top >= monitor.rcWork.top);
    VERIFY(bounds.bottom <= monitor.rcWork.bottom);
    window.hide();
    state.selected = 0;
    state.candidates.resize(6);
    for (auto& candidate : state.candidates) candidate.text = U"你";
    state.candidates[5].text = U"su3";
    window.show(engine, caret);
    // Fractional DPI used to round the combined inset differently from painting,
    // leaving too little space for the raw-input candidate ("s...").
    for (const UINT test_dpi : {96u, 120u, 144u, 192u}) {
        SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(test_dpi, test_dpi), 0);
        RECT client{};
        GetClientRect(hwnd, &client);
        const auto p = [test_dpi](int value) { return MulDiv(value, static_cast<int>(test_dpi), 96); };
        HFONT font = CreateFontW(-p(17), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");
        HDC dc = GetDC(hwnd);
        const auto old = SelectObject(dc, font);
        SIZE measured{};
        GetTextExtentPoint32W(dc, L"su3", 3, &measured);
        VERIFY(client.right - (2*p(6) + p(3) + p(2) + p(18) + p(3) + p(2)) >= measured.cx);
        SelectObject(dc, old);
        ReleaseDC(hwnd, dc);
        DeleteObject(font);
    }
}
}
int wmain(int argc,wchar_t** argv){
    using namespace ime;
    using namespace ime::windows;
    VERIFY(wide(U"台灣😀")==L"台灣\U0001f600");
    VERIFY(utf32(wide(U"台灣😀"))==U"台灣😀");
    VERIFY(utf32(std::wstring(1,static_cast<wchar_t>(0xd800)))==U"\ufffd");
    Engine engine;
    VERIFY(!accepts_key(engine,VK_RETURN,false,false,false));
    VERIFY(!accepts_key(engine,'A',true,false,false));
    VERIFY(accepts_key(engine,'S',false,false,false));
    bool committed{};
    {
        Engine tone;tone.set_input("g/");
        handle_key(tone,VK_SPACE,0,false,committed);
        VERIFY(tone.state().raw_keys=="g/ ");
        VERIFY(tone.state().mode==Mode::Composing);
        VERIFY(std::any_of(tone.state().candidates.begin(),tone.state().candidates.end(),[](const auto& c){return c.text==U"生";}));
        handle_key(tone,VK_SPACE,0,false,committed);
        VERIFY(tone.state().mode==Mode::Converted);
        VERIFY(tone.preedit()==U"生");
        VERIFY(tone.state().candidate_menu_closed);
        VERIFY(!committed);
    }
    {
        Engine flow;
        flow.set_input("g/cji6dj;4fm06gjo3");
        handle_key(flow,VK_SPACE,0,false,committed);
        VERIFY(flow.state().candidate_menu_closed);
        VERIFY(flow.state().segments.size()==2);
        handle_key(flow,VK_RIGHT,0,false,committed);
        VERIFY(flow.state().active_segment==1);
        handle_key(flow,VK_SPACE,0,false,committed);
        VERIFY(!flow.state().candidate_menu_closed);
        const auto size=flow.state().candidates.size();
        handle_key(flow,VK_SPACE,0,false,committed);
        VERIFY(flow.state().selected==1);
        VERIFY(flow.state().candidates.size()==size);
        VERIFY(!committed);
        handle_key(flow,VK_ESCAPE,0,false,committed);
        VERIFY(flow.preedit()==U"生活礦泉水");
        handle_key(flow,VK_SPACE,0,false,committed);
        VERIFY(!flow.state().candidate_menu_closed);
        const auto result=handle_key(flow,VK_RETURN,0,false,committed);
        VERIFY(committed);VERIFY(result==U"生活礦泉水");
        VERIFY(flow.state().mode==Mode::Empty);
    }
    {
        Engine first_tone;
        for(char c:std::string("su"))handle_key(first_tone,static_cast<WPARAM>(toupper(c)),c,false,committed);
        handle_key(first_tone,VK_SPACE,0,false,committed);
        VERIFY(first_tone.state().raw_keys=="su ");
        VERIFY(first_tone.state().mode==Mode::Composing);
        handle_key(first_tone,VK_SPACE,0,false,committed);
        VERIFY(first_tone.state().mode==Mode::Converted);
    }
    {
        Engine trailing_first_tone;
        for(char c:std::string("su3cl"))handle_key(trailing_first_tone,static_cast<WPARAM>(toupper(c)),c,false,committed);
        handle_key(trailing_first_tone,VK_SPACE,0,false,committed);
        VERIFY(trailing_first_tone.state().raw_keys=="su3cl ");
        VERIFY(trailing_first_tone.state().mode==Mode::Composing);
    }
    {
        Engine enter_select;
        enter_select.set_input("su3cl3su3cl3");
        handle_key(enter_select, VK_RETURN, 0, false, committed);
        VERIFY(committed);
        VERIFY(enter_select.state().mode == Mode::Empty);
    }
    {
        Engine segmented;
        segmented.set_input("su3cl3su3cl3");
        segmented.select(0);
        handle_key(segmented,VK_LEFT,0,true,committed);
        handle_key(segmented,VK_LEFT,0,true,committed);
        VERIFY(segmented.state().segments.size()==2);
        segmented.open_candidates();
        handle_key(segmented,'1','1',false,committed);
        VERIFY(segmented.state().active_segment==0);
    }
    {
        Engine invalid_position;
        invalid_position.set_input("su#");
        handle_key(invalid_position,VK_SPACE,0,false,committed);
        VERIFY(invalid_position.state().raw_keys=="su#");
        VERIFY(invalid_position.state().mode==Mode::Converted);
    }
    {
        Settings settings;
        settings.smart_paste = true;
        Engine paste_menu(settings);
        paste_menu.paste(U"貼上內容");
        VERIFY(paste_menu.state().mode == Mode::Converted);
        VERIFY(paste_menu.state().candidates.size() == 6);
        // 1–6 select this column; empty shortcuts must not leak to the host.
        VERIFY(accepts_key(paste_menu, '1', false, false, false));
        VERIFY(accepts_key(paste_menu, '6', false, false, false));
        VERIFY(accepts_key(paste_menu, '7', false, false, false));
        handle_key(paste_menu, '7', '7', false, committed);
        VERIFY(paste_menu.state().selected == 0);
        handle_key(paste_menu, VK_DOWN, 0, false, committed);
        VERIFY(paste_menu.state().selected == 1);
        handle_key(paste_menu, '6', '6', false, committed);
        VERIFY(paste_menu.state().selected == 5);
    }
    for(char c:std::string("su3cl3"))handle_key(engine,static_cast<WPARAM>(toupper(c)),c,false,committed);
    VERIFY(engine.state().raw_keys=="su3cl3");
    handle_key(engine,VK_SPACE,0,false,committed);
    VERIFY(engine.state().mode==Mode::Converted);
    handle_key(engine,VK_F10,0,false,committed);
    const auto raw=handle_key(engine,VK_RETURN,0,false,committed);
    VERIFY(committed);
    VERIFY(!raw.empty());
    VERIFY(engine.state().mode==Mode::Empty);
    engine.set_input("su3cl3");
    handle_key(engine,VK_ESCAPE,0,false,committed);
    VERIFY(engine.state().mode==Mode::Empty);
    const HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(argc!=2)return 2;
    HMODULE module=LoadLibraryW(argv[1]);VERIFY(module!=nullptr);
    if(module){
        using GetClass=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);
        using CanUnload=HRESULT(WINAPI*)();
        const auto get_class=reinterpret_cast<GetClass>(GetProcAddress(module,"DllGetClassObject"));
        const auto unload=reinterpret_cast<CanUnload>(GetProcAddress(module,"DllCanUnloadNow"));
        VERIFY(get_class&&unload);
        if(get_class&&unload){
            VERIFY(unload()==S_OK);
            VERIFY(get_class(kTextService,IID_IClassFactory,nullptr)==E_POINTER);
            ComPtr<IClassFactory> factory;
            VERIFY(SUCCEEDED(get_class(kTextService,IID_IClassFactory,reinterpret_cast<void**>(factory.put()))));
            VERIFY(unload()==S_FALSE);
            if(factory){
                ComPtr<ITfTextInputProcessorEx> service;
                VERIFY(SUCCEEDED(factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,reinterpret_cast<void**>(service.put()))));
                if(service){
                    VERIFY(service->Activate(nullptr,0)==E_INVALIDARG);
                    ComPtr<ITfKeyEventSink> keys;
                    VERIFY(SUCCEEDED(service->QueryInterface(IID_ITfKeyEventSink,reinterpret_cast<void**>(keys.put()))));
                    if(keys){BOOL eaten=TRUE;VERIFY(SUCCEEDED(keys->OnTestKeyDown(nullptr,'A',0,&eaten)));VERIFY(eaten==FALSE);VERIFY(keys->OnKeyDown(nullptr,'A',0,nullptr)==E_POINTER);}
                    if(keys)verify_refused_edits(service.get(),keys.get());
                    ComPtr<ITfDisplayAttributeProvider> attributes;
                    VERIFY(SUCCEEDED(service->QueryInterface(IID_ITfDisplayAttributeProvider,reinterpret_cast<void**>(attributes.put()))));
                    if(attributes){
                        ComPtr<IEnumTfDisplayAttributeInfo> enumeration;
                        VERIFY(SUCCEEDED(attributes->EnumDisplayAttributeInfo(enumeration.put())));
                        if(enumeration){ITfDisplayAttributeInfo* entries[2]{};ULONG count{};VERIFY(enumeration->Next(2,entries,&count)==S_OK);VERIFY(count==2);for(auto* entry:entries)if(entry){TF_DISPLAYATTRIBUTE value{};VERIFY(SUCCEEDED(entry->GetAttributeInfo(&value)));entry->Release();}}
                    }
                    VERIFY(SUCCEEDED(service->Deactivate()));
                }
                factory.reset();
            }
            VERIFY(unload()==S_OK);
        }
        FreeLibrary(module);
    }
    {
        CandidateWindow candidates;engine.set_input("su3cl3");engine.convert();engine.open_candidates();
        const HWND before=GetFocus();candidates.show(engine,{20,20,20,40});
        VERIFY(candidates.handle()!=nullptr);
        VERIFY(GetFocus()==before);
        // Owner-drawn candidate window: verify it is visible and does not steal focus.
        VERIFY(IsWindowVisible(candidates.handle()));
        candidates.hide();
        VERIFY(!IsWindowVisible(candidates.handle()));
        // Show with reading for playground mode.
        candidates.show(engine,{20,20,20,40},true);
        VERIFY(IsWindowVisible(candidates.handle()));
        VERIFY(GetFocus()==before);
        candidates.hide();
    }
    if(SUCCEEDED(apartment))CoUninitialize();
    verify_candidate_columns();
    std::cout<<"Windows adapter smoke: "<<failures<<" failures\n";
    return failures?1:0;
}
