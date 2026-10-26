// Include once after the retired #if 0 block, before ipc_cmd_async.
// Existing main.cpp dependencies (no new worker/background hooks):
// struct RunOut { std::string out; DWORD exitCode; bool ran; };
// static RunOut runCapture(const std::wstring&, DWORD, const std::function<bool()>& = {});
// static std::wstring U2W(const std::string&); static std::string W2U(const std::wstring&);
// Existing HardwareWriteLease(const char*) guards shared hardware write ownership.
// Evidence source configuration/lifetime belongs exclusively to the parent source owner.
#include "sleep_static_wake_policy.h"
#include "sleep_static_wake_receipt.h"
#include <windows.h>
#include <tlhelp32.h>
#include <winsvc.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <powrprof.h>
#include <shlobj.h>
#include <aclapi.h>
#include <sddl.h>
#include <chrono>
#include <ctime>
#include <mutex>
#include <map>
#include <set>
#include <sstream>
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"setupapi.lib")
#pragma comment(lib,"cfgmgr32.lib")
#pragma comment(lib,"powrprof.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
static bool sgAbnormalSourceReady(); // read-only; implemented by the parent

namespace ymcc { namespace staticwake { namespace native {
static constexpr GUID sleepGroup={0x238c9fa8,0x0aad,0x41ed,{0x83,0xf4,0x97,0xbe,0x24,0x2c,0x8f,0x20}};
static constexpr GUID rtcWake={0xbd3b718a,0x0680,0x4d9d,{0x8a,0xb2,0xe1,0xd2,0xb4,0xac,0x80,0x6d}};
static constexpr GUID systemButton={0x4afa3d53,0x74a7,0x11d0,{0xbe,0x5e,0,0xa0,0xc9,0x06,0x28,0x57}};
inline std::string trim(std::string s) {
    const auto a=s.find_first_not_of(" \t\r\n");
    if(a==std::string::npos)return {};
    return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);
}
inline std::wstring lower(std::wstring s) {
    if(s.empty())return s;
    std::wstring out(s.size(),L'\0');
    if(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,s.data(),static_cast<int>(s.size()),out.data(),static_cast<int>(out.size()),nullptr,nullptr,0)!=static_cast<int>(s.size()))return {};
    return out;
}
inline std::string keyName(const std::string& s) {return W2U(lower(U2W(s)));}
inline bool argValid(const std::string& s) {
    return !s.empty()&&s.size()<32700&&std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32;});
}
// Microsoft C/C++ argv quoting. No cmd.exe, shell operators or interpolated shell.
inline std::wstring quote(const std::wstring& arg) {
    std::wstring out=L"\"";std::size_t slashes=0;
    for(wchar_t c:arg) {
        if(c==L'\\'){++slashes;continue;}
        out.append(slashes*(c==L'"'?2:1),L'\\');slashes=0;
        if(c==L'"')out.push_back(L'\\');out.push_back(c);
    }
    out.append(slashes*2,L'\\');out.push_back(L'"');return out;
}
inline std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;std::istringstream in(text);std::string line;
    while(std::getline(in,line)){line=trim(std::move(line));if(!line.empty())out.push_back(line);}
    return out;
}
inline bool decode(const std::string& bytes,std::string& out) {
    if(bytes.empty()){out.clear();return true;}
    if(bytes.size()>2&&((static_cast<unsigned char>(bytes[0])==0xff&&static_cast<unsigned char>(bytes[1])==0xfe)||bytes[1]==0)) {
        const std::size_t offset=static_cast<unsigned char>(bytes[0])==0xff?2:0;
        if((bytes.size()-offset)%2)return false;
        std::wstring w;
        for(std::size_t i=offset;i+1<bytes.size();i+=2)w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(bytes[i])|(static_cast<unsigned>(static_cast<unsigned char>(bytes[i+1]))<<8)));
        if(!w.empty()&&w.back()==0)w.pop_back();out=W2U(w);return true;
    }
    UINT cp=CP_UTF8;
    int n=MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);
    if(!n){cp=GetOEMCP();n=MultiByteToWideChar(cp,0,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);}
    if(!n)return false;
    std::wstring w(static_cast<std::size_t>(n),L'\0');
    if(!MultiByteToWideChar(cp,cp==CP_UTF8?MB_ERR_INVALID_CHARS:0,bytes.data(),static_cast<int>(bytes.size()),w.data(),n))return false;
    out=W2U(w);return out.find('\0')==std::string::npos;
}
// Never decode undocumented registry bit masks. Token listing must be complete,
// including all three invariant section names, or software mutation fails closed.
inline bool parseOverrides(const std::string& text,std::map<std::string,Json>& out,std::string& error,const std::map<std::string,std::string>* callerNames=nullptr) {
    if(!callerNames){error="override-caller-name-evidence-required";return false;}
    std::string section;std::set<std::string> sections;
    for(const auto& line:lines(text)) {
        if(line.front()=='['&&line.back()==']') {
            section=line.substr(1,line.size()-2);
            if((section!="PROCESS"&&section!="SERVICE"&&section!="DRIVER")||!sections.insert(section).second){error="override-sections-unsupported-or-duplicate";return false;}
            continue;
        }
        if(section.empty()){error="override-preamble-unrecognized";return false;}
        // At most three possible token splits, resolved against independent
        // registry NAME evidence. A caller ending in SYSTEM is never guessed.
        std::string remaining=line,match;std::set<std::string> flags;Json matchedFlags;unsigned matches=0;
        for(unsigned split=0;split<3;++split) {
            const auto last=remaining.find_last_of(" \t");if(last==std::string::npos)break;
            const auto token=remaining.substr(last+1);
            if((token!="SYSTEM"&&token!="DISPLAY"&&token!="AWAYMODE")||!flags.insert(token).second)break;
            remaining=trim(remaining.substr(0,last));
            const auto key=section+"\n"+keyName(remaining);
            if(!callerNames->count(key))continue;
            ++matches;match=key;matchedFlags=Json::array();for(const auto& flag:flags)matchedFlags.push_back(flag);
        }
        if(matches!=1||out.count(match)){error="override-name-evidence-ambiguous-or-mismatched";return false;}
        out[match]=std::move(matchedFlags);
    }
    if(sections!=std::set<std::string>{"PROCESS","SERVICE","DRIVER"}){error="override-snapshot-incomplete-or-localized";return false;}
    if(out.size()!=callerNames->size()){error="override-registry-name-and-CLI-snapshot-differ";return false;}
    return true;
}
struct Node {DEVINST dn=0;std::wstring id,service,hardware;std::set<std::wstring> labels;};
struct Inventory {std::vector<Node> nodes;std::set<DEVINST> protectedNodes;DEVINST root=0;bool protectionReady=false;};
// Only a successful command may supply this snapshot. A real inventory label
// always wins over a supported localized empty-list marker; no other locale is guessed.
inline bool emptyArmedList(const std::vector<std::string>& ls,const Inventory& inv) {
    if(ls.size()!=1)return false;
    const bool supported=ls.front()=="NONE"||ls.front()=="\xE6\x97\xA0";
    if(!supported)return false;
    const auto label=lower(U2W(ls.front()));
    return std::none_of(inv.nodes.begin(),inv.nodes.end(),[&](const Node& node){return node.labels.count(label)!=0;});
}
inline bool parseArmed(const std::string& text,const Inventory& inv,std::set<std::string>& names,std::string& e) {
    const auto ls=lines(text);
    if(ls.empty()){e="wake-armed-empty-output-unproven";return false;}
    if(text.find('\0')!=std::string::npos){e="wake-armed-unrecognized-output";return false;}
    if(emptyArmedList(ls,inv)){names.clear();return true;}
    std::set<std::string> parsed;
    for(const auto& name:ls) {
        const auto key=keyName(name);const auto label=lower(U2W(name));
        const bool known=std::any_of(inv.nodes.begin(),inv.nodes.end(),[&](const Node& node){return node.labels.count(label)!=0;});
        if(!argValid(name)||key.empty()||!known||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),nullptr,0)){e="wake-armed-unrecognized-output";return false;}
        parsed.insert(key); // Unrelated duplicate labels are harmless; target resolution is unique.
    }
    names=std::move(parsed);return true;
}
inline std::wstring property(HDEVINFO list,SP_DEVINFO_DATA& dev,DWORD prop) {
    DWORD type=0,bytes=0;
    SetupDiGetDeviceRegistryPropertyW(list,&dev,prop,&type,nullptr,0,&bytes);
    if(!bytes||bytes>131072)return {};
    std::vector<BYTE> buffer(bytes+sizeof(wchar_t)*2,0);
    if(!SetupDiGetDeviceRegistryPropertyW(list,&dev,prop,&type,buffer.data(),bytes,nullptr)||(type!=REG_SZ&&type!=REG_MULTI_SZ&&type!=REG_EXPAND_SZ))return {};
    const auto* w=reinterpret_cast<const wchar_t*>(buffer.data());
    if(type!=REG_MULTI_SZ)return w;
    std::wstring all;
    for(std::size_t i=0;i<bytes/sizeof(wchar_t)&&w[i];) {const std::wstring v=w+i;all+=v+L";";i+=v.size()+1;}
    return all;
}
inline bool virtualOrFan(const Node& n) {
    const auto h=lower(n.hardware),s=lower(n.service);
    return s==L"vigembus"||s==L"vjoy"||s==L"hidhide"||h.find(L"vigem")!=std::wstring::npos||h.find(L"pnp0c0b")!=std::wstring::npos;
}
inline bool excluded(const Node& n) {
    const auto id=lower(n.id);
    return virtualOrFan(n)||id.rfind(L"root\\",0)==0||id.rfind(L"swd\\",0)==0;
}
inline std::string winError(const char* operation) {return std::string(operation)+":"+std::to_string(GetLastError());}
// Both directory and journal file require a protected, administrator/SYSTEM-only
// owner and DACL. Directory traversal privilege cannot substitute for file ACLs.
static constexpr const wchar_t* journalSecuritySddl=L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)";
inline bool trustedJournalSecurity(PSECURITY_DESCRIPTOR sd) {
    if(!sd||!IsValidSecurityDescriptor(sd))return false;
    PSID owner=nullptr;BOOL defaulted=FALSE,present=FALSE;PACL acl=nullptr;
    BYTE admin[SECURITY_MAX_SID_SIZE]{},system[SECURITY_MAX_SID_SIZE]{};DWORD adminSize=sizeof(admin),systemSize=sizeof(system);
    if(!CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,admin,&adminSize)||!CreateWellKnownSid(WinLocalSystemSid,nullptr,system,&systemSize)||
        !GetSecurityDescriptorOwner(sd,&owner,&defaulted)||!owner||!IsValidSid(owner)||
        (!EqualSid(owner,admin)&&!EqualSid(owner,system))||!GetSecurityDescriptorDacl(sd,&present,&acl,&defaulted)||!present||!acl||!IsValidAcl(acl)||!acl->AceCount)return false;
    SECURITY_DESCRIPTOR_CONTROL control=0;DWORD revision=0;
    if(!GetSecurityDescriptorControl(sd,&control,&revision)||!(control&SE_DACL_PROTECTED))return false;
    for(DWORD index=0;index<acl->AceCount;++index) {
        void* ace=nullptr;if(!GetAce(acl,index,&ace))return false;
        const auto* header=static_cast<const ACE_HEADER*>(ace);
        if(header->AceType!=ACCESS_ALLOWED_ACE_TYPE||header->AceSize<sizeof(ACCESS_ALLOWED_ACE))return false;
        const auto* allowed=static_cast<const ACCESS_ALLOWED_ACE*>(ace);PSID sid=const_cast<DWORD*>(&allowed->SidStart);
        if(!IsValidSid(sid)||(!EqualSid(sid,admin)&&!EqualSid(sid,system)))return false;
    }
    return true;
}
struct DeviceObservation {Inventory inventory;std::set<std::string> armedNames;};
struct Native {
    using Overrides=std::map<std::string,Json>;
    ObservationCache<Overrides> softwareObservation;
    SoftwareSnapshotCircuit softwareCircuit; // reset ONLY at independent operation boundary
    ObservationCache<DeviceObservation> deviceObservation;
    void invalidateObservations() {softwareObservation.invalidate();deviceObservation.invalidate();}
    std::uint64_t deadline=GetTickCount64()+180000;
    std::function<bool()> canceled;
    std::wstring directory,path;
    Native() {
        PWSTR base=nullptr;
        if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData,KF_FLAG_DONT_VERIFY,nullptr,&base))&&base) {
            directory=std::wstring(base)+L"\\YeManCC-StaticWakePolicy";path=directory+L"\\journal-v1.json";
        }
        if(base)CoTaskMemFree(base);
    }
    bool isCanceled() const noexcept {try{return canceled&&canceled();}catch(...){return true;}}
    bool time(std::string& e) const {
        if(isCanceled()){e="operation-canceled";return false;}
        if(GetTickCount64()<deadline)return true;e="operation-deadline-180s";return false;
    }
    bool command(const std::vector<std::wstring>& args,std::string& out,std::string& e) {
        if(!time(e))return false;
        wchar_t sys[32768]{};const UINT n=GetSystemDirectoryW(sys,32768);
        if(!n||n>=32768){e="absolute-System32-unavailable";return false;}
        const std::wstring exe=std::wstring(sys,n)+L"\\powercfg.exe";
        if(exe.size()<3||exe[1]!=L':'||exe[2]!=L'\\'){e="System32-path-not-absolute";return false;}
        std::wstring cmd=quote(exe);
        for(const auto& a:args){if(a.find(L'\0')!=std::wstring::npos){e="command-NUL-rejected";return false;}cmd+=L" "+quote(a);}
        const auto now=GetTickCount64();if(now>=deadline){e="operation-deadline-180s";return false;}
        // Only the read-only global overrides snapshot gets compatibility headroom.
        // Writes and other queries stay short; both share the existing 180s deadline.
        const DWORD timeout=static_cast<DWORD>((std::min)(
            std::uint64_t{args.size()==1&&args.front()==L"/requestsoverride"?5000u:2500u},deadline-now));
        if(!time(e))return false;
        const auto run=runCapture(cmd,timeout,[this]{return isCanceled()||GetTickCount64()>=deadline;});
        if(!time(e))return false;
        // REQUIRED low-frequency failure evidence. Keep in the ordinary journal/error
        // path; never gate it on verbose logging or remove it as a cost optimization.
        const auto failureDetail=[&]() {
            const auto phase=args.empty()?"unspecified":(args.front()==L"/requestsoverride"?(args.size()==1?"overrides-snapshot":"override-write"):
                (args.front()==L"/devicequery"?"wake-armed":(args.front()==L"/devicedisablewake"?"wake-disable":(args.front()==L"/deviceenablewake"?"wake-enable":
                (args.front()==L"/setacvalueindex"?"timer-ac":(args.front()==L"/setdcvalueindex"?"timer-dc":(args.front()==L"/setactive"?"scheme-activate":"other")))))));
            return ";ran="+std::to_string(run.ran?1:0)+";elapsedMs="+std::to_string(GetTickCount64()-now)+";timeoutMs="+std::to_string(timeout)+";phase="+phase+";completion="+run.completion+";win32Error="+std::to_string(run.win32Error);
        };
        if(!run.ran||run.exitCode!=0){e="powercfg-failed:"+std::to_string(run.exitCode)+failureDetail();return false;}
        if(!decode(run.out,out)){e="powercfg-output-encoding-unrecognized"+failureDetail();return false;}return true;
    }
    bool overrideCallerNames(std::map<std::string,std::string>& names,std::string& e) {
        if(!time(e))return false;
        // Observed namespace used ONLY as corroborating caller-name evidence.
        // A changed/missing layout cannot grant permission: complete CLI/name
        // agreement is mandatory. No registry value is decoded or modified.
        HKEY root=nullptr;
        const auto code=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerRequestOverride",0,KEY_READ|KEY_WOW64_64KEY,&root);
        if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)return true;
        if(code!=ERROR_SUCCESS){e="override-name-registry-open:"+std::to_string(code);return false;}
        bool ok=true;
        for(DWORD index=0;;++index) {
            if(!time(e)){ok=false;break;}
            wchar_t category[256]{};DWORD count=256;
            const auto next=RegEnumKeyExW(root,index,category,&count,nullptr,nullptr,nullptr,nullptr);
            if(next==ERROR_NO_MORE_ITEMS)break;
            if(next!=ERROR_SUCCESS){e="override-name-category-enumeration:"+std::to_string(next);ok=false;break;}
            const auto cat=lower(category);
            const std::string type=cat==L"process"?"PROCESS":(cat==L"service"?"SERVICE":(cat==L"driver"?"DRIVER":""));
            if(type.empty()){e="override-name-registry-class-unsupported";ok=false;break;}
            HKEY child=nullptr;
            const auto opened=RegOpenKeyExW(root,category,0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&child);
            if(opened!=ERROR_SUCCESS){e="override-name-class-open:"+std::to_string(opened);ok=false;break;}
            for(DWORD valueIndex=0;;++valueIndex) {
                if(!time(e)){ok=false;break;}
                std::vector<wchar_t> name(32768,0);DWORD length=static_cast<DWORD>(name.size()),kind=0,bytes=0;
                const auto value=RegEnumValueW(child,valueIndex,name.data(),&length,nullptr,&kind,nullptr,&bytes);
                if(value==ERROR_NO_MORE_ITEMS)break;
                if(value!=ERROR_SUCCESS||kind!=REG_DWORD||bytes!=sizeof(DWORD)){e="override-name-value-layout-unsupported";ok=false;break;}
                const auto caller=W2U(std::wstring(name.data(),length));
                if(!argValid(caller)||keyName(caller).empty()||!names.emplace(type+"\n"+keyName(caller),caller).second){e="override-registry-caller-ambiguous";ok=false;break;}
            }
            RegCloseKey(child);if(!ok)break;
        }
        RegCloseKey(root);return ok;
    }
    bool overrides(std::map<std::string,Json>& map,std::string& e) {
        std::map<std::string,std::string> callers;std::string text;
        return overrideCallerNames(callers,e)&&command({L"/requestsoverride"},text,e)&&parseOverrides(text,map,e,&callers);
    }
    bool inventory(Inventory& out,std::string& e) {
        if(!time(e))return false;
        const HDEVINFO list=SetupDiGetClassDevsW(nullptr,nullptr,nullptr,DIGCF_ALLCLASSES|DIGCF_PRESENT);
        if(list==INVALID_HANDLE_VALUE){e=winError("pnp-enumeration");return false;}
        bool ok=true;std::set<DEVINST> seeds;
        for(DWORD index=0;;++index) {
            if(!time(e)){ok=false;break;}
            SP_DEVINFO_DATA d{};d.cbSize=sizeof(d);
            if(!SetupDiEnumDeviceInfo(list,index,&d)){if(GetLastError()!=ERROR_NO_MORE_ITEMS){ok=false;e=winError("pnp-next");}break;}
            wchar_t id[MAX_DEVICE_ID_LEN]{};
            if(CM_Get_Device_IDW(d.DevInst,id,MAX_DEVICE_ID_LEN,0)!=CR_SUCCESS){ok=false;e="pnp-instance-id-unavailable";break;}
            Node node;node.dn=d.DevInst;node.id=id;node.service=property(list,d,SPDRP_SERVICE);node.hardware=property(list,d,SPDRP_HARDWAREID);
            for(const auto prop:{SPDRP_FRIENDLYNAME,SPDRP_DEVICEDESC}) {auto name=property(list,d,prop);if(!name.empty())node.labels.insert(lower(name));}
            const auto hardware=lower(node.hardware);
            if(hardware.find(L"pnp0c0c")!=std::wstring::npos||hardware.find(L"pnp0c0e")!=std::wstring::npos)seeds.insert(node.dn);
            out.nodes.push_back(std::move(node));
        }
        SetupDiDestroyDeviceInfoList(list);
        if(!ok)return false;
        // Protect ALL system button interfaces (power and sleep), without sending
        // IOCTLs or touching input state. Their ancestors are also immutable.
        const HDEVINFO buttons=SetupDiGetClassDevsW(&systemButton,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
        if(buttons==INVALID_HANDLE_VALUE){e=winError("system-button-interfaces");return true;}
        bool buttonsOk=true;
        for(DWORD index=0;;++index) {
            if(!time(e)){buttonsOk=false;break;}
            SP_DEVICE_INTERFACE_DATA iface{};iface.cbSize=sizeof(iface);
            if(!SetupDiEnumDeviceInterfaces(buttons,nullptr,&systemButton,index,&iface)){if(GetLastError()!=ERROR_NO_MORE_ITEMS){buttonsOk=false;e=winError("system-button-next");}break;}
            DWORD bytes=0;SetupDiGetDeviceInterfaceDetailW(buttons,&iface,nullptr,0,&bytes,nullptr);
            if(!bytes||bytes>131072){buttonsOk=false;e="system-button-detail-size";break;}
            std::vector<BYTE> storage(bytes,0);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());detail->cbSize=sizeof(*detail);
            SP_DEVINFO_DATA d{};d.cbSize=sizeof(d);
            if(!SetupDiGetDeviceInterfaceDetailW(buttons,&iface,detail,bytes,nullptr,&d)){buttonsOk=false;e=winError("system-button-detail");break;}
            seeds.insert(d.DevInst);
        }
        SetupDiDestroyDeviceInfoList(buttons);
        if(!buttonsOk||seeds.empty()){if(e.empty())e="human-power-button-path-unproven";return true;}
        DEVINST root=0;
        if(CM_Locate_DevNodeW(&root,nullptr,CM_LOCATE_DEVNODE_NORMAL)!=CR_SUCCESS){e="pnp-root-unavailable";return true;}
        out.root=root;
        for(const auto seed:seeds) {
            DEVINST current=seed;std::set<DEVINST> chain;bool rooted=false;
            for(unsigned depth=0;depth<128;++depth) {
                if(!time(e))return false;
                if(!chain.insert(current).second)break;
                out.protectedNodes.insert(current);
                if(current==root){rooted=true;break;}
                DEVINST parent=0;if(CM_Get_Parent(&parent,current,0)!=CR_SUCCESS)break;current=parent;
            }
            if(!rooted){e="power-button-ancestor-chain-incomplete";return true;}
        }
        out.protectionReady=true;return true;
    }
    const Node* resolve(const Inventory& inv,const std::string& name) const {
        const auto label=lower(U2W(name));const Node* match=nullptr;
        for(const auto& n:inv.nodes)if(n.labels.count(label)){if(match)return nullptr;match=&n;}
        return match;
    }
    bool excludedPath(const Inventory& inv,const Node& node,std::string& e) const {
        if(excluded(node))return true;
        if(!inv.root){e="device-ancestor-root-unproven";return true;}
        DEVINST current=node.dn;std::set<DEVINST> visited;
        for(unsigned depth=0;depth<128;++depth) {
            if(!time(e))return true;
            if(current==inv.root)return false;
            if(!visited.insert(current).second)break;
            DEVINST parent=0;if(CM_Get_Parent(&parent,current,0)!=CR_SUCCESS)break;
            const auto found=std::find_if(inv.nodes.begin(),inv.nodes.end(),[&](const Node& n){return n.dn==parent;});
            if(found==inv.nodes.end())break;
            if(virtualOrFan(*found))return true;
            current=parent;
        }
        e="device-ancestor-chain-incomplete";return true; // Never guess a virtual/fan ancestor is absent.
    }
    bool armed(const Inventory& inv,std::set<std::string>& names,std::string& e) {
        std::string text;return command({L"/devicequery",L"wake_armed"},text,e)&&parseArmed(text,inv,names,e);
    }
    bool active(GUID& guid,std::string& e) const {
        if(!time(e))return false;
        GUID* value=nullptr;const auto code=PowerGetActiveScheme(nullptr,&value);
        if(code!=ERROR_SUCCESS||!value){if(value)LocalFree(value);e="active-power-scheme:"+std::to_string(code);return false;}
        guid=*value;LocalFree(value);return true;
    }
    static std::string guidText(const GUID& guid) {wchar_t value[40]{};return StringFromGUID2(guid,value,40)?W2U(value):std::string{};}
    static bool guidParse(const std::string& text,GUID& guid) {return text.size()==38&&SUCCEEDED(CLSIDFromString(U2W(text).c_str(),&guid));}
    bool collect(Json& items,Json& protect,Json& failures,std::string& e) {
        const auto problem=[&](const char* kind,const std::string& why){failures.push_back({{"kind",kind},{"error",why}});};
        // Running process snapshot plus ALL installed service names (including stopped).
        // This cannot enumerate arbitrary future executable names.
        std::set<std::string> callers;std::set<DWORD> fanPids;
        const auto add=[&](const std::string& type,const std::wstring& wide,bool fanService=false) {
            const auto name=W2U(wide);if(!argValid(name)||keyName(name).empty()){problem("software","invalid-caller-name");return;}
            const auto key=type+"\n"+keyName(name);if(!callers.insert(key).second)return;
            if((type=="PROCESS"&&fanHostProcessName(name))||fanService) {
                protect.push_back({{"kind","software"},{"name",name},{"type",type},{"reason",fanService?"ymcc-fanhost-service-pid-owner":"ymcc-fanhost-owner"}});return;
            }
            items.push_back({{"key",key},{"kind","software"},{"name",name},{"target",{{"type",type},{"name",name}}},{"expected",Json::array({"AWAYMODE","DISPLAY","SYSTEM"})}});
        };
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(snapshot==INVALID_HANDLE_VALUE)problem("software",winError("process-snapshot"));
        else {
            PROCESSENTRY32W process{};process.dwSize=sizeof(process);
            if(!Process32FirstW(snapshot,&process))problem("software",winError("process-first"));
            else do {if(!time(e))break;if(process.th32ProcessID){if(fanHostProcessName(W2U(process.szExeFile)))fanPids.insert(process.th32ProcessID);add("PROCESS",process.szExeFile);}}while(Process32NextW(snapshot,&process));
            if(GetLastError()!=ERROR_NO_MORE_FILES&&time(e))problem("software",winError("process-next"));CloseHandle(snapshot);
        }
        SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_ENUMERATE_SERVICE);
        if(!manager)problem("software",winError("service-manager"));
        else {
            DWORD resume=0;
            do {
                if(!time(e))break;
                std::vector<BYTE> buffer(256*1024);DWORD needed=0,count=0;
                const BOOL done=EnumServicesStatusExW(manager,SC_ENUM_PROCESS_INFO,SERVICE_TYPE_ALL,SERVICE_STATE_ALL,buffer.data(),static_cast<DWORD>(buffer.size()),&needed,&count,&resume,nullptr);
                const DWORD code=done?ERROR_SUCCESS:GetLastError();
                if(!done&&code!=ERROR_MORE_DATA){problem("software","service-enumeration:"+std::to_string(code));break;}
                const auto* values=reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
                for(DWORD i=0;i<count;++i)add("SERVICE",values[i].lpServiceName,fanPids.count(values[i].ServiceStatusProcess.dwProcessId)!=0);
                if(done)break;
                if(!count){problem("software","service-enumeration-no-progress");break;}
            }while(resume);
            CloseServiceHandle(manager);
        }
        // Timers/devices are a separate collected subset; individual errors survive.
        GUID scheme{};
        if(active(scheme,e)) {
            const auto guid=guidText(scheme);
            for(const char* mode:{"AC","DC"})items.push_back({{"key","timer\n"+guid+"\n"+mode},{"kind","timers"},{"name",std::string(mode)+" wake timers"},{"target",{{"scheme",guid},{"mode",mode}}},{"expected",0u}});
        }else problem("timers",e);
        Inventory inv;std::string deviceError;
        if(!inventory(inv,deviceError))problem("devices",deviceError);
        else {
            std::string text;
            if(!command({L"/devicequery",L"wake_armed"},text,deviceError))problem("devices",deviceError);
            else {
                std::set<std::string> armedNames;
                if(!parseArmed(text,inv,armedNames,deviceError))problem("devices",deviceError);
                else if(!armedNames.empty())for(const auto& name:lines(text)) {
                const auto* node=resolve(inv,name);
                const auto reason=!inv.protectionReady?"human-power-button-path-unproven":(!node?"ambiguous-device-name":(inv.protectedNodes.count(node->dn)?"human-power-button-or-ancestor":(excludedPath(inv,*node,deviceError)?"excluded-fan-or-virtual-device":"")));
                if(*reason){protect.push_back({{"name",name},{"reason",reason}});continue;}
                items.push_back({{"key","device\n"+W2U(lower(node->id))},{"kind","devices"},{"name",name},{"target",{{"instanceId",W2U(node->id)},{"name",name}}},{"expected",false}});
                }
            }
            if(!inv.protectionReady)problem("devices",deviceError.empty()?"human-power-button-path-unproven":deviceError);
        }
        // Same one-shot candidates/budget: AC/DC and devices lead, then Steam.
        orderScanCandidates(items);
        return time(e);
    }
    bool sample(const Json& item,Json& value,std::string& e,bool observation) {
        if(!time(e))return false;
        const auto kind=item.value("kind",std::string{});const auto& target=item["target"];
        if(kind=="software") {
            const auto type=target.value("type",std::string{}),name=target.value("name",std::string{});
            if((type!="PROCESS"&&type!="SERVICE")||!argValid(name)||item["key"]!=type+"\n"+keyName(name)){e="invalid-software-target";return false;}
            if(type=="PROCESS"&&fanHostProcessName(name)){e="protected-ymcc-fanhost-owner";return false;}
            SoftwareSnapshotFailure failure;
            if(softwareCircuit.failure(failure)){e=failure.error;return false;}
            const auto load=[this](Overrides& map,std::string& error) {
                return softwareCircuit.query([&](std::string& why){return overrides(map,why);},error);
            };
            const auto now=GetTickCount64();
            const bool ok=observation?softwareObservation.observe(now,load,e):softwareObservation.refresh(now,load,e);
            if(!ok||!time(e))return false;
            const auto& map=softwareObservation.value();
            const auto it=map.find(type+"\n"+keyName(name));value=it==map.end()?Json::array():it->second;return true;
        }
        if(kind=="timers") {
            GUID scheme{};const auto mode=target.value("mode",std::string{});
            if(!guidParse(target.value("scheme",std::string{}),scheme)||(mode!="AC"&&mode!="DC")){e="invalid-timer-target";return false;}
            DWORD index=0;const auto code=mode=="AC"?PowerReadACValueIndex(nullptr,&scheme,&sleepGroup,&rtcWake,&index):PowerReadDCValueIndex(nullptr,&scheme,&sleepGroup,&rtcWake,&index);
            if(code!=ERROR_SUCCESS){e="timer-read:"+std::to_string(code);return false;}value=index;return true;
        }
        if(kind=="devices") {
            const auto load=[this](DeviceObservation& snapshot,std::string& error){return inventory(snapshot.inventory,error)&&armed(snapshot.inventory,snapshot.armedNames,error);};
            const auto now=GetTickCount64();
            const bool ok=observation?deviceObservation.observe(now,load,e):deviceObservation.refresh(now,load,e);
            if(!ok||!time(e))return false;
            const auto& snapshot=deviceObservation.value();
            const auto* node=resolve(snapshot.inventory,target.value("name",std::string{}));
            if(!node||lower(node->id)!=lower(U2W(target.value("instanceId",std::string{})))){e="device-name-or-instance-identity-changed";return false;}
            value=snapshot.armedNames.count(keyName(target["name"].get<std::string>()))!=0;return true;
        }
        e="invalid-item-kind";return false;
    }
    bool read(const Json& item,Json& value,std::string& e) {return sample(item,value,e,false);}
    bool observe(const Json& item,Json& value,std::string& e) {return sample(item,value,e,true);}
    bool write(const Json& item,const Json& value,std::string& e) {
        // Every attempt invalidates observations, including a failed lease or a
        // command that fails after changing a setting. Write/readback stay fresh.
        invalidateObservations();
        if(!time(e)||!valueValid(item,value))return false;
        try {
            if(!time(e))return false;
            HardwareWriteLease lease("sleep-static-wake.one-shot");
            if(!time(e))return false;
            const auto kind=item.value("kind",std::string{});const auto& t=item["target"];std::string out;
            if(kind=="software") {
                const auto type=t.value("type",std::string{}),name=t.value("name",std::string{});
                if((type!="PROCESS"&&type!="SERVICE")||!argValid(name)||item["key"]!=type+"\n"+keyName(name)){e="invalid-software-target";return false;}
            if(type=="PROCESS"&&fanHostProcessName(name)){e="protected-ymcc-fanhost-owner";return false;}
                std::vector<std::wstring> args={L"/requestsoverride",U2W(type),U2W(name)};
                for(const auto& flag:value)args.push_back(U2W(flag.get<std::string>()));
                // No-flags removal is attempted only for our saved empty baseline;
                // Policy must confirm the result, otherwise OFF stays pending.
                return command(args,out,e);
            }
            if(kind=="devices") {
                Inventory inv;if(!inventory(inv,e))return false;
                const auto* node=resolve(inv,t.value("name",std::string{}));
                if(!node||lower(node->id)!=lower(U2W(t.value("instanceId",std::string{})))){e="device-identity-ambiguous";return false;}
                if(excludedPath(inv,*node,e)||(!value.get<bool>()&&(!inv.protectionReady||inv.protectedNodes.count(node->dn)))){e="protected-device-fail-safe";return false;}
                // This changes wake permission ONLY. Never disable a devnode/router.
                return command({value.get<bool>()?L"/deviceenablewake":L"/devicedisablewake",U2W(t["name"].get<std::string>())},out,e);
            }
            if(kind=="timers") {
                GUID scheme{},current{};const auto mode=t.value("mode",std::string{}),text=t.value("scheme",std::string{});
                if(!guidParse(text,scheme)||(mode!="AC"&&mode!="DC")){e="invalid-timer-target";return false;}
                if(value==item["expected"]&&(!active(current,e)||!IsEqualGUID(scheme,current))){e="active-scheme-changed-before-apply";return false;}
                if(!command({mode=="AC"?L"/setacvalueindex":L"/setdcvalueindex",U2W(text),U2W(guidText(sleepGroup)),U2W(guidText(rtcWake)),std::to_wstring(value.get<DWORD>())},out,e))return false;
                // Apply only if it is STILL current. Never switch an external plan.
                if(!active(current,e))return false;
                if(IsEqualGUID(scheme,current))return command({L"/setactive",U2W(text)},out,e);
                return true; // Inactive saved scheme restored without activating it.
            }
        }catch(const std::exception& ex){e=std::string("write-lease-or-command: ")+ex.what();return false;}
        e="invalid-item-kind";return false;
    }
    bool secureDirectory(bool create,std::string& e) {
        if(!time(e))return false;
        if(directory.empty()){e="ProgramData-unavailable";return false;}
        if(create) {
            PSECURITY_DESCRIPTOR sd=nullptr;
            if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(journalSecuritySddl,SDDL_REVISION_1,&sd,nullptr)){e=winError("journal-security");return false;}
            SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};const BOOL made=CreateDirectoryW(directory.c_str(),&sa);const DWORD code=made?ERROR_SUCCESS:GetLastError();LocalFree(sd);
            if(!made&&code!=ERROR_ALREADY_EXISTS){e="journal-directory:"+std::to_string(code);return false;}
        }
        const auto attrs=GetFileAttributesW(directory.c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES){e=winError("journal-directory-attributes");return false;}
        if(!(attrs&FILE_ATTRIBUTE_DIRECTORY)||(attrs&FILE_ATTRIBUTE_REPARSE_POINT)){e="journal-directory-unsafe";return false;}
        PSECURITY_DESCRIPTOR sd=nullptr;PSID owner=nullptr;PACL acl=nullptr;
        const DWORD code=GetNamedSecurityInfoW(const_cast<LPWSTR>(directory.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&acl,nullptr,&sd);
        const bool ok=code==ERROR_SUCCESS&&trustedJournalSecurity(sd);
        if(sd)LocalFree(sd);
        if(!ok)e="journal-directory-owner-or-ACL-untrusted";return ok;
    }
    bool fileSecurity(HANDLE file,std::string& e) const {
        PSECURITY_DESCRIPTOR sd=nullptr;
        const auto code=GetSecurityInfo(file,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,nullptr,nullptr,nullptr,nullptr,&sd);
        const bool ok=code==ERROR_SUCCESS&&trustedJournalSecurity(sd);if(sd)LocalFree(sd);
        if(!ok)e="journal-file-owner-or-ACL-untrusted";return ok;
    }
    bool load(Json& value,std::string& e) {
        if(!time(e))return false;
        if(path.empty()){e="ProgramData-unavailable";return false;}
        const auto attr=GetFileAttributesW(directory.c_str());
        if(attr==INVALID_FILE_ATTRIBUTES&&(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND)){value=nullptr;return true;}
        if(!secureDirectory(false,e))return false;
        HANDLE file=CreateFileW(path.c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND){value=nullptr;return true;}e=winError("journal-open");return false;}
        if(!fileSecurity(file,e)){CloseHandle(file);return false;}
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
        if(!GetFileInformationByHandle(file,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||info.nNumberOfLinks!=1||!GetFileSizeEx(file,&size)||size.QuadPart<=0||size.QuadPart>32*1024*1024){CloseHandle(file);e="journal-file-invalid-or-too-large";return false;}
        std::string bytes(static_cast<std::size_t>(size.QuadPart),'\0');DWORD got=0;
        const BOOL read=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr);CloseHandle(file);
        if(!read||got!=bytes.size()){e="journal-short-read";return false;}
        value=Json::parse(bytes,nullptr,false);if(value.is_discarded()){e="journal-JSON-invalid";return false;}return true;
    }
    bool persist(const Json& value,std::string& e) {
        if(!time(e)||!secureDirectory(true,e))return false;
        // REQUIRED per-item write-ahead evidence. Atomic replace + disk flush is
        // intentionally independent of all high-frequency logging preferences.
        const auto bytes=value.dump();if(bytes.size()>32*1024*1024){e="journal-size-budget";return false;}
        const auto temp=path+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        PSECURITY_DESCRIPTOR sd=nullptr;
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(journalSecuritySddl,SDDL_REVISION_1,&sd,nullptr)){e=winError("journal-file-security");return false;}
        SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};
        HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE|READ_CONTROL,0,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
        const auto openError=file==INVALID_HANDLE_VALUE?GetLastError():ERROR_SUCCESS;LocalFree(sd);
        if(file==INVALID_HANDLE_VALUE){e="journal-temp-create:"+std::to_string(openError);return false;}
        if(!fileSecurity(file,e)){CloseHandle(file);DeleteFileW(temp.c_str());return false;}
        DWORD wrote=0;bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr)&&wrote==bytes.size()&&FlushFileBuffers(file);
        const auto code=ok?ERROR_SUCCESS:GetLastError();CloseHandle(file);
        if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
        if(!ok){e="journal-commit:"+std::to_string(code?code:GetLastError());DeleteFileW(temp.c_str());}return ok;
    }
    Ports ports() {
        Ports p;
        p.load=[this](Json& v,std::string& e){return load(v,e);};p.persist=[this](const Json& v,std::string& e){return persist(v,e);};
        p.collect=[this](Json& a,Json& b,Json& c,std::string& e){return collect(a,b,c,e);};
        p.read=[this](const Json& a,Json& b,std::string& e){return read(a,b,e);};p.write=[this](const Json& a,const Json& b,std::string& e){return write(a,b,e);};
        p.monotonicMs=[](){return static_cast<std::uint64_t>(GetTickCount64());};p.epoch=[](){return static_cast<std::uint64_t>(std::time(nullptr));};
        p.sourceReady=[this](){return !isCanceled()&&sgAbnormalSourceReady();};
        p.canceled=[this](){return isCanceled();};
        p.observe=[this](const Json& a,Json& b,std::string& e){return observe(a,b,e);};
        p.beginOperation=[this](){invalidateObservations();softwareCircuit.reset();};
        p.softwareSnapshotFailure=[this](SoftwareSnapshotFailure& failure){return softwareCircuit.failure(failure);};return p;
    }
};
static std::timed_mutex inProcess;
struct MachineLock {
    HANDLE handle=nullptr;bool acquired=false;
    explicit MachineLock(DWORD waitMs) {
        PSECURITY_DESCRIPTOR sd=nullptr;
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",SDDL_REVISION_1,&sd,nullptr))return;
        SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};handle=CreateMutexW(&sa,FALSE,L"Global\\YeManCC.StaticWakePolicy.v1");LocalFree(sd);
        if(handle){const auto r=WaitForSingleObject(handle,waitMs);acquired=r==WAIT_OBJECT_0||r==WAIT_ABANDONED;}
    }
    ~MachineLock(){if(acquired)ReleaseMutex(handle);if(handle)CloseHandle(handle);}
    MachineLock(const MachineLock&)=delete;MachineLock& operator=(const MachineLock&)=delete;
};
inline Json run(bool restore,std::function<bool()> canceled) {
    const auto started=GetTickCount64();
    Native native;native.canceled=std::move(canceled);
    // REQUIRED low-frequency receipt. Reuses existing counters; no new queries,
    // timers or per-item records. Elapsed includes sleep and is not CPU time.
    const auto completed=[&](Json result) {
        result["operationEvidence"]={{"action",restore?"restore":"scan"},
            {"elapsedMs",GetTickCount64()-started},
            {"elapsedScope","operation-uptime;may-include-suspension;not-CPU-time"},
            {"softwareSnapshot",native.softwareCircuit.evidence()},
            {"diagnosticOnly",true},{"mandatoryEvidence",true},{"detailLogsRequired",false}};
        return result;
    };
    // Worker only: OFF may wait for the canceled scan's bounded child command.
    // This wait shares the operation deadline; the UI never waits on this lock.
    const auto lockDeadline=GetTickCount64()+(restore?5000:1);
    std::unique_lock<std::timed_mutex> local(inProcess,std::defer_lock);
    if(!local.try_lock_for(std::chrono::milliseconds(restore?5000:1))) {
        Policy policy(native.ports());auto status=policy.status();status["lastError"]="static-wake-operation-busy";status["failed"]=status["failed"].get<std::size_t>()+1;return completed({{"ok",false},{"status",status}});
    }
    const auto lockNow=GetTickCount64();
    const DWORD lockRemaining=lockNow<lockDeadline?static_cast<DWORD>(lockDeadline-lockNow):0;
    MachineLock machine(lockRemaining);
    if(!machine.acquired) {
        Policy policy(native.ports());auto status=policy.status();status["lastError"]="machine-journal-owner-lock-unavailable";status["failed"]=status["failed"].get<std::size_t>()+1;return completed({{"ok",false},{"status",status}});
    }
    Policy policy(native.ports());const auto now=GetTickCount64();
    if(now>=native.deadline){auto status=policy.status();status["lastError"]="operation-deadline-180s";status["failed"]=status["failed"].get<std::size_t>()+1;return completed({{"ok",false},{"status",status}});}
    return completed(restore?policy.restore(native.deadline-now):policy.scan(native.deadline-now));
}
}}} // namespace ymcc::staticwake::native

// Parent IPC wrapper MUST retain default low-frequency scan start/end, bounded
// failure summary and restore result logs. These are REQUIRED diagnostic records;
// do not gate them on verbose/frequent logs or delete them for performance.
static const std::wstring SW_STATIC_WAKE_JOURNAL_PATH = ymcc::staticwake::native::Native().path;
// Ordinary export copies this existing file only. No rescan, mkdir or config reads.
static std::wstring swExportJournalPath() {return SW_STATIC_WAKE_JOURNAL_PATH;}
static json swStatusSummary(const json& input) {
    json out;
    for(const auto* field:{"enabled","phase","lastScanEpoch","devices","software","timers","protectedDevices","failed","restorePending","diagnosticSourceReady","lastError"})out[field]=input.at(field);
    auto error=out["lastError"].get<std::string>();
    if(error.size()>512) {
        error.resize(512);
        while(!error.empty()&&(static_cast<unsigned char>(error.back())&0xc0)==0x80)error.pop_back();
        if(!error.empty()&&static_cast<unsigned char>(error.back())>=0xc0)error.pop_back();
    }
    out["lastError"]=std::move(error);return out;
}
static ymcc::staticwake::OperationReceipts g_swOperationReceipts;
static json swGetStatus(const std::string& requestId = {}) {
    ymcc::staticwake::native::Native native;
    auto status=swStatusSummary(ymcc::staticwake::Policy(native.ports()).status());
    if(!requestId.empty())status["operation"]=g_swOperationReceipts.get(requestId);
    return status;
}
// Correlate completion only; the existing machine/process locks still own all
// writes. A timed-out frontend can read this receipt without reissuing a write.
static json swManualOperation(bool restore,const std::string& requestId,const std::function<json()>& work) {
    if(requestId.empty())return work(); // Older callers keep their original contract.
    const std::string action=restore?"restore":"scan";
    if(!g_swOperationReceipts.begin(requestId,action)) {
        const auto receipt=g_swOperationReceipts.get(requestId);
        const bool same=receipt.value("action",std::string{})==action;
        const bool completed=same&&receipt.value("state",std::string{})=="completed";
        return {{"ok",completed&&receipt.value("ok",false)},{"status",swGetStatus(requestId)}};
    }
    try {
        auto result=work();
        g_swOperationReceipts.complete(requestId,action,result.value("ok",false));
        result["status"]["operation"]=g_swOperationReceipts.get(requestId);
        return result;
    }catch(...) {
        g_swOperationReceipts.complete(requestId,action,false);
        throw;
    }
}
static json swPublicOperation(bool restore,std::function<bool()> canceled) {
    try {auto result=ymcc::staticwake::native::run(restore,std::move(canceled));result["status"]=swStatusSummary(result["status"]);return result;}
    catch(const std::exception& e) {
        auto status=ymcc::staticwake::emptyJournal();
        ymcc::staticwake::native::Native native;status=ymcc::staticwake::Policy(native.ports()).status();
        status["lastError"]=std::string("static-wake-exception: ")+e.what();status["failed"]=status["failed"].get<std::size_t>()+1;
        status["phase"]=restore?"restore-pending":"partial";
        return {{"ok",false},{"status",swStatusSummary(status)}};
    }
}
static json swScan(std::function<bool()> canceled = {}) {return swPublicOperation(false,std::move(canceled));}
static json swRestore(std::function<bool()> canceled = {}) {return swPublicOperation(true,std::move(canceled));}
