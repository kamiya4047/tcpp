#include "common.h"
#include <commctrl.h>
#include <array>

namespace {
using namespace ime;
using namespace ime::windows;
struct PhraseApp {
    Engine engine;
    HWND keys{},text{},list{},status{};
    std::string path;
    HFONT font{};
    void refresh() {
        SendMessageW(list,LB_RESETCONTENT,0,0);
        for(const auto& phrase:engine.user_dictionary().phrases()) {
            const auto label=wide(from_utf8(phrase.keys))+L"  →  "+wide(phrase.text)+L"  ("+std::to_wstring(phrase.selections)+L")";
            SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        }
    }
};
LRESULT CALLBACK phrase_procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) noexcept {
    auto* self=reinterpret_cast<PhraseApp*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<PhraseApp*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wparam,lparam);
    try {
        if(message==WM_CREATE) {
            self->font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            auto control=[&](const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int width,int height,int id){
                const HWND child=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,x,y,width,height,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
                SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(self->font),TRUE);return child;
            };
            control(L"STATIC",L"Register a phrase using standard Taiwan keyboard keys, including tones.\r\nExample: su3cl3 → 你好. Changes apply to the next composition.",0,18,16,620,44,0);
            control(L"STATIC",L"Reading keys",0,18,74,130,24,0);
            self->keys=control(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,150,70,455,28,11);
            SendMessageW(self->keys,EM_SETLIMITTEXT,256,0);
            control(L"STATIC",L"Phrase",0,18,114,130,24,0);
            self->text=control(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,150,110,455,28,12);
            SendMessageW(self->text,EM_SETLIMITTEXT,128,0);
            control(L"BUTTON",L"Add phrase",BS_DEFPUSHBUTTON|WS_TABSTOP,18,155,135,32,1);
            control(L"BUTTON",L"Remove selected",BS_PUSHBUTTON|WS_TABSTOP,165,155,155,32,2);
            control(L"BUTTON",L"Reset selected ranking",BS_PUSHBUTTON|WS_TABSTOP,332,155,175,32,3);
            control(L"BUTTON",L"Reload",BS_PUSHBUTTON|WS_TABSTOP,519,155,85,32,4);
            self->list=control(L"LISTBOX",L"",WS_BORDER|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY,18,203,587,245,13);
            self->status=control(L"STATIC",L"Counts record selections only when local learning is enabled.",0,18,466,595,60,0);
            const auto settings=settings_path();
            if(settings.empty()){SetWindowTextW(self->status,L"Local app data folder is unavailable.");return 0;}
            const auto bytes=(settings.parent_path()/L"user-dictionary.tsv").u8string();self->path.assign(bytes.begin(),bytes.end());
            if(!self->engine.set_user_dictionary_path(self->path))SetWindowTextW(self->status,L"Could not read dictionary. Existing data will not be overwritten.");
            self->refresh();return 0;
        }
        if(message==WM_COMMAND) {
            const auto command=LOWORD(wparam);
            if(command==13 && HIWORD(wparam)==LBN_SELCHANGE) {
                const auto selected=SendMessageW(self->list,LB_GETCURSEL,0,0);
                const auto& phrases=self->engine.user_dictionary().phrases();
                if(selected>=0 && static_cast<std::size_t>(selected)<phrases.size()) {
                    const auto& phrase=phrases[static_cast<std::size_t>(selected)];
                    SetWindowTextW(self->keys,wide(from_utf8(phrase.keys)).c_str());SetWindowTextW(self->text,wide(phrase.text).c_str());
                }
                return 0;
            }
            if(command<1 || command>4)return 0;
            if(self->path.empty()){SetWindowTextW(self->status,L"Cannot write: local app data folder is unavailable.");return 0;}
            bool success=false;
            if(command==1) {
                wchar_t keys[257]{},surface[129]{};GetWindowTextW(self->keys,keys,257);GetWindowTextW(self->text,surface,129);
                success=self->engine.register_user_phrase(to_utf8(utf32(keys)),utf32(surface));
            } else if(command==4) success=self->engine.reload_user_dictionary();
            else {
                const auto selected=SendMessageW(self->list,LB_GETCURSEL,0,0);
                const auto& phrases=self->engine.user_dictionary().phrases();
                if(selected<0 || static_cast<std::size_t>(selected)>=phrases.size()){SetWindowTextW(self->status,L"Select a phrase first.");return 0;}
                const auto phrase=phrases[static_cast<std::size_t>(selected)];
                if(command==2) success=self->engine.remove_user_phrase(phrase.keys,phrase.text);
                else if(self->engine.reload_user_dictionary()) {
                    auto dictionary=self->engine.user_dictionary();
                    success=dictionary.remove(phrase.keys,phrase.text) && dictionary.add(phrase.keys,phrase.text) && dictionary.save(self->path);
                    if(success)success=self->engine.reload_user_dictionary();
                }
            }
            SetWindowTextW(self->status,success?L"Saved. Changes apply to the next composition.":L"Operation failed. Check reading keys, phrase length (1–64 characters), and folder write permission.");
            self->refresh();return 0;
        }
        if(message==WM_NCDESTROY){SetWindowLongPtrW(window,GWLP_USERDATA,0);delete self;}
    } catch(...) {SetWindowTextW(self->status,L"Operation failed. Check dictionary data and write permission.");}
    return DefWindowProcW(window,message,wparam,lparam);
}
HWND open_phrases(HWND owner,bool show=true) {
    WNDCLASSW cls{};cls.lpfnWndProc=phrase_procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"TaiwanBopomofo.Phrases";cls.hbrBackground=GetSysColorBrush(COLOR_WINDOW);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    RegisterClassW(&cls);
    auto* state=new PhraseApp;
    const auto window=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"Taiwan Bopomofo — User phrases",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,650,575,owner,nullptr,cls.hInstance,state);
    if(window && show)ShowWindow(window,SW_SHOWNORMAL);
    return window;
}
constexpr std::array<const wchar_t*,8> names{L"Initial-only chaining",L"Typo tolerance",L"English fallback",L"Candidate explanations",L"Local neural ranking",L"Smart Paste (Ctrl+V when composition is empty)",L"Taiwan internet provider (explicit opt-in)",L"Learn selected candidates locally"};
struct SettingsApp {
    Settings settings{read_user_settings()};
    std::array<HWND,8> checks{};
    std::array<HWND,5> hotkeys{};
    HWND count{},status{};
    HFONT font{};
    std::array<bool*,8> flags(){return {&settings.chaining,&settings.typo_tolerance,&settings.english_fallback,&settings.explanations,&settings.neural_enabled,&settings.smart_paste,&settings.internet_enabled,&settings.learning};}
};
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) noexcept {
    auto* self=reinterpret_cast<SettingsApp*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<SettingsApp*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wparam,lparam);
    try {
        switch(message){
        case WM_CREATE:{
            self->font=CreateFontW(-19,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            auto control=[&](const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int width,int height,int id){HWND child=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,x,y,width,height,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(self->font),TRUE);return child;};
            control(L"STATIC",L"Taiwan Bopomofo settings",0,22,18,640,32,0);
            control(L"STATIC",L"Changes apply to the next composition. Standard Taiwan keyboard layout.",0,22,54,670,32,0);
            auto flags=self->flags();
            for(std::size_t i=0;i<names.size();++i){self->checks[i]=control(L"BUTTON",names[i],BS_AUTOCHECKBOX|WS_TABSTOP,22,96+static_cast<int>(i)*34,650,28,100+static_cast<int>(i));SendMessageW(self->checks[i],BM_SETCHECK,*flags[i]?BST_CHECKED:BST_UNCHECKED,0);}
            control(L"STATIC",L"Candidate count (2–50)",0,22,378,260,28,0);
            self->count=control(L"EDIT",std::to_wstring(self->settings.candidate_count).c_str(),WS_BORDER|WS_TABSTOP|ES_NUMBER,290,376,70,28,120);
            control(L"STATIC",L"Hotkeys: enter F-key number 1–24; use 0 to disable",0,22,418,660,28,0);
            constexpr const wchar_t* labels[]{L"Chinese",L"Bopomofo",L"Romanized",L"Full width",L"Raw Latin"};
            for(std::size_t i=0;i<5;++i){control(L"STATIC",labels[i],0,22+static_cast<int>(i)*133,454,130,28,0);self->hotkeys[i]=control(L"EDIT",std::to_wstring(self->settings.hotkeys[i]?self->settings.hotkeys[i]-VK_F1+1:0).c_str(),WS_BORDER|WS_TABSTOP|ES_NUMBER,22+static_cast<int>(i)*133,486,70,28,130+static_cast<int>(i));}
            control(L"STATIC",L"Privacy: no clipboard history. Secure/password fields bypass the IME.\r\nShift+Space toggles Latin input; Ctrl+Shift+R reconverts selected text.",0,22,540,670,58,0);
            control(L"BUTTON",L"Save settings",BS_DEFPUSHBUTTON|WS_TABSTOP,22,612,160,38,1);
            control(L"BUTTON",L"Open settings folder",BS_PUSHBUTTON|WS_TABSTOP,194,612,220,38,2);
            control(L"BUTTON",L"Manage user phrases",BS_PUSHBUTTON|WS_TABSTOP,426,612,244,38,3);
            self->status=control(L"STATIC",L"",0,22,672,665,66,0);
            return 0;
        }
        case WM_COMMAND:
            if(LOWORD(wparam)==1){
                auto flags=self->flags();for(std::size_t i=0;i<flags.size();++i)*flags[i]=SendMessageW(self->checks[i],BM_GETCHECK,0,0)==BST_CHECKED;
                wchar_t value[32]{};GetWindowTextW(self->count,value,32);const long count=wcstol(value,nullptr,10);
                if(count<2||count>50){SetWindowTextW(self->status,L"Candidate count must be between 2 and 50.");return 0;}self->settings.candidate_count=static_cast<std::size_t>(count);
                for(std::size_t i=0;i<5;++i){GetWindowTextW(self->hotkeys[i],value,32);const long key=wcstol(value,nullptr,10);if(key<0||key>24){SetWindowTextW(self->status,L"Hotkeys must be from 0 to 24.");return 0;}self->settings.hotkeys[i]=key?VK_F1+static_cast<int>(key)-1:0;}
                for(std::size_t i=0;i<5;++i)for(std::size_t j=i+1;j<5;++j)if(self->settings.hotkeys[i]&&self->settings.hotkeys[i]==self->settings.hotkeys[j]){SetWindowTextW(self->status,L"Choose a different F-key for each transformation.");return 0;}
                SetWindowTextW(self->status,write_user_settings(self->settings)?L"Saved. Changes apply to the next composition.":L"Could not save settings. Check write permission for your local app data folder.");
            }else if(LOWORD(wparam)==2){const auto path=settings_path().parent_path();std::error_code error;std::filesystem::create_directories(path,error);if(!error)ShellExecuteW(window,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
            else if(LOWORD(wparam)==3)open_phrases(window);
            return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        }
    }catch(...){SetWindowTextW(self->status,L"The operation failed. Existing settings remain on disk.");}
    return DefWindowProcW(window,message,wparam,lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int show){
    try {
        enable_dpi_awareness();CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        SettingsApp app;
        WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.lpszClassName=L"TaiwanBopomofo.Settings";cls.hbrBackground=GetSysColorBrush(COLOR_WINDOW);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassW(&cls))return 1;
        HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"Taiwan Bopomofo — Settings",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,735,805,nullptr,nullptr,instance,&app);
        if(!window)return 1;
        if(std::wstring_view(command).find(L"--smoke")!=std::wstring_view::npos){
            const auto phrases=open_phrases(window,false);if(!phrases)return 1;
            SendMessageW(phrases,WM_CLOSE,0,0);PostMessageW(window,WM_CLOSE,0,0);
        }else ShowWindow(window,show);
        MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){const auto root=GetAncestor(message.hwnd,GA_ROOT);if(!IsDialogMessageW(root?root:window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
        if(app.font)DeleteObject(app.font);CoUninitialize();return static_cast<int>(message.wParam);
    }catch(...){return 1;}
}
