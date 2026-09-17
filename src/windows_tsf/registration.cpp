#include "server.h"
#include <iterator>
#include <new>
#include <string>

namespace ime::windows {
HINSTANCE module_instance{};
std::atomic<long> live_objects{};
namespace {
class Factory final : public IClassFactory {
public:
    Factory() { ++live_objects; }
    ~Factory() { --live_objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return outer ? CLASS_E_NOAGGREGATION : create_text_service(iid, out);
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { if (lock) ++live_objects; else --live_objects; return S_OK; }
private:
    std::atomic<ULONG> refs_{1};
};
struct Apartment {
    HRESULT result{CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)};
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
std::wstring registry_path() {
    wchar_t guid[40]{};
    StringFromGUID2(kTextService, guid, static_cast<int>(std::size(guid)));
    return std::wstring(L"Software\\Classes\\CLSID\\") + guid;
}
HRESULT set_registry_string(const std::wstring& path, const wchar_t* name, const std::wstring& value) {
    HKEY key{};
    LSTATUS result = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
    result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(result);
}
const GUID categories[] = {GUID_TFCAT_TIP_KEYBOARD, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER};
constexpr wchar_t kProfileSpec[] = L"0x0404:{3C3DE834-1C7F-4DCE-9889-3EAA2351E623}{F77B5732-77F6-497B-8120-AA25E375E097}";
HRESULT configure_current_user_profile(bool install) {
    HMODULE input = LoadLibraryExW(L"input.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!input) return HRESULT_FROM_WIN32(GetLastError());
    using InstallLayoutOrTip = BOOL(CALLBACK*)(LPCWSTR,DWORD);
    const auto function = reinterpret_cast<InstallLayoutOrTip>(GetProcAddress(input,"InstallLayoutOrTip"));
    HRESULT result = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    if (function) {
        SetLastError(ERROR_SUCCESS);
        if (function(kProfileSpec,install ? 0u : 1u)) result = S_OK;
        else { const DWORD error = GetLastError(); result = error ? HRESULT_FROM_WIN32(error) : E_FAIL; }
    }
    FreeLibrary(input);
    return result;
}
HRESULT processor_registered(ITfInputProcessorProfiles* profiles, bool& registered) {
    registered = false;
    ComPtr<IEnumGUID> entries;
    HRESULT hr = profiles->EnumInputProcessorInfo(entries.put());
    if (FAILED(hr)) return hr;
    for (;;) {
        GUID clsid{}; ULONG count{};
        hr = entries->Next(1,&clsid,&count);
        if (FAILED(hr)) return hr;
        if (hr == S_FALSE && count == 0) return S_OK;
        if (hr != S_OK || count != 1) return E_UNEXPECTED;
        if (clsid == kTextService) { registered = true; return S_OK; }
    }
}
HRESULT unregister_server() {
    Apartment apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) return apartment.result;
    HRESULT result = configure_current_user_profile(false);
    if (result == E_FAIL) result = S_OK;
    ComPtr<ITfCategoryMgr> categories_manager;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(categories_manager.put()));
    if (SUCCEEDED(hr)) for (const auto& category : categories) {
        hr = categories_manager->UnregisterCategory(kTextService, category, kTextService);
        if (FAILED(hr)) result = hr;
    }
    else result = hr;
    ComPtr<ITfInputProcessorProfiles> profiles;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(profiles.put()));
    if (SUCCEEDED(hr)) { hr = profiles->Unregister(kTextService); if (FAILED(hr)) result = hr; }
    else result = hr;
    const LSTATUS deleted = RegDeleteTreeW(HKEY_LOCAL_MACHINE, registry_path().c_str());
    if (deleted != ERROR_SUCCESS && deleted != ERROR_FILE_NOT_FOUND) result = HRESULT_FROM_WIN32(deleted);
    return result;
}
HRESULT register_server() {
    Apartment apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) return apartment.result;
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(module_instance, path, static_cast<DWORD>(std::size(path)));
    if (!length || length >= std::size(path)) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    const auto key = registry_path();
    ComPtr<ITfInputProcessorProfiles> profiles;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(profiles.put()));
    if (FAILED(hr)) return hr;
    bool existing{};
    hr = processor_registered(profiles.get(),existing);
    if (FAILED(hr)) return hr;
    if (existing) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    // Refuse an in-place upgrade: rollback must never delete a previous install.
    // Creating the key with a disposition also protects against a registry race.
    HKEY reserved{}; DWORD disposition{};
    const LSTATUS created = RegCreateKeyExW(HKEY_LOCAL_MACHINE,key.c_str(),0,nullptr,0,KEY_READ | KEY_WRITE,nullptr,&reserved,&disposition);
    if (created != ERROR_SUCCESS) return HRESULT_FROM_WIN32(created);
    RegCloseKey(reserved);
    if (disposition != REG_CREATED_NEW_KEY) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    struct Rollback {
        bool active{true};
        ~Rollback() {
            if (active) try {
                if (FAILED(unregister_server())) OutputDebugStringW(L"Taiwan Bopomofo: registration rollback incomplete; run the profile probe.\n");
            } catch (...) { OutputDebugStringW(L"Taiwan Bopomofo: registration rollback raised an error.\n"); }
        }
    } rollback;
    hr = set_registry_string(key, nullptr, kServiceName);
    if (FAILED(hr)) return hr;
    hr = set_registry_string(key + L"\\InprocServer32", nullptr, path);
    if (SUCCEEDED(hr)) hr = set_registry_string(key + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    if (SUCCEEDED(hr)) hr = profiles->Register(kTextService);
    if (SUCCEEDED(hr)) hr = profiles->AddLanguageProfile(kTextService, kLanguage, kLanguageProfile, kServiceName,
        static_cast<ULONG>(std::size(kServiceName)-1), path, length, 0);
    if (SUCCEEDED(hr)) hr = profiles->EnableLanguageProfile(kTextService, kLanguage, kLanguageProfile, TRUE);
    ComPtr<ITfCategoryMgr> manager;
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(manager.put()));
    if (SUCCEEDED(hr)) for (const auto& category : categories) { hr = manager->RegisterCategory(kTextService, category, kTextService); if (FAILED(hr)) break; }
    if (SUCCEEDED(hr)) hr = configure_current_user_profile(true);
    if (SUCCEEDED(hr)) rollback.active = false;
    return hr;
}
}
}
extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { ime::windows::module_instance = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}
extern "C" HRESULT WINAPI DllCanUnloadNow() { return ime::windows::live_objects.load() == 0 ? S_OK : S_FALSE; }
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (clsid != ime::windows::kTextService) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new(std::nothrow) ime::windows::Factory;
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(iid, out); factory->Release(); return hr;
}
extern "C" HRESULT WINAPI DllRegisterServer() {
    try { return ime::windows::register_server(); } catch (...) { return E_UNEXPECTED; }
}
extern "C" HRESULT WINAPI DllUnregisterServer() {
    try { return ime::windows::unregister_server(); } catch (...) { return E_UNEXPECTED; }
}
