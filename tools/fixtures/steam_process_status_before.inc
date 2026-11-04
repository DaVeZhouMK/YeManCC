    // ── proc.running：Toolhelp32 进程枚举（取代 Get-Process 子进程）。
    //    args: { names: ["RTSS","steam"] }（不带 .exe，不区分大小写；
    //    支持 * 结尾前缀匹配，如 "RTSSHooksLoader*"）。返回 {"RTSS":true,...} ──
    ipc_on("proc.running", [](const json& a) -> json {
        std::vector<std::wstring> names;
        if (a.contains("names") && a["names"].is_array())
            for (auto& n : a["names"])
                if (n.is_string()) names.push_back(U2W(n.get<std::string>()));
        json out = json::object();
        for (auto& n : names) out[W2U(n)] = false;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return out;
        PROCESSENTRY32W pe{ sizeof(pe) };
        if (Process32FirstW(snap, &pe)) {
            do {
                std::wstring exe = pe.szExeFile;
                // 去 .exe 后缀
                if (exe.size() > 4 && _wcsicmp(exe.c_str() + exe.size() - 4, L".exe") == 0)
                    exe.resize(exe.size() - 4);
                for (auto& n : names) {
                    if (n.empty()) continue;
                    bool hit;
                    if (n.back() == L'*') {
                        std::wstring pre = n.substr(0, n.size() - 1);
                        hit = exe.size() >= pre.size() &&
                              _wcsnicmp(exe.c_str(), pre.c_str(), pre.size()) == 0;
                    } else {
                        hit = _wcsicmp(exe.c_str(), n.c_str()) == 0;
                    }
                    if (hit) out[W2U(n)] = true;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return out;
    });

