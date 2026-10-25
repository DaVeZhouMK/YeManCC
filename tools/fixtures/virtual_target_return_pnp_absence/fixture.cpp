#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include "virtual_target_return_pnp_absence.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
namespace proof=ymcc::targetReturn::pnpAbsence;
using J=nlohmann::json;
// Retained in the binary to verify the actual Windows reader and SDK ABI.
// main below calls only verify(ids, fakeReader), never this exported function.
extern "C" __declspec(dllexport) unsigned productionPnpAbsenceCompileOnly(const char* id){
    const std::vector<std::string> known=id?std::vector<std::string>{id}:std::vector<std::string>{};
    return static_cast<unsigned>(proof::verify(known).state);
}
std::wstring wide(const std::string& s){return {s.begin(),s.end()};}
std::vector<wchar_t> multi(std::initializer_list<std::wstring> members){std::vector<wchar_t> out;for(const auto& s:members){out.insert(out.end(),s.begin(),s.end());out.push_back(0);}if(out.empty())out.push_back(0);out.push_back(0);return out;}
struct Node {
    DEVINST devinst=1;
    std::wstring id=L"ACPI\\UNRELATED\\ONE";
    CONFIGRET idResult=CR_SUCCESS,propertyResult=CR_NO_SUCH_VALUE;
    bool idUnterminated=false;
    DEVPROPTYPE type=DEVPROP_TYPE_STRING_LIST;
    std::vector<wchar_t> hardware=multi({});
    std::optional<ULONG> returnedBytes;
};
struct Reader {
    std::vector<Node> nodes;
    bool openFails=false,nullSet=false,destroyFails=false,badCb=false;
    DWORD openError=ERROR_ACCESS_DENIED,endError=ERROR_NO_MORE_ITEMS,lastError=0;
    std::optional<DWORD> failAt;
    std::string throwAt;
    unsigned opens=0,enums=0,ids=0,properties=0,closes=0,errors=0;
    bool abi=true;
    J trace=J::array();
    HDEVINFO set=reinterpret_cast<HDEVINFO>(std::uintptr_t(0x127));
    void event(const char* op){trace.push_back({{"op",op}});if(throwAt==op)throw std::runtime_error("controlled fake Reader exception");}
    HDEVINFO getClassDevs(const GUID* category,PCWSTR enumerator,HWND window,DWORD flags){++opens;event("begin");abi=abi&&!category&&!enumerator&&!window&&flags==(DIGCF_PRESENT|DIGCF_ALLCLASSES);if(openFails){lastError=openError;return nullSet?nullptr:INVALID_HANDLE_VALUE;}return set;}
    BOOL enumerate(HDEVINFO handle,DWORD index,PSP_DEVINFO_DATA info){++enums;event("enumerate");abi=abi&&handle==set&&info&&info->cbSize==sizeof(SP_DEVINFO_DATA)&&index==enums-1;trace.back()["index"]=index;if(!info)return FALSE;if((failAt&&index==*failAt)||index>=nodes.size()){lastError=endError;trace.back()["error"]=lastError;return FALSE;}info->DevInst=nodes[index].devinst;if(badCb)info->cbSize=0;trace.back()["devinst"]=info->DevInst;return TRUE;}
    BOOL destroy(HDEVINFO handle){++closes;event("destroy");abi=abi&&handle==set;if(destroyFails){lastError=ERROR_INVALID_HANDLE;return FALSE;}return TRUE;}
    Node& node(DEVINST devinst){for(auto& n:nodes)if(n.devinst==devinst)return n;throw std::runtime_error("unexpected devinst query");}
    CONFIGRET deviceId(DEVINST devinst,PWCHAR out,ULONG chars,ULONG flags){++ids;event("device-id");abi=abi&&out&&chars==ymcc::targetReturn::maxInstanceBytes+1&&flags==0;auto& n=node(devinst);trace.back()["devinst"]=devinst;if(n.idResult!=CR_SUCCESS)return n.idResult;if(n.idUnterminated){std::fill(out,out+chars,L'X');return CR_SUCCESS;}if(n.id.size()>=chars)return CR_BUFFER_SMALL;std::copy(n.id.begin(),n.id.end(),out);out[n.id.size()]=0;return CR_SUCCESS;}
    CONFIGRET hardwareIds(DEVINST devinst,const DEVPROPKEY* key,DEVPROPTYPE* type,PBYTE buffer,PULONG bytes,ULONG flags){++properties;event("hardware-ids");abi=abi&&key&&type&&buffer&&bytes&&flags==0;
        if(!key||!type||!buffer||!bytes)return CR_INVALID_POINTER;
        abi=abi&&key->pid==DEVPKEY_Device_HardwareIds.pid&&std::memcmp(&key->fmtid,&DEVPKEY_Device_HardwareIds.fmtid,sizeof(GUID))==0&&*type==DEVPROP_TYPE_EMPTY&&*bytes==proof::maxHardwareIdBytes;
        auto& n=node(devinst);trace.back()["devinst"]=devinst;trace.back()["cr"]=n.propertyResult;if(n.propertyResult!=CR_SUCCESS)return n.propertyResult;
        *type=n.type;const auto want=n.hardware.size()*sizeof(wchar_t);const auto copied=(std::min)(want,std::size_t(*bytes));if(copied)std::memcpy(buffer,n.hardware.data(),copied);*bytes=n.returnedBytes.value_or(ULONG(want));trace.back()["type"]=*type;trace.back()["bytes"]=*bytes;return CR_SUCCESS;
    }
    DWORD error(){++errors;event("last-error");return lastError;}
};
J failures=J::array(),cases=J::array(),observation=J::object();unsigned checks=0;std::string current;
void check(bool ok,const char* message){++checks;if(!ok)failures.push_back({{"case",current},{"message",message}});}
void expect(const proof::Result& r,proof::State state,proof::Reason reason,unsigned count,DWORD win32=0,CONFIGRET cr=CR_SUCCESS){observation["result"]={{"state",proof::stateName(r.state)},{"reason",proof::reasonName(r.reason)},{"scanned",r.scannedNodes},{"win32Error",r.win32Error},{"configurationReturn",r.configurationReturn}};check(r.state==state,"tri-state matches required proof");check(r.reason==reason,"failure or completion reason is exact");check(r.scannedNodes==count,"bounded scanned-node count is exact");check(r.win32Error==win32,"Win32 diagnostic is preserved");check(r.configurationReturn==cr,"CM diagnostic is preserved");check(r.absent()==(state==proof::State::Absent),"absence accessor never promotes unknown/present");}
void shape(Reader& r){observation["reader"]={{"opens",r.opens},{"enumerations",r.enums},{"idReads",r.ids},{"propertyReads",r.properties},{"closes",r.closes},{"abi",r.abi},{"trace",r.trace}};check(r.abi,"actual verify passes only expected read ABI and broad-present flags");check(r.opens<=1&&r.closes<=1,"single set lifetime without retries or double close");check(r.enums<=proof::maxNodes+1&&r.ids<=proof::maxNodes&&r.properties<=proof::maxNodes,"enumeration and node reads stay within fixed caps");}
void run(const std::string& name,const std::function<void()>& body){current=name;observation=J::object();const auto start=checks;const auto before=failures.size();try{body();}catch(const std::exception& e){failures.push_back({{"case",name},{"exception",e.what()}});}cases.push_back({{"case",name},{"checks",checks-start},{"pass",before==failures.size()},{"observation",observation}});}
Reader with(Node n){Reader r;r.nodes.push_back(std::move(n));return r;}
Node hw(std::wstring value,std::wstring id=L"ROOT\\DIFFERENT_EPOCH\\NEW",DEVINST devinst=1){Node n;n.id=std::move(id);n.devinst=devinst;n.propertyResult=CR_SUCCESS;n.hardware=multi({std::move(value)});return n;}
const std::string oldId="ROOT\\HIDMAESTRO\\OLD_EPOCH";
void absent(Reader& r,unsigned count){expect(proof::verify({oldId},r),proof::State::Absent,proof::Reason::CompleteAbsence,count);shape(r);check(r.enums==count+1,"absence requires explicit normal end after every inspected node");check(r.closes==1,"absence closes its only set");}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    run("absence/empty-current-set",[]{Reader r;absent(r,0);});
    run("absence/no-history-still-scans",[]{auto r=with(hw(L"ACPI\\REAL_DEVICE"));expect(proof::verify({},r),proof::State::Absent,proof::Reason::CompleteAbsence,1);shape(r);check(r.opens==1&&r.ids==1&&r.properties==1&&r.enums==2,"empty history never bypasses family enumeration");});
    run("absence/known-old-root-and-hid-gone",[]{Reader r;r.nodes={hw(L"PCI\\VEN_8086&DEV_1234",L"PCI\\VEN_8086&DEV_1234\\ONE",1),hw(L"HID\\VID_045E&PID_028E",L"HID\\VID_045E&PID_028E\\TWO",2)};expect(proof::verify({oldId,"HID\\VID_054C&PID_0DF2\\OLD"},r),proof::State::Absent,proof::Reason::CompleteAbsence,2);shape(r);check(r.enums==3&&r.properties==2,"all unrelated nodes and terminal end are inspected");});
    run("absence/no-hardware-property",[]{auto r=with(Node{});absent(r,1);check(r.properties==1,"CR_NO_SUCH_VALUE is an explicit complete no-property node");});
    run("absence/empty-valid-multisz",[]{Node n;n.propertyResult=CR_SUCCESS;auto r=with(n);absent(r,1);});
    const std::vector<std::wstring> unrelated={L"ROOT\\HIDMAESTRO_OTHER",L"ROOT\\HIDMAESTROXUSB_OTHER",L"HID\\VID_28DE&PID_1205",L"USB\\VID_28DE&PID_12050",L"USB\\VID_28DE&PID_1206",L"USB\\VID_28DE&PID_1205&MI_00",L"USB\\VID_28DE&PID_1205&REV_0200&mi_03",L"USB\\VID_28DE&PID_1205&X&MI_00&Y",L" ROOT\\HIDMAESTRO ",L"USB\\VID_28DE&PID_1205_EXTRA",L"ACPI\\\u4e2d\u6587",std::wstring(L"ACPI\\")+wchar_t(0xd83d)+wchar_t(0xde00)};
    for(std::size_t i=0;i<unrelated.size();++i)run("absence/non-target-namespace-"+std::to_string(i),[&,i]{auto r=with(hw(unrelated[i]));absent(r,1);});
    const std::vector<std::wstring> supported={L"ROOT\\HIDMAESTRO",L"root\\hidmaestro",L"ROOT\\HIDMAESTROXUSB",L"root\\hidmaestroxusb",L"USB\\VID_28DE&PID_1205",L"usb\\vid_28de&pid_1205&rev_0200",L" USB\\VID_28DE&PID_1205 ",L"\tUSB\\VID_28DE&PID_1205\r\n",L"\u3000USB\\VID_28DE&PID_1205\u00a0",L"USB\\VID_28DE&PID_1205&OTHER"};
    for(std::size_t i=0;i<supported.size();++i)run("family/new-epoch-"+std::to_string(i),[&,i]{auto r=with(hw(supported[i]));expect(proof::verify({oldId},r),proof::State::Present,proof::Reason::SupportedFamilyPresent,1);shape(r);check(r.properties==1&&r.enums==1,"new family blocks without requiring old exact-id reuse");});
    run("family/after-unrelated-node",[]{Reader r;r.nodes={Node{},hw(L"ROOT\\HIDMAESTROXUSB",L"ROOT\\HIDMAESTROXUSB\\NEW",2)};expect(proof::verify({oldId},r),proof::State::Present,proof::Reason::SupportedFamilyPresent,2);shape(r);});
    run("family/multiple-hardware-values",[]{Node n=hw(L"IGNORED\\DEVICE");n.hardware=multi({L"PCI\\UNRELATED",L"ROOT\\HIDMAESTRO",L"HID\\UNRELATED"});auto r=with(n);expect(proof::verify({oldId},r),proof::State::Present,proof::Reason::SupportedFamilyPresent,1);shape(r);});
    run("exact/historical-root-case-insensitive",[]{Node n;n.id=wide(oldId);for(auto& c:n.id)if(c>=L'A'&&c<=L'Z')c=wchar_t(c-L'A'+L'a');n.propertyResult=CR_FAILURE;auto r=with(n);expect(proof::verify({oldId},r),proof::State::Present,proof::Reason::ExactIdPresent,1);shape(r);check(r.properties==0,"exact current identity suffices without unnecessary property read");});
    run("exact/historical-hid-nonfamily",[]{const std::string id="HID\\VID_054C&PID_0DF2\\OLD";Node n;n.id=wide(id);auto r=with(n);expect(proof::verify({oldId,id},r),proof::State::Present,proof::Reason::ExactIdPresent,1);shape(r);});
    const std::vector<std::string> invalid={"","ROOT\\ONLY","ROOT\\DEVICE\\","ROOT\\..\\X","ROOT\\A B\\X","ROOT\\A\\X\\MORE",std::string(ymcc::targetReturn::maxInstanceBytes+1,'A'),std::string("ROOT\\A\\X\0TRAIL",14),"ROOT\\A\\\xc3\xa9"};
    for(std::size_t i=0;i<invalid.size();++i)run("input/invalid-"+std::to_string(i),[&,i]{Reader r;expect(proof::verify({invalid[i]},r),proof::State::Unknown,proof::Reason::InvalidKnownIds,0);shape(r);check(r.opens==0,"invalid historical IDs never open a set");});
    run("input/duplicate-folded-history",[]{Reader r;expect(proof::verify({"ROOT\\A\\ONE","root\\a\\one"},r),proof::State::Unknown,proof::Reason::InvalidKnownIds,0);shape(r);});
    run("input/over-cap",[]{Reader r;std::vector<std::string> ids(proof::maxKnownExactIds+1,oldId);expect(proof::verify(ids,r),proof::State::Unknown,proof::Reason::InvalidKnownIds,0);shape(r);check(r.opens==0,"bounded input rejects before allocation/enumeration");});
    run("enumeration/open-fails",[]{Reader r;r.openFails=true;expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::OpenFailed,0,ERROR_ACCESS_DENIED);shape(r);check(r.closes==0,"invalid set is never destroyed");});
    run("enumeration/null-set",[]{Reader r;r.openFails=true;r.nullSet=true;expect(proof::verify({},r),proof::State::Unknown,proof::Reason::OpenFailed,0,ERROR_ACCESS_DENIED);shape(r);});
    run("enumeration/error-before-first",[]{Reader r;r.endError=ERROR_ACCESS_DENIED;expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::EnumerationFailed,0,ERROR_ACCESS_DENIED);shape(r);});
    run("enumeration/error-after-node",[]{auto r=with(Node{});r.endError=ERROR_INVALID_DATA;expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::EnumerationFailed,1,ERROR_INVALID_DATA);shape(r);});
    run("enumeration/zero-error-is-not-normal-end",[]{Reader r;r.endError=ERROR_SUCCESS;expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::EnumerationFailed,0,ERROR_SUCCESS);shape(r);});
    run("enumeration/bad-info-size",[]{auto r=with(Node{});r.badCb=true;expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::EnumerationDataInvalid,1);shape(r);check(r.ids==0,"malformed descriptor never reaches CM");});
    run("enumeration/missing-devinst",[]{Node n;n.devinst=0;auto r=with(n);expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::MissingDevinst,1);shape(r);});
    run("enumeration/repeated-devinst",[]{Reader r;r.nodes={Node{},Node{}};expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::RepeatedDevinst,2);shape(r);check(r.ids==1,"repeated devinst is not read twice");});
    run("enumeration/repeated-instance-folded",[]{Reader r;Node second;second.devinst=2;second.id=L"acpi\\unrelated\\one";r.nodes={Node{},second};expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::RepeatedInstance,2);shape(r);check(r.properties==1,"duplicate instance stops before second hardware read");});
    run("id/read-error",[]{Node n;n.idResult=CR_FAILURE;auto r=with(n);expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::DeviceIdReadFailed,1,0,CR_FAILURE);shape(r);});
    run("id/buffer-small",[]{Node n;n.idResult=CR_BUFFER_SMALL;auto r=with(n);expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::DeviceIdReadFailed,1,0,CR_BUFFER_SMALL);shape(r);});
    run("id/empty",[]{Node n;n.id.clear();auto r=with(n);expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::DeviceIdMissing,1);shape(r);});
    run("id/unterminated",[]{Node n;n.idUnterminated=true;auto r=with(n);expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::DeviceIdTruncated,1);shape(r);});
    run("id/opaque-whitespace",[]{Node n;n.id=L"ROOT\\BAD ID\\ONE";auto r=with(n);absent(r,1);});
    run("id/opaque-nonascii",[]{Node n;n.id=L"ROOT\\VALID\\\u4e2d";auto r=with(n);absent(r,1);});
    run("id/opaque-colon-before-live-target",[]{Reader r;Node unrelated;unrelated.id=L"SWD\\DAFUPNPPROVIDER\\UUID:OPAQUE";Node target=hw(L"ROOT\\HIDMAESTRO");target.devinst=2;target.id=L"ROOT\\HIDMAESTRO\\NEW";r.nodes={unrelated,target};expect(proof::verify({oldId},r),proof::State::Present,proof::Reason::SupportedFamilyPresent,2);shape(r);check(r.properties==2,"opaque unrelated node never skips later live target");});
    run("id/opaque-colon-complete-absence",[]{Node n;n.id=L"SWD\\DAFUPNPPROVIDER\\UUID:OPAQUE";auto r=with(n);absent(r,1);});
    run("id/opaque-before-read-error",[]{Reader r;Node unrelated;unrelated.id=L"SWD\\WPD\\??#USB#OPAQUE";Node bad;bad.devinst=2;bad.id=L"ROOT\\OTHER\\TWO";bad.propertyResult=CR_FAILURE;r.nodes={unrelated,bad};expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::HardwareIdsReadFailed,2,0,CR_FAILURE);shape(r);});
    run("id/opaque-unicode-folded-duplicate",[]{Reader r;Node a;a.id=L"ROOT\\\u00c9\\ONE";Node b;b.devinst=2;b.id=L"root\\\u00e9\\one";r.nodes={a,b};expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::RepeatedInstance,2);shape(r);});
    run("id/invalid-utf16-diagnostic",[]{Node n;n.id=L"ROOT\\VALID\\";n.id.push_back(wchar_t(0xd800));auto r=with(n);const auto result=proof::verify({oldId},r);expect(result,proof::State::Unknown,proof::Reason::DeviceIdInvalid,1);check(result.deviceIdChars==n.id.size()&&!result.deviceIdTail.empty()&&result.deviceIdTail.back()==0xd800,"bounded invalid-ID code units are retained");shape(r);});
    run("id/control-still-unknown",[]{Node n;n.id=L"ROOT\\VALID\\";n.id.push_back(wchar_t(1));auto r=with(n);expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::DeviceIdInvalid,1);shape(r);});
    run("property/read-error",[]{Node n;n.propertyResult=CR_FAILURE;auto r=with(n);expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::HardwareIdsReadFailed,1,0,CR_FAILURE);shape(r);});
    run("property/node-disappears",[]{Node n;n.propertyResult=CR_NO_SUCH_DEVNODE;auto r=with(n);expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::HardwareIdsReadFailed,1,0,CR_NO_SUCH_DEVNODE);shape(r);});
    run("property/buffer-small",[]{Node n;n.propertyResult=CR_BUFFER_SMALL;auto r=with(n);expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::HardwareIdsTruncated,1,0,CR_BUFFER_SMALL);shape(r);});
    for(const auto type:{DEVPROP_TYPE_STRING,DEVPROP_TYPE_EMPTY,DEVPROP_TYPE_BINARY})run("property/type-"+std::to_string(type),[=]{auto r=with(hw(L"ROOT\\HIDMAESTRO"));r.nodes[0].type=type;expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::HardwareIdsType,1);shape(r);});
    run("property/windows-single-NUL-empty-list",[]{auto r=with(hw(L"IGNORED"));r.nodes[0].hardware={0};expect(proof::verify({oldId},r),proof::State::Absent,proof::Reason::CompleteAbsence,1);shape(r);});
    run("property/empty-list-before-live-target",[]{Reader r;Node empty=hw(L"IGNORED");empty.hardware={0};Node target=hw(L"ROOT\\HIDMAESTRO");target.devinst=2;target.id=L"ROOT\\TARGET\\TWO";r.nodes={empty,target};expect(proof::verify({oldId},r),proof::State::Present,proof::Reason::SupportedFamilyPresent,2);shape(r);});
    run("property/empty-list-before-property-error",[]{Reader r;Node empty=hw(L"IGNORED");empty.hardware={0};Node error;error.devinst=2;error.id=L"ROOT\\OTHER\\TWO";error.propertyResult=CR_FAILURE;r.nodes={empty,error};expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::HardwareIdsReadFailed,2,0,CR_FAILURE);shape(r);});
    const std::vector<std::vector<wchar_t>> malformed={{},{L'A',0},{L'A',L'B'},{L'A',0,0,L'B',0,0},{0,0,0},{0,0,L'B',0,0},{wchar_t(0xd800),0,0},{wchar_t(0xdc00),0,0},{L'A',wchar_t(1),0,0},{L' ',0,0}};
    for(std::size_t i=0;i<malformed.size();++i)run("property/malformed-"+std::to_string(i),[&,i]{auto r=with(hw(L"IGNORED"));r.nodes[0].hardware=malformed[i];expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::HardwareIdsShape,1);shape(r);});
    for(const ULONG bytes:{ULONG(0),ULONG(1),ULONG(3),ULONG(proof::maxHardwareIdBytes+2)})run("property/returned-bytes-"+std::to_string(bytes),[=]{auto r=with(hw(L"IGNORED"));r.nodes[0].returnedBytes=bytes;expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::HardwareIdsShape,1);shape(r);});
    run("property/matched-first-entry-corrupt-tail",[]{auto r=with(hw(L"ROOT\\HIDMAESTRO"));r.nodes[0].hardware.pop_back();r.nodes[0].hardware.push_back(L'X');expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::HardwareIdsShape,1);shape(r);});
    run("property/matched-first-entry-invalid-later-member",[]{auto r=with(hw(L"ROOT\\HIDMAESTRO"));r.nodes[0].hardware=multi({L"ROOT\\HIDMAESTRO",std::wstring(1,wchar_t(0xd800))});expect(proof::verify({oldId},r),proof::State::Unknown,proof::Reason::HardwareIdsShape,1);shape(r);});
    for(const unsigned extra:{0u,1u})run("limits/nodes-"+std::to_string(proof::maxNodes+extra),[=]{Reader r;for(unsigned i=0;i<proof::maxNodes+extra;++i){Node n;n.devinst=DEVINST(i+1);n.id=L"ACPI\\UNRELATED\\"+std::to_wstring(i);r.nodes.push_back(n);}const auto result=proof::verify({oldId},r);expect(result,extra?proof::State::Unknown:proof::State::Absent,extra?proof::Reason::NodeLimit:proof::Reason::CompleteAbsence,proof::maxNodes);shape(r);check(r.ids==proof::maxNodes&&r.properties==proof::maxNodes&&r.enums==proof::maxNodes+1,"cap permits terminal-end query but no 1025th node read");});
    run("cleanup/failure-cannot-prove-absence",[]{Reader r;r.destroyFails=true;expect(proof::verify({},r),proof::State::Unknown,proof::Reason::CleanupFailed,0,ERROR_INVALID_HANDLE);shape(r);check(r.closes==1,"failed close is not retried");});
    for(const std::string op:{"begin","enumerate","device-id","hardware-ids"})run("exceptions/"+op,[=]{auto r=with(Node{});r.throwAt=op;const auto result=proof::verify({oldId},r);check(result.state==proof::State::Unknown&&result.reason==proof::Reason::ReaderException,"reader exception cannot escape or prove absence");shape(r);check(r.closes==(op=="begin"?0u:1u),"exception closes only the opened set once");});
    run("exceptions/destroy",[]{Reader r;r.throwAt="destroy";const auto result=proof::verify({},r);check(result.state==proof::State::Unknown&&result.reason==proof::Reason::CleanupFailed,"destroy exception cannot become absence");shape(r);check(r.closes==1,"throwing close is not repeated");});
    run("diagnostics/names",[]{check(std::string(proof::stateName(proof::State::Absent))=="absent","absent diagnostic");check(std::string(proof::stateName(proof::State::Present))=="present","present diagnostic");check(std::string(proof::stateName(proof::State::Unknown))=="unknown","unknown diagnostic");check(std::string(proof::reasonName(proof::Reason::CompleteAbsence))=="complete-present-snapshot-without-target","complete-end diagnostic");});
    const J result={{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",checks},{"cases",cases},{"failures",failures},{"realDeviceApiExecuted",false},{"productionReaderCompiledAndExportedNotCalled",true},{"readerBasis","actual bounded verify template with fake SetupAPI/CM read responses"},{"deviceWritesThreadsTimersRetries",false},{"physicalOrOwnershipAuthority",false},{"hardwareAccepted",false}};
    std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);out<<result.dump(2)<<"\n";
    std::cout<<J({{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",checks},{"failures",failures}}).dump()<<"\n";return failures.empty()?0:1;
}
