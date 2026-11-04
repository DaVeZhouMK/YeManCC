#pragma once
// Exact MSI startup tasks only. Included after the vendor task allowlist.
// Templates are shipped under PowerControl; no task/service ACL is changed.
static bool oemVendorTaskXmlContext(const std::wstring& name, ComPtr<ITaskService>& service,
                                    ComPtr<ITaskFolder>& folder, std::wstring& leaf) {
    if (!oemVendorTaskPathAllowed(name, oemVendorTaskNamesForFamily(YmccFamily::MsiClaw))) {
        SetLastError(ERROR_INVALID_NAME); return false;
    }
    const auto split = name.find_last_of(L'\\');
    const std::wstring parent = split == std::wstring::npos || split == 0 ? L"\\" : name.substr(0, split);
    leaf = split == std::wstring::npos ? name : name.substr(split + 1);
    VARIANT empty{}; VariantInit(&empty);
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service));
    if (SUCCEEDED(hr)) hr = service->Connect(empty, empty, empty, empty);
    BSTR path = SysAllocString(parent.c_str());
    if (!path) { SetLastError(ERROR_OUTOFMEMORY); return false; }
    if (SUCCEEDED(hr)) hr = service->GetFolder(path, &folder);
    SysFreeString(path);
    if (FAILED(hr)) SetLastError(static_cast<DWORD>(hr));
    return SUCCEEDED(hr);
}
struct OemVendorTaskXmlComScope {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~OemVendorTaskXmlComScope() { if (SUCCEEDED(initialized)) CoUninitialize(); }
    bool ready() const {
        if (SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE) return true;
        SetLastError(static_cast<DWORD>(initialized)); return false;
    }
};
static bool oemVendorTaskXmlDefinition(const std::wstring& name, ITaskService* service,
                                      ComPtr<ITaskDefinition>& definition) {
    if (!oemVendorTaskPathAllowed(name, oemVendorTaskNamesForFamily(YmccFamily::MsiClaw))) {
        SetLastError(ERROR_INVALID_NAME); return false;
    }
    const auto split = name.find_last_of(L'\\');
    const auto leaf = split == std::wstring::npos ? name : name.substr(split + 1);
    const auto path = POWER_CONTROL_DIR + L"\\" + leaf + L".xml";
    struct File { HANDLE value = INVALID_HANDLE_VALUE; ~File() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } file;
    file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(file.value, &info) || !GetFileSizeEx(file.value, &size)) return false;
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || size.QuadPart < 4 || size.QuadPart > 65536) {
        SetLastError(ERROR_INVALID_DATA); return false;
    }
    std::vector<unsigned char> bytes(static_cast<size_t>(size.QuadPart)); DWORD read = 0;
    if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) return false;
    if (read != bytes.size()) { SetLastError(ERROR_READ_FAULT); return false; }
    std::wstring xml;
    if (bytes[0] == 0xff && bytes[1] == 0xfe) {
        if (bytes.size() % 2) { SetLastError(ERROR_INVALID_DATA); return false; }
        xml.resize((bytes.size() - 2) / 2);
        memcpy(xml.data(), bytes.data() + 2, bytes.size() - 2);
    } else {
        const size_t offset = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(bytes.data() + offset), static_cast<int>(bytes.size() - offset), nullptr, 0);
        if (length <= 0) return false;
        xml.resize(static_cast<size_t>(length));
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(bytes.data() + offset), static_cast<int>(bytes.size() - offset), xml.data(), length)) return false;
    }
    if (xml.empty() || xml.find(L'\0') != std::wstring::npos) { SetLastError(ERROR_INVALID_DATA); return false; }
    HRESULT hr = service->NewTask(0, &definition);
    BSTR text = SysAllocStringLen(xml.data(), static_cast<UINT>(xml.size()));
    if (!text) { SetLastError(ERROR_OUTOFMEMORY); return false; }
    if (SUCCEEDED(hr)) hr = definition->put_XmlText(text);
    SysFreeString(text);
    if (FAILED(hr)) SetLastError(static_cast<DWORD>(hr));
    return SUCCEEDED(hr);
}
static bool oemVendorTaskRemovalReady(const std::wstring& name) {
    OemVendorTaskXmlComScope scope; if (!scope.ready()) return false;
    ComPtr<ITaskService> service; ComPtr<ITaskFolder> folder; ComPtr<ITaskDefinition> definition; std::wstring leaf;
    return oemVendorTaskXmlContext(name, service, folder, leaf) && oemVendorTaskXmlDefinition(name, service.Get(), definition);
}
static bool oemVendorTaskRemove(const std::wstring& name) {
    OemVendorTaskXmlComScope scope; if (!scope.ready()) return false;
    ComPtr<ITaskService> service; ComPtr<ITaskFolder> folder; std::wstring leaf;
    if (!oemVendorTaskXmlContext(name, service, folder, leaf)) return false;
    BSTR text = SysAllocString(leaf.c_str());
    if (!text) { SetLastError(ERROR_OUTOFMEMORY); return false; }
    const HRESULT hr = folder->DeleteTask(text, 0); SysFreeString(text);
    if (FAILED(hr)) SetLastError(static_cast<DWORD>(hr));
    return SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}
static bool oemVendorTaskImport(const std::wstring& name) {
    OemVendorTaskXmlComScope scope; if (!scope.ready()) return false;
    ComPtr<ITaskService> service; ComPtr<ITaskFolder> folder; ComPtr<ITaskDefinition> definition; std::wstring leaf;
    if (!oemVendorTaskXmlContext(name, service, folder, leaf) || !oemVendorTaskXmlDefinition(name, service.Get(), definition)) return false;
    ComPtr<IPrincipal> principal; TASK_LOGON_TYPE logon{};
    HRESULT hr = definition->get_Principal(&principal);
    if (SUCCEEDED(hr)) hr = principal->get_LogonType(&logon);
    VARIANT user{}, password{}, sddl{}; VariantInit(&user); VariantInit(&password); VariantInit(&sddl);
    BSTR account = nullptr;
    if (SUCCEEDED(hr)) hr = logon == TASK_LOGON_GROUP ? principal->get_GroupId(&account) : principal->get_UserId(&account);
    if (FAILED(hr)) { SysFreeString(account); SetLastError(static_cast<DWORD>(hr)); return false; }
    if (logon != TASK_LOGON_GROUP && logon != TASK_LOGON_INTERACTIVE_TOKEN && logon != TASK_LOGON_SERVICE_ACCOUNT) {
        SysFreeString(account); SetLastError(ERROR_NOT_SUPPORTED); return false;
    }
    if (account && SysStringLen(account)) { user.vt = VT_BSTR; user.bstrVal = account; }
    else { SysFreeString(account); account = nullptr; }
    BSTR text = SysAllocString(leaf.c_str());
    if (!text) { VariantClear(&user); SetLastError(ERROR_OUTOFMEMORY); return false; }
    ComPtr<IRegisteredTask> registered;
    // CREATE deliberately refuses to overwrite a task recreated by MSI meanwhile.
    hr = folder->RegisterTaskDefinition(text, definition.Get(), TASK_CREATE, user, password, logon, sddl, &registered);
    SysFreeString(text); VariantClear(&user);
    if (FAILED(hr)) SetLastError(static_cast<DWORD>(hr));
    return SUCCEEDED(hr);
}
