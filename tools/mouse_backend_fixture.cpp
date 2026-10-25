#include <windows.h>
#include <string>
int wmain() {
    wchar_t image[32768]{};
    GetModuleFileNameW(nullptr, image, 32768);
    if (std::wstring(image).find(L"ExitBackend.exe") != std::wstring::npos) return 23;
    for (;;) Sleep(100);
}
