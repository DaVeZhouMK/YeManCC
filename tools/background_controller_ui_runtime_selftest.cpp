#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <type_traits>
#include "../deps/json/json.hpp"
#include "../native/xinput_identity_refresh_policy.h"
#include "../native/controller_isolation_hid_reader.h"
#include "../native/controller_hid_source_lane.h"
#if defined(YMCC_ISOLATION_HID_OBSERVE_INCLUDE)
#include "../native/sleep_power_dispatch.h"
#include "../native/sleep_power_state.h"
#include "../native/virtual_recovery_intent.h"
#endif
using json=nlohmann::json;
static std::atomic<bool> g_exitRequested{false};
enum class PowerLifecycle{Ready,Suspended};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static bool g_padConnected=true,g_xinputPhysicalSourceLocked=false;
static DWORD g_xinputPrimarySlot=0,g_xinputConnectedMask=1,g_xinputVirtualEndpointMask=0,g_xinputSlotUnknownMask=0,g_xinputPhysicalSourceMask=1;
static std::atomic<DWORD> g_xinputSlotReprobeMask{0};
static std::atomic<std::uint64_t> g_xinputTopologyEpoch{2},g_xinputSlotIdentityEpoch{3};
static std::uint64_t power=1;
static std::uint64_t currentPowerGeneration(){return power;}
static std::array<unsigned,4> g_xinputSlotIdentityResolvedEpoch{3,0,0,0};
static std::array<std::shared_ptr<const std::string>,4> g_axisCalibrationXinputKeys;
static std::array<ymcc::input::identityRefresh::Slot,4> g_xinputFeedbackIdentity;
struct Snapshot {std::string inputSource="xinput";bool readContextStable=true;
    std::uint64_t readPowerGeneration=1,readTopologyGeneration=2,readIdentityGeneration=3,commonHidReportSequence=0;XINPUT_GAMEPAD pad{};};
static Snapshot g_inputCaptureGamepadSnapshot;
static XINPUT_GAMEPAD g_curPad{};static WORD g_curW=0;
static std::atomic<bool> g_inputReleaseRequired{false};
static bool g_gamepadAxisNeutralAdmissionRequired=false;
static std::mutex g_gamepadReadMx;
static bool enabled=true,fse=true;
static bool gameOverlayEnabled(){return enabled;}
struct Carrier {bool shown=false;bool active()const{return shown;}};
static Carrier g_gameOverlay;
namespace ymcc::bootmode {struct Platform {bool activeKnown=true,active=true;};static Platform readPlatform(){return {true,fse};}}
static std::wstring U2W(const std::string& s){return {s.begin(),s.end()};}
static std::string W2U(const std::wstring& s){std::string out;for(wchar_t c:s)out.push_back(static_cast<char>(c));return out;}
static std::vector<json> logs;
static void appendNativeLifecycleLog(const char* event,const json& j){logs.push_back({{"event",event},{"data",j}});}
static XINPUT_GAMEPAD inputCanonicalizeGamepad(XINPUT_GAMEPAD p){return p;}
static unsigned evaluations=0;
static void gamepadEval(){++evaluations;}
#include <initguid.h>
#include <devpkey.h>
#include "../native/background_controller_ui_runtime.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"hid.lib")
#pragma comment(lib,"cfgmgr32.lib")
static unsigned checks=0;
static void check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
#if defined(YMCC_ISOLATION_HID_PORT_INCLUDE)
namespace isolationPortTest {
static bool bindDetailEnabled=true;
static bool domainLogEnabled(const char*){return bindDetailEnabled;}
enum class Fault {None,Open,Event,Attributes,Preparsed,Caps,Buttons,ButtonCount,Values,ValueCount,Usage,ReportBytes,ExtraValue,AxisRange,DataCapacity,Reset,Begin,Parse,MissingAxis,MetadataAllocation,BufferAllocation};
#if defined(YMCC_ISOLATION_HID_PORT_R3)
static constexpr const char* fixtureRevision="R3-exact-live-interface-reenumeration";
static const GUID kYeManGuidDeviceInterfaceHid{0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0,0x11,0x11,0,0,0x30}};
struct FakeNode {
    DEVINST node=0,parent=0,xusb=0;
    std::wstring instance,path;
    bool present=true;
    Fault metadataFault=Fault::None;
};
struct FakeTopology {
    bool enabled=false;
    std::vector<FakeNode> nodes;
    std::wstring slotPath;
    CONFIGRET locateResult=CR_SUCCESS,childResult=CR_SUCCESS,siblingResult=CR_SUCCESS,listResult=CR_SUCCESS;
    unsigned childCalls=0,siblingCalls=0,listCalls=0;
};
#endif
struct FakeIo {
    Fault fault=Fault::None;
#if defined(YMCC_ISOLATION_HID_PORT_R3)
    FakeTopology topology;
    unsigned readOpens=0,probeOpens=0;
    DWORD openedAccess=0;
    std::vector<std::wstring> readPaths,probePaths;
#endif

    bool device=false,event=false,prep=false,terminal=false,cancelled=false,button=false,completionSuccess=true,synchronous=false;
    bool sourceMatches=true,interfaceKnown=true,rootMatches=true;
    DWORD error=ERROR_SUCCESS,completionBytes=15;
    unsigned opens=0,events=0,preps=0,frees=0,closes=0,reads=0,cancels=0,parses=0,completions=0;
    unsigned closeFailures=0,freeFailures=0;
    ULONGLONG tick=100;
    OVERLAPPED* pending=nullptr;
    BYTE* buffer=nullptr;
    HANDLE deviceHandle=reinterpret_cast<HANDLE>(std::uintptr_t{0x1101});
    HANDLE eventHandle=reinterpret_cast<HANDLE>(std::uintptr_t{0x2201});
    std::vector<std::string> calls;
    std::wstring path,instance=L"HID\\VID_045E&PID_028E\\EXACT";
    std::string verifiedRoot,verifiedInstance;
};
static FakeIo io;
static void reset(Fault fault=Fault::None){check(!io.device&&!io.event&&!io.prep&&!io.pending,"previous fake Port resources returned before next scenario");io=FakeIo{};io.fault=fault;}
#if defined(YMCC_ISOLATION_HID_PORT_R3)
static FakeNode* nodeByInstance(const std::wstring& instance){for(auto& node:io.topology.nodes)if(node.instance==instance)return &node;return nullptr;}
static FakeNode* nodeByPath(const std::wstring& path){for(auto& node:io.topology.nodes)if(node.path==path&&!path.empty())return &node;return nullptr;}
static FakeNode* nodeById(DEVINST value){for(auto& node:io.topology.nodes)if(node.node==value)return &node;return nullptr;}
static bool livePath(const std::wstring& path){const auto* node=nodeByPath(path);return node&&node->present;}
static CONFIGRET CM_Locate_DevNodeW(PDEVINST out,DEVINSTID_W instance,ULONG flags){
    check(out&&instance&&flags==CM_LOCATE_DEVNODE_NORMAL,"fake locate requires explicit instance and normal flags");
    if(io.topology.locateResult!=CR_SUCCESS)return io.topology.locateResult;
    const auto* node=nodeByInstance(instance);if(!node||!node->present)return CR_NO_SUCH_DEVNODE;*out=node->node;return CR_SUCCESS;
}
static CONFIGRET CM_Get_Child(PDEVINST out,DEVINST parent,ULONG flags){
    ++io.topology.childCalls;check(out&&!flags&&io.topology.childCalls<=256,"fake child traversal is explicit and bounded");
    if(io.topology.childResult!=CR_SUCCESS)return io.topology.childResult;
    for(const auto& node:io.topology.nodes)if(node.parent==parent&&node.present){*out=node.node;return CR_SUCCESS;}return CR_NO_SUCH_DEVNODE;
}
static CONFIGRET CM_Get_Sibling(PDEVINST out,DEVINST current,ULONG flags){
    ++io.topology.siblingCalls;check(out&&!flags&&io.topology.siblingCalls<=256,"fake sibling traversal is explicit and bounded");
    if(io.topology.siblingResult!=CR_SUCCESS)return io.topology.siblingResult;
    const auto* source=nodeById(current);if(!source)return CR_NO_SUCH_DEVNODE;bool found=false;
    for(const auto& node:io.topology.nodes){if(node.node==current){found=true;continue;}if(found&&node.parent==source->parent&&node.present){*out=node.node;return CR_SUCCESS;}}return CR_NO_SUCH_DEVNODE;
}
static CONFIGRET CM_Get_Device_IDW(DEVINST current,PWSTR output,ULONG count,ULONG flags){
    check(output&&count&&!flags,"fake device ID lookup uses an explicit bounded buffer");const auto* node=nodeById(current);
    if(!node||!node->present)return CR_NO_SUCH_DEVNODE;if(count<=node->instance.size())return CR_BUFFER_SMALL;
    std::copy(node->instance.begin(),node->instance.end(),output);output[node->instance.size()]=0;return CR_SUCCESS;
}
static std::vector<wchar_t> presentInterfaces(DEVINSTID_W instance){
    check(instance!=nullptr,"interface enumeration is scoped to one exact instance, never all devices");
    const auto* node=nodeByInstance(instance);std::vector<wchar_t> result;
    if(node&&node->present&&node->path.find(L"\\\\?\\hid#")==0){result.assign(node->path.begin(),node->path.end());result.push_back(0);}result.push_back(0);return result;
}
static CONFIGRET CM_Get_Device_Interface_List_SizeW(PULONG count,LPGUID guid,DEVINSTID_W instance,ULONG flags){
    check(count&&guid&&flags==CM_GET_DEVICE_INTERFACE_LIST_PRESENT,"interface list sizing requests PRESENT for an exact instance");
    if(io.topology.listResult!=CR_SUCCESS)return io.topology.listResult;*count=static_cast<ULONG>(presentInterfaces(instance).size());return CR_SUCCESS;
}
static CONFIGRET CM_Get_Device_Interface_ListW(LPGUID guid,DEVINSTID_W instance,PZZWSTR output,ULONG count,ULONG flags){
    ++io.topology.listCalls;check(guid&&output&&flags==CM_GET_DEVICE_INTERFACE_LIST_PRESENT&&io.topology.listCalls<=256,"interface enumeration is bounded and PRESENT only");
    if(io.topology.listResult!=CR_SUCCESS)return io.topology.listResult;const auto value=presentInterfaces(instance);if(count<value.size())return CR_BUFFER_SMALL;
    std::copy(value.begin(),value.end(),output);return CR_SUCCESS;
}
static void HidD_GetHidGuid(LPGUID guid){check(guid!=nullptr,"fake HID class GUID has explicit output");*guid={0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0,0x11,0x11,0,0,0x30}};}
#endif
static DWORD GetLastError(){return io.error;}
static ULONGLONG GetTickCount64(){return io.tick;}
static HANDLE CreateFileW(LPCWSTR path,DWORD access,DWORD sharing,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE existing){
    ++io.opens;io.calls.push_back("open");io.path=path;
#if defined(YMCC_ISOLATION_HID_PORT_R3)
    if(io.topology.enabled){
        check((access==0||access==GENERIC_READ)&&sharing==(FILE_SHARE_READ|FILE_SHARE_WRITE)&&!security&&creation==OPEN_EXISTING&&!existing,"R3 opens only metadata-zero-access or shared read, never writes");
        check(flags==(access==GENERIC_READ?FILE_FLAG_OVERLAPPED:FILE_ATTRIBUTE_NORMAL),"R3 reader uses OVERLAPPED and zero-access metadata probe uses NORMAL flags exactly");
        if(!livePath(path)){io.error=ERROR_FILE_NOT_FOUND;return INVALID_HANDLE_VALUE;}
        if(access==GENERIC_READ){++io.readOpens;io.readPaths.push_back(path);}else{++io.probeOpens;io.probePaths.push_back(path);}
        io.openedAccess=access;
    }else
#endif
    check(access==GENERIC_READ&&sharing==(FILE_SHARE_READ|FILE_SHARE_WRITE)&&!security&&creation==OPEN_EXISTING&&flags==FILE_FLAG_OVERLAPPED&&!existing,"production Port opens only shared read-only overlapped interface");
    if(io.fault==Fault::Open){io.error=ERROR_ACCESS_DENIED;return INVALID_HANDLE_VALUE;}
    check(!io.device&&!io.pending,"production Port cannot open over an unreaped fake device");io.device=true;return io.deviceHandle;
}
static HANDLE CreateEventW(LPSECURITY_ATTRIBUTES security,BOOL manual,BOOL initial,LPCWSTR name){
    ++io.events;check(!security&&manual&&!initial&&!name,"production Port creates a private manual-reset unsignalled event");
    if(io.fault==Fault::Event){io.error=ERROR_NOT_ENOUGH_MEMORY;return nullptr;}io.event=true;return io.eventHandle;
}
static BOOL CloseHandle(HANDLE handle){
    check(!io.pending,"production lifecycle cannot close a handle before terminal I/O is observed");++io.closes;
    if(io.closeFailures){--io.closeFailures;io.error=ERROR_GEN_FAILURE;return FALSE;}
    if(handle==io.deviceHandle){check(io.device,"fake device closes exactly once");io.device=false;return TRUE;}
    check(handle==io.eventHandle&&io.event,"only owned fake event can close");io.event=false;return TRUE;
}
static DWORD WaitForSingleObject(HANDLE event,DWORD timeout){check(event==io.eventHandle&&io.event&&timeout==0,"production completion uses only nonblocking fake event observation");return io.terminal?WAIT_OBJECT_0:WAIT_TIMEOUT;}
static BOOL ResetEvent(HANDLE event){check(event==io.eventHandle&&io.event,"production resets its owned fake event");if(io.fault==Fault::Reset)return FALSE;io.terminal=false;return TRUE;}
static BOOL CancelIoEx(HANDLE device,LPOVERLAPPED pending){
    check(device==io.deviceHandle&&io.device&&pending==io.pending,"production cancellation targets the exact pending read");++io.cancels;io.cancelled=true;return TRUE;
}
static BOOL GetOverlappedResult(HANDLE device,LPOVERLAPPED pending,LPDWORD bytes,BOOL wait){
    check(device==io.deviceHandle&&io.device&&pending==io.pending&&!wait,"production reaps only its own fake operation without waiting");
    if(!io.terminal){io.error=ERROR_IO_INCOMPLETE;return FALSE;}
    *bytes=io.completionBytes;io.pending=nullptr;++io.completions;io.error=io.completionSuccess?ERROR_SUCCESS:ERROR_OPERATION_ABORTED;return io.completionSuccess?TRUE:FALSE;
}
static void complete(bool button,bool success=true,DWORD bytes=15){
    check(io.pending&&!io.terminal,"fake completion requires one live unfinished production read");io.button=button;io.completionSuccess=success;io.completionBytes=bytes;io.terminal=true;io.buffer[0]=0;io.pending->Internal=success?0:ERROR_OPERATION_ABORTED;
}
static BOOL ReadFile(HANDLE device,LPVOID buffer,DWORD bytes,LPDWORD immediate,LPOVERLAPPED pending){
#if defined(YMCC_ISOLATION_HID_PORT_R3)
    check(!io.topology.enabled||io.openedAccess==GENERIC_READ,"zero-access metadata probes cannot submit input reads");
#endif
    ++io.reads;check(device==io.deviceHandle&&io.device&&io.event&&!io.pending&&buffer&&bytes==15&&!immediate&&pending&&pending->hEvent==io.eventHandle,"production submits one exact-size overlapped read with stable storage");
    if(io.fault==Fault::Begin){io.error=ERROR_GEN_FAILURE;return FALSE;}
    io.pending=pending;io.buffer=static_cast<BYTE*>(buffer);io.cancelled=false;io.terminal=false;pending->Internal=0x103;
    if(io.synchronous){complete(false);return TRUE;}io.error=ERROR_IO_PENDING;return FALSE;
}
static bool sameXusbSource(const std::wstring& path,const std::wstring& slot){
    io.calls.push_back("same-xusb");check(!path.empty()&&!slot.empty(),"exact interface and slot paths reach same-XUSB proof");
#if defined(YMCC_ISOLATION_HID_PORT_R3)
    if(io.topology.enabled){const auto* candidate=nodeByPath(path);const auto* selected=nodeByPath(slot);return io.sourceMatches&&candidate&&selected&&candidate->present&&selected->present&&candidate->xusb&&candidate->xusb==selected->xusb&&slot==io.topology.slotPath;}
#endif
    return io.sourceMatches;
}
namespace ymccBackgroundUi {
static bool interfaceNode(const std::wstring& path,DEVINST& node,std::wstring& id){
    io.calls.push_back("interface");check(path.find(L"\\\\?\\")==0,"production resolves the actual interface path before root verification");if(!io.interfaceKnown)return false;
#if defined(YMCC_ISOLATION_HID_PORT_R3)
    if(io.topology.enabled){const auto* value=nodeByPath(path);if(!value||!value->present)return false;node=value->node;id=value->instance;return true;}
#endif
    node=77;id=io.instance;return true;
}
}
namespace ymcc {
namespace controllerhid=::ymcc::controllerhid;
namespace isolationhid=::ymcc::isolationhid;
namespace targetReturn::physicalPath {
struct Result {bool accepted;bool proven()const{return accepted;}};
static Result verify(const std::string& root,const std::string& instance){
    io.calls.push_back("verify");io.verifiedRoot=root;io.verifiedInstance=instance;
    check(instance.find("\\\\?\\")!=0,"root proof receives an instance ID rather than interface path");
#if defined(YMCC_ISOLATION_HID_PORT_R3)
    if(io.topology.enabled){const auto* source=nodeByInstance(U2W(instance));unsigned hops=0;while(source&&++hops<=32){if(W2U(source->instance)==root)return {io.rootMatches};source=nodeById(source->parent);}return {false};}
#endif
    check(instance==W2U(io.instance),"legacy proof receives the exact resolved instance ID");return {io.rootMatches};
}
}
}
static Fault activeFault(){
#if defined(YMCC_ISOLATION_HID_PORT_R3)
    if(io.topology.enabled){const auto* node=nodeByPath(io.path);if(node&&node->metadataFault!=Fault::None)return node->metadataFault;}
#endif
    return io.fault;
}
static BOOLEAN HidD_GetAttributes(HANDLE device,PHIDD_ATTRIBUTES attributes){check(device==io.deviceHandle&&io.device&&attributes->Size==sizeof(*attributes),"production attributes use only the fake opened handle");if(activeFault()==Fault::Attributes)return FALSE;attributes->VendorID=0x045e;attributes->ProductID=0x028e;return TRUE;}
static BOOLEAN HidD_GetPreparsedData(HANDLE device,PHIDP_PREPARSED_DATA* data){check(device==io.deviceHandle&&io.device,"preparsed metadata uses only fake device");if(activeFault()==Fault::Preparsed)return FALSE;io.prep=true;++io.preps;*data=reinterpret_cast<PHIDP_PREPARSED_DATA>(std::uintptr_t{0x3301});return TRUE;}
static BOOLEAN HidD_FreePreparsedData(PHIDP_PREPARSED_DATA data){check(io.prep&&data==reinterpret_cast<PHIDP_PREPARSED_DATA>(std::uintptr_t{0x3301})&&!io.pending,"preparsed metadata is freed after pending I/O returns");if(io.freeFailures){--io.freeFailures;return FALSE;}io.prep=false;++io.frees;return TRUE;}
static NTSTATUS HidP_GetCaps(PHIDP_PREPARSED_DATA data,PHIDP_CAPS caps){
    check(data&&io.prep,"caps are read from fake preparsed metadata");if(activeFault()==Fault::Caps)return HIDP_STATUS_INVALID_PREPARSED_DATA;
    *caps={};caps->UsagePage=1;caps->Usage=activeFault()==Fault::Usage?4:5;caps->InputReportByteLength=activeFault()==Fault::ReportBytes?257:15;caps->NumberInputDataIndices=16;caps->NumberLinkCollectionNodes=4;caps->NumberInputButtonCaps=1;caps->NumberInputValueCaps=activeFault()==Fault::ExtraValue?7:6;return HIDP_STATUS_SUCCESS;
}
static NTSTATUS HidP_GetButtonCaps(HIDP_REPORT_TYPE type,PHIDP_BUTTON_CAPS caps,PUSHORT count,PHIDP_PREPARSED_DATA data){
    check(type==HidP_Input&&data&&io.prep&&*count==1,"production requests complete button caps without truncation");if(activeFault()==Fault::Buttons)return HIDP_STATUS_INVALID_PREPARSED_DATA;
    caps[0]={};caps[0].UsagePage=9;caps[0].IsRange=TRUE;caps[0].IsAbsolute=TRUE;caps[0].Range.UsageMin=1;caps[0].Range.UsageMax=10;caps[0].Range.DataIndexMin=0;caps[0].Range.DataIndexMax=9;if(activeFault()==Fault::ButtonCount)*count=0;return HIDP_STATUS_SUCCESS;
}
static NTSTATUS HidP_GetValueCaps(HIDP_REPORT_TYPE type,PHIDP_VALUE_CAPS caps,PUSHORT count,PHIDP_PREPARSED_DATA data){
    check(type==HidP_Input&&data&&io.prep&&*count==(activeFault()==Fault::ExtraValue?7:6),"production requests every declared value cap");
    if(activeFault()==Fault::Values)return HIDP_STATUS_INVALID_PREPARSED_DATA;
    const std::array<USHORT,6> usages{0x30,0x31,0x33,0x34,0x32,0x39};
    for(unsigned index=0;index<*count;++index){auto& value=caps[index];value={};value.UsagePage=1;value.IsAbsolute=TRUE;value.ReportCount=1;value.NotRange.Usage=usages[index%6];value.NotRange.DataIndex=static_cast<USHORT>(10+index);value.BitSize=index==5?4:16;value.LogicalMin=index==5?1:0;value.LogicalMax=index==5?8:65535;value.LinkCollection=index<2?1:index<4?2:index==4?3:0;}
    if(activeFault()==Fault::AxisRange)caps[0].LogicalMax=131071;if(activeFault()==Fault::ValueCount)--*count;return HIDP_STATUS_SUCCESS;
}
static ULONG HidP_MaxDataListLength(HIDP_REPORT_TYPE type,PHIDP_PREPARSED_DATA data){check(type==HidP_Input&&data&&io.prep,"production capacity is obtained from full fake descriptor");return activeFault()==Fault::DataCapacity?65:16;}
static NTSTATUS HidP_GetData(HIDP_REPORT_TYPE type,PHIDP_DATA data,PULONG count,PHIDP_PREPARSED_DATA prep,PCHAR buffer,ULONG bytes){
    ++io.parses;check(type==HidP_Input&&prep&&io.prep&&buffer==reinterpret_cast<PCHAR>(io.buffer)&&bytes==15&&*count==16,"production parser receives the exact completed report and full descriptor capacity");if(activeFault()==Fault::Parse)return HIDP_STATUS_INVALID_REPORT_LENGTH;
    unsigned used=0;for(unsigned index=0;index<(activeFault()==Fault::MissingAxis?5u:6u);++index){data[used]={};data[used].DataIndex=static_cast<USHORT>(10+index);data[used].RawValue=index==5?0:32768;++used;}
    if(io.button){data[used]={};data[used].DataIndex=0;data[used].On=TRUE;++used;}*count=used;return HIDP_STATUS_SUCCESS;
}
template<class Value> struct PortAllocator {
    using value_type=Value;
    PortAllocator()=default;
    template<class Other> PortAllocator(const PortAllocator<Other>&){}
    Value* allocate(std::size_t count){
        if((io.fault==Fault::MetadataAllocation&&(std::is_same_v<Value,::ymcc::controllerhid::ButtonCaps>||std::is_same_v<Value,::ymcc::controllerhid::ValueCaps>))||(io.fault==Fault::BufferAllocation&&std::is_same_v<Value,BYTE>))throw std::bad_alloc{};
        return std::allocator<Value>{}.allocate(count);
    }
    void deallocate(Value* value,std::size_t count){std::allocator<Value>{}.deallocate(value,count);}
    template<class Other> bool operator==(const PortAllocator<Other>&)const{return true;}
};
namespace isolationPortStd {
using namespace ::std;
template<class Value> using vector=::std::vector<Value,PortAllocator<Value>>;
}
#define CreateFileW(...) (::isolationPortTest::CreateFileW)(__VA_ARGS__)
#define CreateEventW(...) (::isolationPortTest::CreateEventW)(__VA_ARGS__)
#define CloseHandle(...) (::isolationPortTest::CloseHandle)(__VA_ARGS__)
#define WaitForSingleObject(...) (::isolationPortTest::WaitForSingleObject)(__VA_ARGS__)
#define ResetEvent(...) (::isolationPortTest::ResetEvent)(__VA_ARGS__)
#define CancelIoEx(...) (::isolationPortTest::CancelIoEx)(__VA_ARGS__)
#define GetOverlappedResult(...) (::isolationPortTest::GetOverlappedResult)(__VA_ARGS__)
#define ReadFile(...) (::isolationPortTest::ReadFile)(__VA_ARGS__)
#define GetLastError(...) (::isolationPortTest::GetLastError)(__VA_ARGS__)
#define GetTickCount64(...) (::isolationPortTest::GetTickCount64)(__VA_ARGS__)
#define HidD_GetAttributes(...) (::isolationPortTest::HidD_GetAttributes)(__VA_ARGS__)
#define HidD_GetPreparsedData(...) (::isolationPortTest::HidD_GetPreparsedData)(__VA_ARGS__)
#define HidD_FreePreparsedData(...) (::isolationPortTest::HidD_FreePreparsedData)(__VA_ARGS__)
#define HidP_GetCaps(...) (::isolationPortTest::HidP_GetCaps)(__VA_ARGS__)
#define HidP_GetButtonCaps(...) (::isolationPortTest::HidP_GetButtonCaps)(__VA_ARGS__)
#define HidP_GetValueCaps(...) (::isolationPortTest::HidP_GetValueCaps)(__VA_ARGS__)
#define HidP_MaxDataListLength(...) (::isolationPortTest::HidP_MaxDataListLength)(__VA_ARGS__)
#define HidP_GetData(...) (::isolationPortTest::HidP_GetData)(__VA_ARGS__)
#if defined(YMCC_ISOLATION_HID_PORT_R3)
#define CM_Locate_DevNodeW(...) (::isolationPortTest::CM_Locate_DevNodeW)(__VA_ARGS__)
#define CM_Get_Child(...) (::isolationPortTest::CM_Get_Child)(__VA_ARGS__)
#define CM_Get_Sibling(...) (::isolationPortTest::CM_Get_Sibling)(__VA_ARGS__)
#define CM_Get_Device_IDW(...) (::isolationPortTest::CM_Get_Device_IDW)(__VA_ARGS__)
#define CM_Get_Device_Interface_List_SizeW(...) (::isolationPortTest::CM_Get_Device_Interface_List_SizeW)(__VA_ARGS__)
#define CM_Get_Device_Interface_ListW(...) (::isolationPortTest::CM_Get_Device_Interface_ListW)(__VA_ARGS__)
#define HidD_GetHidGuid(...) (::isolationPortTest::HidD_GetHidGuid)(__VA_ARGS__)
#endif
#define std isolationPortStd
#include YMCC_ISOLATION_HID_OEM_INCLUDE
enum class InspectionStage {Unobserved};
#include YMCC_ISOLATION_HID_DEVICE_INCLUDE
#include YMCC_ISOLATION_HID_PORT_INCLUDE
#undef std
#if defined(YMCC_ISOLATION_HID_PORT_R3)
#undef CM_Locate_DevNodeW
#undef CM_Get_Child
#undef CM_Get_Sibling
#undef CM_Get_Device_IDW
#undef CM_Get_Device_Interface_List_SizeW
#undef CM_Get_Device_Interface_ListW
#undef HidD_GetHidGuid
#endif
#undef CreateFileW
#undef CreateEventW
#undef CloseHandle
#undef WaitForSingleObject
#undef ResetEvent
#undef CancelIoEx
#undef GetOverlappedResult
#undef ReadFile
#undef GetLastError
#undef GetTickCount64
#undef HidD_GetAttributes
#undef HidD_GetPreparsedData
#undef HidD_FreePreparsedData
#undef HidP_GetCaps
#undef HidP_GetButtonCaps
#undef HidP_GetValueCaps
#undef HidP_MaxDataListLength
#undef HidP_GetData
static auto key(){return ::ymcc::isolationhid::Key{1,2,3,4,5,0,L"USB\\VID_045E&PID_028E\\ROOT",L"\\\\?\\hid#vid_045e&pid_028e#exact#{guid}",L"\\\\?\\xusb#exact"};}
static void assertReturned(const Port& port){check(port.returned()&&!io.device&&!io.event&&!io.prep&&!io.pending&&io.preps==io.frees,"actual production Port returns every fake handle, event and preparsed allocation");}
#if defined(YMCC_ISOLATION_HID_PORT_R3)
static void prepareTopology(const ::ymcc::isolationhid::Key& source,bool reverse=false){
    io.topology.enabled=true;io.topology.slotPath=source.slotPath;
    io.topology.nodes={
        {10,0,0,source.root,L"",true},
        {11,10,11,L"USB\\VID_045E&PID_028E\\XUSB",source.slotPath,true},
        {12,11,11,reverse?L"HID\\VID_045E&PID_028E&IG_02\\OLD":L"HID\\VID_045E&PID_028E&IG_03\\OLD",source.preferredPath,true},
        {13,11,11,reverse?L"HID\\VID_045E&PID_028E&IG_03\\NEW":L"HID\\VID_045E&PID_028E&IG_02\\NEW",
            reverse?L"\\\\?\\hid#vid_045e&pid_028e&ig_03#new#{guid}":L"\\\\?\\hid#vid_045e&pid_028e&ig_02#new#{guid}",false},
        {20,0,0,L"USB\\VID_045E&PID_028E\\FOREIGN",L"",true},
        {21,20,21,L"USB\\VID_045E&PID_028E\\FOREIGN-XUSB",L"\\\\?\\xusb#foreign",true},
        {22,21,21,L"HID\\VID_045E&PID_028E&IG_02\\FOREIGN",L"\\\\?\\hid#vid_045e&pid_028e&ig_02#foreign#{guid}",true}
    };
}
static json lastBindReceipt(){
    for(auto entry=logs.rbegin();entry!=logs.rend();++entry)
        if(entry->at("event")=="native-isolation-shared-hid-bind"||entry->at("event")=="native-isolation-shared-hid-bind-boundary")return entry->at("data");
    throw std::runtime_error("R3 actual Port must emit its bind receipt");
}
static void checkBindReceipt(const char* stage,const std::wstring& chosen){
    const auto receipt=lastBindReceipt();
    check(receipt.at("stage")==stage&&receipt.contains("preferredStage")&&receipt.contains("configRet"),"R3 actual receipt preserves selected stage, preferred stage and CM result");
    check(receipt.at("path")==W2U(chosen)&&receipt.at("access")=="shared-read-only-overlapped"&&!receipt.at("inputDeliveryProven").get<bool>(),"R3 receipt identifies actual chosen path without claiming hardware delivery");
}
static void checkBindLanes(){
    using Reader=::ymcc::isolationhid::Reader<Port>;using Frame=::ymcc::controllerhid::Frame;
    auto source=key();
    for(const bool detail:{false,true}){
        bindDetailEnabled=detail;reset(Fault::Event);prepareTopology(source);logs.clear();
        Reader reader;Frame frame;std::uint64_t seq=0,tick=0;
        for(unsigned i=0;i<6;++i)check(!reader.read(&source,100+250*i,frame,seq,tick),"faulted bind produces no report");
        check(io.readOpens==4,"receipt routing keeps four actual opens");
        std::vector<json> binds;
        for(const auto& row:logs)if(row.at("event")=="native-isolation-shared-hid-bind"||row.at("event")=="native-isolation-shared-hid-bind-boundary")binds.push_back(row);
        check(binds.size()==(detail?4u:2u),"unchanged middle failures only enter the detailed lane");
        check(binds.front().at("event")=="native-isolation-shared-hid-bind-boundary"&&binds.front().at("data").at("boundary")=="first-failure"&&binds.front().at("data").at("attempt")==1,"first failed attempt is resident");
        check(binds.back().at("event")=="native-isolation-shared-hid-bind-boundary"&&binds.back().at("data").at("boundary")=="bind-attempt-limit"&&binds.back().at("data").at("attempt")==4,"fourth failed attempt retains the limit receipt");
        if(detail)check(binds.at(1).at("event")=="native-isolation-shared-hid-bind"&&binds.at(2).at("event")=="native-isolation-shared-hid-bind","detail-on intermediate attempts keep their original event name");
        auto next=source;++next.topology;
        check(!reader.read(&next,1750,frame,seq,tick)&&lastBindReceipt().at("boundary")=="first-failure"&&lastBindReceipt().at("attempt")==1,"topology change starts a fresh first failure");
        check(reader.retire(),"failed attempts retire without extra resources");
    }
    bindDetailEnabled=false;reset(Fault::Event);prepareTopology(source);logs.clear();
    Reader reader;Frame frame;std::uint64_t seq=0,tick=0;
    check(!reader.read(&source,100,frame,seq,tick),"injected event creation fails");
    io.fault=Fault::None;nodeById(12)->present=false;
    check(!reader.read(&source,350,frame,seq,tick)&&lastBindReceipt().at("boundary")=="failure-changed"&&lastBindReceipt().at("stage")=="no-current-game-interface","changed failure stage is resident with detail disabled");
    nodeById(12)->present=true;
    check(!reader.read(&source,600,frame,seq,tick)&&reader.port().pending()&&lastBindReceipt().at("boundary")=="bound-after-failure"&&!lastBindReceipt().at("inputDeliveryProven").get<bool>(),"rebind is a resident receipt before completion of any report");
    complete(false);check(reader.retire(),"recovered binding retires after fake completion");
    bindDetailEnabled=true;
}
static void checkReenumeration(){
    using Reader=::ymcc::isolationhid::Reader<Port>;using Frame=::ymcc::controllerhid::Frame;
    auto source=key();source.preferredPath=L"\\\\?\\hid#vid_045e&pid_028e&ig_03#old#{guid}";
    for(const bool reverse:{false,true}){
        auto original=source;
        if(reverse)original.preferredPath=L"\\\\?\\hid#vid_045e&pid_028e&ig_02#old#{guid}";
        reset();prepareTopology(original,reverse);
        Reader reader;Frame frame;std::uint64_t sequence=0,tick=0;
        check(!reader.read(&original,100,frame,sequence,tick)&&reader.port().pending(),"R3 starts original live HID read through actual Port");
        complete(true);check(reader.read(&original,110,frame,sequence,tick)&&frame.pad.wButtons==XINPUT_GAMEPAD_A&&sequence==1,"R3 original HID delivers genuine DOWN");
        complete(false);check(reader.read(&original,120,frame,sequence,tick)&&!frame.pad.wButtons&&sequence==2,"R3 original HID delivers genuine UP");
        nodeById(12)->present=false;nodeById(13)->present=!reverse;auto replacement=original;++replacement.topology;++replacement.occupant;
        check(!reader.read(&replacement,130,frame,sequence,tick)&&io.cancels==1&&io.readOpens==1,"R3 reenumeration cannot bind alternate while old HID read is nonterminal");
        complete(true);const auto oldParses=io.parses;
        std::uint64_t reboundAt=140;
        check(!reader.read(&replacement,reboundAt,frame,sequence,tick)&&io.parses==oldParses&&sequence==2,"R3 discards late old HID DOWN before fresh bind");
        if(reverse){
            check(!reader.port().pending()&&io.readOpens==1,"Win5 old HID retires without opening a reader while no current game interface is visible");
            const auto missing=lastBindReceipt();
            check(missing.at("stage")=="no-current-game-interface"&&missing.at("boundary")=="first-failure"&&
                missing.at("preferredStage")=="interface-ancestry"&&missing.at("error")==ERROR_INVALID_DATA&&
                missing.at("attempt")==1&&missing.at("edge")==original.edge,"Win5 stale IG_02 retains its failed attempt and isolation edge");
            nodeById(13)->present=true;
            check(!reader.read(&replacement,reboundAt+210,frame,sequence,tick)&&!reader.port().pending()&&io.readOpens==1&&sequence==2,
                "Win5 delayed IG_03 waits for the existing retry with no stale report");
            reboundAt+=250;
            check(!reader.read(&replacement,reboundAt,frame,sequence,tick)&&sequence==2,"Win5 successful bind alone does not deliver an input report");
            const auto recovery=lastBindReceipt();
            check(recovery.at("attempt")==2&&recovery.at("boundary")=="bound-after-failure","Win5 retry recovers on the same key without a further topology change");
        }
        check(reader.port().pending(),"R3 alternate binding starts a fresh read");
        const auto chosen=nodeById(13)->path;
        checkBindReceipt("bound-current",chosen);check(lastBindReceipt().at("configRet")==CR_SUCCESS,"R3 successful alternate reports successful complete inventory");
        check(lastBindReceipt().at("preferredPath")==W2U(original.preferredPath)&&lastBindReceipt().at("edge")==original.edge,"R3 rebind receipt preserves the stale preference and same isolation edge");
        check(io.readPaths==std::vector<std::wstring>{original.preferredPath,chosen}&&io.probeOpens>0,"R3 opens GENERIC_READ only on original HID then unique live alternate");
        check(std::find(io.probePaths.begin(),io.probePaths.end(),nodeById(22)->path)==io.probePaths.end(),"R3 bounded root enumeration does not probe foreign USB subtree");
        for(unsigned report=0;report<4;++report){const bool down=report%2==0;complete(down);check(reader.read(&replacement,reboundAt+10+report,frame,sequence,tick)&&frame.pad.wButtons==(down?XINPUT_GAMEPAD_A:0)&&sequence==3+report,"R3 rebound real Port delivers each fresh DOWN and UP in order");}
        const auto heldSequence=sequence;check(reader.read(&replacement,reboundAt+20,frame,sequence,tick)&&sequence==heldSequence&&!frame.pad.wButtons,"R3 pending held UP is not a fresh report");
        check(!reader.read(nullptr,reboundAt+30,frame,sequence,tick)&&io.cancels==2&&!reader.port().canBind(),"R3 permit revocation retires current pending read without rebind");
        complete(true);const auto beforeRetireParses=io.parses;check(reader.retire()&&io.parses==beforeRetireParses,"R3 revocation discards late DOWN and returns actual Port resources");assertReturned(reader.port());
        std::cout<<json{{"case",reverse?"R3-old-IG02-to-unique-live-IG03":"R3-old-IG03-to-unique-live-IG02"},{"fixtureRevision",fixtureRevision},{"fieldReference",reverse?json("Win5-20261119-004933-edge-34"):json(nullptr)},{"delayedAlternate",reverse},{"freshReboundReports",4},{"oldPendingRebindBlocked",true},{"foreignSubtreeUntouched",true},{"revocationLateDownDiscarded",true}}.dump()<<'\n';
    }
    for(unsigned rejection=0;rejection<3;++rejection){reset();prepareTopology(source);nodeById(12)->present=false;nodeById(13)->present=true;
        if(rejection==0){nodeById(13)->xusb=21;}
        if(rejection==1)io.topology.nodes.push_back({14,11,11,L"HID\\VID_045E&PID_028E&IG_01\\SECOND",L"\\\\?\\hid#vid_045e&pid_028e&ig_01#second#{guid}",true});
        if(rejection==2)nodeById(11)->present=false;
        Reader reader;Frame frame;std::uint64_t sequence=0,tick=0;
        check(!reader.read(&source,100,frame,sequence,tick)&&io.readOpens==0&&io.reads==0,"R3 foreign same-root XUSB, ambiguous alternates, or revoked live slot never open GENERIC_READ");assertReturned(reader.port());
        const char* expectedStage=rejection==0?"no-current-game-interface":rejection==1?"current-game-interface-ambiguous":"slot-root-unproven";
        check(lastBindReceipt().at("stage")==expectedStage,"R3 denied alternate reports its exact rejection stage");
        std::cout<<json{{"case","R3-alternate-denied"},{"fixtureRevision",fixtureRevision},{"reason",rejection==0?"different-XUSB":rejection==1?"ambiguous":"slot-revoked"},{"readOpens",io.readOpens},{"metadataProbes",io.probeOpens}}.dump()<<'\n';
    }
    for(unsigned failure=0;failure<4;++failure){reset();prepareTopology(source);nodeById(12)->present=false;nodeById(13)->present=true;
        if(failure==0)io.topology.locateResult=CR_NO_SUCH_DEVNODE;
        if(failure==1)io.topology.childResult=CR_FAILURE;
        if(failure==2)io.topology.siblingResult=CR_FAILURE;
        if(failure==3)io.topology.listResult=CR_BUFFER_SMALL;
        Reader reader;Frame frame;std::uint64_t sequence=0,tick=0;
        check(!reader.read(&source,100,frame,sequence,tick)&&io.readOpens==0,"R3 unavailable or incomplete CM inventory fails closed before read access");assertReturned(reader.port());
        const char* stage=failure==0?"root-unavailable":failure==3?"current-interface-list-unavailable":"current-device-tree-unavailable";
        check(lastBindReceipt().at("stage")==stage&&lastBindReceipt().at("configRet")!=CR_SUCCESS,"R3 CM failure receipt retains actual failure stage and CONFIGRET");
        std::cout<<json{{"case","R3-CM-inventory-failure"},{"fixtureRevision",fixtureRevision},{"stage",lastBindReceipt().at("stage")},{"configRet",lastBindReceipt().at("configRet")},{"readOpens",io.readOpens}}.dump()<<'\n';
    }
    for(const auto fault:{Fault::Attributes,Fault::Preparsed,Fault::Caps}){reset();prepareTopology(source);nodeById(12)->present=false;nodeById(13)->present=true;nodeById(13)->metadataFault=fault;
        Reader reader;Frame frame;std::uint64_t sequence=0,tick=0;
        check(!reader.read(&source,100,frame,sequence,tick)&&io.readOpens==0&&io.probeOpens==1,"R3 unknown metadata probe cannot authorize read access");assertReturned(reader.port());
        check(lastBindReceipt().at("stage")=="current-interface-probe-unavailable","R3 failed zero-access metadata probe reports explicit unavailable stage");
    }
    for(unsigned limit=0;limit<2;++limit){reset();prepareTopology(source);nodeById(12)->present=false;nodeById(13)->present=true;
        const unsigned added=limit==0?33:17;
        for(unsigned index=0;index<added;++index){const auto suffix=std::to_wstring(index);io.topology.nodes.push_back({static_cast<DEVINST>(100+index),10,21,L"HID\\LIMIT\\"+suffix,limit==0?L"":L"\\\\?\\hid#limit#"+suffix,true});}
        Reader reader;Frame frame;std::uint64_t sequence=0,tick=0;
        check(!reader.read(&source,100,frame,sequence,tick)&&io.readOpens==0,"R3 bounded node or interface inventory refuses oversized subtree before read access");assertReturned(reader.port());
        check(lastBindReceipt().at("stage")=="current-inventory-limit","R3 inventory cap reports explicit limit stage");
    }
    reset();prepareTopology(source);nodeById(12)->present=false;nodeById(13)->present=true;
    io.topology.nodes.push_back({14,11,11,L"HID\\VID_045E&PID_028E&IG_01\\BAD-CAPS",L"\\\\?\\hid#vid_045e&pid_028e&ig_01#badcaps#{guid}",true,Fault::Usage});
    {
        Reader reader;Frame frame;std::uint64_t sequence=0,tick=0;
        check(!reader.read(&source,100,frame,sequence,tick)&&reader.port().pending()&&io.readPaths==std::vector<std::wstring>{nodeById(13)->path},"R3 invalid metadata alternate cannot create false ambiguity or gain read access");
        complete(false);check(reader.read(&source,110,frame,sequence,tick)&&!frame.pad.wButtons,"R3 valid unique alternate still delivers genuine neutral");
        check(!reader.retire(),"R3 alternate caps case retains pending cancellation");complete(false,false,0);check(reader.retire(),"R3 alternate caps case returns resources after terminal cancellation");assertReturned(reader.port());
    }
}
#endif
namespace retirementWiring {
namespace ymccCommonHid {
static ::ymcc::commonhid::Lane lane;
static std::atomic<bool> uiRetirementPending{false};
namespace isolationDirect {
static ::ymcc::commonhid::Lane lane;
static ::ymcc::isolationhid::Reader<Port> reader;
static ::ymcc::isolationhid::Key key;
static bool haveKey=false;
static std::uint64_t reportSequence=0;
static std::atomic<bool> backgroundRetirementPending{false};
#include YMCC_ISOLATION_HID_RETIRE_INCLUDE
}
}
namespace ymccBackgroundUi {
static ::ymcc::backgroundui::Lane lane;
}
static std::atomic<bool> g_inputReleaseRequired{false};
static bool g_gamepadAxisNeutralAdmissionRequired=false;
#include YMCC_ISOLATION_HID_HANDOFF_INCLUDE
static void reset() {
    ymccCommonHid::lane.clear();ymccCommonHid::isolationDirect::lane.clear();ymccBackgroundUi::lane.clear();
    ymccCommonHid::uiRetirementPending.store(false);ymccCommonHid::isolationDirect::backgroundRetirementPending.store(false);
    g_inputReleaseRequired.store(false);g_gamepadAxisNeutralAdmissionRequired=false;
}
}
static void checkIdleWithdrawal(){
    namespace wiring=retirementWiring;namespace direct=wiring::ymccCommonHid::isolationDirect;
    reset();wiring::reset();
    const auto source=key();
    ::ymcc::controllerhid::Frame frame;std::uint64_t sequence=0,tick=0;
    check(!direct::reader.read(&source,100,frame,sequence,tick)&&io.pending,"idle test starts with one real production Port operation");
    direct::haveKey=true;direct::key=source;direct::withdraw();
    check(!direct::haveKey&&!direct::reader.port().returned()&&io.cancels==1,"withdrawing clears the key but retains the pending cancellation");
    for(unsigned i=0;i<128;++i)direct::withdraw();
    check(io.pending&&io.cancels==1&&io.completions==0&&io.parses==0,"waiting withdrawal does not replay cancellation or parse stale samples");
    io.closeFailures=3;complete(true);
    direct::withdraw();
    check(io.closeFailures==1&&!direct::reader.port().returned()&&io.completions==1&&io.parses==0,"terminal report is discarded and a failed close remains debt");
    io.freeFailures=1;
    direct::withdraw();
    check(io.closeFailures==0&&io.prep&&!io.device&&!io.event&&!direct::reader.port().returned(),"metadata failure remains debt after both handles return");
    direct::withdraw();assertReturned(direct::reader.port());
    const auto reads=io.reads,closes=io.closes,frees=io.frees;
    for(unsigned i=0;i<128;++i)direct::withdraw();
    check(io.reads==reads&&io.closes==closes&&io.frees==frees&&io.cancels==1,"empty withdrawal never reopens, cancels, or returns resources again");
    check(!direct::reader.read(&source,200,frame,sequence,tick)&&io.reads==reads+1,"fresh ownership can bind after retirement ends");
    complete(false);check(direct::reader.read(&source,210,frame,sequence,tick)&&sequence&&tick==210&&!frame.pad.wButtons,"rebound first report can be neutral");
    direct::withdraw();complete(false,false,0);direct::withdraw();assertReturned(direct::reader.port());
}
static void checkPort(){
    using Reader=::ymcc::isolationhid::Reader<Port>;using Frame=::ymcc::controllerhid::Frame;
    const auto source=key();
    reset();{
        Reader reader;Frame frame;std::uint64_t sequence=0,tick=0;
        check(!reader.read(&source,100,frame,sequence,tick)&&reader.port().pending(),"actual Port starts one asynchronous read without inventing a report");
#if defined(YMCC_ISOLATION_HID_PORT_R3)
        check(io.calls==std::vector<std::string>{"interface","same-xusb","verify","open"}&&io.path==source.preferredPath&&io.verifiedRoot==W2U(source.root),"R3 exact live interface then same-XUSB and instance/root proof precede fake CreateFile");
#else
        check(io.calls==std::vector<std::string>{"same-xusb","interface","verify","open"}&&io.path==source.preferredPath&&io.verifiedRoot==W2U(source.root),"exact interface resolution and instance/root proof precede fake CreateFile");
#endif
        auto* stable=&reader.port().session.ov;auto* buffer=reader.port().session.buf.data();const auto event=reader.port().session.ovEvent;
        for(unsigned index=0;index<3;++index)check(!reader.read(&source,110+index,frame,sequence,tick)&&io.reads==1&&&reader.port().session.ov==stable&&reader.port().session.buf.data()==buffer&&reader.port().session.ovEvent==event,"pending actual Port never repeats begin or moves its operation storage");
        complete(true);check(reader.read(&source,120,frame,sequence,tick)&&frame.pad.wButtons==XINPUT_GAMEPAD_A&&frame.completeBasic&&!frame.independentTriggers&&sequence==1,"actual Port parser and Reader deliver a real DOWN");
        const auto reportSequence=sequence,reportTick=tick;
        check(reader.read(&source,130,frame,sequence,tick)&&sequence==reportSequence&&tick==reportTick&&io.parses==1&&io.reads==2,"actual Port held cache is not another completion");
        complete(false);check(reader.read(&source,140,frame,sequence,tick)&&!frame.pad.wButtons&&!frame.pad.sThumbLX&&sequence==reportSequence+1&&io.parses==2,"actual Port parser and Reader deliver real neutral UP");
        auto replacement=source;++replacement.identity;
        check(!reader.read(&replacement,150,frame,sequence,tick)&&io.cancels==1&&io.opens==1&&!reader.port().canBind(),"generation change cancels and forbids rebind while actual operation is nonterminal");
        io.tick=5000;check(!reader.read(&replacement,5000,frame,sequence,tick)&&io.opens==1&&io.closes==0&&io.prep,"cancel age is not resource-return proof");
        complete(true);const auto oldParses=io.parses;
        check(!reader.read(&replacement,5010,frame,sequence,tick)&&io.opens==2&&io.parses==oldParses&&reader.port().pending(),"late cancelled DOWN is discarded before new exact-key bind");
        complete(false);check(reader.read(&replacement,5020,frame,sequence,tick)&&!frame.pad.wButtons&&sequence==3,"replacement delivers only its own completed report");
        check(!reader.retire()&&io.cancels==2,"actual Port exit retains unfinished cancellation");complete(true,false,0);check(reader.retire(),"terminal aborted read lets actual lifecycle finish retirement");assertReturned(reader.port());
    }
    const std::array failures{Fault::Open,Fault::Event,Fault::Attributes,Fault::Preparsed,Fault::Caps,Fault::Buttons,Fault::ButtonCount,Fault::Values,Fault::ValueCount,Fault::Usage,Fault::ReportBytes,Fault::ExtraValue,Fault::AxisRange,Fault::DataCapacity,Fault::Reset,Fault::Begin};
    for(const auto fault:failures){reset(fault);Reader reader;Frame frame;std::uint64_t sequence=0,tick=0;check(!reader.read(&source,100,frame,sequence,tick),"production Port bind or begin API/descriptor failure returns no report");check(io.parses==0,"failed bind and begin never invoke report decoding");assertReturned(reader.port());}
    for(unsigned rejection=0;rejection<3;++rejection){reset();io.sourceMatches=rejection!=0;io.interfaceKnown=rejection!=1;io.rootMatches=rejection!=2;Reader reader;Frame frame;std::uint64_t sequence=0,tick=0;check(!reader.read(&source,100,frame,sequence,tick)&&io.opens==0,"unproven exact source/interface/root cannot reach even fake CreateFile");assertReturned(reader.port());}
    for(const auto fault:{Fault::Parse,Fault::MissingAxis}){reset();Reader reader;Frame frame;std::uint64_t sequence=0,tick=0;reader.read(&source,100,frame,sequence,tick);io.fault=fault;complete(true);check(!reader.read(&source,110,frame,sequence,tick)&&io.parses==1,"parse failure or incomplete report rejects actual completed DOWN");assertReturned(reader.port());}
    reset();{
        Reader reader;Frame frame;std::uint64_t sequence=0,tick=0;reader.read(&source,100,frame,sequence,tick);complete(true,true,14);check(!reader.read(&source,110,frame,sequence,tick)&&io.parses==0,"short actual completion is rejected before HID parser");assertReturned(reader.port());
    }
    reset();retirementWiring::reset();{
        namespace wiring=retirementWiring;namespace direct=wiring::ymccCommonHid::isolationDirect;
        Reader reader;Frame frame;std::uint64_t sequence=0,tick=0;reader.read(&source,100,frame,sequence,tick);complete(true);
        check(reader.read(&source,110,frame,sequence,tick)&&frame.pad.wButtons==XINPUT_GAMEPAD_A,"UP-rearm failure case first delivers actual DOWN");
        const ::ymcc::commonhid::Epoch epoch{source.power,source.topology,source.identity,source.occupant,source.slot};
        ::ymcc::commonhid::Sample sample{};XINPUT_GAMEPAD neutral{};
        wiring::ymccCommonHid::lane.observe(epoch,{frame.pad,30,105,false,true,32768,false});
        wiring::ymccBackgroundUi::lane.observe({source.power,source.topology,source.identity},frame.pad);
        check(wiring::ymccCommonHid::lane.read(epoch,true,neutral,sample)&&sample.pad.wButtons==XINPUT_GAMEPAD_A,"UP failure combination starts with actual selected same-epoch Raw held DOWN");
        direct::lane.observe(epoch,{frame.pad,31,110,false,true,32768,true});
        check(direct::lane.read(epoch,true,neutral,sample,false)&&sample.isolationSharedRead,"UP failure combination starts with selected direct DOWN");
        complete(false);io.fault=Fault::Begin;const auto downSequence=sequence;
        check(reader.read(&source,120,frame,sequence,tick)&&io.parses==2&&sequence==downSequence+1&&tick==120&&!frame.pad.wButtons,"completed real UP survives successor begin failure exactly once");assertReturned(reader.port());
        direct::lane.observe(epoch,{frame.pad,32,tick,false,true,32768,true});
        check(direct::lane.read(epoch,true,neutral,sample,false)&&!sample.pad.wButtons&&sample.isolationSharedRead,"completed real UP reaches actual direct Lane ahead of old Raw DOWN");
        check(!reader.read(&source,121,frame,sequence,tick)&&sequence==downSequence+1&&io.reads==3,"failed successor clears internal completed-frame cache immediately");
        direct::retireSample();
        check(!direct::lane.hasSample()&&!wiring::ymccCommonHid::lane.hasSample(),"actual retirement clears direct and old Raw samples");
        check(wiring::ymccCommonHid::uiRetirementPending.load()&&direct::backgroundRetirementPending.load(),"actual retirement publishes both UI handoff notifications");
        check(wiring::ymccBackgroundUi::lane.hasSample()&&!wiring::g_inputReleaseRequired.load()&&!wiring::g_gamepadAxisNeutralAdmissionRequired,"BUS-side retirement leaves UI-only cache and UI barriers untouched");
        wiring::commonControllerHidUiHandoff();
        check(!wiring::ymccBackgroundUi::lane.hasSample()&&wiring::g_inputReleaseRequired.load()&&wiring::g_gamepadAxisNeutralAdmissionRequired,"actual UI handoff clears old Background DOWN and raises neutral release barriers");
        check(!wiring::ymccCommonHid::uiRetirementPending.load()&&!direct::backgroundRetirementPending.load(),"actual UI handoff consumes retirement notifications once");
        check(!(direct::lane.read(epoch,true,neutral,sample,false)||wiring::ymccCommonHid::lane.read(epoch,true,neutral,sample)),"retired actual Lanes cannot fall back to old Raw DOWN");
        check(!reader.read(&source,200,frame,sequence,tick)&&io.opens==1,"last completed UP does not bypass original same-key retry delay");
        io.fault=Fault::None;check(!reader.read(&source,350,frame,sequence,tick)&&io.opens==2&&reader.port().pending()&&sequence==downSequence+1,"same-key rebind starts pending without resurrecting UP or DOWN cache");
        complete(true);check(reader.read(&source,360,frame,sequence,tick)&&frame.pad.wButtons==XINPUT_GAMEPAD_A&&sequence==downSequence+2,"rebound actual Port delivers only a new genuine report");
        check(!reader.retire(),"rebound pending operation retains retirement debt");complete(false,false,0);check(reader.retire(),"rebound terminal cancellation returns resources");assertReturned(reader.port());
        std::cout<<json{{"case","completed-UP-next-begin-failure-with-old-Raw"},{"decodedUpDeliveredOnce",true},{"rebindPendingHasNoCache",true},{"realRetirementAndUiHandoffExecuted",true},{"oldRawReplayBlocked",true},{"runtimeObserveExecuted",false}}.dump()<<'\n';
    }
    for(const auto fault:{Fault::MetadataAllocation,Fault::BufferAllocation}){reset(fault);
        Reader reader;Frame frame;std::uint64_t sequence=0,tick=0;bool caught=false,delivered=false;
        try{delivered=reader.read(&source,100,frame,sequence,tick);}catch(...){caught=true;}
        check(!caught&&!delivered&&reader.port().lastError==ERROR_NOT_ENOUGH_MEMORY,"actual Port bind catches allocation failure and returns false with out-of-memory status");assertReturned(reader.port());
        check(!reader.port().bound()&&reader.port().canBind()&&!reader.port().session.closing,"allocation failure leaves no partial bind or nonterminal resource debt");
        io.fault=Fault::None;check(!reader.read(&source,200,frame,sequence,tick)&&io.opens==1,"actual bind exception recovery retains original bounded retry delay");
        check(!reader.read(&source,350,frame,sequence,tick)&&io.opens==2&&reader.port().pending(),"actual production catch permits same-key retry after resource return");
        complete(false);check(reader.read(&source,360,frame,sequence,tick)&&!frame.pad.wButtons&&sequence==1,"allocation recovery delivers only a genuine newly completed neutral report");
        check(!reader.retire(),"allocation recovery still retains unfinished cancellation");complete(false,false,0);check(reader.retire(),"allocation recovery retires only after terminal cancellation");assertReturned(reader.port());
        std::cout<<json{{"case",fault==Fault::MetadataAllocation?"metadata-push-back-bad-alloc":"report-buffer-assign-bad-alloc"},{"exceptionPropagated",caught},{"resourcesReturned",true},{"sameKeyRetryRecovered",true},{"globalAllocationFault",false},{"productionCatchExecuted",true}}.dump()<<'\n';
    }
    reset();io.synchronous=true;{
        Reader reader;Frame frame;std::uint64_t sequence=0,tick=0;
        check(reader.read(&source,100,frame,sequence,tick)&&!frame.pad.wButtons&&sequence==1&&tick==100,
            "actual Port delivers a synchronous first report without waiting for the next evaluation");
        check(io.reads==2&&io.completions==1&&reader.port().ready(),"synchronous successor is armed but not consumed in the first evaluation");
        check(reader.read(&source,110,frame,sequence,tick)&&sequence==2&&io.reads==3&&io.completions==2,
            "each evaluation consumes at most one real report");
        check(reader.retire(),"already-terminal successor is reaped during retirement");assertReturned(reader.port());
    }
    reset();retirementWiring::reset();{
        namespace wiring=retirementWiring;namespace direct=wiring::ymccCommonHid::isolationDirect;
        const ::ymcc::commonhid::Epoch epoch{source.power,source.topology,source.identity,source.occupant,source.slot};
        XINPUT_GAMEPAD down{},neutral{};down.wButtons=XINPUT_GAMEPAD_A;::ymcc::commonhid::Sample sample{};
        wiring::ymccCommonHid::lane.observe(epoch,{down,40,100,false,true,32768,false});
        wiring::ymccBackgroundUi::lane.observe({source.power,source.topology,source.identity},down);
        direct::haveKey=true;direct::key=source;direct::reportSequence=40;direct::withdraw();
        check(wiring::ymccCommonHid::lane.hasSample()&&wiring::ymccBackgroundUi::lane.hasSample(),"never-direct withdrawal preserves original Raw and Background caches");
        check(!wiring::ymccCommonHid::uiRetirementPending.load()&&!direct::backgroundRetirementPending.load(),"never-direct withdrawal emits no UI retirement notification");
        check(!direct::haveKey&&direct::key==::ymcc::isolationhid::Key{}&&!direct::reportSequence,"actual withdrawal resets direct session identity and sequence");
        wiring::commonControllerHidUiHandoff();
        check(wiring::ymccBackgroundUi::lane.hasSample()&&!wiring::g_inputReleaseRequired.load()&&!wiring::g_gamepadAxisNeutralAdmissionRequired,"never-direct UI handoff leaves non-FSE original behavior unchanged");
        Frame frame;std::uint64_t sequence=0,tick=0;
        check(!direct::reader.read(&source,100,frame,sequence,tick)&&direct::reader.port().pending(),"withdrawal case uses actual Port and Reader pending operation");
        direct::lane.observe(epoch,{neutral,41,110,false,true,32768,true});
        check(direct::lane.hasSample()&&!direct::lane.selected(),"retirement gate case has a real neutral sample before source selection");
        direct::withdraw();
        check(!wiring::ymccCommonHid::lane.hasSample()&&!direct::lane.hasSample()&&wiring::ymccBackgroundUi::lane.hasSample(),"unselected direct sample withdrawal clears Raw but defers Background to UI");
        check(direct::backgroundRetirementPending.load()&&wiring::ymccCommonHid::uiRetirementPending.load()&&io.cancels==1&&!direct::reader.port().canBind(),"withdrawal publishes UI facts and retains nonterminal reader debt");
        wiring::commonControllerHidUiHandoff();
        check(!wiring::ymccBackgroundUi::lane.hasSample()&&wiring::g_inputReleaseRequired.load()&&wiring::g_gamepadAxisNeutralAdmissionRequired,"UI owns actual Background clear even for unselected neutral direct sample");
        wiring::ymccCommonHid::lane.observe(epoch,{down,42,120,false,true,32768,false});
        wiring::ymccBackgroundUi::lane.observe({source.power,source.topology,source.identity},down);
        wiring::g_inputReleaseRequired.store(false);wiring::g_gamepadAxisNeutralAdmissionRequired=false;
        direct::retireSample();wiring::commonControllerHidUiHandoff();
        check(wiring::ymccCommonHid::lane.read(epoch,true,neutral,sample)&&sample.sequence==42&&wiring::ymccBackgroundUi::lane.hasSample(),"idempotent retirement cannot clear later Raw or Background samples without new direct ownership");
        check(!wiring::g_inputReleaseRequired.load()&&!wiring::g_gamepadAxisNeutralAdmissionRequired,"idempotent retirement does not replay consumed UI barriers");
        complete(true);check(direct::reader.retire()&&io.parses==0,"late cancelled withdrawal DOWN is discarded without decoder use");assertReturned(direct::reader.port());
        std::cout<<json{{"case","actual-retireSample-withdraw-ui-handoff"},{"neverDirectPreserved",true},{"unselectedDirectRetiresRaw",true},{"busDidNotClearUiCache",true},{"uiHandoffClearedBackground",true},{"idempotent",true}}.dump()<<'\n';
    }
}
}
#endif
static void checkCommonHidLane(){
    using ymcc::commonhid::Epoch;using ymcc::commonhid::Lane;using ymcc::commonhid::Sample;
    const Epoch epoch{1,2,3,4,0};
    XINPUT_GAMEPAD neutral{},native{};native.wButtons=XINPUT_GAMEPAD_B;native.bLeftTrigger=37;native.bRightTrigger=91;
    Sample observed{},rawNeutral{{},1,100,false,true,32000,false};
    Lane raw;
    check(!raw.read(epoch,true,neutral,observed,false)&&!raw.selected(),"forced selection cannot invent a sample");
    raw.observe(epoch,rawNeutral);
    check(!raw.read(epoch,true,neutral,observed)&&raw.hasSample()&&!raw.selected(),"default Raw neutral still waits for active disagreement");
    check(!raw.read(epoch,true,native,observed,true)&&!raw.selected(),"explicit Raw disagreement gate also rejects neutral despite active native input");
    auto rawDown=rawNeutral;rawDown.pad.wButtons=XINPUT_GAMEPAD_A;rawDown.sequence=2;rawDown.observedTick=110;
    raw.observe(epoch,rawDown);
    check(!raw.read(epoch,true,native,observed)&&!raw.selected(),"default Raw active input still waits while native input is active");
    check(!raw.read(epoch,true,native,observed,true)&&!raw.selected(),"explicit true does not bypass original active disagreement");
    XINPUT_GAMEPAD triggerNative{};triggerNative.bLeftTrigger=37;triggerNative.bRightTrigger=91;
    check(raw.read(epoch,true,triggerNative,observed)&&raw.selected()&&observed.pad.wButtons==XINPUT_GAMEPAD_A,"default Raw selects only after original active disagreement");
    check(!observed.isolationSharedRead&&observed.pad.bLeftTrigger==37&&observed.pad.bRightTrigger==91,"default partial Raw preserves native triggers and its Raw transport identity");
    auto rawUp=rawNeutral;rawUp.sequence=3;rawUp.observedTick=120;raw.observe(epoch,rawUp);
    check(raw.read(epoch,true,triggerNative,observed)&&!observed.pad.wButtons&&observed.sequence==3,"selected default Raw still delivers a real neutral release");
    Sample directNeutral{{},10,200,false,true,31000,true};
    directNeutral.pad.bLeftTrigger=201;directNeutral.pad.bRightTrigger=202;
    Lane direct;direct.observe(epoch,directNeutral);
    check(direct.read(epoch,true,native,observed,false)&&direct.selected()&&!observed.pad.wButtons&&!observed.pad.sThumbLX,"exact direct first neutral is authoritative even against active native input");
    check(observed.isolationSharedRead&&observed.sequence==10&&observed.observedTick==200&&observed.completeBasic&&observed.combinedTriggerRaw==31000,"direct authority preserves actual report and transport metadata");
    check(observed.pad.bLeftTrigger==37&&observed.pad.bRightTrigger==91,"partial direct combined trigger data cannot replace independent native triggers");
    check(direct.read(epoch,true,neutral,observed,false)&&observed.sequence==10&&observed.observedTick==200&&!observed.pad.bLeftTrigger&&!observed.pad.bRightTrigger,"held direct report neither becomes new nor holds obsolete native trigger values");
    auto oldRawDown=rawDown;oldRawDown.sequence=20;oldRawDown.observedTick=300;
    Lane staleRaw;staleRaw.observe(epoch,oldRawDown);
    check(staleRaw.read(epoch,true,neutral,observed)&&observed.pad.wButtons==XINPUT_GAMEPAD_A&&!observed.isolationSharedRead,"same-epoch old Raw held DOWN is actually selected before arbitration");
    auto freshDirectUp=directNeutral;freshDirectUp.sequence=21;freshDirectUp.observedTick=310;
    Lane authoritative;authoritative.observe(epoch,freshDirectUp);unsigned rawReads=0;
    const bool used=authoritative.read(epoch,true,neutral,observed,false)||[&]{++rawReads;return staleRaw.read(epoch,true,neutral,observed);}();
    check(used&&!observed.pad.wButtons&&observed.sequence==21&&observed.isolationSharedRead,"first completed direct UP wins over old Raw DOWN without relabeling it Raw");
    check(rawReads==0&&staleRaw.selected(),"authoritative direct short-circuits old Raw without claiming Raw delivered its UP");
    auto olderDirect=freshDirectUp;olderDirect.sequence=20;olderDirect.pad.wButtons=XINPUT_GAMEPAD_A;authoritative.observe(epoch,olderDirect);
    check(authoritative.read(epoch,true,neutral,observed,false)&&observed.sequence==21&&!observed.pad.wButtons,"older direct sample cannot resurrect a held DOWN");
    Lane independent;auto independentNeutral=directNeutral;independentNeutral.independentTriggers=true;independentNeutral.pad.bLeftTrigger=0;independentNeutral.pad.bRightTrigger=0;
    independent.observe(epoch,independentNeutral);
    check(independent.read(epoch,true,native,observed,false)&&!observed.pad.bLeftTrigger&&!observed.pad.bRightTrigger,"real independent direct trigger UP overrides active native triggers");
    auto independentDown=independentNeutral;independentDown.sequence=11;independentDown.observedTick=210;independentDown.pad.bLeftTrigger=123;independentDown.pad.bRightTrigger=234;
    independent.observe(epoch,independentDown);
    check(independent.read(epoch,true,neutral,observed,false)&&observed.pad.bLeftTrigger==123&&observed.pad.bRightTrigger==234,"real independent direct trigger DOWN is preserved exactly");
    Lane triggerOnlyRaw;auto rawTrigger=independentDown;rawTrigger.isolationSharedRead=false;triggerOnlyRaw.observe(epoch,rawTrigger);
    check(!triggerOnlyRaw.read(epoch,true,neutral,observed)&&!triggerOnlyRaw.selected(),"default Raw trigger-only input does not bypass the original basic disagreement gate");
    rawTrigger.pad.wButtons=XINPUT_GAMEPAD_A;++rawTrigger.sequence;triggerOnlyRaw.observe(epoch,rawTrigger);
    check(triggerOnlyRaw.read(epoch,true,neutral,observed,true)&&observed.pad.bLeftTrigger==123&&observed.pad.bRightTrigger==234&&!observed.isolationSharedRead,"explicit original Raw admission preserves proven independent triggers");
    Lane denied;denied.observe(epoch,directNeutral);
    check(!denied.read(epoch,false,neutral,observed,false)&&!denied.hasSample()&&!denied.selected(),"direct authority never bypasses source eligibility and retires its held sample");
    check(!denied.read(epoch,true,neutral,observed,false),"a later eligible read cannot revive a retired direct sample");
    Lane replaced;replaced.observe(epoch,directNeutral);auto changed=epoch;++changed.identity;
    check(!replaced.read(changed,true,neutral,observed,false)&&!replaced.hasSample(),"direct authority cannot cross a source epoch boundary");
    check(!replaced.read(epoch,true,neutral,observed,false),"returning to an old epoch cannot revive the retired direct sample");
    for(const bool omitSequence:{true,false}){
        Lane invalid;auto invalidSample=directNeutral;if(omitSequence)invalidSample.sequence=0;else invalidSample.observedTick=0;invalid.observe(epoch,invalidSample);
        check(!invalid.read(epoch,true,neutral,observed,false)&&!invalid.hasSample(),"direct authority requires a real report sequence and observation stamp");
    }
}
struct IsolationFakePort {
    using Key=ymcc::isolationhid::Key;
    using Frame=ymcc::controllerhid::Frame;
    using ReadResult=ymcc::isolationhid::ReadResult;
    bool bindAllowed=true,beginAllowed=true;
    bool boundState=false,pendingState=false,closingState=false;
    unsigned bindCalls=0,beginCalls=0,pollCalls=0,cancelCalls=0;
    unsigned deliveredFrames=0,discardedFrames=0;
    void (*duringBegin)(IsolationFakePort&)=nullptr;
    void (*duringPoll)()=nullptr;
    std::vector<Key> bindKeys;
    ReadResult completion=ReadResult::Waiting;
    Frame incoming{};
    bool bound()const{return boundState;}
    bool pending()const{return pendingState;}
    bool canBind()const{return !boundState&&!pendingState&&!closingState;}
    bool returned()const{return !boundState&&!pendingState&&!closingState;}
    bool bind(const Key& key,unsigned){
        ++bindCalls;bindKeys.push_back(key);
        if(!canBind())throw std::runtime_error("FakePort cannot bind before resource return");
        boundState=bindAllowed;return boundState;
    }
    bool begin(){
        ++beginCalls;
        if(!boundState||pendingState||closingState)throw std::runtime_error("FakePort duplicate or retired begin");
        if(!beginAllowed)return false;
        pendingState=true;
        if(auto callback=std::exchange(duringBegin,nullptr))callback(*this);
        return true;
    }
    void complete(const Frame& frame){
        if(!pendingState||completion!=ReadResult::Waiting)throw std::runtime_error("FakePort completion without one pending read");
        incoming=frame;completion=ReadResult::Frame;
    }
    void fail(){
        if(!pendingState||completion!=ReadResult::Waiting)throw std::runtime_error("FakePort failure without one pending read");
        completion=ReadResult::Failed;
    }
    ReadResult poll(Frame& frame){
        ++pollCalls;
        if(auto callback=std::exchange(duringPoll,nullptr))callback();
        if(closingState)throw std::runtime_error("FakePort polled a closing read");
        if(!pendingState||completion==ReadResult::Waiting)return ReadResult::Waiting;
        const auto result=completion;
        if(result==ReadResult::Frame){frame=incoming;++deliveredFrames;}
        pendingState=false;completion=ReadResult::Waiting;incoming={};return result;
    }
    unsigned readCancelRequests=0;
    ymcc::isolationhid::DrainResult discardPending(bool requestCancel){
        using Result=ymcc::isolationhid::DrainResult;
        if(completion==ReadResult::Waiting){
            if(requestCancel)++readCancelRequests;
            return Result::Waiting;
        }
        if(completion==ReadResult::Frame)++discardedFrames;
        pendingState=false;completion=ReadResult::Waiting;incoming={};
        return Result::Drained;
    }
    void close(){
        boundState=false;
        if(pendingState){if(!closingState)++cancelCalls;closingState=true;}
        else{closingState=false;completion=ReadResult::Waiting;incoming={};}
    }
    void serviceClosing(){
        if(!closingState||completion==ReadResult::Waiting)return;
        if(completion==ReadResult::Frame)++discardedFrames;
        pendingState=closingState=false;completion=ReadResult::Waiting;incoming={};
    }
};
static ymcc::controllerhid::Frame isolationFrame(WORD buttons){
    ymcc::controllerhid::Frame frame;frame.pad.wButtons=buttons;frame.completeBasic=true;return frame;
}
#if defined(YMCC_ISOLATION_HID_OBSERVE_INCLUDE)
namespace pendingIsolationReader {
using namespace ymcc::isolationhid;
using Epoch=ymcc::commonhid::Epoch;
static std::atomic<std::shared_ptr<const Permit>> g_controllerIsolationHidPermit;
static std::atomic<bool> g_controllerFrontendIsolationEnabled{true},g_msiNativeForegroundObserverActive{true},
    g_inputReady{true},g_msiNativeForegroundPowerHold{false},g_inputCaptureStop{false},
    g_msiNativeForegroundVirtualYieldPending{false};
static std::atomic<std::uint64_t> g_msiNativeForegroundEdge{2},g_sharedPowerSuspendSerial{7};
static std::atomic<std::uint64_t> g_win5WakeReaderHost{0},g_win5WakeReaderDemand{0},
    g_wakeKeptTargetGeneration{0},g_wakeKeptTargetSuspendSerial{0},g_inputHostVirtualIntentRevision{4};
static std::atomic<unsigned> g_inputHostVirtualIntentWriters{0};
static ymcc::inputHost::VirtualRecoveryIntent g_inputHostRecoveryIntent;
static std::atomic<bool> g_inputHostPowerAdmissionRevoked{false};
static std::uint64_t fixtureHost=9;
static bool fixtureFse=true,externalPriority=false,returningPhysical=false;
static std::mutex g_hidHidePhysicalIdentityMx;
static struct {bool resolved=true;unsigned vendorId=0x045e,productId=0x028e;
    std::string baseContainerDeviceInstanceId="root",sourcePath="preferred";} g_hidHidePhysicalIdentity;
static struct {bool virtualTargetEnabled=false;} g_realStickTest;
static ymcc::sleeppower::Dispatch g_sleepPowerDispatch;
static std::atomic<bool> g_hardwareWriteGate{true};
static struct {bool isolated=false;} g_aiFanMockSession;
static bool virtualPriority=false,uiOwned=true;
static std::uint64_t topology=3;
static ULONGLONG clockTick=100;
static ULONGLONG fixtureTick(){return clockTick;}
static void beginQuery(){
    if(!g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++clockTick,[](const auto&){return true;}))
        throw std::runtime_error("fixture suspend callback not posted");
}
static void cancelQuery(){
    const auto receipt=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPENDFAILED,++clockTick);
    if(!receipt)throw std::runtime_error("fixture query failure not dispatched");
    g_sleepPowerDispatch.complete(receipt,clockTick);
}
static void coalesceQuery(){beginQuery();cancelQuery();}
static bool msiNativeForegroundUiEligible(){return uiOwned;}
static bool msiNativeForegroundVirtualWorkHasPriority(){return virtualPriority;}
static bool inputHostExitFenceRaised(){return false;}
static std::string inputHostConfiguredPersona(){return g_realStickTest.virtualTargetEnabled?"steamdeck":"disabled";}
static bool hardwareWriteAllowed(){return g_hardwareWriteGate&&g_powerLifecycle==PowerLifecycle::Ready&&
    !g_aiFanMockSession.isolated&&!g_sleepPowerDispatch.pendingSuspend();}
static bool context(Epoch& epoch,std::wstring* path=nullptr){epoch={power,topology,4,5,0};if(path)*path=L"slot";return true;}
namespace ymccBackgroundUi {static bool fseActive(){return fixtureFse;}}
static bool eligible(){return fixtureFse && !externalPriority && !returningPhysical;}
static std::uint64_t inputHostEpochToken(){return fixtureHost;}
static bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& out){
    if(!hardwareWriteAllowed())return false;
    out.generation=power;out.suspendSerial=g_sharedPowerSuspendSerial.load();
    out.observedNotificationSequence=g_sleepPowerDispatch.observedSequence();
    out.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
    return true;
}
namespace ymccCommonHid {static ymcc::commonhid::Lane lane;}
static bool uiEvaluation=true;
static Reader<IsolationFakePort> reader;
static ymcc::commonhid::Lane lane;
static Key key;
static bool haveKey=false;
static std::uint64_t reportSequence=0,sequence=0;
static unsigned retiredSamples=0;
static void retireSample(){if(lane.hasSample())++retiredSamples;lane.clear();}
#define GetTickCount64 fixtureTick
#include YMCC_ISOLATION_HID_OBSERVE_INCLUDE
#if defined(YMCC_ISOLATION_HID_RAW_FENCE_INCLUDE)
namespace ymccCommonHid {
static std::uint64_t dispatchGeneration=0;
namespace isolationDirect {static std::atomic<bool> backgroundRetirementPending{false};}
}
#include YMCC_ISOLATION_HID_RAW_FENCE_INCLUDE
#endif
#undef GetTickCount64
static void observe(bool ui){
    Epoch current;context(current);
    if(eligible())observe(ui,current);
    else {withdraw();reader.port().serviceClosing();} // Production Port::close also services closing.
}
static void checkWin5WakeReader(){
    auto& port=reader.port();
    g_realStickTest.virtualTargetEnabled=true;
    g_xinputPhysicalSourceLocked=true;
    g_wakeKeptTargetGeneration=power;
    g_wakeKeptTargetSuspendSerial=7;
    const auto configure=[](bool target,const char* persona,unsigned revision=1){
        g_inputHostRecoveryIntent.configure(json{{"outputTarget",{{"revision",revision}}}},"",true,target,persona);
    };
    configure(true,"steamdeck");
    const Epoch slot{power,topology,4,5,0};
    ymcc::commonhid::Sample output;
    const auto read=[&](XINPUT_GAMEPAD original={}){
        auto current=slot;current.power=power;
        return lane.readWakeFallback(current,sampleReadAllowed(true),original,clockTick,output);
    };
    const auto down=isolationFrame(XINPUT_GAMEPAD_A),up=isolationFrame(0);
    observe(true);
    check(!port.bindCalls,"non-Win5 or non-SD wake without the original Host permit must not open HID");
    check(std::string_view(publishWin5WakeReader(fixtureHost,fixtureHost,g_inputHostRecoveryIntent.revision()))=="published",
        "current SD demand publishes the retained wake reader permit");
    observe(false);
    check(!port.bindCalls,"BUS does not open the shared port");
    observe(true);
    check(port.bindCalls==1 && !read(),"first wake opens one exact read; waiting is not activity");
    port.complete(up);++clockTick;observe(true);
    check(!read(),"a real neutral alone cannot select the backup");
    port.complete(down);++clockTick;observe(true);
    check(read() && output.pad.wButtons==XINPUT_GAMEPAD_A,"fresh HID DOWN replaces neutral XInput");
    clockTick+=300;observe(false);
    check(read() && output.pad.wButtons==XINPUT_GAMEPAD_A,"a selected live event-driven hold does not expire on a quiet report stream");
    port.complete(up);++clockTick;observe(true);
    check(read() && !output.pad.wButtons,"fresh UP releases the selected backup");
    const auto oldSequence=output.sequence;
#if defined(YMCC_ISOLATION_HID_RAW_FENCE_INCLUDE)
    check(ymccCommonHid::win5WakeRawAllowed(),"retained READY permits Raw data");
    ++clockTick;const auto queuedBeforeSleep=clockTick;
    check(ymccCommonHid::win5WakeRawAllowed(queuedBeforeSleep),"new pre-sleep Raw is current before the boundary");
    ymccCommonHid::lane.observe(slot,{down.pad,1,clockTick,false,true,0});
    check(ymccCommonHid::lane.read(slot,true,{},output),"test begins with a cached active Raw report");
    const auto dispatchBefore=ymccCommonHid::dispatchGeneration;
#endif
    beginQuery();observe(true);
    check(!read() && !port.bound() && port.cancelCalls==1,"new sleep immediately revokes old selection");
    cancelQuery();g_sharedPowerSuspendSerial=8;observe(true);
    check(!read() && port.bindCalls==1,"same-generation second sleep cannot use the first completion");
#if defined(YMCC_ISOLATION_HID_RAW_FENCE_INCLUDE)
    check(ymccCommonHid::win5WakeRawAllowed() && !ymccCommonHid::lane.hasSample() &&
        ymccCommonHid::dispatchGeneration>dispatchBefore,"same-generation Ready retires cached Raw and its batch token");
    check(!ymccCommonHid::win5WakeRawAllowed(queuedBeforeSleep),"a Raw DOWN queued before the sleep boundary is rejected");
#endif
    g_wakeKeptTargetSuspendSerial=8;observe(true);
    check(!read() && port.bindCalls==1,"new retained wake cannot open before old read returns");
    port.complete(down);++clockTick;observe(true);
    check(!read() && port.bindCalls==2 && port.discardedFrames==1,"late first-wake DOWN is discarded before rebinding");
#if defined(YMCC_ISOLATION_HID_RAW_FENCE_INCLUDE)
    check(!ymccCommonHid::win5WakeRawAllowed(queuedBeforeSleep) && !ymccCommonHid::lane.hasSample(),
        "direct port completion cannot revive old Raw data");
    check(ymccCommonHid::win5WakeRawAllowed(clockTick),"a new queued Raw report is not held behind the direct port");
    ymccCommonHid::lane.observe(slot,{up.pad,2,clockTick,false,true,0});
    check(!ymccCommonHid::lane.read(slot,true,{},output),"fresh Raw neutral cannot resurface the old DOWN");
    ++clockTick;
    check(ymccCommonHid::win5WakeRawAllowed(clockTick),"fresh Raw DOWN remains admissible");
    ymccCommonHid::lane.observe(slot,{down.pad,3,clockTick,false,true,0});
    check(ymccCommonHid::lane.read(slot,true,{},output) && output.pad.wButtons==XINPUT_GAMEPAD_A,
        "fresh Raw DOWN selects when the original source is neutral");
    ymccCommonHid::lane.clear();
#endif
    port.complete(down);++clockTick;observe(true);
    check(read() && output.sequence>oldSequence,"fresh second-wake DOWN reaches the same source and Host");
    XINPUT_GAMEPAD native{};native.wButtons=XINPUT_GAMEPAD_B;
    check(!read(native) && !read(),"live XInput resumes authority without replaying cached HID");
    port.complete(down);port.duringPoll=coalesceQuery;++clockTick;observe(true);
    check(!read() && !port.bound(),"a late shared notification invalidates in-flight HID completion");
    port.complete(down);++clockTick;observe(true);
    check(!read() && port.bindCalls==3,"a false sleep query allows a clean rebind without accepting prior data");
    for(unsigned boundary=0;boundary<7;++boundary){
        port.complete(down);++clockTick;observe(true);
        check(read(),"negative control starts with an active current sample");
        if(boundary==0)fixtureFse=false;
        if(boundary==1)configure(true,"xbox");
        if(boundary==2)g_realStickTest.virtualTargetEnabled=false;
        if(boundary==3)externalPriority=true;
        if(boundary==4)++fixtureHost;
        if(boundary==5)returningPhysical=true;
        if(boundary==6)g_inputHostPowerAdmissionRevoked=true;
        observe(false);
        check(!read() && !port.bound(),"FSE exit, stale intent, OFF, external, Host, physical return or power revocation retires data");
        port.complete(up);observe(false);
        check(port.returned(),"retired shared read discards its terminal report");
        fixtureFse=true;g_realStickTest.virtualTargetEnabled=true;
        externalPriority=false;fixtureHost=9;returningPhysical=false;g_inputHostPowerAdmissionRevoked=false;
        if(boundary==1){
            const auto original=g_win5WakeReaderDemand.load();
            configure(true,"steamdeck");
            observe(true);
            check(!port.bound()&&!read(),"returning from a different persona does not revive the old wake permit");
            configure(false,"disabled");configure(true,"steamdeck");
            g_win5WakeReaderHost=0;
            check(std::string_view(publishWin5WakeReader(fixtureHost,fixtureHost,original))=="demand-changed"&&!g_win5WakeReaderHost,
                "an OFF/ON sequence cannot renew the original demand at publication");
            check(std::string_view(publishWin5WakeReader(fixtureHost+1,fixtureHost,g_inputHostRecoveryIntent.revision()))=="arm-host-mismatch"&&!g_win5WakeReaderHost,
                "a replacement Host cannot inherit the original permit");
            check(std::string_view(publishWin5WakeReader(fixtureHost,fixtureHost,g_inputHostRecoveryIntent.revision()))=="published",
                "the remaining negative controls use a new simulated proof");
        }
        observe(true);
        check(port.bound() && !read(),"restored authority opens only a fresh read");
    }
#if defined(YMCC_ISOLATION_HID_RAW_FENCE_INCLUDE)
    check(ymccCommonHid::win5WakeRawAllowed(),"retained reader arms Raw handoff tracking");
    ++clockTick;const auto queuedBeforeWithdraw=clockTick;
    ymccCommonHid::lane.observe(slot,{down.pad,4,clockTick,false,true,0});
    check(ymccCommonHid::lane.read(slot,true,{},output),"retirement control starts with cached Raw DOWN");
    coalesceQuery();++g_sharedPowerSuspendSerial;
    g_win5WakeReaderHost=0; // Host process itself was not replaced.
    check(!ymccCommonHid::win5WakeRawAllowed(queuedBeforeWithdraw) && !ymccCommonHid::lane.hasSample(),
        "withdrawing authority before READY observation still retires cached and queued Raw DOWN");
    check(!ymccCommonHid::win5WakeRawAllowed(clockTick),"the exact handoff tick cannot pass as a fresh message");
    ++clockTick;
    check(ymccCommonHid::win5WakeRawAllowed(clockTick),"the next fresh ordinary Raw report is not blocked");
    clockTick+=251;
    check(ymccCommonHid::win5WakeRawAllowed(queuedBeforeWithdraw),"departed wake stops applying its fence after the original queue-age bound");
#endif
    withdraw();port.complete(up);port.serviceClosing();
    check(port.returned(),"shared port returns all simulated resources");

    // Exercise actual public Dispatch transitions, not two repeats of a cancelled query.
    const auto retainedHost=fixtureHost,retainedIntent=g_inputHostRecoveryIntent.revision();
    const auto grantRetainedReader=[&](std::uint64_t armedDemand){
        g_wakeKeptTargetGeneration=power;
        g_wakeKeptTargetSuspendSerial=g_sharedPowerSuspendSerial.load();
        return std::string_view(publishWin5WakeReader(retainedHost,fixtureHost,armedDemand));
    };
    check(grantRetainedReader(retainedIntent)=="published","retained Host begins with an existing current permit");
    observe(true);
    check(port.bound()&&port.pending()&&!read(),"retained Host starts the repeated wake check with no cached selection");
    auto previousSelection=sequence;
    unsigned refreshRevision=2;
    struct WakeCycle {WORD button;bool advanceGeneration,activeFirst;};
    const WakeCycle cycles[]{
        {XINPUT_GAMEPAD_X,false,false},{XINPUT_GAMEPAD_Y,false,false},{XINPUT_GAMEPAD_A,false,false},
        // Win5 field order: neutral first, held on the first report, neutral first.
        {XINPUT_GAMEPAD_X,true,false},{XINPUT_GAMEPAD_Y,true,true},{XINPUT_GAMEPAD_A,true,false},
    };
    for(const auto& cycle:cycles){
        const auto oldPower=power;
        const auto button=cycle.button;
        const auto oldSerial=g_sharedPowerSuspendSerial.load(),oldNotice=g_sleepPowerDispatch.observedSequence();
        const auto binds=port.bindCalls,discarded=port.discardedFrames;
        const auto suspend=g_sleepPowerDispatch.window(PBT_APMSUSPEND,++clockTick);
        check(suspend.sequence>oldNotice&&g_sleepPowerDispatch.pendingSuspend(),"public suspend is observed before withdrawing the wake reader");
        observe(false);
        check(!read()&&!port.bound()&&port.pending(),"a public suspend retires selection and holds the old I/O until completion");
        g_win5WakeReaderHost=0;g_inputReady=false;g_powerLifecycle=PowerLifecycle::Suspended;
        ++g_sharedPowerSuspendSerial;
        g_sleepPowerDispatch.complete(suspend,clockTick);
        const auto resume=g_sleepPowerDispatch.window(PBT_APMRESUMEAUTOMATIC,++clockTick);
        check(resume.sequence>suspend.sequence,"the public resume belongs to a later notification in this cycle");
        if(cycle.advanceGeneration)++power;
        g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=true;
        g_sleepPowerDispatch.complete(resume,clockTick);
        const auto armedDemand=g_inputHostRecoveryIntent.revision();
        const auto oldWriterRevision=g_inputHostVirtualIntentRevision.load();
        ++g_inputHostVirtualIntentWriters;++g_inputHostVirtualIntentRevision;
        configure(true,"steamdeck",refreshRevision++);
        const bool writerAtCompletion=button==XINPUT_GAMEPAD_Y;
        const auto finishRefresh=[] {++g_inputHostVirtualIntentRevision;--g_inputHostVirtualIntentWriters;};
        if(!writerAtCompletion)finishRefresh();
        port.complete(down);++clockTick;observe(true);
        check(!read()&&port.returned()&&port.bindCalls==binds&&port.discardedFrames==discarded+1,
            "Ready without the current permit drains the previous cycle's DOWN without opening a port");

        check(g_inputHostVirtualIntentRevision!=oldWriterRevision && armedDemand==retainedIntent &&
            grantRetainedReader(armedDemand)=="published","same-value refresh between arm and completion cannot retire SD demand");
        if(writerAtCompletion){
            observe(true);
            check(!read()&&port.bindCalls==binds,"published wake cannot read while the settings writer is active");
            finishRefresh();
        }
        observe(false);
        check(port.bindCalls==binds&&!read(),"BUS does not open or synthesize input for the retained Host");
        observe(true);
        check(port.bindCalls==binds+1&&port.pending()&&!read(),"a permitted but silent port is not proof of restored input");
        const auto& opened=port.bindKeys.back();
        check(opened.power==oldPower+(cycle.advanceGeneration?1:0)&&opened.edge==retainedHost&&opened.wakeIntent==retainedIntent&&
            opened.wakeSuspend==oldSerial+1&&opened.wakeNotice==resume.sequence,
            "each resume uses the current generation and its own suspend serial and notice with the same Host and demand");
        if(!cycle.activeFirst){
            port.complete(up);++clockTick;observe(true);
            check(lane.hasSample()&&!read(),"a completed neutral first report cannot select the backup after resume");
        }
        port.complete(isolationFrame(button));++clockTick;observe(true);
        const auto polls=port.pollCalls;
        observe(false);
        check(read()&&output.pad.wButtons==button&&output.sequence>previousSelection&&port.pollCalls==polls,
            "BUS sees only the new published DOWN without taking the UI read completion");
        previousSelection=output.sequence;
        if(cycle.advanceGeneration){
            port.complete(up);++clockTick;observe(true);
            check(read()&&!output.pad.wButtons&&output.sequence>previousSelection&&fixtureHost==retainedHost,
                "a new UP releases the retained Host after a generation change");
            previousSelection=output.sequence;
        }
    }

    const auto reenterPower=power,reenterSerial=g_sharedPowerSuspendSerial.load(),reenterNotice=g_sleepPowerDispatch.observedSequence();
    const auto reenterBinds=port.bindCalls,reenterDiscarded=port.discardedFrames;
    fixtureFse=false;observe(false);
    check(!read()&&!port.bound()&&port.pending(),"FSE departure retires the current selected sample and starts return");
    fixtureFse=true;observe(true);
    check(!read()&&port.bindCalls==reenterBinds,"same-wake FSE reentry waits for the departing I/O to return");
    port.complete(down);++clockTick;observe(true);
    check(!read()&&port.bindCalls==reenterBinds+1&&port.discardedFrames==reenterDiscarded+1,
        "FSE reentry discards the departed DOWN before opening its fresh read");
    check(g_sharedPowerSuspendSerial==reenterSerial&&g_sleepPowerDispatch.observedSequence()==reenterNotice&&
        fixtureHost==retainedHost&&power==reenterPower,"FSE reentry did not require a sleep, Host change or power generation change");
    port.complete(isolationFrame(XINPUT_GAMEPAD_B));++clockTick;observe(true);
    const auto reenterPolls=port.pollCalls;
    observe(false);
    check(read()&&output.pad.wButtons==XINPUT_GAMEPAD_B&&output.sequence>previousSelection&&port.pollCalls==reenterPolls,
        "BUS can select only the fresh sample after same-wake FSE reentry");
    port.complete(up);++clockTick;observe(true);
    check(read()&&!output.pad.wButtons,"a fresh UP is preserved after FSE reentry");
    check(!read(native)&&!read(),"active XInput takes back selection without reviving the fresh HID cache");
    withdraw();port.complete(up);port.serviceClosing();
    check(port.returned(),"the repeated wakes and FSE reentry return every simulated resource");
}
static void checkPendingReader(){
    const auto permit=std::make_shared<Permit>(Permit{power,2,L"root",L"preferred",7});
    g_controllerIsolationHidPermit.store(permit);
    auto& port=reader.port();
    observe(true);
    port.complete(isolationFrame(XINPUT_GAMEPAD_A));++clockTick;observe(true);
    check(lane.hasSample()&&port.bound()&&port.pending(),"fixture reached a bound Win5 isolation reader with published DOWN");
    const auto beforePolls=port.pollCalls,beforeBegins=port.beginCalls;
    const Epoch slot{power,topology,4,5,0};
    const auto oldDown=ymcc::commonhid::Sample{isolationFrame(XINPUT_GAMEPAD_A).pad,1,clockTick};
    ymccCommonHid::lane.observe(slot,oldDown);
    beginQuery();observe(false);observe(true);
    check(haveKey&&port.bound()&&port.pending()&&!port.cancelCalls&&!lane.hasSample()&&retiredSamples==1&&
        port.pollCalls==beforePolls&&port.beginCalls==beforeBegins,"unclassified suspend retired the original Win5 HID read or continued reading");
    check(!sampleReadAllowed(true)&&!ymccCommonHid::lane.hasSample(),"pending notification allowed Raw or the direct cached DOWN");
    port.complete(isolationFrame(XINPUT_GAMEPAD_A));
    cancelQuery();observe(true);
    check(!lane.hasSample()&&!sampleReadAllowed(true)&&port.bindCalls==1&&port.beginCalls==beforeBegins+1&&
        port.discardedFrames==1&&port.readCancelRequests==0,
        "cancelled notification reused an old report or reopened the same handle");
    port.complete(isolationFrame(0));++clockTick;observe(true);
    ymcc::commonhid::Sample selected;
    check(lane.read(slot,sampleReadAllowed(true),{},selected,false)&&selected.pad.wButtons==0&&
        port.bindCalls==1&&port.cancelCalls==0,"fresh UP did not recover on the original handle");
    beginQuery();observe(false);
    cancelQuery();observe(true);observe(true);
    const auto beginsDuringDrain=port.beginCalls;
    check(!sampleReadAllowed(true)&&port.readCancelRequests==1&&port.pending(),"restart did not request one read cancellation");
    beginQuery();observe(true);
    cancelQuery();observe(true);
    check(port.beginCalls==beginsDuringDrain&&port.readCancelRequests==1,"overlapping query duplicated a read or cancellation before terminal");
    port.fail();observe(true);
    check(port.beginCalls==beginsDuringDrain+1&&port.bindCalls==1&&!sampleReadAllowed(true),"terminal cancellation did not restart on the original handle");
    port.complete(isolationFrame(0));++clockTick;observe(true);
    check(sampleReadAllowed(true)&&lane.hasSample(),"fresh receipt did not reopen arbitration");
    for(unsigned boundary=0;boundary<9;++boundary){
        const auto beforeCancel=port.cancelCalls;
        beginQuery();
        if(boundary==0)++g_sharedPowerSuspendSerial;
        if(boundary==1)virtualPriority=true;
        if(boundary==2)uiOwned=false;
        if(boundary==3)g_hardwareWriteGate=false;
        if(boundary==4)++g_msiNativeForegroundEdge;
        if(boundary==5)++topology;
        if(boundary==6)g_controllerIsolationHidPermit.store({});
        if(boundary==7)g_realStickTest.virtualTargetEnabled=true;
        if(boundary==8)g_powerLifecycle=PowerLifecycle::Suspended;
        observe(false);
        check(!haveKey&&!lane.hasSample()&&port.cancelCalls==beforeCancel+1&&!port.bound(),
            "real boundary did not retire the read while notification was pending");
        port.complete(isolationFrame(XINPUT_GAMEPAD_A));observe(false);
        check(port.returned()&&!lane.hasSample()&&!haveKey,"terminal cancellation did not discard the retired frame or reopened while pending");
        cancelQuery();g_sharedPowerSuspendSerial=7;virtualPriority=false;
        uiOwned=true;g_hardwareWriteGate=true;g_msiNativeForegroundEdge=2;topology=3;
        g_controllerIsolationHidPermit.store(permit);g_realStickTest.virtualTargetEnabled=false;
        g_powerLifecycle=PowerLifecycle::Ready;
        observe(true);
        port.complete(isolationFrame(0));++clockTick;observe(true);
        check(port.bound()&&lane.hasSample(),"new read did not re-enter with current readiness");
    }
    withdraw();port.complete(isolationFrame(0));port.serviceClosing();
    check(port.returned(),"fixture retained a Win5 HID resource");
}
static void checkCoalescedReader(){
    g_controllerIsolationHidPermit.store(std::make_shared<Permit>(Permit{power,2,L"root",L"preferred",7}));
    auto& port=reader.port();
    const Epoch slot{power,topology,4,5,0};
    ymcc::commonhid::Sample selected;
    const auto oldFrame=isolationFrame(XINPUT_GAMEPAD_A);
    observe(true);
    port.complete(isolationFrame(0));++clockTick;observe(true);
    check(lane.read(slot,sampleReadAllowed(true),{},selected,false)&&port.bound(),"fixture reached the admitted reader");
    const auto sleepBefore=g_sleepPowerDispatch.observedSleepBoundarySequence();
    port.complete(oldFrame);
    ymccCommonHid::lane.observe(slot,{oldFrame.pad,1,clockTick});
    coalesceQuery();
    check(!g_sleepPowerDispatch.pendingSuspend()&&g_sleepPowerDispatch.observedSleepBoundarySequence()>sleepBefore&&
        g_sharedPowerSuspendSerial==7,"both notifications finished before polling without changing the accepted sleep serial");
    observe(true);
    check(!lane.hasSample()&&!ymccCommonHid::lane.hasSample()&&!sampleReadAllowed(true)&&port.discardedFrames==1&&
        port.bindCalls==1&&port.cancelCalls==0,"coalesced query published the report or kept cached input across the observed sleep boundary");
    port.complete(isolationFrame(0));++clockTick;observe(true);
    check(lane.read(slot,sampleReadAllowed(true),{},selected,false)&&!selected.pad.wButtons&&port.bindCalls==1,
        "same handle did not accept a fresh neutral report after the coalesced query");
    port.complete(oldFrame);port.duringPoll=coalesceQuery;++clockTick;observe(true);
    check(!lane.hasSample()&&!sampleReadAllowed(true)&&port.bound()&&port.bindCalls==1,
        "a query coalesced inside the read returned the old DOWN or reopened the device");
    port.complete(oldFrame);++clockTick;observe(true);
    check(!lane.hasSample()&&port.discardedFrames==2,"replacement read launched before rechecking the boundary returned its crossed frame");
    port.complete(isolationFrame(0));++clockTick;observe(true);
    check(lane.read(slot,sampleReadAllowed(true),{},selected,false)&&!selected.pad.wButtons&&port.bindCalls==1,
        "normal sampling did not recover on the original reader");
    port.complete(oldFrame);++clockTick;observe(true);
    const auto beforeSelectionRetirements=retiredSamples;
    coalesceQuery();
    check(!sampleReadAllowed(true)&&!lane.hasSample()&&retiredSamples==beforeSelectionRetirements+1&&port.bound(),
        "query after observation must retire the direct sample before it can bypass the UI release handoff");
    g_msiNativeForegroundVirtualYieldPending=true;coalesceQuery();observe(false);
    check(!haveKey&&!lane.hasSample()&&!port.bound()&&port.cancelCalls==1,
        "coalesced notification must not retain the reader against virtual priority");
    port.complete(oldFrame);observe(false);
    check(port.returned()&&!lane.hasSample(),"virtual yield leaked the retiring terminal frame");
}
static void checkFirstReportReader(){
    g_controllerIsolationHidPermit.store(std::make_shared<Permit>(Permit{power,2,L"root",L"preferred",7}));
    auto& port=reader.port();
    const Epoch slot{power,topology,4,5,0};
    ymcc::commonhid::Sample selected;
    port.duringBegin=[](IsolationFakePort& pending){pending.complete(isolationFrame(XINPUT_GAMEPAD_A));};
    observe(true);
    check(lane.read(slot,sampleReadAllowed(true),{},selected,false)&&selected.pad.wButtons==XINPUT_GAMEPAD_A&&
        port.beginCalls==2&&port.deliveredFrames==1,"ready first report must reach YMCC in its first evaluation");
    withdraw();port.complete(isolationFrame(0));port.serviceClosing();
    port.duringBegin=[](IsolationFakePort& pending){
        coalesceQuery();pending.complete(isolationFrame(XINPUT_GAMEPAD_A));
    };
    ++clockTick;observe(true);
    check(!lane.hasSample()&&!sampleReadAllowed(true)&&port.bound()&&port.deliveredFrames==2,
        "same-evaluation acquisition must discard a report that crossed a sleep notification");
    port.complete(isolationFrame(XINPUT_GAMEPAD_A));
    port.duringBegin=[](IsolationFakePort& pending){pending.complete(isolationFrame(0));};
    ++clockTick;observe(true);
    check(lane.read(slot,sampleReadAllowed(true),{},selected,false)&&!selected.pad.wButtons&&port.bindCalls==2&&
        port.discardedFrames==2,"first fresh neutral report after draining must win on the same handle");
    withdraw();port.complete(isolationFrame(0));port.serviceClosing();
    check(port.returned(),"fast first read still returns its original resources");
}
}
#endif
static void checkIsolationHidReader(){
    using namespace ymcc::isolationhid;
    using Frame=ymcc::controllerhid::Frame;
    using TestReader=Reader<IsolationFakePort>;
    Key key;key.power=1;key.edge=9;key.topology=2;key.identity=3;key.occupant=4;key.slot=0;
    key.root=L"USB\\exact-selected-root";key.preferredPath=L"\\\\?\\hid#exact-selected-collection";
    key.slotPath=L"\\\\?\\usb#exact-selected-source";
    const Permit permit{key.power,key.edge,key.root,key.preferredPath};
    Scene scene;scene.xboxFse=scene.originalIsolation=scene.physicalSource=scene.virtualOff=scene.powerReady=true;
    scene.power=key.power;scene.edge=key.edge;
    check(admitted(permit,scene),"FSE original isolation admits exact physical virtual-OFF Ready scene");
    const auto rejectScene=[&](const Permit& rejectedPermit,const Scene& rejectedScene,const char* message){
        check(!admitted(rejectedPermit,rejectedScene),message);
        TestReader reader;Frame frame;std::uint64_t sequence=0,observedTick=0;
        check(!reader.read(admitted(rejectedPermit,rejectedScene)?&key:nullptr,100,frame,sequence,observedTick),"denied scene cannot return a frame");
        check(reader.port().bindCalls==0&&reader.port().beginCalls==0&&reader.port().pollCalls==0&&reader.port().returned(),"denied scene never opens or starts a device");
    };
    auto rejectedScene=scene;rejectedScene.xboxFse=false;rejectScene(permit,rejectedScene,"non-FSE retains original input route");
    rejectedScene=scene;rejectedScene.originalIsolation=false;rejectScene(permit,rejectedScene,"isolation OFF excluded");
    rejectedScene=scene;rejectedScene.physicalSource=false;rejectScene(permit,rejectedScene,"unproven physical source excluded");
    rejectedScene=scene;rejectedScene.virtualOff=false;rejectScene(permit,rejectedScene,"virtual ON has priority over isolation reader");
    rejectedScene=scene;rejectedScene.powerReady=false;rejectScene(permit,rejectedScene,"not Ready excluded");
    rejectedScene=scene;rejectedScene.stopping=true;rejectScene(permit,rejectedScene,"exit fence excluded");
    rejectedScene=scene;rejectedScene.higherPriority=true;rejectScene(permit,rejectedScene,"higher-priority recovery excludes isolation reader");
    rejectedScene=scene;++rejectedScene.power;rejectScene(permit,rejectedScene,"different power generation excluded");
    rejectedScene=scene;++rejectedScene.edge;rejectScene(permit,rejectedScene,"different foreground edge excluded");
    auto rejectedPermit=permit;rejectedPermit.power=0;rejectScene(rejectedPermit,scene,"zero permit power excluded");
    rejectedPermit=permit;rejectedPermit.edge=0;rejectScene(rejectedPermit,scene,"zero permit edge excluded");
    rejectedPermit=permit;rejectedPermit.root.clear();rejectScene(rejectedPermit,scene,"empty physical root excluded");
    {
        TestReader reader;auto& port=reader.port();Frame frame;std::uint64_t sequence=0,observedTick=0;
        check(!reader.read(&key,100,frame,sequence,observedTick)&&port.bindCalls==1&&port.beginCalls==1&&port.pending(),"first read binds and begins without inventing a sample");
        for(const auto now:{101ull,120ull,1000ull}){
            check(!reader.read(&key,now,frame,sequence,observedTick)&&port.beginCalls==1&&port.deliveredFrames==0,"pending read is not restarted or counted as a report");
        }
        port.complete(isolationFrame(XINPUT_GAMEPAD_A));
        check(reader.read(&key,1010,frame,sequence,observedTick)&&frame.pad.wButtons==XINPUT_GAMEPAD_A&&sequence==1&&observedTick==1010,"actual DOWN completion publishes its sequence and observation time");
        const auto downSequence=sequence,downTick=observedTick;
        check(port.beginCalls==2&&port.pending(),"completed read rearms exactly one successor");
        for(const auto now:{1020ull,1030ull,5000ull}){
            check(reader.read(&key,now,frame,sequence,observedTick)&&frame.pad.wButtons==XINPUT_GAMEPAD_A&&sequence==downSequence&&observedTick==downTick,"held cache does not become a new report or refresh its timestamp");
            check(port.beginCalls==2&&port.deliveredFrames==1,"held cache does not begin another pending read");
        }
        port.complete(isolationFrame(0));
        check(reader.read(&key,5010,frame,sequence,observedTick)&&frame.pad.wButtons==0&&frame.pad.sThumbLX==0&&sequence==downSequence+1&&observedTick==5010,"real neutral UP replaces held DOWN with a new actual report");
        const auto upSequence=sequence,upTick=observedTick;
        check(reader.read(&key,5020,frame,sequence,observedTick)&&frame.pad.wButtons==0&&sequence==upSequence&&observedTick==upTick,"cached neutral UP also keeps its original report identity");
        port.fail();
        check(!reader.read(&key,5030,frame,sequence,observedTick)&&port.returned(),"failed read revokes held sample and returns completed resources");
        check(!reader.read(&key,5040,frame,sequence,observedTick),"a rebound pending read cannot reuse a pre-failure sample");
        port.fail();check(reader.retire()&&port.returned(),"rebound reader retires after cancellation completion");
    }
    {
        TestReader reader;auto& port=reader.port();port.beginAllowed=false;
        Frame frame;std::uint64_t sequence=0,observedTick=0;
        check(!reader.read(&key,100,frame,sequence,observedTick)&&port.beginCalls==1&&port.returned(),"begin failure does not publish or retain an unstarted read");
    }
    std::vector<std::pair<Key,const char*>> changedKeys;
    const auto addKey=[&](auto change,const char* label){auto changed=key;change(changed);changedKeys.emplace_back(std::move(changed),label);};
    addKey([](Key& changed){++changed.topology;},"topology change revokes old sample");
    addKey([](Key& changed){++changed.identity;},"identity change revokes old sample");
    addKey([](Key& changed){++changed.occupant;},"occupant change revokes old sample");
    addKey([](Key& changed){changed.slot=1;},"slot change revokes old sample");
    addKey([](Key& changed){++changed.edge;},"edge change revokes old sample");
    addKey([](Key& changed){++changed.power;},"power change revokes old sample");
    addKey([](Key& changed){changed.root+=L"-new";},"physical root change revokes old sample");
    addKey([](Key& changed){changed.slotPath+=L"-new";},"exact slot path change revokes old sample");
    addKey([](Key& changed){changed.preferredPath+=L"-new";},"preferred collection change revokes old sample");
    for(const auto& [changed,label]:changedKeys){
        TestReader reader;auto& port=reader.port();Frame frame;std::uint64_t sequence=0,observedTick=0;
        check(!reader.read(&key,100,frame,sequence,observedTick),"generation test starts with a pending read");
        port.complete(isolationFrame(XINPUT_GAMEPAD_A));
        check(reader.read(&key,110,frame,sequence,observedTick)&&frame.pad.wButtons==XINPUT_GAMEPAD_A,"generation test holds old physical sample");
        const auto oldSequence=sequence;
        check(!reader.read(&changed,120,frame,sequence,observedTick),label);
        check(port.bindCalls==1&&port.beginCalls==2&&port.cancelCalls==1&&port.pending()&&!port.returned(),"generation change cannot rebind before cancellation completes");
        check(!reader.read(&changed,125,frame,sequence,observedTick)&&port.bindCalls==1&&port.cancelCalls==1,"repeated new key cannot duplicate close or bind while draining");
        port.complete(isolationFrame(XINPUT_GAMEPAD_X));
        check(!reader.read(&changed,130,frame,sequence,observedTick)&&port.bindCalls==2&&port.bindKeys.back()==changed,"late old report is drained before exact new-key bind");
        check(port.discardedFrames==1&&port.deliveredFrames==1&&port.beginCalls==3,"cancelled late report never reaches sample lane");
        port.complete(isolationFrame(XINPUT_GAMEPAD_B));
        check(reader.read(&changed,140,frame,sequence,observedTick)&&frame.pad.wButtons==XINPUT_GAMEPAD_B&&sequence==oldSequence+1&&observedTick==140,"new generation publishes only its own actual completion");
        port.fail();check(reader.retire()&&port.returned(),"generation test returns its final pending read");
    }
    {
        TestReader reader;auto& port=reader.port();Frame frame;std::uint64_t sequence=0,observedTick=0;
        check(!reader.read(&key,100,frame,sequence,observedTick),"scene exit test starts reader");
        port.complete(isolationFrame(XINPUT_GAMEPAD_A));
        check(reader.read(&key,110,frame,sequence,observedTick),"scene exit test has an admitted sample");
        auto outside=scene;outside.xboxFse=false;
        check(!reader.read(admitted(permit,outside)?&key:nullptr,120,frame,sequence,observedTick)&&port.cancelCalls==1,"leaving FSE immediately withdraws the admitted sample");
        port.complete(isolationFrame(XINPUT_GAMEPAD_B));
        check(!reader.read(admitted(permit,outside)?&key:nullptr,130,frame,sequence,observedTick)&&port.returned()&&port.discardedFrames==1&&port.bindCalls==1,"non-FSE drains late input without opening another device");
    }
    {
        TestReader reader;auto& port=reader.port();Frame frame;std::uint64_t sequence=0,observedTick=0;
        check(!reader.read(&key,100,frame,sequence,observedTick),"exit test starts one pending operation");
        check(!reader.retire()&&!port.returned()&&port.cancelCalls==1,"exit retains pending resources until actual completion");
        check(!reader.retire()&&port.cancelCalls==1&&port.bindCalls==1,"repeated exit does not duplicate cancellation or reopen a device");
        port.fail();check(reader.retire()&&port.returned(),"exit returns resources after cancelled I/O completes");
        check(reader.retire()&&port.returned()&&port.bindCalls==1,"completed exit remains idempotent");
    }
    {
        TestReader reader;auto& port=reader.port();port.bindAllowed=false;
        Frame frame;std::uint64_t sequence=0,observedTick=0;unsigned attempts=0;
        for(const auto now:{100ull,350ull,600ull,850ull}){
            ++attempts;
            check(!reader.read(&key,now,frame,sequence,observedTick)&&port.bindCalls==attempts,"failed bind consumes one of four attempts at the 250ms boundary");
            for(const auto beforeRetry:{now+1,now+249}){
                check(!reader.read(&key,beforeRetry,frame,sequence,observedTick)&&port.bindCalls==attempts,"failed bind cannot retry inside its 250ms interval");
            }
        }
        for(const auto now:{1100ull,10000ull,100000ull}){
            check(!reader.read(&key,now,frame,sequence,observedTick)&&port.bindCalls==4&&port.beginCalls==0,"same key never retries after four failed binds");
        }
        auto changed=key;++changed.identity;
        for(const auto now:{100001ull,100251ull,100501ull,100751ull}){
            ++attempts;
            check(!reader.read(&changed,now,frame,sequence,observedTick)&&port.bindCalls==attempts&&port.bindKeys.back()==changed,"new exact key receives its own bounded bind admission");
            check(!reader.read(&changed,now+249,frame,sequence,observedTick)&&port.bindCalls==attempts,"new key also obeys the 250ms retry interval");
        }
        check(!reader.read(&changed,200000,frame,sequence,observedTick)&&port.bindCalls==8,"new key budget is bounded independently");
        check(reader.retire()&&port.returned(),"failed bind path retains no device resources");
    }
    std::vector<std::pair<Key,const char*>> invalidKeys;
    const auto addInvalid=[&](auto change,const char* label){auto invalid=key;change(invalid);invalidKeys.emplace_back(std::move(invalid),label);};
    addInvalid([](Key& invalid){invalid.power=0;},"zero key power rejected");
    addInvalid([](Key& invalid){invalid.edge=0;},"zero key edge rejected");
    addInvalid([](Key& invalid){invalid.topology=0;},"zero key topology rejected");
    addInvalid([](Key& invalid){invalid.identity=0;},"zero key identity rejected");
    addInvalid([](Key& invalid){invalid.occupant=0;},"zero key occupant rejected");
    addInvalid([](Key& invalid){invalid.slot=4;},"out-of-range key slot rejected");
    addInvalid([](Key& invalid){invalid.slot=~0u;},"large out-of-range key slot rejected");
    addInvalid([](Key& invalid){invalid.root.clear();},"empty key root rejected");
    addInvalid([](Key& invalid){invalid.slotPath.clear();},"empty key slot path rejected");
    for(const auto& [invalid,label]:invalidKeys){
        TestReader reader;auto& port=reader.port();Frame frame;std::uint64_t sequence=0,observedTick=0;
        check(!reader.read(&invalid,100,frame,sequence,observedTick),label);
        check(port.bindCalls==0&&port.beginCalls==0&&port.pollCalls==0&&port.returned(),"invalid key cannot open or poll a device");
        check(!reader.read(&key,110,frame,sequence,observedTick),"valid key may enter after an invalid key was refused");
        port.complete(isolationFrame(XINPUT_GAMEPAD_A));
        check(reader.read(&key,120,frame,sequence,observedTick),"invalid key retirement test holds a valid prior sample");
        check(!reader.read(&invalid,130,frame,sequence,observedTick)&&port.cancelCalls==1&&port.bindCalls==1,"invalid key revokes a prior valid held sample without rebinding");
        port.complete(isolationFrame(XINPUT_GAMEPAD_X));
        check(!reader.read(&invalid,140,frame,sequence,observedTick)&&port.discardedFrames==1&&port.returned()&&port.bindCalls==1,"invalid key cannot revive a cancelled late sample");
    }
}
int main(int argc,char** argv)try {
    if(argc>1){
#if defined(YMCC_ISOLATION_HID_PORT_INCLUDE)
        if(argc==2&&std::string_view(argv[1])=="--isolation-hid-idle-withdrawal"){
            isolationPortTest::checkIdleWithdrawal();
            std::cout<<json{{"checks",checks},{"scope","production Port idle withdrawal with fake system APIs"},{"physicalInput",false}}.dump()<<'\n';return 0;
        }
#endif
#if defined(YMCC_ISOLATION_HID_OBSERVE_INCLUDE)
        if(argc==2&&(std::string_view(argv[1])=="--isolation-hid-pending-reader" ||
                     std::string_view(argv[1])=="--isolation-hid-coalesced-reader" ||
                     std::string_view(argv[1])=="--isolation-hid-first-report" ||
                     std::string_view(argv[1])=="--isolation-hid-win5-kept-wake")){
            if(std::string_view(argv[1])=="--isolation-hid-pending-reader")pendingIsolationReader::checkPendingReader();
            else if(std::string_view(argv[1])=="--isolation-hid-coalesced-reader")pendingIsolationReader::checkCoalescedReader();
            else if(std::string_view(argv[1])=="--isolation-hid-first-report")pendingIsolationReader::checkFirstReportReader();
            else pendingIsolationReader::checkWin5WakeReader();
            json result{{"checks",checks},{"scope","actual isolation reader observation and shared dispatch with fake HID I/O"},{"physicalInput",false}};
            if(std::string_view(argv[1])=="--isolation-hid-win5-kept-wake")result["selectedCase"]=argv[1];
            std::cout<<result.dump()<<'\n';return 0;
        }
#endif
        if(argc!=2||std::string(argv[1])!="--isolation-hid-port")throw std::runtime_error("unknown selftest selection");
#if defined(YMCC_ISOLATION_HID_PORT_INCLUDE)
#if !defined(YMCC_ISOLATION_HID_PORT_R3_ONLY)
        isolationPortTest::checkPort();
#endif
#if defined(YMCC_ISOLATION_HID_PORT_R3)
        isolationPortTest::checkReenumeration();
        isolationPortTest::checkBindLanes();
#endif
#if defined(YMCC_ISOLATION_HID_PORT_R3_ONLY)
        std::cout<<json{{"checks",checks},{"scope","R3 production Port reenumeration and bounded inventory only"},{"fixtureRevision",isolationPortTest::fixtureRevision},{"physicalInput",false},{"original297Executed",false},{"r2PortCasesExecuted",false}}.dump()<<'\n';return 0;
#else
        std::cout<<json{{"checks",checks},{"scope","mechanically extracted production Windows Port, OEM lifecycle and Device with explicit fake system APIs"},{"physicalInput",false},{"original297Executed",false},{"fixedBoundaries",{"completed-UP-delivered-once-on-next-begin-failure","allocation-exceptions-close-and-recover","actual-retirement-clears-Raw-and-UI-handoff-clears-Background"}},{"allocationFaultInjected",true}}.dump()<<'\n';return 0;
#endif
#else
        throw std::runtime_error("Windows Port selection requires mechanically extracted source include pins");
#endif
    }
    using namespace ymccBackgroundUi;
    g_axisCalibrationXinputKeys[0]=std::make_shared<const std::string>("\\\\?\\usb#actual-selected-source");
    using namespace ymcc::input::identityRefresh;
    g_xinputFeedbackIdentity[0].observe({true,true,false,false,L"actual-selected-source",Kind::Physical,3,3,1,1});
    Epoch e;check(context(e)&&e.slot==0&&e.episode,"actual production context with current physical proof");
    const auto rearm=[&] {lane.clear();lane.observe(e,{});XINPUT_GAMEPAD p{};p.wButtons=XINPUT_GAMEPAD_A;lane.observe(e,p);};
    rearm();{BackgroundControllerUiScope scope;check(g_curW==XINPUT_GAMEPAD_A&&g_curPad.wButtons==g_curW,"UI sees raw state");
        check(g_inputCaptureGamepadSnapshot.pad.wButtons==0,"BUS snapshot untouched");}
    check(g_curW==0&&g_curPad.wButtons==0,"original tuple restored at scope exit");
    const auto early=[&] {BackgroundControllerUiScope scope;check(g_curW==XINPUT_GAMEPAD_A,"early return receives raw");return;};
    early();check(!g_curW&&!g_curPad.wButtons,"early return restores original tuple");
    try{BackgroundControllerUiScope scope;throw 5;}catch(int){}
    check(!g_curW&&!g_curPad.wButtons,"exception restores original tuple");
    g_curPad.bLeftTrigger=24;g_curPad.bRightTrigger=88;
    {BackgroundControllerUiScope scope;check(g_curPad.bLeftTrigger==24&&g_curPad.bRightTrigger==88,"native triggers survive override");}
    check(g_curPad.bLeftTrigger==24&&g_curPad.bRightTrigger==88,"native triggers restored");g_curPad={};
    enabled=false;{BackgroundControllerUiScope scope;check(!g_curW&&!lane.hasSample(),"old presentation route never consumes fallback");}enabled=true;
    check(g_inputReleaseRequired&&g_gamepadAxisNeutralAdmissionRequired,"retiring raw lane uses original neutral handoff guards");
    g_inputReleaseRequired=false;g_gamepadAxisNeutralAdmissionRequired=false;rearm();
    {BackgroundControllerUiScope scope(true);check(g_curW==XINPUT_GAMEPAD_A&&g_curPad.wButtons==g_curW,"explicit allow keeps original Raw UI override");}
    check(!g_curW&&!g_curPad.wButtons,"explicit allow restores the original neutral tuple");
    {BackgroundControllerUiScope scope(false);
        check(!g_curW&&!g_curPad.wButtons,"disallowed old Raw held DOWN cannot overwrite current direct neutral UP");
        check(!lane.hasSample(),"disallow retires the old Raw UI lane");
        check(!g_inputCaptureGamepadSnapshot.pad.wButtons&&g_inputCaptureGamepadSnapshot.commonHidReportSequence==0,"scope admission neither changes BUS data nor labels direct input as Raw");}
    check(!g_curW&&!g_curPad.wButtons,"disallow restores the original neutral tuple");
    check(g_inputReleaseRequired&&g_gamepadAxisNeutralAdmissionRequired,"disallow preserves original release and axis-neutral retirement barriers");
    rearm();{BackgroundControllerUiScope scope(true);check(g_curW==XINPUT_GAMEPAD_A,"explicit allow can rearm the original Raw UI route");}
    g_curW=XINPUT_GAMEPAD_B;g_curPad.wButtons=g_curW;g_curPad.sThumbLX=17000;g_curPad.bLeftTrigger=37;g_curPad.bRightTrigger=91;
    {BackgroundControllerUiScope scope(false);
        check(g_curW==XINPUT_GAMEPAD_B&&g_curPad.wButtons==XINPUT_GAMEPAD_B&&g_curPad.sThumbLX==17000,"disallow preserves the current nonneutral source tuple instead of old Raw DOWN");
        check(g_curPad.bLeftTrigger==37&&g_curPad.bRightTrigger==91,"disallow preserves current independent trigger values");}
    check(g_curW==XINPUT_GAMEPAD_B&&g_curPad.wButtons==XINPUT_GAMEPAD_B&&g_curPad.sThumbLX==17000&&g_curPad.bLeftTrigger==37&&g_curPad.bRightTrigger==91,"disallow scope exit restores the exact current source tuple");
    g_curW=0;g_curPad={};rearm();
    {BackgroundControllerUiScope scope;check(g_curW==XINPUT_GAMEPAD_A&&g_curPad.wButtons==g_curW,"default admission still permits the original non-direct Raw UI route");}
    check(!g_curW&&!g_curPad.wButtons,"default admission still restores the original tuple");
    rearm();g_xinputVirtualEndpointMask=1;check(!context(e),"virtual endpoint excluded");g_xinputVirtualEndpointMask=0;
    g_xinputSlotUnknownMask=1;check(!context(e),"unknown endpoint excluded");g_xinputSlotUnknownMask=0;
    g_xinputSlotReprobeMask=1;check(!context(e),"reprobe pending excluded");g_xinputSlotReprobeMask=0;
    g_xinputSlotIdentityResolvedEpoch[0]=2;check(!context(e),"old slot identity excluded");g_xinputSlotIdentityResolvedEpoch[0]=3;
    g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=2;check(!context(e),"other pinned controller excluded");g_xinputPhysicalSourceMask=1;
    g_padConnected=false;check(!context(e),"disconnected excluded");g_padConnected=true;
    g_inputCaptureGamepadSnapshot.inputSource="msi-dinput";check(!context(e),"other physical source owner excluded");g_inputCaptureGamepadSnapshot.inputSource="xinput";
    g_powerLifecycle=PowerLifecycle::Suspended;check(!context(e),"sleep excluded");g_powerLifecycle=PowerLifecycle::Ready;
    ++power;check(!context(e),"old power snapshot excluded");--power;
    ++g_xinputTopologyEpoch;check(!context(e),"old topology snapshot excluded");--g_xinputTopologyEpoch;
    g_inputCaptureGamepadSnapshot.readContextStable=false;check(!context(e),"unstable read excluded");g_inputCaptureGamepadSnapshot.readContextStable=true;
    check(context(e),"context restored");rearm();invalidate();check(!lane.hasSample()&&devices.empty(),"device event clears cache and held state");
    RAWINPUT raw{};raw.header.dwType=RIM_TYPEHID;raw.header.dwSize=sizeof(raw);raw.data.hid.dwSizeHid=15;
    check(!backgroundControllerUiCapture(&raw,sizeof(raw)),"zero report count rejected");raw.data.hid.dwCount=0xffffffffu;
    check(!backgroundControllerUiCapture(&raw,sizeof(raw)),"overflow count rejected");raw.data.hid.dwCount=1;
    check(!backgroundControllerUiCapture(&raw,1),"truncated packet rejected");
    check(!backgroundControllerUiCapture(nullptr,0),"null packet rejected");
    check(context(e),"physical context before malformed packet");rearm();
    boundDevice=reinterpret_cast<HANDLE>(std::uintptr_t{1});raw.header.hDevice=reinterpret_cast<HANDLE>(std::uintptr_t{2});raw.data.hid.dwCount=0;
    check(!backgroundControllerUiCapture(&raw,sizeof(raw))&&lane.hasSample(),"other device's malformed report cannot clear selected input");
    raw.header.hDevice=boundDevice;
    check(!backgroundControllerUiCapture(&raw,sizeof(raw))&&!lane.hasSample(),"malformed selected release clears held state");
    invalidate();check(!boundDevice,"device boundary clears exact raw collection binding");
    check(!sameXusbSource(L"not-an-interface",L"not-a-controller"),"unresolved physical binding rejected");
    check(evaluations==0,"invalid reports do not create semantic callbacks");
    checkIsolationHidReader();
    checkCommonHidLane();
    std::cout<<json{{"checks",checks},{"scope","production runtime context, scoped override, packet rejection, isolation HID Reader with FakePort and common HID Lane authority"},
        {"physicalInput",false},{"busSnapshotUnchanged",true}}.dump()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
