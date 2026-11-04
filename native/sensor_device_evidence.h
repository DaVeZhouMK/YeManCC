#pragma once
// Explicit-export metadata only. No sensor streams, device control, service control or repair.
// C++17, independent of main.cpp's JSON alias and Windows include order.
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpropdef.h>
#include <winsvc.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>

namespace ymcc::sensors::device_evidence {
inline constexpr std::uint64_t budget_ms = 1500;
inline constexpr std::size_t max_snapshot_bytes = 256 * 1024;
inline constexpr std::size_t max_property_bytes = 4096;
inline constexpr std::size_t max_array_entries = 16;
inline constexpr unsigned max_nodes = 64, max_parent_depth = 6, max_services = 64;
inline constexpr GUID sensor_class = {0x5175d334,0xc371,0x4806,{0xb3,0xba,0x71,0xfd,0x53,0xc9,0x25,0x8d}};

// Local SDK keys avoid INITGUID and do not change the including translation unit.
inline constexpr GUID device_fmt = {0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}};
inline constexpr GUID driver_fmt = {0xa8b865dd,0x2e3d,0x4094,{0xad,0x97,0xe5,0x93,0xa7,0x0c,0x75,0xd6}};
inline constexpr GUID problem_fmt = {0x4340a6c5,0x93fa,0x4706,{0x97,0x2c,0x7b,0x64,0x80,0x08,0xa5,0xa7}};
enum class Field { InstanceId, HardwareIds, CompatibleIds, ClassGuid, Description, Manufacturer,
    IsPresent, ProblemCode, ProblemStatus, DriverInfPath, DriverProvider, DriverVersion, Service, EnumeratorName };
struct PropertySpec { Field field; const char* name; DEVPROPKEY key; DEVPROPTYPE type; };
inline constexpr PropertySpec properties[] = {
    {Field::InstanceId,"deviceInstanceId",{{0x78c34fc8,0x104a,0x4aca,{0x9e,0xa4,0x52,0x4d,0x52,0x99,0x6e,0x57}},256},DEVPROP_TYPE_STRING},
    {Field::HardwareIds,"hardwareIds",{device_fmt,3},DEVPROP_TYPE_STRING_LIST},
    {Field::CompatibleIds,"compatibleIds",{device_fmt,4},DEVPROP_TYPE_STRING_LIST},
    {Field::ClassGuid,"classGuid",{device_fmt,10},DEVPROP_TYPE_GUID},
    {Field::Description,"description",{device_fmt,2},DEVPROP_TYPE_STRING},
    {Field::Manufacturer,"manufacturer",{device_fmt,13},DEVPROP_TYPE_STRING},
    {Field::IsPresent,"isPresent",{{0x540b947e,0x8b40,0x45bc,{0xa8,0xa2,0x6a,0x0b,0x89,0x4c,0xbd,0xa2}},5},DEVPROP_TYPE_BOOLEAN},
    {Field::ProblemCode,"problemCode",{problem_fmt,3},DEVPROP_TYPE_UINT32},
    {Field::ProblemStatus,"problemStatus",{problem_fmt,12},DEVPROP_TYPE_NTSTATUS},
    {Field::DriverInfPath,"driverInfPath",{driver_fmt,5},DEVPROP_TYPE_STRING},
    {Field::DriverProvider,"driverProvider",{driver_fmt,9},DEVPROP_TYPE_STRING},
    {Field::DriverVersion,"driverVersion",{driver_fmt,3},DEVPROP_TYPE_STRING},
    {Field::Service,"service",{device_fmt,6},DEVPROP_TYPE_STRING},
    {Field::EnumeratorName,"enumeratorName",{device_fmt,24},DEVPROP_TYPE_STRING}
};
struct Result {
    bool ok = false;
    DWORD win32Error = ERROR_SUCCESS;
    CONFIGRET configRet = CR_SUCCESS;
    bool hasConfigRet = false;
};
struct Property : Result {
    DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
    DWORD requiredBytes = 0;
    std::vector<unsigned char> bytes;
};
struct Node { DEVINST devinst = 0; HDEVINFO set = INVALID_HANDLE_VALUE; SP_DEVINFO_DATA info{}; };
struct NodeResult : Result { Node node{}; };
struct SetResult : Result { HDEVINFO handle = INVALID_HANDLE_VALUE; };
struct ServiceHandleResult : Result { SC_HANDLE handle = nullptr; };
struct NodeStatus : Result { ULONG flags = 0, problem = 0; };
struct ServiceStatus : Result { SERVICE_STATUS_PROCESS value{}; };
struct ServiceConfig : Result { DWORD startType = 0, requiredBytes = 0; };
struct Identity : Result { DWORD pid = 0; std::uint64_t creationFileTime = 0; };
struct Observation : Result { std::uint64_t fileTimeUtc = 0; };
inline Result win_result(bool ok, DWORD error) { Result r; r.ok=ok; r.win32Error=ok?ERROR_SUCCESS:error; return r; }
inline Result cm_result(CONFIGRET cr) { Result r; r.ok=cr==CR_SUCCESS; r.hasConfigRet=true; r.configRet=cr; return r; }
inline std::uint64_t filetime_value(const FILETIME& t) { return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime; }

// Every external metadata API is a separate Ops method, bracketed by the collector's budget checks.
struct WindowsOps {
    std::uint64_t monotonicMs() const { return GetTickCount64(); }
    Observation observedTime() const {
        Observation r; FILETIME t{}; GetSystemTimeAsFileTime(&t); r.fileTimeUtc=filetime_value(t); r.ok=true; return r;
    }
    Identity identity() const {
        Identity r; r.pid=GetCurrentProcessId(); FILETIME birth{},exit{},kernel{},user{};
        const bool ok=GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user)!=FALSE;
        const DWORD error=ok?ERROR_SUCCESS:GetLastError(); static_cast<Result&>(r)=win_result(ok,error);
        if(ok)r.creationFileTime=filetime_value(birth); return r;
    }
    SetResult openSensorClass() const {
        SetResult r; // flags=0 intentionally includes phantom nodes; never infer presence from enumeration.
        r.handle=SetupDiGetClassDevsW(&sensor_class,nullptr,nullptr,0);
        static_cast<Result&>(r)=win_result(r.handle!=INVALID_HANDLE_VALUE,r.handle==INVALID_HANDLE_VALUE?GetLastError():ERROR_SUCCESS); return r;
    }
    NodeResult enumNode(HDEVINFO set, unsigned index) const {
        NodeResult r; r.node.info.cbSize=sizeof(SP_DEVINFO_DATA); r.node.set=set;
        const bool ok=SetupDiEnumDeviceInfo(set,index,&r.node.info)!=FALSE;
        const DWORD error=ok?ERROR_SUCCESS:GetLastError(); static_cast<Result&>(r)=win_result(ok,error);
        if(ok)r.node.devinst=r.node.info.DevInst; return r;
    }
    Property property(const Node& node,const PropertySpec& spec) const {
        Property r; r.bytes.resize(max_property_bytes); DWORD required=0;
        if(node.set!=INVALID_HANDLE_VALUE) {
            auto info=node.info;
            const bool ok=SetupDiGetDevicePropertyW(node.set,&info,&spec.key,&r.type,r.bytes.data(),
                static_cast<DWORD>(r.bytes.size()),&required,0)!=FALSE;
            const DWORD error=ok?ERROR_SUCCESS:GetLastError(); static_cast<Result&>(r)=win_result(ok,error);
        } else {
            ULONG bytes=static_cast<ULONG>(r.bytes.size());
            const CONFIGRET cr=CM_Get_DevNode_PropertyW(node.devinst,&spec.key,&r.type,r.bytes.data(),&bytes,0);
            static_cast<Result&>(r)=cm_result(cr); required=bytes;
        }
        r.requiredBytes=required;
        if(r.ok&&required<=max_property_bytes)r.bytes.resize(required); else r.bytes.clear();
        return r;
    }
    NodeStatus nodeStatus(const Node& node) const {
        NodeStatus r; const CONFIGRET cr=CM_Get_DevNode_Status(&r.flags,&r.problem,node.devinst,0);
        static_cast<Result&>(r)=cm_result(cr); return r;
    }
    NodeResult parent(const Node& node) const {
        NodeResult r; const CONFIGRET cr=CM_Get_Parent(&r.node.devinst,node.devinst,0);
        static_cast<Result&>(r)=cm_result(cr); return r;
    }
    ServiceHandleResult openScm() const {
        ServiceHandleResult r; r.handle=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
        static_cast<Result&>(r)=win_result(r.handle!=nullptr,r.handle?ERROR_SUCCESS:GetLastError()); return r;
    }
    ServiceHandleResult openService(SC_HANDLE scm,const std::wstring& name,DWORD access) const {
        ServiceHandleResult r; r.handle=OpenServiceW(scm,name.c_str(),access);
        static_cast<Result&>(r)=win_result(r.handle!=nullptr,r.handle?ERROR_SUCCESS:GetLastError()); return r;
    }
    ServiceStatus serviceStatus(SC_HANDLE service) const {
        ServiceStatus r; DWORD needed=0;
        const bool ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&r.value),sizeof(r.value),&needed)!=FALSE;
        const DWORD error=ok?ERROR_SUCCESS:GetLastError(); static_cast<Result&>(r)=win_result(ok,error); return r;
    }
    ServiceConfig serviceConfig(SC_HANDLE service) const {
        ServiceConfig r; alignas(QUERY_SERVICE_CONFIGW) std::array<unsigned char,max_property_bytes> buffer{};
        const bool ok=QueryServiceConfigW(service,reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data()),
            static_cast<DWORD>(buffer.size()),&r.requiredBytes)!=FALSE;
        const DWORD error=ok?ERROR_SUCCESS:GetLastError(); static_cast<Result&>(r)=win_result(ok,error);
        if(ok)r.startType=reinterpret_cast<const QUERY_SERVICE_CONFIGW*>(buffer.data())->dwStartType; return r;
    }
    void closeSet(HDEVINFO h) const { SetupDiDestroyDeviceInfoList(h); }
    void closeService(SC_HANDLE h) const { CloseServiceHandle(h); }
};
struct RestoreLastError { DWORD saved=GetLastError(); ~RestoreLastError(){SetLastError(saved);} };
template<class Ops> struct SetOwner {
    Ops& ops; HDEVINFO handle=INVALID_HANDLE_VALUE;
    ~SetOwner(){if(handle!=INVALID_HANDLE_VALUE)ops.closeSet(handle);}
    SetOwner(const SetOwner&)=delete; SetOwner& operator=(const SetOwner&)=delete;
    explicit SetOwner(Ops& value):ops(value){}
};
template<class Ops> struct ServiceOwner {
    Ops& ops; SC_HANDLE handle=nullptr;
    ~ServiceOwner(){if(handle)ops.closeService(handle);}
    ServiceOwner(const ServiceOwner&)=delete; ServiceOwner& operator=(const ServiceOwner&)=delete;
    explicit ServiceOwner(Ops& value):ops(value){}
};
inline std::string guid_text(const GUID& g) {
    char text[39]{}; std::snprintf(text,sizeof(text),"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
        static_cast<unsigned long>(g.Data1),unsigned(g.Data2),unsigned(g.Data3),unsigned(g.Data4[0]),unsigned(g.Data4[1]),
        unsigned(g.Data4[2]),unsigned(g.Data4[3]),unsigned(g.Data4[4]),unsigned(g.Data4[5]),unsigned(g.Data4[6]),unsigned(g.Data4[7])); return text;
}
// UTF-16 conversion is local, bounded and makes no additional Windows API calls.
inline std::string utf8(const std::wstring& text) {
    std::string out;
    for(std::size_t i=0;i<text.size();++i) {
        std::uint32_t c=static_cast<std::uint16_t>(text[i]);
        if(c>=0xd800&&c<=0xdbff) { if(i+1<text.size()&&text[i+1]>=0xdc00&&text[i+1]<=0xdfff)
            c=0x10000+((c-0xd800)<<10)+(static_cast<std::uint16_t>(text[++i])-0xdc00); else c=0xfffd;
        } else if(c>=0xdc00&&c<=0xdfff)c=0xfffd;
        if(c<0x80)out.push_back(static_cast<char>(c));
        else if(c<0x800){out.push_back(char(0xc0|(c>>6)));out.push_back(char(0x80|(c&63)));}
        else if(c<0x10000){out.push_back(char(0xe0|(c>>12)));out.push_back(char(0x80|((c>>6)&63)));out.push_back(char(0x80|(c&63)));}
        else {out.push_back(char(0xf0|(c>>18)));out.push_back(char(0x80|((c>>12)&63)));out.push_back(char(0x80|((c>>6)&63)));out.push_back(char(0x80|(c&63)));}
    } return out;
}
template<class Json> Json result_json(const Result& r) {
    return Json{{"status",r.ok?"observed":"failed"},{"success",r.ok},{"errorDomain",r.hasConfigRet?"CONFIGRET":"Win32"},
        {"win32Error",r.hasConfigRet?Json(nullptr):Json(r.win32Error)},{"configRet",r.hasConfigRet?Json(r.configRet):Json(nullptr)}};
}
template<class Json> Json decode_property(const Property& p,const PropertySpec& spec,std::wstring* service=nullptr) {
    auto j=result_json<Json>(p); j["value"]=nullptr; j["type"]=p.type; j["expectedType"]=spec.type;
    j["requiredBytes"]=p.requiredBytes; j["truncated"]=p.requiredBytes>max_property_bytes||p.bytes.size()>max_property_bytes;
    if(!p.ok)return j;
    if(p.type!=spec.type){j["status"]="type-mismatch";j["success"]=false;return j;}
    const auto size=p.bytes.size(); bool valid=true;
    if(size>max_property_bytes){j["status"]="truncated";j["success"]=false;return j;}
    if(p.type==DEVPROP_TYPE_STRING||p.type==DEVPROP_TYPE_STRING_LIST) {
        if(size%sizeof(wchar_t)!=0||size<sizeof(wchar_t))valid=false;
        else {
            std::wstring text(size/sizeof(wchar_t),L'\0');std::memcpy(text.data(),p.bytes.data(),size);
            if(text.back()!=L'\0')valid=false;
            else if(p.type==DEVPROP_TYPE_STRING) {
                const auto end=text.find(L'\0'); if(end+1!=text.size())valid=false;
                else {text.resize(end);j["value"]=utf8(text);if(service)*service=text;}
            } else {
                if(text.size()<2||text[text.size()-2]!=L'\0')valid=false;
                else { j["value"]=Json::array();std::size_t at=0;
                    while(at+1<text.size()&&text[at]) {
                        const auto end=text.find(L'\0',at);
                        if(end==std::wstring::npos){valid=false;break;}
                        if(j["value"].size()==max_array_entries){j["truncated"]=true;break;}
                        j["value"].push_back(utf8(text.substr(at,end-at)));at=end+1;
                    }
                    // An early terminator with nonzero trailing data is malformed, not a successful empty list.
                    if(!j["truncated"].template get<bool>())for(;at<text.size();++at)if(text[at]){valid=false;break;}
                }
            }
        }
    } else if(p.type==DEVPROP_TYPE_GUID) { if(size!=sizeof(GUID))valid=false;else{GUID g{};std::memcpy(&g,p.bytes.data(),sizeof(g));j["value"]=guid_text(g);} }
    else if(p.type==DEVPROP_TYPE_BOOLEAN) { if(size!=sizeof(DEVPROP_BOOLEAN))valid=false;else {DEVPROP_BOOLEAN b{};std::memcpy(&b,p.bytes.data(),sizeof(b));if(b!=DEVPROP_TRUE&&b!=DEVPROP_FALSE)valid=false;else j["value"]=b==DEVPROP_TRUE;} }
    else { if(size!=sizeof(std::uint32_t))valid=false;else{std::uint32_t value=0;std::memcpy(&value,p.bytes.data(),sizeof(value));j["value"]=value;if(p.type==DEVPROP_TYPE_NTSTATUS)j["representation"]="raw-unsigned-NTSTATUS-bits";} }
    if(!valid){j["value"]=nullptr;j["status"]="malformed";j["success"]=false;}
    // Prevent an exceptionally long property from consuming the entire export. Keep its query/error/type facts.
    if(j.dump(2).size()>max_property_bytes){j["value"]=nullptr;j["truncated"]=true;j["status"]="truncated";if(service)service->clear();}
    if(j["truncated"].template get<bool>())j["status"]="truncated";
    return j;
}

template<class Json,class Ops> struct Budget {
    Ops& ops; Json& root; std::uint64_t start; bool timeLimit=false,overBudget=false,byteLimit=false;
    unsigned calls=0; const char* lastApi="none";
    Budget(Ops& value,Json& doc):ops(value),root(doc),start(ops.monotonicMs()){}
    void clock(){const auto elapsed=ops.monotonicMs()-start;timeLimit=timeLimit||elapsed>=budget_ms;overBudget=overBudget||elapsed>budget_ms;}
    bool before(const char* api) {
        clock();if(timeLimit||byteLimit)return false;
        // Use the same indentation as the export, reserving room for final summaries and a single bounded property.
        if(root.dump(2).size()>=max_snapshot_bytes-16384){byteLimit=true;return false;}
        clock();if(timeLimit)return false;lastApi=api;++calls;return true;
    }
    template<class Fn> bool call(const char* api,Fn&& fn){if(!before(api))return false;fn();clock();return true;}
    bool stopped() const{return timeLimit||byteLimit;}
};

template<class Json,class Ops> Json snapshot_with_ops(Ops& ops) {
    RestoreLastError restore;
    Json root={{"schema",1},{"scope","sensor-class-PnP-driver-service-metadata"},{"readOnly",true},{"writes",false},{"noWrites",true},
        {"explicitExportOnly",true},{"status","partial"},{"hardwareAbsenceProven",false},{"driverFaultProven",false},
        {"limits",{{"budgetMs",budget_ms},{"maxSerializedBytes",max_snapshot_bytes},{"propertyBufferBytes",max_property_bytes},
            {"propertyArrayEntries",max_array_entries},{"sensorNodes",max_nodes},{"parentDepth",max_parent_depth},{"services",max_services}}},
        {"observedTime",{{"status","NOT_COLLECTED"}}},{"processIdentity",{{"status","NOT_COLLECTED"}}},
        {"sensorClass",{{"guid",guid_text(sensor_class)},{"includesPhantom",true},{"presenceSource","isPresent property only"},
            {"status","NOT_COLLECTED"},{"enumerationComplete",false},{"truncated",false},{"nodes",Json::array()}}},
        {"services",{{"status","NOT_COLLECTED"},{"statusMeaning","query-coverage-only;not-service-health"},{"truncated",false},{"entries",Json::array()}}},
        {"sensorInterfaces",{{"status","NOT_COLLECTED"},{"reason","priority-budget-reserved-for-class-parent-driver-service-evidence"},
            {"scopeSufficient",false},{"hardwareAbsenceProven",false}}}};
    Budget<Json,Ops> budget(ops,root); bool partial=false,anyTruncated=false;
    Observation observation;
    if(budget.call("GetSystemTimeAsFileTime",[&]{observation=ops.observedTime();})) {
        root["observedTime"]=result_json<Json>(observation);
        root["observedTime"]["fileTimeUtc"]=observation.ok?Json(std::to_string(observation.fileTimeUtc)):Json(nullptr);
        root["observedTime"]["unit"]="100ns since 1601-01-01 UTC";partial=partial||!observation.ok;
    }
    Identity identity;
    if(budget.call("GetProcessTimes",[&]{identity=ops.identity();})) {
        root["processIdentity"]=result_json<Json>(identity);root["processIdentity"]["processId"]=identity.pid;
        root["processIdentity"]["creationFileTime"]=identity.ok?Json(std::to_string(identity.creationFileTime)):Json(nullptr);
        partial=partial||!identity.ok;
    }
    std::vector<std::wstring> services={L"SensorService",L"SensorDataService",L"SensrSvc"};
    auto read_node=[&](Json& row,const Node& node) {
        row={{"devInst",node.devinst},{"properties",Json::object()},{"statusQuery",{{"status","NOT_COLLECTED"}}},{"metadataComplete",false}};
        NodeStatus status;
        if(budget.call("CM_Get_DevNode_Status",[&]{status=ops.nodeStatus(node);})) {
            auto j=result_json<Json>(status);j["flags"]=status.ok?Json(status.flags):Json(nullptr);j["problem"]=status.ok?Json(status.problem):Json(nullptr);
            j["rawFlags"]=status.flags;j["rawProblem"]=status.problem;row["statusQuery"]=std::move(j);partial=partial||!status.ok;
        }
        unsigned observed=0;bool allPropertiesOk=status.ok;
        for(const auto& spec:properties) {
            Property property;
            if(!budget.call(node.set==INVALID_HANDLE_VALUE?"CM_Get_DevNode_PropertyW":"SetupDiGetDevicePropertyW",[&]{property=ops.property(node,spec);}))break;
            std::wstring service;auto j=decode_property<Json>(property,spec,spec.field==Field::Service?&service:nullptr);
            allPropertiesOk=allPropertiesOk&&j["success"].template get<bool>()&&!j["truncated"].template get<bool>();
            partial=partial||!j["success"].template get<bool>()||j["truncated"].template get<bool>();
            anyTruncated=anyTruncated||j["truncated"].template get<bool>();
            if(j["success"].template get<bool>()&&!j["truncated"].template get<bool>()&&!service.empty()&&
                std::none_of(services.begin(),services.end(),[&](const std::wstring& old){return _wcsicmp(old.c_str(),service.c_str())==0;})) {
                if(service.size()>256||services.size()>=max_services){root["services"]["truncated"]=true;anyTruncated=true;partial=true;}
                else services.push_back(service);
            }
            row["properties"][spec.name]=std::move(j);++observed;
        }
        row["uncollectedPropertyCount"]=unsigned(sizeof(properties)/sizeof(properties[0]))-observed;
        row["propertyQueriesComplete"]=observed==sizeof(properties)/sizeof(properties[0])&&!budget.stopped();
        row["metadataComplete"]=row["propertyQueriesComplete"].template get<bool>()&&allPropertiesOk;
    };
    {
        SetOwner<Ops> owner(ops);SetResult opened;
        if(budget.call("SetupDiGetClassDevsW",[&]{opened=ops.openSensorClass();owner.handle=opened.handle;})) {
            auto& cls=root["sensorClass"];cls["open"]=result_json<Json>(opened);cls["status"]=opened.ok?"partial":"failed";partial=partial||!opened.ok;
            if(opened.ok)for(unsigned i=0;i<max_nodes;++i) {
                NodeResult next;
                if(!budget.call("SetupDiEnumDeviceInfo",[&]{next=ops.enumNode(owner.handle,i);}))break;
                if(!next.ok) {
                    cls["enumerationEnd"]=result_json<Json>(next);
                    if(!next.hasConfigRet&&next.win32Error==ERROR_NO_MORE_ITEMS){cls["enumerationComplete"]=true;cls["status"]="complete";}
                    else {cls["status"]="partial";partial=true;}break;
                }
                cls["nodes"].push_back(Json::object());auto& row=cls["nodes"].back();read_node(row,next.node);
                row["parents"]=Json::array();row["parentChain"]={{"status","NOT_COLLECTED"},{"maxDepth",max_parent_depth},{"truncated",false}};
                Node current=next.node;std::vector<DEVINST> seen={current.devinst};
                for(unsigned depth=0;depth<max_parent_depth&&!budget.stopped();++depth) {
                    NodeResult parent;
                    if(!budget.call("CM_Get_Parent",[&]{parent=ops.parent(current);}))break;
                    row["parentChain"]["lastQuery"]=result_json<Json>(parent);
                    if(!parent.ok) { // This raw CR is evidence, never a fabricated healthy/root node.
                        row["parentChain"]["status"]=parent.configRet==CR_NO_SUCH_DEVNODE?"end-or-unavailable":"failed";
                        if(parent.configRet!=CR_NO_SUCH_DEVNODE)partial=true;break;
                    }
                    if(std::find(seen.begin(),seen.end(),parent.node.devinst)!=seen.end()) {row["parentChain"]["status"]="cycle";partial=true;break;}
                    seen.push_back(parent.node.devinst);row["parents"].push_back(Json::object());read_node(row["parents"].back(),parent.node);
                    row["parents"].back()["depth"]=depth+1;current=parent.node;row["parentChain"]["status"]="partial";
                    if(depth+1==max_parent_depth){row["parentChain"]["status"]="depth-limit";row["parentChain"]["truncated"]=true;anyTruncated=true;partial=true;}
                }
                if(budget.stopped()) {row["parentChain"]["status"]="budget-limit";cls["truncated"]=true;break;}
                if(i+1==max_nodes) { // Deliberately do not query a 65th node to infer whether it exists.
                    cls["status"]="node-limit";cls["truncated"]=true;anyTruncated=true;partial=true;
                }
            }
        }
    } // RAII cleanup is mandatory even after budget exhaustion; it is included in elapsed time.
    {
        ServiceOwner<Ops> scm(ops);ServiceHandleResult opened;
        if(budget.call("OpenSCManagerW",[&]{opened=ops.openScm();scm.handle=opened.handle;})) {
            auto& section=root["services"];section["open"]=result_json<Json>(opened);section["status"]=opened.ok?"partial":"failed";partial=partial||!opened.ok;
            if(opened.ok)for(const auto& name:services) {
                if(budget.stopped())break;
                section["entries"].push_back({{"name",utf8(name)},{"statusQuery",{{"status","NOT_COLLECTED"}}},{"startTypeQuery",{{"status","NOT_COLLECTED"}}}});
                auto& entry=section["entries"].back();
                {ServiceOwner<Ops> service(ops);ServiceHandleResult h;
                    if(budget.call("OpenServiceW:SERVICE_QUERY_STATUS",[&]{h=ops.openService(scm.handle,name,SERVICE_QUERY_STATUS);service.handle=h.handle;})) {
                        entry["statusOpen"]=result_json<Json>(h);partial=partial||!h.ok;
                        if(h.ok){ServiceStatus status;if(budget.call("QueryServiceStatusEx",[&]{status=ops.serviceStatus(service.handle);})) {
                            entry["statusQuery"]=result_json<Json>(status);
                            entry["statusQuery"]["currentState"]=status.ok?Json(status.value.dwCurrentState):Json(nullptr);
                            entry["statusQuery"]["processId"]=status.ok?Json(status.value.dwProcessId):Json(nullptr);
                            entry["statusQuery"]["serviceWin32ExitCode"]=status.ok?Json(status.value.dwWin32ExitCode):Json(nullptr);
                            partial=partial||!status.ok;
                        }}
                    }
                }
                {ServiceOwner<Ops> service(ops);ServiceHandleResult h;
                    if(budget.call("OpenServiceW:SERVICE_QUERY_CONFIG",[&]{h=ops.openService(scm.handle,name,SERVICE_QUERY_CONFIG);service.handle=h.handle;})) {
                        entry["configOpen"]=result_json<Json>(h);partial=partial||!h.ok;
                        if(h.ok){ServiceConfig config;if(budget.call("QueryServiceConfigW",[&]{config=ops.serviceConfig(service.handle);})) {
                            entry["startTypeQuery"]=result_json<Json>(config);
                            entry["startTypeQuery"]["startType"]=config.ok?Json(config.startType):Json(nullptr);
                            entry["startTypeQuery"]["requiredBytes"]=config.requiredBytes;
                            entry["startTypeQuery"]["truncated"]=config.requiredBytes>max_property_bytes;
                            partial=partial||!config.ok||config.requiredBytes>max_property_bytes;anyTruncated=anyTruncated||config.requiredBytes>max_property_bytes;
                        }}
                    }
                }
            }
            if(opened.ok&&section["entries"].size()==services.size()&&!budget.stopped())section["status"]="complete";
        }
        root["services"]["requestedCount"]=services.size();
    }
    budget.clock();
    if(budget.stopped()){partial=true;anyTruncated=true;root["sensorClass"]["truncated"]=true;root["services"]["truncated"]=true;}
    root["collectionStatus"]=partial||!root["sensorClass"]["enumerationComplete"].template get<bool>()?"partial":"complete-within-declared-scope";
    // Interfaces and other device classes are intentionally outside this snapshot's coverage.
    root["status"]="partial";root["scopeSufficientForHardwareAbsence"]=false;
    root["truncated"]=anyTruncated;root["budgetExhausted"]=budget.timeLimit;root["overBudget"]=budget.overBudget;
    root["byteLimitReached"]=budget.byteLimit;root["apiCalls"]=budget.calls;root["lastApi"]=budget.lastApi;
    root["elapsedMs"]=ops.monotonicMs()-budget.start;root["singleSystemCallHardInterruptible"]=false;
    root["cleanupAfterBudgetAllowed"]=true;
    root["serializedBytes"]=0;
    for(unsigned i=0;i<3;++i)root["serializedBytes"]=root.dump(2).size();
    budget.clock();root["elapsedMs"]=ops.monotonicMs()-budget.start;
    root["budgetExhausted"]=budget.timeLimit;root["overBudget"]=budget.overBudget;
    if(budget.timeLimit){root["collectionStatus"]="partial";root["truncated"]=true;}
    for(unsigned i=0;i<2;++i)root["serializedBytes"]=root.dump(2).size();
    return root;
}
template<class Json> Json snapshot() { WindowsOps ops; return snapshot_with_ops<Json>(ops); }
} // namespace ymcc::sensors::device_evidence
