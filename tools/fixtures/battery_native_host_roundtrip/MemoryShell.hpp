#pragma once
extern "C" __declspec(dllimport) wchar_t** __stdcall CommandLineToArgvW(const wchar_t*,int*);
extern "C" __declspec(dllimport) void* __stdcall LocalFree(void*);
