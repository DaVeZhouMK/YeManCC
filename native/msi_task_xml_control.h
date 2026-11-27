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
static std::wstring oemVendorMsiReceiptPath(const std::wstring& directory, const std::wstring& name) {
    std::uint64_t id = 14695981039346656037ULL;
    for (auto c : name) { id ^= static_cast<std::uint16_t>(towlower(c)); id *= 1099511628211ULL; }
    return directory + L"\\msi-task-" + std::to_wstring(id) + L".json";
}
static bool oemVendorMsiRestorable(IRegisteredTask* task,const std::wstring& name) {
    ComPtr<ITaskDefinition> definition; ComPtr<IPrincipal> principal;
    ComPtr<IActionCollection> actions; ComPtr<IAction> action; ComPtr<IExecAction> exec;
    TASK_LOGON_TYPE logon{}; TASK_RUNLEVEL_TYPE level{}; LONG count=0;
    if (FAILED(task->get_Definition(&definition)) || FAILED(definition->get_Principal(&principal)) ||
        FAILED(principal->get_LogonType(&logon)) || logon!=TASK_LOGON_GROUP ||
        FAILED(principal->get_RunLevel(&level)) || (level!=TASK_RUNLEVEL_LUA && level!=TASK_RUNLEVEL_HIGHEST) ||
        FAILED(definition->get_Actions(&actions)) || FAILED(actions->get_Count(&count)) || count!=1 ||
        FAILED(actions->get_Item(1,&action)) || FAILED(action.As(&exec))) return false;
    BSTR group=nullptr,path=nullptr,args=nullptr,cwd=nullptr;
    PWSTR programFiles=nullptr;
    bool ok=SUCCEEDED(principal->get_GroupId(&group)) && group && wcscmp(group,L"S-1-5-32-545")==0 &&
        SUCCEEDED(exec->get_Path(&path)) && path && SUCCEEDED(exec->get_Arguments(&args)) &&
        SUCCEEDED(exec->get_WorkingDirectory(&cwd)) && (!cwd || !SysStringLen(cwd)) &&
        SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX86,0,nullptr,&programFiles));
    if (ok) {
        const bool server=_wcsicmp(name.substr(name.find_last_of(L'\\')+1).c_str(),L"MSI_Center_M_Server")==0;
        const auto expected=std::wstring(programFiles)+L"\\MSI\\MSI Center M\\"+
            (server?L"MSI_Center_M_Server.exe":L"MSI Center M Updater.exe");
        std::wstring actual(path,SysStringLen(path));
        if(actual.size()>=2 && actual.front()==L'"' && actual.back()==L'"')actual=actual.substr(1,actual.size()-2);
        const auto attrs=GetFileAttributesW(expected.c_str());
        ok=_wcsicmp(actual.c_str(),expected.c_str())==0 && _wcsicmp(args?args:L"",server?L"":L"MSI_Center_M")==0 &&
           attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) &&
           ymcc::updateconfig::ordinaryParents(std::filesystem::path(expected));
    }
    CoTaskMemFree(programFiles); SysFreeString(group); SysFreeString(path); SysFreeString(args); SysFreeString(cwd);
    if(!ok)SetLastError(ERROR_NOT_SUPPORTED);
    return ok;
}
static bool oemVendorMsiCapture(const std::wstring& name) {
    using namespace ymcc::maintenance;
    try {
        const auto directory = receiptsDirectory();
        if (directory.empty()) { SetLastError(ERROR_ACCESS_DENIED); return false; }
        const auto path = oemVendorMsiReceiptPath(directory,name);
        const auto prior = ymcc::updateconfig::read(path,65536);
        json record;
        bool exists = false;
        if (!oemVendorTaskAccess(name,[&](IRegisteredTask* task) {
            if (!oemVendorMsiRestorable(task,name)) return false;
            // Capture the actual definition, never infer it from the template shipped in the ZIP.
            BSTR content = nullptr; VARIANT_BOOL enabled{};
            const auto hr = task->get_Xml(&content);
            const std::wstring xml = content ? std::wstring(content,SysStringLen(content)) : std::wstring{};
            SysFreeString(content);
            if (FAILED(hr) || xml.empty() || xml.size() > 20000 || FAILED(task->get_Enabled(&enabled))) return false;
            record = {{"schema",1},{"id","msi-task"},{"taskPath",W2U(name)},{"xml",W2U(xml)},{"enabled",enabled != VARIANT_FALSE},{"removed",false}};
            return true;
        },exists) || !exists || !record.is_object()) return false;
        if (prior.kind != Kind::missing) {
            if (prior.kind != Kind::ready || !protectedReceipt(path) || prior.value != record) {
                SetLastError(ERROR_REVISION_MISMATCH); return false;
            }
            return true;
        }
        return saveReceipt(path,prior,record);
    } catch (...) { SetLastError(ERROR_INVALID_DATA); return false; }
}
static bool oemVendorMsiConfirmRemoved(const std::wstring& name) {
    using namespace ymcc::maintenance;
    try {
        const auto directory = receiptsDirectory();
        if (directory.empty()) return false;
        const auto path = oemVendorMsiReceiptPath(directory,name);
        const auto prior = ymcc::updateconfig::read(path,65536);
        if (prior.kind != Kind::ready || !protectedReceipt(path) || prior.value.value("schema",0) != 1 ||
            prior.value.value("id",std::string{}) != "msi-task" || prior.value.value("taskPath",std::string{}) != W2U(name)) return false;
        auto committed = prior.value;
        committed["removed"] = true;
        return saveReceipt(path,prior,committed);
    } catch (...) { return false; }
}
static void oemVendorMsiRetire(const std::wstring& name) {
    using namespace ymcc::maintenance;
    try {
        const auto directory = receiptsDirectory();
        if (directory.empty()) return;
        const auto path = oemVendorMsiReceiptPath(directory,name);
        const auto image = ymcc::updateconfig::read(path,65536);
        if (image.kind == Kind::ready && protectedReceipt(path) && image.value.value("id",std::string{}) == "msi-task" &&
            image.value.value("taskPath",std::string{}) == W2U(name)) DeleteFileW(path.c_str());
    } catch (...) { }
}
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
    return oemVendorTaskXmlContext(name, service, folder, leaf) && oemVendorTaskXmlDefinition(name, service.Get(), definition) && oemVendorMsiCapture(name);
}
static bool oemVendorTaskRemove(const std::wstring& name) {
    OemVendorTaskXmlComScope scope; if (!scope.ready()) return false;
    ComPtr<ITaskService> service; ComPtr<ITaskFolder> folder; std::wstring leaf;
    if (!oemVendorTaskXmlContext(name, service, folder, leaf)) return false;
    BSTR text = SysAllocString(leaf.c_str());
    if (!text) { SetLastError(ERROR_OUTOFMEMORY); return false; }
    const HRESULT hr = folder->DeleteTask(text, 0); SysFreeString(text);
    if (FAILED(hr)) SetLastError(static_cast<DWORD>(hr));
    return SUCCEEDED(hr);
}
static bool oemVendorTaskImport(const std::wstring& name) {
    OemVendorTaskXmlComScope scope; if (!scope.ready()) return false;
    ComPtr<ITaskService> service; ComPtr<ITaskFolder> folder; ComPtr<ITaskDefinition> definition; std::wstring leaf;
    if (!oemVendorTaskXmlContext(name, service, folder, leaf)) return false;
    try {
        using namespace ymcc::maintenance;
        const auto dir = receiptsDirectory();
        const auto record = dir.empty() ? std::wstring{} : oemVendorMsiReceiptPath(dir,name);
        const auto image = record.empty() ? Image{Kind::missing} : ymcc::updateconfig::read(record,65536);
        if (image.kind == Kind::missing) {
            if (!oemVendorTaskXmlDefinition(name, service.Get(), definition)) return false;
        } else {
            if (image.kind != Kind::ready || !protectedReceipt(record) || image.value.value("schema",0) != 1 ||
                image.value.value("id",std::string{}) != "msi-task" || image.value.value("taskPath",std::string{}) != W2U(name)) {
                SetLastError(ERROR_INVALID_DATA); return false;
            }
            const auto xml = U2W(image.value.at("xml").get<std::string>());
            BSTR text = SysAllocStringLen(xml.data(),static_cast<UINT>(xml.size()));
            if (!text) { SetLastError(ERROR_OUTOFMEMORY); return false; }
            auto read = service->NewTask(0,&definition);
            if (SUCCEEDED(read)) read = definition->put_XmlText(text);
            SysFreeString(text);
            if (FAILED(read)) { SetLastError(static_cast<DWORD>(read)); return false; }
        }
    } catch (...) { SetLastError(ERROR_INVALID_DATA); return false; }
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
