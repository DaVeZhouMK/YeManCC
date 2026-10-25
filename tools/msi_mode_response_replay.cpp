// Offline response decoder regression. Never calls queryMode, officialStack,
// opens a HID handle, changes settings, starts YMCC or runs the runtime hosts.
#include "../native/msi_controller_cause_diagnostics.h"
#include <fstream>
#include <iostream>
#pragma comment(lib,"hid.lib")
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"user32.lib")
int wmain(int argc,wchar_t** argv){
    using nlohmann::json;using ymcc::msi::cause::response;
    if(argc!=2)return 2;std::ifstream in(argv[1]);json fixture;in>>fixture;
    const auto bytes=fixture.at("bytes").get<std::vector<BYTE>>();unsigned cases=0,failed=0;
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    auto actual=response(bytes.data(),bytes.size());check("real 0x10 response prefix is accepted",actual.value("modeKnown",false)&&actual.at("observedMode")==fixture.at("expectedMode"));
    check("real prefix unchanged and no full packet invented",actual.at("first16")==fixture.at("bytes")&&actual.at("length")==16&&fixture.at("recordedLength")==64);
    check("decoder cannot grant physical control or writer proof",actual.at("deviceModeVerified")==false&&actual.at("modeWriterProven")==false);
    check("parser version separates old misclassification",actual.at("responseParserVersion")==2);
    auto copy=bytes;copy[0]=15;check("request-style report ID rejected",!response(copy.data(),copy.size()).value("modeKnown",true));
    copy=bytes;copy[4]=38;check("query echo rejected",!response(copy.data(),copy.size()).value("modeKnown",true));
    copy=bytes;copy[4]=36;check("switch command echo rejected",!response(copy.data(),copy.size()).value("modeKnown",true));
    copy=bytes;copy[3]=0;check("unexpected envelope rejected",!response(copy.data(),copy.size()).value("modeKnown",true));
    copy=bytes;copy[5]=3;check("unrecognized mode remains unknown",!response(copy.data(),copy.size()).value("modeKnown",true));
    check("truncated reply rejected",!response(bytes.data(),5).value("modeKnown",true));
    for(BYTE mode:{BYTE(1),BYTE(2),BYTE(4)}){copy=bytes;copy[5]=mode;auto r=response(copy.data(),copy.size());check("synthetic recognized mode values parsed without control authority",r.value("modeKnown",false)&&r.at("observedMode")==mode&&!r.value("deviceModeVerified",true));}
    std::cout<<"cases="<<cases<<" failed="<<failed<<"\n";return failed?3:0;
}
