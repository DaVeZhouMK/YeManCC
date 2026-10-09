#include <windows.h>
#include <cwchar>
#include <cstring>
static HMODULE module;
static wchar_t profile[MAX_PATH]{};
static DWORD shadow=1;
extern "C" __declspec(dllexport) void LoadProfile(LPCSTR name) {
    if (GetFileAttributesW(L"mock-fault-sdk") != INVALID_FILE_ATTRIBUTES)
        RaiseException(0xe0234567,0,0,nullptr);
    if (std::strcmp(name,"")) RaiseException(0xe0345678,0,0,nullptr);
    GetModuleFileNameW(module,profile,MAX_PATH); *std::wcsrchr(profile,L'\\')=0;
    wcscat_s(profile,L"\\Profiles\\Global");
    shadow=GetPrivateProfileIntW(L"OSD",L"EnableBgnd",1,profile);
}
extern "C" __declspec(dllexport) BOOL GetProfileProperty(LPCSTR name,LPBYTE value,DWORD size) {
    if (std::strcmp(name,"EnableBgnd") || size!=sizeof(DWORD)) return FALSE;
    std::memcpy(value,&shadow,size);return TRUE;
}
extern "C" __declspec(dllexport) BOOL SetProfileProperty(LPCSTR name,LPBYTE value,DWORD size) {
    if (std::strcmp(name,"EnableBgnd") || size!=sizeof(DWORD)) return FALSE;
    std::memcpy(&shadow,value,size);return TRUE;
}
extern "C" __declspec(dllexport) void SaveProfile(LPCSTR) {
    WritePrivateProfileStringW(L"OSD",L"EnableBgnd",shadow?L"1":L"0",profile);
}
extern "C" __declspec(dllexport) void UpdateProfiles() {}
BOOL WINAPI DllMain(HINSTANCE h,DWORD r,LPVOID) {if(r==DLL_PROCESS_ATTACH){module=h;DisableThreadLibraryCalls(h);}return TRUE;}
