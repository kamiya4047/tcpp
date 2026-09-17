#include "common.h"
#include <iostream>

namespace {
constexpr wchar_t kRegistryPath[] = L"Software\\Classes\\CLSID\\{3C3DE834-1C7F-4DCE-9889-3EAA2351E623}\\InprocServer32";

std::wstring registered_path() {
    wchar_t value[32768]{};
    DWORD bytes = sizeof(value);
    const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE,kRegistryPath,nullptr,
        RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,nullptr,value,&bytes);
    return status == ERROR_SUCCESS ? std::wstring(value) : std::wstring{};
}
}

int wmain(int argc,wchar_t** argv) {
    using namespace ime::windows;
    const bool expect_absent=argc==2 && std::wstring_view(argv[1])==L"--expect-absent";
    const std::wstring path = registered_path();
    const HRESULT apartment = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
        std::wcerr << L"COM initialization failed: 0x" << std::hex << static_cast<unsigned long>(apartment) << L'\n';
        return 2;
    }

    ComPtr<ITfInputProcessorProfiles> profiles;
    HRESULT profile_result = CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles,reinterpret_cast<void**>(profiles.put()));
    bool profile_registered{};
    if (SUCCEEDED(profile_result)) {
        ComPtr<IEnumTfLanguageProfiles> entries;
        profile_result = profiles->EnumLanguageProfiles(kLanguage,entries.put());
        if (SUCCEEDED(profile_result)) {
            TF_LANGUAGEPROFILE profile{}; ULONG count{};
            while (entries->Next(1,&profile,&count) == S_OK && count == 1) {
                if (profile.clsid == kTextService && profile.guidProfile == kLanguageProfile) { profile_registered=true; break; }
            }
            if (!profile_registered) profile_result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
    }
    BOOL enabled = FALSE;
    if (profile_registered) profile_result = profiles->IsEnabledLanguageProfile(kTextService,kLanguage,kLanguageProfile,&enabled);

    std::wcout << L"COM server: " << (path.empty() ? L"not registered" : path) << L'\n';
    std::wcout << L"TSF profile: " << (profile_registered ? L"registered" : L"not registered") << L'\n';
    std::wcout << L"Current user: " << (enabled ? L"enabled" : L"not enabled") << L'\n';
    if (FAILED(profile_result)) std::wcout << L"Profile HRESULT: 0x" << std::hex << static_cast<unsigned long>(profile_result) << L'\n';

    const bool profile_absent=profile_result==HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    profiles.reset();
    if (SUCCEEDED(apartment)) CoUninitialize();
    if(expect_absent)return path.empty() && !profile_registered && !enabled && profile_absent ? 0 : 1;
    return !path.empty() && profile_registered && enabled ? 0 : 1;
}
