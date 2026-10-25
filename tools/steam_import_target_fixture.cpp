#include <windows.h>
extern "C" {
__declspec(dllexport) volatile LONG YmccDataImportFixture = 701;
__declspec(dllexport) __declspec(noinline) int YmccCodeImportFixture() { return static_cast<int>(YmccDataImportFixture) + 1; }
}
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
