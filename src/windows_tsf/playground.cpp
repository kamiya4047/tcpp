#include "common.h"
#include "candidate_window.h"
#include "ime/bopomofo.h"
#include <sstream>

namespace {
using namespace ime;
using namespace ime::windows;
struct Playground {
    Engine engine{read_user_settings()};
    CandidateWindow candidates;
    HWND window{}, state{}, hypotheses{}, output{};
    HFONT font{};
    bool smoke{};
    int result{};
    void reload_settings() {
        engine.set_settings(read_user_settings());
        const auto file = settings_path();
        if (!file.empty()) {
            const auto dictionary = (file.parent_path() / L"user-dictionary.tsv").u8string();
            engine.set_user_dictionary_path(std::string(dictionary.begin(), dictionary.end()));
        }
    }
    void refresh() {
        const auto& s = engine.state();
        std::wstring description = L"Raw keys: " + wide(from_utf8(s.raw_keys)) + L"\r\nBopomofo: " + wide(s.reading) + L"\r\nComposition: " + wide(engine.preedit());
        description += L"\r\nSegments: ";
        for (std::size_t i = 0; i < s.segments.size(); ++i) description += (i == s.active_segment ? L" [" : L"  ") + wide(s.segments[i].surface) + (i == s.active_segment ? L"]" : L"");
        SetWindowTextW(state,description.c_str());
        std::wostringstream details;
        for (const auto& token : bopomofo::parse(s.raw_keys)) details << L"Token " << token.source_begin << L".." << token.source_end << L"  " << wide(token.symbols) << L"\r\n";
        for (std::size_t i = 0; i < s.candidates.size(); ++i) {
            const auto& c = s.candidates[i];
            details << i+1 << L". " << wide(c.text) << L"   score=" << c.score << L"   confidence=" << c.confidence << L"   " << wide(from_utf8(c.provider)) << L"\r\n";
            if (!c.explanation.empty()) details << L"     " << wide(c.explanation) << L"\r\n";
        }
        SetWindowTextW(hypotheses,details.str().c_str());
        POINT anchor{430,100}; ClientToScreen(window,&anchor);
        candidates.show(engine,{anchor.x,anchor.y,anchor.x,anchor.y+24},true);
    }
    void commit_text(std::u32string_view text) {
        const auto converted = wide(text);
        SendMessageW(output,EM_SETSEL,static_cast<WPARAM>(-1),static_cast<LPARAM>(-1));
        SendMessageW(output,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(converted.c_str()));
    }
};
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) noexcept {
    auto* self = reinterpret_cast<Playground*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message == WM_NCCREATE) { self = static_cast<Playground*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams); self->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
    if (!self) return DefWindowProcW(window,message,wparam,lparam);
    try {
        switch (message) {
        case WM_CREATE: {
            self->reload_settings();
            self->font=CreateFontW(-19,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft JhengHei UI");
            auto control=[&](const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int width,int height,int id) {
                HWND child=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,x,y,width,height,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
                SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(self->font),TRUE); return child;
            };
            control(L"STATIC",L"Type here: su3cl3 → Space → Enter. Click this background to resume typing.",0,18,14,850,30,0);
            control(L"STATIC",L"Candidates include phrase + shorter spans · choose a character to shift the boundary · More candidates expands the list",0,18,48,950,30,0);
            self->state=control(L"STATIC",L"",SS_LEFT,18,94,900,126,0);
            control(L"BUTTON",L"Convert",BS_PUSHBUTTON,18,226,108,34,10);
            control(L"BUTTON",L"Commit",BS_PUSHBUTTON,136,226,108,34,11);
            control(L"BUTTON",L"Cancel",BS_PUSHBUTTON,254,226,108,34,12);
            control(L"BUTTON",L"Settings",BS_PUSHBUTTON,372,226,108,34,13);
            control(L"STATIC",L"Committed text / native Windows EDIT host (click inside to test an installed TSF IME):",0,18,278,900,28,0);
            self->output=control(L"EDIT",L"",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,18,310,940,110,20);
            control(L"STATIC",L"Parser tokens and ranked candidates",0,18,438,900,28,0);
            self->hypotheses=control(L"EDIT",L"",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,18,470,940,260,21);
            self->candidates.set_selection_handler([self](std::size_t index){self->engine.confirm_candidate(index);self->refresh();});
            self->candidates.set_navigation_handler([self](WPARAM key) {
                bool committed{};
                handle_key(self->engine, key, 0, false, committed);
                self->refresh();
            });
            self->refresh();
            if (self->smoke) PostMessageW(window,WM_APP+1,0,0);
            return 0;
        }
        case WM_LBUTTONDOWN: SetFocus(window); return 0;
        case WM_SETFOCUS: if (self->engine.state().mode == Mode::Empty) self->reload_settings(); return 0;
        case WM_KEYDOWN: {
            if (self->engine.state().mode == Mode::Empty) self->reload_settings();
            bool committed{};
            const bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
            const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0;
            if (control && wparam=='V' && self->engine.settings().smart_paste && self->engine.state().mode==Mode::Empty) self->engine.paste(clipboard_text(window));
            else if (accepts_key(self->engine,wparam,control,(GetKeyState(VK_MENU)&0x8000)!=0,shift)) {
                const auto result=handle_key(self->engine,wparam,translated_key(wparam,lparam),shift,committed);
                if (committed) self->commit_text(result);
            } else if (wparam==VK_SPACE) self->commit_text(U" ");
            self->refresh(); return 0;
        }
        case WM_CHAR: return 0;
        case WM_COMMAND:
            if (HIWORD(wparam)==BN_CLICKED) {
                switch(LOWORD(wparam)) {
                case 10:self->engine.convert();break;
                case 11:self->commit_text(self->engine.commit());break;
                case 12:self->engine.cancel();break;
                case 13: {
                    wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768);
                    const auto settings=std::filesystem::path(path).parent_path()/L"ime_settings.exe";
                    ShellExecuteW(window,L"open",settings.c_str(),nullptr,nullptr,SW_SHOWNORMAL); break;
                }
                }
                SetFocus(window);self->refresh();
            }
            return 0;
        case WM_APP+1: {
            self->engine.set_input("su3cl3");self->engine.convert();self->refresh();
            const auto committed=self->engine.commit();self->commit_text(committed);
            wchar_t result[128]{};GetWindowTextW(self->output,result,128);
            self->result=committed.empty()||utf32(result)!=committed?1:0;
            DestroyWindow(window);return 0;
        }
        case WM_DPICHANGED: {
            const auto* suggested=reinterpret_cast<RECT*>(lparam);
            SetWindowPos(window,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;
        }
        case WM_DESTROY:self->candidates.hide();PostQuitMessage(self->result);return 0;
        }
    } catch (...) { MessageBoxW(window,L"The operation failed. Your committed text remains in the editor.",L"Taiwan Bopomofo",MB_OK|MB_ICONERROR); }
    return DefWindowProcW(window,message,wparam,lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int show) {
    try {
        enable_dpi_awareness();
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        Playground app;app.smoke=std::wstring_view(command).find(L"--smoke")!=std::wstring_view::npos;
        WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.lpszClassName=L"TaiwanBopomofo.Playground";cls.hbrBackground=GetSysColorBrush(COLOR_WINDOW);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if (!RegisterClassW(&cls)) return 1;
        HWND window=CreateWindowExW(0,cls.lpszClassName,L"Taiwan Bopomofo — Playground",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1000,800,nullptr,nullptr,instance,&app);
        if (!window) return 1;
        if (!app.smoke) { ShowWindow(window,show);UpdateWindow(window);SetFocus(window); }
        MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
        if(app.font)DeleteObject(app.font);
        CoUninitialize();return static_cast<int>(message.wParam);
    } catch (...) { return 1; }
}
