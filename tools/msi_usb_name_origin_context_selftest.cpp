// Pure source/parent/generation and argument tests. No mapping/device/process API calls.
#define NOMINMAX
#include "msi_usb_name_origin_context.h"
#include "msi_usb_name_origin_join.h"
#include <json.hpp>
#include <fstream>
#include <iostream>
#include <functional>
using nlohmann::json;
namespace names=ymcc::msi::usbName;
int main(int argc,char** argv){
    unsigned checks=0,failed=0;json failures=json::array();
    auto check=[&](bool value,const char* label){++checks;if(!value){++failed;failures.push_back(label);}};
    names::Context c;c.nativePid=17;c.parentCreationTime=1234;c.boardAuthorized=1;c.sourceValid=1;
    c.sourceGeneration=2;c.powerGeneration=3;c.hostEpoch=4;c.observedTick=100;
    names::text(c.board,"Claw 8 AI+ A2VM");names::text(c.usbRoot,"USB\\VID_0DB0&PID_1902\\a");
    names::text(c.interfacePath,"\\\\?\\HID#VID_0DB0&PID_1902&MI_00&COL01#exact-fixture");
    check(names::eligible(c,17,1234),"minimal nonempty root suffix accepted");
    for(auto field:{"version","bytes","pid","birth","boardAuthority","sourceValid","sourceGeneration","powerGeneration"}){
        auto v=c;const std::string f=field;if(f=="version")v.version=2;if(f=="bytes")--v.bytes;if(f=="pid")++v.nativePid;if(f=="birth")++v.parentCreationTime;
        if(f=="boardAuthority")v.boardAuthorized=0;if(f=="sourceValid")v.sourceValid=0;if(f=="sourceGeneration")v.sourceGeneration=0;if(f=="powerGeneration")v.powerGeneration=0;
        check(!names::eligible(v,17,1234),field);
    }
    check(!names::eligible(c,0,1234),"zero caller pid");check(!names::eligible(c,17,0),"zero caller birth");
    for(const auto* root:{"","USB\\VID_0DB0&PID_1902\\","USB\\VID_0DB0&PID_1901\\x","USB\\VID_0DB0&PID_1902&MI_00\\x","HID\\VID_0DB0&PID_1902\\x","USB\\VID_045E&PID_028E\\x"}){
        auto v=c;names::text(v.usbRoot,root);check(!names::eligible(v,17,1234),"invalid source root");
    }
    for(const auto* iface:{"","\\\\?\\HID#VID_0DB0&PID_1902&MI_00&COL02#fixture","\\\\?\\HID#VID_0DB0&PID_1902&MI_01&COL01#fixture","\\\\?\\HID#VID_0DB0&PID_1901&MI_00&COL01#fixture","\\\\?\\HID#VID_045E&PID_028E&MI_00&COL01#fixture"}){
        auto v=c;names::text(v.interfacePath,iface);check(!names::eligible(v,17,1234),"invalid collection");
    }
    auto v=c;names::text(v.usbRoot,"usb\\vid_0db0&pid_1902\\a");names::text(v.interfacePath,"\\\\?\\hid#vid_0db0&pid_1902&mi_00&col01#fixture");check(names::eligible(v,17,1234),"case insensitive source gate");
    v=c;v.board.fill('x');check(!names::eligible(v,17,1234),"unterminated board");v=c;names::text(v.board,"");check(!names::eligible(v,17,1234),"empty board");
    v=c;v.usbRoot.fill('x');check(!names::eligible(v,17,1234),"unterminated root");v=c;v.interfacePath.fill('x');check(!names::eligible(v,17,1234),"unterminated interface");
    check(names::sameGeneration(c,c),"same generation");
    check(names::captureKey(c)==names::captureKey(c),"same complete tuple has same capture key");
    for(auto field:{"hostEpoch","sourceGeneration","powerGeneration"}){
        auto newer=c;const std::string f=field;if(f=="hostEpoch")++newer.hostEpoch;if(f=="sourceGeneration")++newer.sourceGeneration;if(f=="powerGeneration")++newer.powerGeneration;
        check(names::captureKey(c)!=names::captureKey(newer),"join generation field change requires distinct capture key");
    }
    auto heartbeat=c;heartbeat.observedTick=200;names::text(heartbeat.phase,"current-source");
    check(names::captureKey(c)==names::captureKey(heartbeat),"heartbeat/phase do not create diagnostic retries");
    for(auto field:{"version","bytes","pid","birth","boardAuthority","sourceValid","sourceGeneration","powerGeneration","hostEpoch","board","root","interface"}){
        v=c;const std::string f=field;if(f=="version")++v.version;if(f=="bytes")++v.bytes;if(f=="pid")++v.nativePid;if(f=="birth")++v.parentCreationTime;
        if(f=="boardAuthority")v.boardAuthorized=0;if(f=="sourceValid")v.sourceValid=0;if(f=="sourceGeneration")++v.sourceGeneration;if(f=="powerGeneration")++v.powerGeneration;if(f=="hostEpoch")++v.hostEpoch;
        if(f=="board")names::text(v.board,"different");if(f=="root")names::text(v.usbRoot,"different");if(f=="interface")names::text(v.interfacePath,"different");check(!names::sameGeneration(c,v),field);
    }
    v=c;v.observedTick=200;names::text(v.phase,"current-source");check(names::sameGeneration(c,v),"heartbeat phase and time do not change source generation");
    check(names::fresh(c,100)&&names::fresh(c,2100),"inclusive heartbeat bounds");check(!names::fresh(c,99)&&!names::fresh(c,2101),"future and stale heartbeat");v=c;v.observedTick=0;check(!names::fresh(v,0),"unknown heartbeat");
    check(names::contextMappingName(17,1234)==L"Local\\YeManCC-MSI-NameContext-17-1234","exact parent mapping");check(names::contextMappingName(0,1234).empty()&&names::contextMappingName(17,0).empty(),"invalid mapping identity");
    const std::vector<std::wstring> base={L"observer",L"output",L"msi-evidence",L"60",L"17",L"1234"};
    auto options=names::observerOptions(base);check(options.valid&&!options.rawRequested&&options.contextName.empty()&&options.positionalCount==6,"legacy args preserved");
    auto args=base;args.push_back(L"--raw-msi-receipts");options=names::observerOptions(args);check(options.valid&&options.rawRequested&&options.positionalCount==6,"legacy raw suffix");
    args=base;args.insert(args.end(),{L"--raw-msi-receipts",L"--msi-name-context",L"name"});options=names::observerOptions(args);check(options.valid&&options.rawRequested&&options.contextName==L"name"&&options.positionalCount==6,"native composed suffix");
    args=base;args.insert(args.end(),{L"--msi-name-context",L"name",L"--raw-msi-receipts"});options=names::observerOptions(args);check(options.valid&&options.rawRequested&&options.contextName==L"name"&&options.positionalCount==6,"reverse suffix order");
    for(const auto& tail:{std::vector<std::wstring>{L"--raw-msi-receipts",L"--raw-msi-receipts"},std::vector<std::wstring>{L"--msi-name-context"},std::vector<std::wstring>{L"--msi-name-context",L""},std::vector<std::wstring>{L"--msi-name-context",L"x",L"--msi-name-context",L"y"}}){args=base;args.insert(args.end(),tail.begin(),tail.end());check(!names::observerOptions(args).valid,"duplicate or missing suffix rejected");}
    args=base;args.insert(args.end(),{L"3",L"1",L"--raw-msi-receipts",L"--msi-name-context",L"name"});options=names::observerOptions(args);check(options.valid&&options.positionalCount==8,"wake identity remains positional");
    std::array<char,4> bounded{};check(names::text(bounded,"abc")&&names::text(bounded)=="abc","bounded text");check(!names::text(bounded,"abcd"),"overlength text rejected");check(!names::text(bounded,std::string("a\0b",3)),"embedded zero rejected");

    v=c;names::bindPower(v,3,3);check(v.sourceValid==1&&v.powerGeneration==3,"bound power preserved");
    names::bindPower(v,3,4);check(v.sourceValid==0&&v.powerGeneration==3,"power advance invalidates old source without relabelling");
    names::bindPower(v,3,3);check(v.sourceValid==0,"invalid power lease does not self-regrant");
    v=c;names::bindPower(v,0,0);check(v.sourceValid==0&&v.powerGeneration==0,"unknown binding power refused");
    using Bytes=std::vector<uint8_t>;
    auto config=[](unsigned declared,const std::vector<Bytes>& descriptors){Bytes bytes={9,2,9,0,uint8_t(declared),1,0,0x80,50};for(const auto& d:descriptors)bytes.insert(bytes.end(),d.begin(),d.end());bytes[2]=uint8_t(bytes.size());bytes[3]=uint8_t(bytes.size()>>8);return bytes;};
    const Bytes i0={9,4,0,0,0,3,0,0,2},i0a={9,4,0,1,0,3,0,0,3},i1={9,4,1,0,0,3,0,0,4};
    auto coverage=[&](const Bytes& full){return names::configurationCoverage(Bytes(full.begin(),full.begin()+9),names::configurationDescriptor(full),0);};
    check(coverage(config(1,{i0})).value("valid",false),"complete main interface");
    check(coverage(config(2,{i0,i1})).value("valid",false),"complete distinct interface set");
    check(coverage(config(1,{i0,i0a})).value("valid",false),"alternate settings are not distinct interfaces");
    check(!coverage(config(1,{})).value("valid",false),"declared interface absent even though structural decoder succeeds");
    check(!coverage(config(2,{i0})).value("valid",false),"partial declared interface set");
    check(!coverage(config(1,{i1})).value("valid",false),"current MI00 interface absent");
    check(!coverage(config(1,{i0,i0})).value("valid",false),"duplicate interface and alternate identity");
    check(!coverage(config(0,{i0})).value("valid",false),"zero declared count cannot prove main source");
    check(coverage(config(1,{{8,11,0,1,3,0,0,6},i0})).value("valid",false),"complete IAD range");
    check(!coverage(config(1,{{8,11,0,2,3,0,0,6},i0})).value("valid",false),"IAD range missing interface");
    check(!coverage(config(1,{{8,11,0,0,3,0,0,6},i0})).value("valid",false),"empty IAD range");
    check(!coverage(config(1,{{8,11,255,2,3,0,0,6},i0})).value("valid",false),"IAD range overflow");
    const auto full=config(1,{i0});Bytes header(full.begin(),full.begin()+9);auto different=header;different[5]=2;
    check(!names::configurationCoverage(different,names::configurationDescriptor(full),0).value("valid",false),"full/header configuration mismatch");
    different=header;different.pop_back();check(!names::configurationCoverage(different,names::configurationDescriptor(full),0).value("valid",false),"short header rejected");
    check(!names::configurationCoverage(header,names::configurationDescriptor(Bytes{9,2}),0).value("valid",false),"invalid full descriptor rejected");
    json report={{"checks",checks},{"failed",failed},{"failures",failures},{"deviceApiCalls",0},{"mappingApiCalls",0},{"processApiCalls",0},{"scope","pure context and CLI suffix decisions; not shared-mapping or hardware acceptance"}};
    if(argc==2){std::ofstream out(argv[1]);out<<report.dump(2)<<'\n';if(!out)return 3;}std::cout<<report.dump()<<'\n';return failed?1:0;
}
