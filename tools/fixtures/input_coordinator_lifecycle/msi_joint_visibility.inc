// Explicit joint-fixture OS state only. No CLI, driver, registry or PnP API is
// called here. Production apply/refresh/restore and its journals remain callers.
#include <cwctype>
#include <cwchar>
#include <cstdint>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace msiJointVisibility {
struct State {
    bool configured=false,cloaking=false;
    std::set<std::wstring> allowedDevices,allowedApplications,hidden,applications;
    std::vector<std::wstring> commands;
    unsigned cycles=0;
};
static State state;
static std::wstring statePath;
static constexpr DWORD maxStateBytes=1024*1024;
static std::wstring key(std::wstring value) {
    for(auto& c:value)c=static_cast<wchar_t>(std::towupper(c));
    return value;
}
static bool ordinaryValue(const std::wstring& value) {
    return !value.empty()&&value.find_first_of(L"\r\n\"") == std::wstring::npos &&
        value.find(L'\0')==std::wstring::npos;
}
static json document(const State& value) {
    auto paths=[](const std::set<std::wstring>& values){
        json result=json::array();for(const auto& path:values)result.push_back(W2U(path));return result;
    };
    json commands=json::array();for(const auto& command:value.commands)commands.push_back(W2U(command));
    return {{"schema",1},{"allowedDevices",paths(value.allowedDevices)},
        {"allowedApplications",paths(value.allowedApplications)},{"hidden",paths(value.hidden)},
        {"applications",paths(value.applications)},{"cloaking",value.cloaking},
        {"commands",commands},{"cycles",value.cycles}};
}
static HANDLE openState(DWORD creation) {
    const auto name=std::filesystem::path(statePath).filename().wstring();
    const std::wstring prefix=L"msi-visibility-",suffix=L".json";
    const auto digits=name.size()>prefix.size()+suffix.size()
        ?name.substr(prefix.size(),name.size()-prefix.size()-suffix.size()):std::wstring{};
    if(statePath.empty()||!std::filesystem::path(statePath).is_absolute()||
       name.compare(0,prefix.size(),prefix)!=0||!name.ends_with(suffix)||digits.empty()||
       digits[0]==L'0'||digits.find_first_not_of(L"0123456789")!=std::wstring::npos)
        throw std::logic_error("MSI-joint-visibility-state-path-not-owned");
    const HANDLE file=CreateFileW(statePath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,
        nullptr,creation,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::logic_error("MSI-joint-visibility-state-open-failed");
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file,&info)||
       (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||info.nNumberOfLinks!=1){
        const bool closed=CloseHandle(file)!=FALSE;
        if(!closed)throw std::logic_error("MSI-joint-visibility-rejected-handle-close-failed");
        throw std::logic_error("MSI-joint-visibility-state-not-ordinary");
    }
    return file;
}
static void closeState(HANDLE file) {
    if(!CloseHandle(file))throw std::logic_error("MSI-joint-visibility-state-close-failed");
}
static void writeState(HANDLE file) {
    const auto raw=document(state).dump();DWORD written=0;LARGE_INTEGER zero{};
    if(raw.size()>maxStateBytes||!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN)||
       !WriteFile(file,raw.data(),static_cast<DWORD>(raw.size()),&written,nullptr)||
       written!=raw.size()||!SetEndOfFile(file)||!FlushFileBuffers(file))
        throw std::logic_error("MSI-joint-visibility-state-write-failed");
}
static void readState(HANDLE file) {
    LARGE_INTEGER size{},zero{};DWORD count=0;
    if(!GetFileSizeEx(file,&size)||size.QuadPart<=0||size.QuadPart>maxStateBytes||
       !SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))
        throw std::logic_error("MSI-joint-visibility-state-size-invalid");
    std::string raw(static_cast<std::size_t>(size.QuadPart),'\0');
    if(!ReadFile(file,raw.data(),static_cast<DWORD>(raw.size()),&count,nullptr)||count!=raw.size())
        throw std::logic_error("MSI-joint-visibility-state-read-failed");
    const auto value=json::parse(raw);
    if(!value.is_object()||value.size()!=8||value.at("schema")!=1||
       !value.at("cloaking").is_boolean()||!value.at("cycles").is_number_unsigned())
        throw std::logic_error("MSI-joint-visibility-state-schema-invalid");
    auto paths=[&](const char* name){
        const auto& items=value.at(name);std::set<std::wstring> result;
        if(!items.is_array()||items.size()>256)throw std::logic_error("MSI-joint-visibility-state-paths-invalid");
        for(const auto& item:items){
            if(!item.is_string())throw std::logic_error("MSI-joint-visibility-state-path-type");
            auto path=U2W(item.get<std::string>());
            if(!ordinaryValue(path)||!result.insert(key(path)).second)
                throw std::logic_error("MSI-joint-visibility-state-path-invalid");
        }
        return result;
    };
    State next;next.configured=true;
    next.allowedDevices=paths("allowedDevices");next.allowedApplications=paths("allowedApplications");
    if(next.allowedDevices!=state.allowedDevices||next.allowedApplications!=state.allowedApplications)
        throw std::logic_error("MSI-joint-visibility-declared-inputs-drifted");
    next.hidden=paths("hidden");next.applications=paths("applications");
    for(const auto& path:next.hidden)if(!next.allowedDevices.contains(path))
        throw std::logic_error("MSI-joint-visibility-state-unknown-hidden-path");
    for(const auto& path:next.applications)if(!next.allowedApplications.contains(path))
        throw std::logic_error("MSI-joint-visibility-state-unknown-application");
    next.cloaking=value.at("cloaking").get<bool>();next.cycles=value.at("cycles").get<unsigned>();
    if(next.cycles>4096)throw std::logic_error("MSI-joint-visibility-cycle-limit");
    const auto& commands=value.at("commands");
    if(!commands.is_array()||commands.size()>4096)throw std::logic_error("MSI-joint-visibility-command-log-invalid");
    for(const auto& command:commands){
        if(!command.is_string())throw std::logic_error("MSI-joint-visibility-command-type-invalid");
        next.commands.push_back(U2W(command.get<std::string>()));
    }
    state=std::move(next);
}
static void configure(const std::vector<std::wstring>& devices,
                      const std::vector<std::wstring>& applications,const std::wstring& filePath) {
    if(state.configured)
        throw std::logic_error("MSI-joint-visibility-prior-scope-not-returned");
    if(devices.empty()||applications.empty())
        throw std::logic_error("MSI-joint-visibility-finite-inputs-required");
    State next;
    for(const auto& path:devices){
        if(!ordinaryValue(path))throw std::logic_error("MSI-joint-visibility-bad-device-input");
        next.allowedDevices.insert(key(path));
    }
    for(const auto& path:applications){
        if(!ordinaryValue(path))throw std::logic_error("MSI-joint-visibility-bad-application-input");
        next.allowedApplications.insert(key(path));
    }
    next.configured=true;state=std::move(next);statePath=filePath;
    const HANDLE file=openState(CREATE_NEW);
    try{writeState(file);}catch(...){closeState(file);throw;}
    closeState(file);
}
static std::wstring argument(const std::wstring& command,const std::wstring& prefix) {
    if(command.size()<=prefix.size()+2||command.compare(0,prefix.size(),prefix)!=0||
       command[prefix.size()]!=L'"'||command.back()!=L'"')
        throw std::logic_error("MSI-joint-visibility-exact-quoted-argument-required");
    auto value=command.substr(prefix.size()+1,command.size()-prefix.size()-2);
    if(!ordinaryValue(value))throw std::logic_error("MSI-joint-visibility-invalid-argument");
    return key(std::move(value));
}
static std::string listing(const std::set<std::wstring>& values,const char* command) {
    std::string result;
    for(const auto& value:values){result+=command;result+=" \"";result+=W2U(value);result+="\"\n";}
    return result;
}
static bool execute(const std::wstring& command,std::string* output,DWORD* exitCode,
                    ULONGLONG budgetDeadline) {
    if(!state.configured)throw std::logic_error("MSI-joint-visibility-not-configured");
    if(budgetDeadline&&GetTickCount64()>=budgetDeadline){
        if(output)output->clear();if(exitCode)*exitCode=ERROR_TIMEOUT;return false;
    }
    std::string answer;
    if(command==L"--dev-list")answer=listing(state.hidden,"--dev-hide");
    else if(command==L"--app-list")answer=listing(state.applications,"--app-reg");
    else if(command==L"--cloak-state")answer=state.cloaking?"--cloak-on\n":"--cloak-off\n";
    else if(command==L"--inv-state")answer="--inv-off\n";
    else if(command==L"--cloak-on")state.cloaking=true;
    else if(command==L"--cloak-off")state.cloaking=false;
    else {
        struct Operation {const wchar_t* prefix;bool device,add;};
        static constexpr Operation operations[]={
            {L"--dev-hide ",true,true},{L"--dev-unhide ",true,false},
            {L"--app-reg ",false,true},{L"--app-unreg ",false,false}};
        bool matched=false;
        for(const auto& operation:operations){
            const std::wstring prefix=operation.prefix;
            if(command.compare(0,prefix.size(),prefix)!=0)continue;
            const auto value=argument(command,prefix);
            const auto& allowed=operation.device?state.allowedDevices:state.allowedApplications;
            if(!allowed.contains(value))throw std::logic_error("MSI-joint-visibility-undeclared-target");
            auto& values=operation.device?state.hidden:state.applications;
            if(operation.add)values.insert(value);else values.erase(value);
            matched=true;break;
        }
        if(!matched)throw std::logic_error("MSI-joint-visibility-unknown-command");
    }
    if(state.commands.size()>=4096)throw std::logic_error("MSI-joint-visibility-command-limit");
    state.commands.push_back(command);
    if(output)*output=std::move(answer);if(exitCode)*exitCode=ERROR_SUCCESS;return true;
}
static bool run(const std::wstring& command,std::string* output,DWORD* exitCode,
                ULONGLONG budgetDeadline) {
    const HANDLE file=openState(OPEN_EXISTING);bool result=false;
    try{readState(file);result=execute(command,output,exitCode,budgetDeadline);if(result)writeState(file);}
    catch(...){closeState(file);throw;}
    closeState(file);return result;
}
static bool cycle(const std::wstring& root,const char* reason) {
    if(!reason||!ordinaryValue(root))throw std::logic_error("MSI-joint-cycle-input-invalid");
    const auto normalized=key(root);
    if(normalized!=LR"(USB\VID_0DB0&PID_1901\MSI_POLICY_RETURN_BUILTIN)"&&
       normalized!=LR"(USB\VID_0DB0&PID_1902\MSI_POLICY_RETURN_BUILTIN)")
        throw std::logic_error("MSI-joint-cycle-unknown-root");
    const HANDLE file=openState(OPEN_EXISTING);
    try{
        readState(file);
        if(state.cycles>=4096||state.commands.size()>=4096)
            throw std::logic_error("MSI-joint-cycle-limit");
        ++state.cycles;state.commands.push_back(L"cycle "+root);writeState(file);
    }catch(...){closeState(file);throw;}
    closeState(file);return true;
}
static bool supplyCycleSignal(const std::string& runId,std::uint64_t hostEpoch,
                              std::uint64_t power,const char* outcome) {
    if(!outcome||(std::string(outcome)!="done"&&std::string(outcome)!="failed"&&
                  std::string(outcome)!="cancelled"))
        throw std::logic_error("MSI-joint-cycle-signal-outcome-unknown");
    const HANDLE file=openState(OPEN_EXISTING);bool supplied=false;
    try{
        readState(file);
        constexpr const wchar_t* begin=L"managed:HIDHIDE_CYCLE_BEGIN ";
        constexpr const wchar_t* signal=L"fixture-signal:HIDHIDE_CYCLE ";
        json binding;bool terminal=false;
        for(std::size_t i=0;i<state.commands.size();++i){
            const auto& command=state.commands[i];
            if(command.rfind(begin,0)==0){
                binding=json::parse(W2U(command.substr(std::wcslen(begin))));terminal=false;
                if(!binding.is_object()||binding.size()!=5||binding.at("requestIndex")!=i)
                    throw std::logic_error("MSI-joint-cycle-BEGIN-binding-invalid");
            }else if(command.rfind(signal,0)==0){
                const auto value=json::parse(W2U(command.substr(std::wcslen(signal))));
                if(binding.is_object()&&value.at("requestIndex")==binding.at("requestIndex")&&
                   value.at("path")==binding.at("path")&&value.at("runId")==binding.at("runId")&&
                   value.at("epoch")==binding.at("epoch")&&value.at("powerGeneration")==binding.at("powerGeneration"))
                    terminal=true;
            }
        }
        if(binding.is_object()&&!terminal&&binding.at("runId")==runId&&
           binding.at("epoch")==hostEpoch&&binding.at("powerGeneration")==power){
            if(state.commands.size()>=4096)throw std::logic_error("MSI-joint-cycle-signal-command-limit");
            const auto root=key(U2W(binding.at("path").get<std::string>()));
            if(!state.allowedDevices.contains(root)||
               (root!=LR"(USB\VID_0DB0&PID_1901\MSI_POLICY_RETURN_BUILTIN)"&&
                root!=LR"(USB\VID_0DB0&PID_1902\MSI_POLICY_RETURN_BUILTIN)"))
                throw std::logic_error("MSI-joint-cycle-signal-unknown-root");
            auto completed=binding;completed["state"]=outcome;
            completed["detail"]="explicit-controlled-fixture-cycle-signal";
            state.commands.push_back(std::wstring(signal)+U2W(completed.dump()));
            writeState(file);supplied=true;
        }
    }catch(...){closeState(file);throw;}
    closeState(file);return supplied;
}
static void retire() {
    const HANDLE file=openState(OPEN_EXISTING);
    try{readState(file);}catch(...){closeState(file);throw;}
    closeState(file);
    if(!state.configured)throw std::logic_error("MSI-joint-visibility-model-not-configured");
    // Preserve all final lists and resident cloak in the owned evidence file.
    // The original journal/return consumers decide ownership and discharge;
    // closing this model's observation scope is never proof of product return.
    state.configured=false;
}
} // namespace msiJointVisibility
