static void closeJoyXoffAsync(const char* reason) {
    if (g_joyXoffCloseInFlight.exchange(true, std::memory_order_acq_rel)) return;
    try {
        std::thread([reasonText = std::string(reason ? reason : "unknown")] {
        json result = {
            {"reason", reasonText},
            {"blocking", false},
            {"status", "already_off"},
            {"closed", 0}
        };
        std::vector<DWORD> pids;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{sizeof(pe)};
            if (Process32FirstW(snap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"Joyxoff.exe") == 0)
                        pids.push_back(pe.th32ProcessID);
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
        }
        int attempted = 0;
        for (DWORD pid : pids) {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
                                   FALSE, pid);
            if (!h) continue;
            wchar_t path[MAX_PATH * 4]{};
            DWORD pathLen = static_cast<DWORD>(std::size(path));
            const bool pathOk = QueryFullProcessImageNameW(h, 0, path, &pathLen) != FALSE;
            const bool exactPath = pathOk && _wcsicmp(path, JOYXOFF_EXE.c_str()) == 0;
            if (exactPath) {
                ++attempted;
                if (TerminateProcess(h, 0) != FALSE) {
                    const DWORD wait = WaitForSingleObject(h, 1500);
                    if (wait == WAIT_OBJECT_0)
                        result["closed"] = result["closed"].get<int>() + 1;
                }
            }
            CloseHandle(h);
        }
        if (attempted > 0) {
            result["status"] = result["closed"].get<int>() == attempted ? "closed" : "failed";
            result["attempted"] = attempted;
        }
        traceLog("joyxoff auto-close reason=%s status=%s attempted=%d closed=%d",
                 reasonText.c_str(), result["status"].get<std::string>().c_str(),
                 attempted, result["closed"].get<int>());
        if (!g_exitRequested.load(std::memory_order_acquire) && g_hwnd && IsWindow(g_hwnd)) {
            auto* payload = new json(std::move(result));
            if (!PostMessageW(g_hwnd, WM_SG_JOYXOFF_RESULT, 0,
                              reinterpret_cast<LPARAM>(payload)))
                delete payload;
        }
            g_joyXoffCloseInFlight.store(false, std::memory_order_release);
        }).detach();
    } catch (...) {
        g_joyXoffCloseInFlight.store(false, std::memory_order_release);
    }
}
