// Tool execution probe only: creates/deletes one file in the job-owned TEMP.
// No application, device, driver, service or capture consumer is executed.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
int main() {
    wchar_t directory[MAX_PATH] = {}, file[MAX_PATH] = {};
    const DWORD count = GetTempPathW(MAX_PATH, directory);
    if (!count || count >= MAX_PATH) return 10;
    if (!GetTempFileNameW(directory, L"YCC", 0, file)) return 11;
    if (!DeleteFileW(file)) return 12;
    std::puts("PRIMARY_TOOL_PROBE_PASS");
    return 0;
}
