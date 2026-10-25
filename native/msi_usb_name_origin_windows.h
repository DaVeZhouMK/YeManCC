#pragma once
#include "msi_usb_name_origin_context.h"
#include "msi_usb_name_origin_decode.h"
#include "msi_usb_name_origin_join.h"
#include "msi_usb_name_origin_io.h"
#include <cfgmgr32.h>
#include <devpkey.h>
#include <usbiodef.h>
#include <set>
#include <optional>
#include <cstring>
namespace ymcc::msi::usbName {
inline std::wstring wide(const std::string&s){const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);if(n<=0)return{};std::wstring w(size_t(n),L'\0');if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),w.data(),n)!=n)return{};return w;}
inline std::string utf8(const std::wstring&w){const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);if(n<=0)return{};std::string s(size_t(n),'\0');if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),int(w.size()),s.data(),n,nullptr,nullptr)!=n)return{};return s;}
inline Json contextFacts(const Context& c){return{{"version",c.version},{"nativePid",c.nativePid},{"parentCreationTime",c.parentCreationTime},{"sourceGeneration",c.sourceGeneration},{"powerGeneration",c.powerGeneration},{"hostEpoch",c.hostEpoch},{"observedTick",c.observedTick},{"boardAuthority",c.boardAuthorized==1},{"sourceValid",c.sourceValid==1},{"board",text(c.board)},{"usbRoot",text(c.usbRoot)},{"interfacePath",text(c.interfacePath)},{"phase",text(c.phase)}};}
struct RootBinding{bool ok=false;DEVINST node=0,parent=0;std::wstring rootId,parentId,driverKey,hubPath;Json facts;};
inline RootBinding resolveRoot(const std::string& root){RootBinding b;b.rootId=wide(root);b.facts={{"requestedRoot",root},{"ioWrites",false}};
    if(b.rootId.empty()){b.facts["state"]="invalid-root-text";return b;}
    const auto locate=CM_Locate_DevNodeW(&b.node,b.rootId.data(),CM_LOCATE_DEVNODE_NORMAL);b.facts["locateConfigRet"]=locate;if(locate!=CR_SUCCESS)return b;
    wchar_t observed[512]{};const auto idRc=CM_Get_Device_IDW(b.node,observed,512,0);b.facts["idConfigRet"]=idRc;
    if(idRc!=CR_SUCCESS||asciiUpper(utf8(observed))!=asciiUpper(root)){b.facts["state"]="root-node-identity-differs";return b;}
    ULONG status=0,problem=0;const auto statusRc=CM_Get_DevNode_Status(&status,&problem,b.node,0);b.facts["statusConfigRet"]=statusRc;b.facts["status"]=status;b.facts["problem"]=problem;
    if(statusRc!=CR_SUCCESS||!(status&DN_STARTED)||problem){b.facts["state"]="root-not-present-started";return b;}
    wchar_t driver[512]{};DEVPROPTYPE type=0;ULONG bytes=sizeof(driver);const auto driverRc=CM_Get_DevNode_PropertyW(b.node,&DEVPKEY_Device_Driver,&type,reinterpret_cast<PBYTE>(driver),&bytes,0);
    b.facts["driverKeyConfigRet"]=driverRc;if(driverRc!=CR_SUCCESS||type!=DEVPROP_TYPE_STRING||bytes<sizeof(wchar_t)||bytes>sizeof(driver)||bytes%sizeof(wchar_t)||driver[bytes/sizeof(wchar_t)-1]!=0||!driver[0])return b;
    b.driverKey=driver;b.facts["driverKey"]=utf8(b.driverKey);
    const auto parentRc=CM_Get_Parent(&b.parent,b.node,0);b.facts["parentConfigRet"]=parentRc;if(parentRc!=CR_SUCCESS)return b;
    wchar_t parentId[512]{};const auto parentIdRc=CM_Get_Device_IDW(b.parent,parentId,512,0);b.facts["parentIdConfigRet"]=parentIdRc;if(parentIdRc!=CR_SUCCESS||!parentId[0])return b;b.parentId=parentId;b.facts["parentInstance"]=utf8(b.parentId);
    ULONG size=0;const auto listSizeRc=CM_Get_Device_Interface_List_SizeW(&size,const_cast<GUID*>(&GUID_DEVINTERFACE_USB_HUB),b.parentId.data(),CM_GET_DEVICE_INTERFACE_LIST_PRESENT);b.facts["hubListSizeConfigRet"]=listSizeRc;
    if(listSizeRc!=CR_SUCCESS||size<2||size>32768)return b;std::vector<wchar_t> list(size,L'\0');
    const auto listRc=CM_Get_Device_Interface_ListW(const_cast<GUID*>(&GUID_DEVINTERFACE_USB_HUB),b.parentId.data(),list.data(),size,CM_GET_DEVICE_INTERFACE_LIST_PRESENT);b.facts["hubListConfigRet"]=listRc;if(listRc!=CR_SUCCESS)return b;
    size_t first=0;while(first<list.size()&&list[first])++first;if(!first||first+1>=list.size()||list[first+1]!=0){b.facts["state"]="hub-interface-not-unique-or-incomplete";return b;}
    b.hubPath.assign(list.data(),first);b.facts["hubInterface"]=utf8(b.hubPath);b.ok=true;b.facts["state"]="root-parent-hub-bound";return b;
}
inline std::vector<uint8_t> descriptorRequest(unsigned port,unsigned type,unsigned index,unsigned language,unsigned bytes){const size_t offset=offsetof(USB_DESCRIPTOR_REQUEST,Data);std::vector<uint8_t> data(offset+bytes,0);auto*r=reinterpret_cast<USB_DESCRIPTOR_REQUEST*>(data.data());r->ConnectionIndex=port;r->SetupPacket.bmRequest=0x80;r->SetupPacket.bRequest=6;r->SetupPacket.wValue=USHORT((type<<8)|index);r->SetupPacket.wIndex=USHORT(language);r->SetupPacket.wLength=USHORT(bytes);return data;}
inline bool driverKey(const IoReply&r,std::wstring&key){key.clear();const size_t offset=offsetof(USB_NODE_CONNECTION_DRIVERKEY_NAME,DriverKeyName);if(!r.ok||r.bytes.size()<offset+sizeof(wchar_t))return false;
    const auto*p=reinterpret_cast<const USB_NODE_CONNECTION_DRIVERKEY_NAME*>(r.bytes.data());if(p->ActualLength<offset+sizeof(wchar_t)||p->ActualLength>r.bytes.size()||p->ActualLength>4096||(p->ActualLength-offset)%sizeof(wchar_t))return false;
    // WDK ActualLength bounds the structure; USBView reads a NUL-terminated
    // DriverKeyName within it. A terminator need not occupy the final WCHAR.
    const auto*value=reinterpret_cast<const wchar_t*>(r.bytes.data()+offset);const size_t count=(p->ActualLength-offset)/sizeof(wchar_t);
    size_t n=0;while(n<count&&value[n])++n;if(!n||n==count)return false;key.assign(value,n);return true;
}
// Query only the already bound parent hub. Unknown/error ports still reject the
// exact join; only a successful NoDeviceConnected receipt needs no driver key.
// Injected callbacks exercise this same production scan without platform I/O.
// Diagnostic-only copy of returned header bytes. Never supplies admission data:
// exactRootPort below retains its original size/index/status/ownership gates.
inline Json connectionHeaderFacts(const IoReply& reply,unsigned requested){
    constexpr size_t minimum=offsetof(USB_NODE_CONNECTION_INFORMATION_EX,PipeList);
    const size_t bounded=std::min(reply.bytes.size(),minimum);
    Json facts={{"returnedHeaderBytes",bounded},{"minHeaderBytes",minimum},{"connectionQueryOk",reply.ok},
        {"headerConnectionIndex",nullptr},{"headerConnectionStatus",nullptr},{"headerDeviceIsHub",nullptr},
        {"headerNumberOfOpenPipes",nullptr},{"requestIndexMatches",nullptr}};
    auto field=[&](size_t offset,auto value,const char* name){
        if(offset<=bounded&&sizeof(value)<=bounded-offset){std::memcpy(&value,reply.bytes.data()+offset,sizeof(value));facts[name]=value;}
    };
    field(offsetof(USB_NODE_CONNECTION_INFORMATION_EX,ConnectionIndex),ULONG{},"headerConnectionIndex");
    field(offsetof(USB_NODE_CONNECTION_INFORMATION_EX,ConnectionStatus),ULONG{},"headerConnectionStatus");
    field(offsetof(USB_NODE_CONNECTION_INFORMATION_EX,DeviceIsHub),UCHAR{},"headerDeviceIsHub");
    field(offsetof(USB_NODE_CONNECTION_INFORMATION_EX,NumberOfOpenPipes),ULONG{},"headerNumberOfOpenPipes");
    if(!facts["headerConnectionIndex"].is_null())facts["requestIndexMatches"]=facts["headerConnectionIndex"]==requested;
    constexpr char digits[]="0123456789abcdef";std::string hex;hex.reserve(bounded*2);
    for(size_t i=0;i<bounded;++i){const auto byte=reply.bytes[i];hex.push_back(digits[byte>>4]);hex.push_back(digits[byte&15]);}
    facts["returnedHeaderHex"]=std::move(hex);
    if(reply.receipt.contains("state"))facts["connectionIoState"]=reply.receipt["state"];
    if(reply.receipt.contains("error"))facts["connectionIoError"]=reply.receipt["error"];
    return facts;
}
template<class Query,class Clock,class Pending>
inline Json exactRootPort(unsigned ports,const std::wstring&expected,Query query,Clock clock,Pending pending,uint64_t deadline){
    Json result={{"allPortBindingsKnown",false},{"driverKeyMatches",0},{"connectionIndex",0},{"portBindings",Json::array()}};
    if(!ports||ports>64||expected.empty()){result["reason"]=expected.empty()?"root-driver-key-unavailable":"hub-port-count-outside-bound";return result;}
    bool allKnown=true,checkedPending=false;unsigned matched=0,port=0;uint64_t checkedTick=0;
    // Store the existing clock/pending checks for diagnostics; do not add checks.
    for(unsigned i=1;i<=ports&&(checkedTick=clock())<deadline&&!(checkedPending=pending());++i){
        Json facts={{"connectionIndex",i},{"connectionKnown",false},{"driverKeyRequired",false},{"driverKeyKnown",false},{"resolved",false}};
        std::vector<uint8_t> request(4096,0);reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(request.data())->ConnectionIndex=i;
        auto connection=query(IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,std::move(request));
        facts.update(connectionHeaderFacts(connection,i));facts["reason"]="connection-query-failed";
        if(connection.ok&&connection.bytes.size()>=offsetof(USB_NODE_CONNECTION_INFORMATION_EX,PipeList)){
            const auto*c=reinterpret_cast<const USB_NODE_CONNECTION_INFORMATION_EX*>(connection.bytes.data());
            facts["reason"]="connection-index-mismatch";
            if(c->ConnectionIndex==i){facts["connectionKnown"]=true;facts["connectionStatus"]=c->ConnectionStatus;
                facts["reason"]="unsupported-connection-status";
                if(c->ConnectionStatus==NoDeviceConnected){facts["resolved"]=true;facts["state"]="proven-empty-port";facts["reason"]="proven-empty-port";}
                else if(c->ConnectionStatus==DeviceConnected){
                    facts["driverKeyRequired"]=true;std::vector<uint8_t> keyRequest(4096,0);reinterpret_cast<USB_NODE_CONNECTION_DRIVERKEY_NAME*>(keyRequest.data())->ConnectionIndex=i;
                    auto reply=query(IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,std::move(keyRequest));std::wstring candidate;
                    const bool header=reply.ok&&reply.bytes.size()>=offsetof(USB_NODE_CONNECTION_DRIVERKEY_NAME,DriverKeyName);
                    if(header)facts["driverKeyActualLength"]=reinterpret_cast<const USB_NODE_CONNECTION_DRIVERKEY_NAME*>(reply.bytes.data())->ActualLength;
                    const bool known=header&&reinterpret_cast<const USB_NODE_CONNECTION_DRIVERKEY_NAME*>(reply.bytes.data())->ConnectionIndex==i&&driverKey(reply,candidate);
                    facts["driverKeyKnown"]=known;facts["resolved"]=known;
                    facts["reason"]=!reply.ok?"driver-key-query-failed":!header?"driver-key-header-too-short":
                        reinterpret_cast<const USB_NODE_CONNECTION_DRIVERKEY_NAME*>(reply.bytes.data())->ConnectionIndex!=i?"driver-key-index-mismatch":"driver-key-name-invalid";
                    if(known){const bool same=asciiUpper(utf8(candidate))==asciiUpper(utf8(expected));facts["driverKeyMatchesRoot"]=same;facts["reason"]=same?"root-driver-key-match":"other-driver-key";if(same){++matched;port=i;}}
                }
            }
        }else if(connection.ok)facts["reason"]="connection-header-too-short";
        if(!facts["resolved"].get<bool>())allKnown=false;
        result["portBindings"].push_back(std::move(facts));
    }
    result["driverKeyMatches"]=matched;result["connectionIndex"]=port;
    result["allPortBindingsKnown"]=allKnown&&result["portBindings"].size()==ports&&(checkedTick=clock())<deadline&&!(checkedPending=pending());
    result["reason"]=!allKnown?"port-binding-unresolved":checkedTick>=deadline?"scan-deadline-expired":checkedPending?"prior-read-pending":
        result["portBindings"].size()!=ports?"port-scan-incomplete":matched==0?"root-driver-key-not-found":matched>1?"duplicate-root-driver-key":"exact-root-port-proven";
    return result;
}
class NameProvenance {
    ContextReader reader;HANDLE parentHandle=nullptr;uint32_t parent=0;uint64_t created=0;ReadOnlyUsbIo io;
    std::set<CaptureKey> captured;unsigned samples=0;bool unavailableLogged=false,budgetLogged=false;
public:
    bool open(const std::wstring&name,uint32_t pid,uint64_t creation,HANDLE verifiedParent){if(!verifiedParent||verifiedParent==INVALID_HANDLE_VALUE)return false;parentHandle=verifiedParent;parent=pid;created=creation;const auto expected=contextMappingName(pid,creation);return name==expected&&reader.open(name);}
    template<class Emit> void observe(Emit emit){Context begin{};if(WaitForSingleObject(parentHandle,0)!=WAIT_TIMEOUT||!reader.snapshot(begin)||!eligible(begin,parent,created)||!fresh(begin,GetTickCount64())){
        if(!unavailableLogged){emit(Json{{"event","msi-usb-name-origin"},{"version",1},{"state","current-authorized-source-unavailable"},{"context",contextFacts(begin)},{"writesHardware",false}});unavailableLogged=true;}return;}
        unavailableLogged=false;io.reap();const auto key=captureKey(begin);if(captured.count(key))return;if(samples>=8){if(!budgetLogged){emit(Json{{"event","msi-usb-name-origin"},{"version",1},{"state","sample-budget-exhausted"},{"sampleLimit",8},{"context",contextFacts(begin)},{"writesHardware",false}});budgetLogged=true;}return;}captured.insert(key);++samples;
        const uint64_t started=GetTickCount64(),deadline=started+2500;Json row={{"event","msi-usb-name-origin"},{"version",1},{"sample",samples},{"sampleLimit",8},{"captureKeyFields",Json::array({"powerGeneration","sourceGeneration","hostEpoch"})},{"beginTick",started},{"captureBudgetMs",2500},{"contextMaxAgeMs",2000},{"contextBegin",contextFacts(begin)},{"writesHardware",false},{"joined",false},{"descriptorAtomicSnapshot",false},{"physicalIncarnationProven",false},{"rootBindingProven",false},{"io",Json::array()},{"strings",Json::array()},{"scope","Native current MSI DInput source identity plus hub-driver descriptor readback; no firmware/wire, physical controls or Steam consumption proof"}};
        auto binding=resolveRoot(text(begin.usbRoot));row["bindingBegin"]=binding.facts;
        std::wstring utf8ToHub=binding.hubPath;
        auto query=[&](DWORD code,std::vector<uint8_t> request){
            const bool portQuery=code==IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX||code==IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME||code==IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION;
            const auto requestedPort=portQuery&&request.size()>=sizeof(ULONG)?*reinterpret_cast<const ULONG*>(request.data()):0;
            auto reply=io.query(utf8ToHub,code,std::move(request),deadline);if(portQuery)reply.receipt["connectionIndex"]=requestedPort;
            row["io"].push_back(reply.receipt);return reply;};
        auto done=[&](const char*state){Context end{};const bool coherent=reader.snapshot(end);row["contextEndKnown"]=coherent;if(coherent)row["contextEnd"]=contextFacts(end);
            const auto after=resolveRoot(text(begin.usbRoot));row["bindingEnd"]=after.facts;
            const bool parentLive=WaitForSingleObject(parentHandle,0)==WAIT_TIMEOUT;row["parentLiveAtEnd"]=parentLive;
            const bool nativeSame=parentLive&&coherent&&eligible(end,parent,created)&&fresh(end,GetTickCount64())&&sameGeneration(begin,end);
            const bool bindingSame=binding.ok&&after.ok&&binding.node==after.node&&binding.parent==after.parent&&binding.driverKey==after.driverKey&&binding.hubPath==after.hubPath;
            row["nativeGenerationStable"]=nativeSame;row["rootBindingStable"]=bindingSame;row["endTick"]=GetTickCount64();row["pendingReads"]=io.pending();row["state"]=state;
            row["joined"]=nativeSame&&bindingSame&&row.value("rootBindingProven",false)&&row.value("descriptorSetValid",false)&&row["endTick"].get<uint64_t>()<deadline;
            emit(row);};
        if(!binding.ok){done("root-parent-hub-binding-unavailable");return;}
        std::vector<uint8_t> node(sizeof(USB_NODE_INFORMATION),0);reinterpret_cast<USB_NODE_INFORMATION*>(node.data())->NodeType=UsbHub;
        auto hub=query(IOCTL_USB_GET_NODE_INFORMATION,std::move(node));if(!hub.ok||hub.bytes.size()<sizeof(USB_NODE_INFORMATION)){done("hub-node-query-unavailable");return;}
        const auto*h=reinterpret_cast<const USB_NODE_INFORMATION*>(hub.bytes.data());const unsigned ports=h->u.HubInformation.HubDescriptor.bNumberOfPorts;row["hubPorts"]=ports;
        if(!ports||ports>64){done("hub-port-count-outside-bound");return;}
        const auto portBinding=exactRootPort(ports,binding.driverKey,query,[]{return GetTickCount64();},[&]{return io.pending();},deadline);
        row["portBinding"]=portBinding;const auto port=portBinding["connectionIndex"].get<unsigned>();
        row["driverKeyMatches"]=portBinding["driverKeyMatches"];row["connectionIndex"]=port;
        if(!portBinding["allPortBindingsKnown"].get<bool>()||portBinding["driverKeyMatches"]!=1||GetTickCount64()>=deadline||io.pending()){done("exact-root-port-unproven");return;}
        std::vector<uint8_t> connection(4096,0);reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(connection.data())->ConnectionIndex=port;
        auto connected=query(IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,std::move(connection));if(!connected.ok||connected.bytes.size()<offsetof(USB_NODE_CONNECTION_INFORMATION_EX,PipeList)){done("connection-descriptor-unavailable");return;}
        const auto*c=reinterpret_cast<const USB_NODE_CONNECTION_INFORMATION_EX*>(connected.bytes.data());row["connectionStatus"]=c->ConnectionStatus;row["currentConfiguration"]=c->CurrentConfigurationValue;
        if(c->ConnectionStatus!=DeviceConnected||c->DeviceIsHub||c->DeviceDescriptor.idVendor!=0x0db0||c->DeviceDescriptor.idProduct!=0x1902){done("root-port-device-identity-mismatch");return;}row["rootBindingProven"]=true;
        auto descriptor=[&](unsigned type,unsigned index,unsigned language,unsigned size){auto reply=query(IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,descriptorRequest(port,type,index,language,size));row["io"].back()["descriptorType"]=type;row["io"].back()["descriptorIndex"]=index;row["io"].back()["languageId"]=language;
            const size_t offset=offsetof(USB_DESCRIPTOR_REQUEST,Data);if(reply.ok&&reply.bytes.size()>=offset)reply.bytes.erase(reply.bytes.begin(),reply.bytes.begin()+offset);else{reply.ok=false;reply.bytes.clear();}return reply;};
        auto device=descriptor(1,0,0,18);row["deviceDescriptor"]=deviceDescriptor(device.bytes);const auto&dev=row["deviceDescriptor"];
        if(!device.ok||!dev.value("valid",false)||dev.value("vendorId",0)!=0x0db0||dev.value("productId",0)!=0x1902){done("device-descriptor-unproven");return;}
        const unsigned count=dev.value("numConfigurations",0);if(!count||count>4||!c->CurrentConfigurationValue){done("active-configuration-unavailable");return;}
        Json config=Json::object();unsigned matches=0;row["configurationHeaders"]=Json::array();
        for(unsigned i=0;i<count;++i){auto head=descriptor(2,i,0,9);const auto&b=head.bytes;const bool header=head.ok&&b.size()>=9&&b[0]==9&&b[1]==2;const unsigned total=header?unsigned(b[2]|(unsigned(b[3])<<8)):0;
            row["configurationHeaders"].push_back({{"index",i},{"rawBytes",b},{"headerKnown",header},{"totalLength",total}});if(!header||total<9||total>16384){done("configuration-header-unproven");return;}
            if(b[5]!=c->CurrentConfigurationValue)continue;auto full=descriptor(2,i,0,total);auto parsed=configurationDescriptor(full.bytes);const auto coverage=configurationCoverage(b,parsed,0);row["configurationCoverage"]=coverage;if(!coverage.value("valid",false)){row["configurationDescriptor"]=parsed;done("configuration-interface-set-unproven");return;}if(!full.ok||!parsed.value("valid",false)||parsed.value("configurationValue",0)!=c->CurrentConfigurationValue){row["configurationDescriptor"]=parsed;done("configuration-descriptor-unproven");return;}config=std::move(parsed);++matches;}
        row["activeConfigurationMatches"]=matches;if(matches!=1){done("active-configuration-not-unique");return;}row["configurationDescriptor"]=config;
        std::set<unsigned> indexes;const auto product=dev.value("iProduct",0u);if(product)indexes.insert(product);
        // The current physical main interface is MI_00; captured MSI auxiliaries
        // share MI_00/MI_01. Record every active interface/IAD index separately.
        for(const auto&iface:config["interfaces"])if(iface.value("iInterface",0u))indexes.insert(iface["iInterface"].get<unsigned>());
        for(const auto&iad:config["iads"])if(iad.value("iFunction",0u))indexes.insert(iad["iFunction"].get<unsigned>());
        if(indexes.size()>16){done("relevant-string-index-budget-exceeded");return;}
        auto zero=descriptor(3,0,0,255);row["languageDescriptor"]=stringDescriptor(zero.bytes,0);const auto&languages=row["languageDescriptor"];
        if(!zero.ok||!languages.value("valid",false)||!languages.contains("languageIds")||languages["languageIds"].empty()){done("reported-language-unavailable");return;}
        const unsigned language=languages["languageIds"][0].get<unsigned>();if(!language){done("advertised-language-zero-invalid");return;}row["selectedLanguageId"]=language;bool stringsKnown=true;
        for(unsigned index:indexes){auto reply=descriptor(3,index,language,255);auto parsed=stringDescriptor(reply.bytes,uint8_t(index));parsed["index"]=index;parsed["languageId"]=language;stringsKnown=stringsKnown&&reply.ok&&parsed.value("valid",false);
            if(parsed.value("valid",false)&&parsed.value("utf16WellFormed",false)&&parsed.contains("utf16")){std::wstring name;for(const auto&unit:parsed["utf16"])name.push_back(wchar_t(unit.get<unsigned>()));parsed["utf8"]=utf8(name);}row["strings"].push_back(parsed);if(io.pending())break;}
        row["descriptorSetValid"]=stringsKnown&&row["strings"].size()==indexes.size();row["relevantNonzeroStringIndexes"]=indexes;done(row["descriptorSetValid"].get<bool>()?"captured-driver-descriptors":"indexed-string-evidence-incomplete");
    }
};
}