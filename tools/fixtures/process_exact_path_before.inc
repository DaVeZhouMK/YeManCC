    ipc_on("proc.findExact", [](const json& a) -> json {
        const auto expected = U2W(a.value("path", std::string{}));
        if (expected.empty()) return json{{"found", false}, {"pid", 0}};
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return json{{"found", false}, {"pid", 0}};
        PROCESSENTRY32W pe{ sizeof(pe) };
        if (Process32FirstW(snap, &pe)) {
            do {
                const auto image = processImagePath(pe.th32ProcessID);
                if (!image.empty() && sameFinalPath(image, expected)) {
                    const auto pid = static_cast<int>(pe.th32ProcessID);
                    CloseHandle(snap);
                    return json{{"found", true}, {"pid", pid}};
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return json{{"found", false}, {"pid", 0}};
    });