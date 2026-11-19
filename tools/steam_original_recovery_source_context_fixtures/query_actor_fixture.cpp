#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// Owned, inert x64 fixture DLL only. No Steam, COM, registry, files or devices.
// This tag is discovered by real GetProcAddress, never called by the consumer.
extern "C" __declspec(dllexport) unsigned __cdecl QueryActorFixtureTag()noexcept{return 0x51414631u;}
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){return TRUE;}

