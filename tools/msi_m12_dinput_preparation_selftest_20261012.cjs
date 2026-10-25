// Offline evidence: compile the actual pre-/post-diagnostic preparation functions
// against Win32 fakes, compare every pre-existing HID operation and report byte.
// No product startup, driver, services, PnP or real hardware calls are linked.
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const repo=path.resolve(__dirname,'..'),out=path.resolve(process.argv[2]||'');
if(!out.toLowerCase().startsWith('g:\\yemancc-work\\_scratch\\'))throw Error('scratch output required');
fs.mkdirSync(out,{recursive:true});
const beforePath=path.join(out,'main-before.cpp');
const currentPath=path.join(repo,'native','main.cpp');
const before=fs.readFileSync(beforePath,'utf8'),current=fs.readFileSync(currentPath,'utf8');
function fn(s,name){s=s.replace(/\r\n/g,'\n');const start=s.indexOf('static bool '+name+'('),end=s.indexOf('\n}\n',start);if(start<0||end<0)throw Error(name+' missing');return s.slice(start,end+3);}
const old=(fn(before,'msiClawEnsureM12Remap')+'\n'+fn(before,'msiClawPrepareForVirtualStart')).replaceAll('msiClawEnsureM12Remap','baselineEnsure').replaceAll('msiClawPrepareForVirtualStart','baselinePrepare').replaceAll('g_msiM12RemapDone','baselineCache');
const changed=fn(current,'msiClawEnsureM12Remap')+'\n'+fn(current,'msiClawPrepareForVirtualStart');
const header=path.join(repo,'native/msi_rear_sd_preparation_receipt.h').replaceAll('\\','/'),gate=path.join(repo,'native/msi_blocked_start_preparation.h').replaceAll('\\','/');
const cpp=String.raw`#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <json.hpp>
#include "${header}"
#include "${gate}"
#include "${path.join(repo,'native/msi_rear_protocol.h').replaceAll('\\','/')}"
using json=nlohmann::json;
using BYTE=unsigned char;using BOOL=int;using DWORD=uint32_t;using USHORT=uint16_t;using HANDLE=int;using HDEVINFO=int;using GUID=int;
constexpr int INVALID_HANDLE_VALUE=-1,DIGCF_PRESENT=1,DIGCF_DEVICEINTERFACE=2,GENERIC_READ=1,GENERIC_WRITE=2,FILE_SHARE_READ=1,FILE_SHARE_WRITE=2,OPEN_EXISTING=1,FILE_ATTRIBUTE_NORMAL=1,FALSE=0,HIDP_STATUS_SUCCESS=1;
constexpr GUID kYeManGuidDeviceInterfaceHid=0;constexpr USHORT kMsiClawVendorId=0x0db0,kMsiClawPidXInput=0x1901,kMsiClawPidDInput=0x1902;
struct SP_DEVICE_INTERFACE_DATA{DWORD cbSize=0;unsigned index=0;};struct SP_DEVINFO_DATA{DWORD cbSize=0;};struct SP_DEVICE_INTERFACE_DETAIL_DATA_W{DWORD cbSize=0;wchar_t DevicePath[160]{};};
struct HIDD_ATTRIBUTES{DWORD Size=0;USHORT VendorID=0,ProductID=0,VersionNumber=0;};struct HIDP_CAPS{USHORT UsagePage=0,Usage=0,OutputReportByteLength=64;};using PHIDP_PREPARSED_DATA=int*;
enum class YmccFamily{Other,MsiClaw};struct Machine{YmccFamily family=YmccFamily::MsiClaw;}g_machineIdentity;
bool g_inputHostReleaseQuarantined=false,g_msiM12RemapDone=false,baselineCache=false,knownBoard=true;
struct Endpoint{USHORT vendor=0x0db0,product=0x1901,firmware=0x411,page=0xffa0,usage=1;bool sizeOk=true,detailOk=true,openOk=true,attrOk=true,prepOk=true,capsOk=true;};
std::vector<Endpoint> endpoints;bool classOk=true;DWORD lastError=0;unsigned failureMask=0,shortMask=0;bool enforceDinputProtocol=false;
std::vector<std::string> calls;std::vector<std::array<BYTE,64>> reports;std::vector<json> logs;
void ymccResolveMachineIdentityOnce(){calls.push_back("resolve");}bool msiClawKnownBoardAuthority(){calls.push_back("authority");return knownBoard;}
DWORD GetLastError(){return lastError;}
HDEVINFO SetupDiGetClassDevsW(const GUID*,void*,void*,int){calls.push_back("class");lastError=5;return classOk?1:INVALID_HANDLE_VALUE;}
bool SetupDiEnumDeviceInterfaces(HDEVINFO,void*,const GUID*,DWORD index,SP_DEVICE_INTERFACE_DATA* p){calls.push_back("enum:"+std::to_string(index));if(index>=endpoints.size()){lastError=259;return false;}p->index=index;return true;}
bool SetupDiGetDeviceInterfaceDetailW(HDEVINFO,SP_DEVICE_INTERFACE_DATA* i,SP_DEVICE_INTERFACE_DETAIL_DATA_W* p,DWORD,DWORD* bytes,SP_DEVINFO_DATA*){auto& e=endpoints.at(i->index);calls.push_back(std::string(p?"detail:":"size:")+std::to_string(i->index));if(!p){if(bytes)*bytes=e.sizeOk?sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W):0;lastError=122;return false;}if(!e.detailOk)return false;auto name=std::to_wstring(i->index+1);std::copy(name.begin(),name.end(),p->DevicePath);p->DevicePath[name.size()]=0;return true;}
HANDLE CreateFileW(const wchar_t* p,int,int,void*,int,int,void*){int h=std::stoi(std::wstring(p));calls.push_back("open:"+std::to_string(h));if(!endpoints.at(h-1).openOk){lastError=5;return INVALID_HANDLE_VALUE;}return h;}
bool HidD_GetAttributes(HANDLE h,HIDD_ATTRIBUTES* a){calls.push_back("attrs:"+std::to_string(h));auto& e=endpoints.at(h-1);a->VendorID=e.vendor;a->ProductID=e.product;a->VersionNumber=e.firmware;return e.attrOk;}
bool HidD_GetPreparsedData(HANDLE h,PHIDP_PREPARSED_DATA* p){calls.push_back("prep:"+std::to_string(h));if(!endpoints.at(h-1).prepOk)return false;*p=reinterpret_cast<int*>(static_cast<intptr_t>(h));return true;}
int HidP_GetCaps(PHIDP_PREPARSED_DATA p,HIDP_CAPS* c){int h=static_cast<int>(reinterpret_cast<intptr_t>(p));calls.push_back("caps:"+std::to_string(h));auto& e=endpoints.at(h-1);c->UsagePage=e.page;c->Usage=e.usage;return e.capsOk?HIDP_STATUS_SUCCESS:0;}
void HidD_FreePreparsedData(PHIDP_PREPARSED_DATA p){calls.push_back("free:"+std::to_string(reinterpret_cast<intptr_t>(p)));}void CloseHandle(HANDLE h){calls.push_back("close:"+std::to_string(h));}void SetupDiDestroyDeviceInfoList(HDEVINFO){calls.push_back("destroy");}
bool WriteFile(HANDLE h,const void* data,DWORD bytes,DWORD* written,void*){calls.push_back("write:"+std::to_string(h)+":"+std::to_string(bytes));if(bytes!=64)throw std::runtime_error("unexpected write length");std::array<BYTE,64> r{};std::memcpy(r.data(),data,64);reports.push_back(r);unsigned bit=1u<<(reports.size()-1);bool contractOk=!enforceDinputProtocol||(r[0]==0x0f&&r[3]==0x3c&&(r[4]==0x22||(r[4]==0x21&&r[8]==2&&r[9]==1&&r[10]==0)));bool accepted=!(failureMask&bit)&&contractOk;*written=accepted?((shortMask&bit)?63:64):0;lastError=accepted?0:87;return accepted;}
void Sleep(DWORD ms){calls.push_back("sleep:"+std::to_string(ms));}
void appendNativeLifecycleLog(const char* event,const json& fields){json j=fields;j["event"]=event;logs.push_back(j);}
${old}
${changed}
struct Trace{std::vector<std::string> calls;std::vector<std::array<BYTE,64>> reports;std::vector<json> logs;bool result=false,cache=false;};
const std::array<std::array<int,5>,8> referenceFirmware={{{355,0,122,1,31},{358,0,186,1,99},{359,0,186,1,99},{529,0,122,1,31},{535,0,186,1,99},{537,0,186,1,99},{776,0,186,1,99},{1041,0,186,1,99}}};
unsigned checks=0,scenarios=0;
void check(bool ok,const std::string& label){++checks;if(!ok)throw std::runtime_error(label);}
void resetIo(){calls.clear();reports.clear();logs.clear();lastError=0;}
void compare(const std::string& name,const std::vector<Endpoint>& e,const char* expected,bool cached=false,YmccFamily family=YmccFamily::MsiClaw){++scenarios;const bool privateBlocked=family==YmccFamily::MsiClaw&&knownBoard&&g_inputHostReleaseQuarantined;endpoints=e;g_machineIdentity.family=family;baselineCache=cached;resetIo();Trace b;b.result=baselinePrepare();b.calls=calls;b.reports=reports;b.logs=logs;b.cache=baselineCache;g_msiM12RemapDone=cached;resetIo();ymcc::msi::rearSdPreparation::Receipt r;bool v=msiClawPrepareForVirtualStart(&r);check(privateBlocked ? !v : enforceDinputProtocol ? (v&&!b.result) : v==b.result,name+" return unchanged except repaired protocol rejection");check(privateBlocked ? !g_msiM12RemapDone : enforceDinputProtocol ? (g_msiM12RemapDone&&!b.cache) : g_msiM12RemapDone==b.cache,name+" cache only on complete writes");auto hidCalls=[](std::vector<std::string> v){v.erase(std::remove(v.begin(),v.end(),"authority"),v.end());return v;};check(privateBlocked ? hidCalls(calls).empty() : enforceDinputProtocol ? (reports.size()==3) : hidCalls(calls)==hidCalls(b.calls),name+" discovery and IO cadence unchanged; contract-negative old enumerates past failed writes");check(privateBlocked ? reports.empty() : reports.size()==b.reports.size(),name+" no added command writes");check(privateBlocked ? logs.empty() : logs.size()==b.logs.size(),name+" no added remap/refusal event count");check(r.returned==v,name+" receipt returned exact");check(r.outcome==std::string(expected),name+" expected disposition");check(r.hardwareWriteIssued==!reports.empty(),name+" write issue honest");check(r.writeSets*3==reports.size(),name+" diagnostic adds no firmware retry");auto fields=ymcc::msi::rearSdPreparation::fields<json>(r);if(r.hardwareWriteIssued){for(size_t slot=0;slot<3;++slot){check(r.commands[slot].apiAccepted==(r.commands[slot].error==0),name+" per-command raw Win32 result");check(r.commands[slot].written<=64,name+" exact command bytes recorded");}check(r.wireReportId==15&&r.outputReportByteLength==64,name+" command report metadata exact");}check(!fields.at("mappingVerified").get<bool>(),name+" no acceptance claim");check(!fields.at("retryIssuedByDiagnostic").get<bool>(),name+" no retry claim");check(json::parse(fields.dump())==fields,name+" JSON roundtrip");if(r.commandMatches==0)check(reports.empty(),name+" no matching endpoint no writes");if(!reports.empty()){for(size_t i=0;i<reports.size();i+=3){check(reports[i][0]==0x0f&&reports[i][3]==0x3c&&reports[i][4]==0x21&&reports[i][8]==2&&reports[i][9]==1&&reports[i][10]==0,name+" M1 DInput framing");check(reports[i+1][0]==0x0f&&reports[i+1][8]==2&&reports[i+1][9]==1&&reports[i+1][10]==0,name+" M2 DInput framing");check(reports[i+2][0]==0x0f&&reports[i+2][3]==0x3c&&reports[i+2][4]==0x22,name+" Sync framing");auto firmware=endpoints.at(i/3).firmware;std::array<BYTE,64> m1{},m2{},sync{};bool found=false;for(auto row:referenceFirmware){if(row[0]!=firmware)continue;found=true;m1={0x0f,0,0,0x3c,0x21,1,BYTE(row[1]),BYTE(row[2]),2,1,0};m2={0x0f,0,0,0x3c,0x21,1,BYTE(row[3]),BYTE(row[4]),2,1,0};sync={0x0f,0,0,0x3c,0x22};}check(found&&reports[i]==m1&&reports[i+1]==m2&&reports[i+2]==sync,name+" full64B equality to independently parsed HC table, zero-offset extension and GetM12(false)");}}}
int main(){try{Endpoint e;
compare("non-msi",{},"not-msi-family",false,YmccFamily::Other);compare("cached",{},"process-cache-hit",true);compare("source-not-enumerated",{},"command-interface-not-found");classOk=false;compare("class-failed",{},"hid-class-enumeration-failed");classOk=true;
for(int flag=0;flag<7;++flag){auto x=e;switch(flag){case 0:x.sizeOk=false;break;case 1:x.detailOk=false;break;case 2:x.openOk=false;break;case 3:x.attrOk=false;break;case 4:x.prepOk=false;break;case 5:x.capsOk=false;break;case 6:x.vendor=0x1234;break;}compare("endpoint-discovery-rejection-"+std::to_string(flag),{x},"command-interface-not-found");}
auto x=e;x.usage=2;compare("wrong-usage",{x},"command-interface-not-found");x=e;x.product=0xffff;compare("wrong-pid",{x},"command-interface-not-found");x=e;x.firmware=0x419;compare("unknown-firmware",{x},"firmware-not-in-table");
for(auto fw:{0x163,0x166,0x167,0x211,0x217,0x219,0x308,0x411}){x=e;x.firmware=fw;compare("known-firmware-"+std::to_string(fw),{x},"write-receipts-complete");}
x=e;x.product=0x1902;x.page=0xfff0;x.usage=0x40;compare("existing-dinput-command",{x},"write-receipts-complete");
for(unsigned mask=1;mask<8;++mask){failureMask=mask;compare("existing-failed-write-"+std::to_string(mask),{e},"command-write-incomplete");}failureMask=0;
for(unsigned mask=1;mask<8;++mask){shortMask=mask;compare("existing-short-write-"+std::to_string(mask),{e},"command-write-incomplete");}shortMask=0;
failureMask=1;compare("existing-try-next-matched-interface-after-failure",{e,e},"write-receipts-complete");failureMask=0;
g_inputHostReleaseQuarantined=true;compare("existing-quarantine-gate",{e},"release-quarantine-blocked-before-m12");g_inputHostReleaseQuarantined=false;
compare("source-returned-later-explicit-call-only",{e},"write-receipts-complete");compare("later-call-cache-no-rewrite",{e},"process-cache-hit",true);enforceDinputProtocol=true;compare("old shifted-keyboard-packets-rejected-new-DInput-packets-accepted",{e},"write-receipts-complete");enforceDinputProtocol=false;
std::cout<<json{{"ok",true},{"scenarios",scenarios},{"checks",checks},{"existingPreparationGateBuild",ymcc::msi::preparation::buildEnabled},{"hardware_io","none; all external APIs are linked local fakes"},{"scope","actual production MSI M12 fix, old-function protocol negative control and HC packet reference; not real firmware/Steam acceptance"}}.dump(2)<<"\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
`;
fs.writeFileSync(path.join(out,'actual-msi-m12-dinput-regression.cpp'),cpp);
const hashes={before:crypto.createHash('sha256').update(before).digest('hex'),current:crypto.createHash('sha256').update(current).digest('hex'),actualExtractedFunctions:crypto.createHash('sha256').update(changed).digest('hex'),hcDeviceSource:crypto.createHash('sha256').update(fs.readFileSync("G:/YeManCC-Work/Isolated/Tasks/HC-Candidate-20260902/active/workspace/HandheldCompanion/Devices/MSI/ClawA1M.cs")).digest('hex'),hcWriteExtension:crypto.createHash('sha256').update(fs.readFileSync("G:/YeManCC-Work/Isolated/Tasks/HC-Candidate-20260902/active/workspace/HandheldCompanion/Extensions/HidDeviceExtensions.cs")).digest('hex'),receiptHeader:crypto.createHash('sha256').update(fs.readFileSync(header)).digest('hex'),testSource:crypto.createHash('sha256').update(cpp).digest('hex')};
fs.writeFileSync(path.join(out,'test-source-binding.json'),JSON.stringify(hashes,null,2));console.log(JSON.stringify(hashes));
