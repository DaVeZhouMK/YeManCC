#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <iostream>
#include "steam_document_read.h"

using namespace ymcc::steamdocuments;
using ymcc::steamlive::Endpoint;

int main(){
    const Endpoint menuEndpoint{33,8080},mainEndpoint{33,8081},alias{33,8081,L"::1"};
    auto page=[](const Endpoint& endpoint,const char* id,const char* title,const char* url){
        return Json{{"id",id},{"type","page"},{"title",title},{"url",url},
            {"webSocketDebuggerUrl","ws://127.0.0.1:"+std::to_string(endpoint.port)+"/devtools/page/"+id}};
    };
    auto mainPage=[&](const Endpoint& endpoint){return page(endpoint,"main","Steam Big Picture","https://steamloopback.host/index.html?browserType=3");};
    const Json popup=page(menuEndpoint,"popup","MainMenu_uid1","about:blank");
    const Json quick=page(mainEndpoint,"quick","QuickAccess_uid2","about:blank");
    const Json complete=Json::array({mainPage(mainEndpoint),quick});
    unsigned checks=0,failed=0;
    auto run=[&](const char* label,std::vector<Endpoint> candidates,Json first,Json second,
                 unsigned expectedProbes,unsigned expectedTargets,const char* expectedError="",bool revoke=false,bool lostFirst=false){
        ++checks;
        unsigned calls=0,rejections=0;
        TargetSourceFailure source;
        bool current=true,correct=false;
        std::string error;
        auto checkpoint=[&]{if(!current)throw std::runtime_error("document-read-cancelled");};
        auto read=[&](const Endpoint& candidate){
            ++calls;
            if(revoke)current=false;
            if(lostFirst&&candidate==menuEndpoint)throw std::runtime_error("document-transport-unavailable");
            return candidate==menuEndpoint?first:second;
        };
        auto rejected=[&](const std::exception&,bool){++rejections;};
        try{
#ifdef YMCC_DOCUMENT_ENDPOINT_PREIMAGE
            auto selected=ymcc::steamdiscovery::select<std::vector<Target>>(ymcc::steamdiscovery::Kind::Documents,
                candidates,checkpoint,[](const auto&){return true;},[&](const auto& candidate)->std::optional<std::vector<Target>>{
                    auto found=targets(read(candidate),candidate,&source);
                    if(found.empty())return {};return found;
                },rejected);
#else
            auto selected=selectDocumentTargetEndpoint(candidates,source,checkpoint,read,rejected);
#endif
            correct=expectedTargets?selected.endpoint==mainEndpoint&&selected.payload&&selected.payload->size()==expectedTargets&&
                std::any_of(selected.payload->begin(),selected.payload->end(),[](const Target& target){return target.kind==Kind::BigPicture;}):
                !selected.endpoint&&!selected.payload;
        }catch(const std::exception& e){error=e.what();correct=!error.empty()&&error==expectedError;}
        if(!*expectedError&&(!correct||!error.empty()))correct=false;
        if(*expectedError&&error!=expectedError)correct=false;
        if(error=="document-source-unconfirmed"&&(!source.captured||source.kind!=Kind::MainMenu))correct=false;
        if(calls!=expectedProbes||rejections!=static_cast<unsigned>(lostFirst))correct=false;
        if(!correct)++failed;
        std::cout<<(correct?"PASS ":"FAIL ")<<label<<" probes="<<calls<<" rejected="<<rejections<<" error="<<error<<'\n';
    };
    auto foreign=popup;foreign["url"]="https://invalid.example/";
    auto invalidMain=mainPage(mainEndpoint);invalidMain["webSocketDebuggerUrl"]="ws://127.0.0.1:8080/devtools/page/main";
    run("auxiliary-prefix-does-not-mask-main",{menuEndpoint,mainEndpoint},Json::array({popup}),complete,2,2);
    run("auxiliary-tail-does-not-mask-main",{mainEndpoint,menuEndpoint},Json::array({popup}),complete,2,2);
    run("auxiliary-only-has-no-candidate",{menuEndpoint},Json::array({popup}),Json::array(),1,0);
    run("empty-list-then-main",{menuEndpoint,mainEndpoint},Json::array(),Json::array({mainPage(mainEndpoint)}),2,1);
    run("main-alone-remains-eligible",{mainEndpoint},Json::array(),Json::array({mainPage(mainEndpoint)}),1,1);
    run("two-distinct-main-endpoints-still-ambiguous",{menuEndpoint,mainEndpoint},Json::array({mainPage(menuEndpoint)}),complete,2,0,"document-context-ambiguous");
    run("selected-endpoint-alias-not-probed-again",{mainEndpoint,alias},Json::array(),complete,1,2);
    run("invalid-auxiliary-origin-not-hidden",{menuEndpoint,mainEndpoint},Json::array({foreign}),complete,1,0,"document-source-unconfirmed");
    run("invalid-auxiliary-tail-origin-not-hidden",{mainEndpoint,menuEndpoint},Json::array({foreign}),complete,2,0,"document-source-unconfirmed");
    run("foreign-main-socket-not-accepted",{mainEndpoint},Json::array(),Json::array({invalidMain}),1,0,"document-targets-invalid");
    run("revoked-during-auxiliary-probe",{menuEndpoint,mainEndpoint},Json::array({popup}),complete,1,0,"document-read-cancelled",true);
    run("unavailable-prefix-still-falls-through",{menuEndpoint,mainEndpoint},Json::array(),complete,2,2,"",false,true);
    std::cout<<"DOCUMENT_ENDPOINT_SELFTEST checks="<<checks<<" failed="<<failed<<" realSteam=0\n";
    return failed?1:0;
}
