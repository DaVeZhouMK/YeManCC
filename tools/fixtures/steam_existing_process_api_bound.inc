// Explicit, one-shot field selection of the existing public read-only channel.
// No candidate/business method invocation, PID registration or default group.
#include <fstream>
#include "steam_information_client.h"
namespace existing_process_api_field {
using Json=nlohmann::json;
struct Held {
    HANDLE process=nullptr;
    ~Held(){if(process)CloseHandle(process);}
    Held()=default;Held(const Held&)=delete;Held& operator=(const Held&)=delete;
    bool bind(const Json& row){
        if(process||!row.is_object()||row.size()!=4||!row.at("pid").is_number_unsigned()||
            !row.at("birth").is_string()||!row.at("path").is_string()||!row.at("session").is_number_unsigned())return false;
        const auto rawPid=row.at("pid").get<std::uint64_t>(),rawSession=row.at("session").get<std::uint64_t>();
        const auto birthText=row.at("birth").get<std::string>();
        if(!rawPid||rawPid>MAXDWORD||rawSession>MAXDWORD||birthText.empty()||birthText.size()>20||
            birthText.find_first_not_of("0123456789")!=std::string::npos)return false;
        const auto born=std::stoull(birthText);if(!born)return false;
        process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,static_cast<DWORD>(rawPid));
        if(!process)return false;
        FILETIME creation{},exit{},kernel{},user{};DWORD session=0;wchar_t image[32768]{};DWORD size=static_cast<DWORD>(std::size(image));
        return GetProcessId(process)==rawPid&&GetProcessTimes(process,&creation,&exit,&kernel,&user)&&
            ((std::uint64_t(creation.dwHighDateTime)<<32)|creation.dwLowDateTime)==born&&
            QueryFullProcessImageNameW(process,0,image,&size)&&ProcessIdToSessionId(static_cast<DWORD>(rawPid),&session)&&
            session==rawSession&&ymcc::steamlive::normalizedPath(std::wstring(image,size))==
                ymcc::steamlive::normalizedPath(ymcc::steaminfo::utf16(row.at("path").get<std::string>()))&&current();
    }
    bool current()const noexcept{return process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;}
    bool close()noexcept{if(!process)return true;const auto old=process;process=nullptr;return CloseHandle(old)!=FALSE;}
};
inline int runField(const char* file,bool continuation=false){
    Json report={{"schema","ymcc.existing-process-api-metadata-field.v1"},{"steamBusinessMethodsInvoked",false},
        {"registrationAttempted",false},{"mutatedBusinessState",false},{"sleepActions",0},
        {"sourceAccountGenerationAuthority",false},{"metadataOnlyNotNativeSignature",true},
        {"catalogComplete",false},{"surfaceScope","fixed namespaces, filtered names, at most two prototype levels"}};
    Held main,steam,game;int code=2;
    try{
        std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("bound-field-input-missing");
        input.seekg(0,std::ios::end);const auto length=input.tellg();if(length<=0||length>16384)throw std::runtime_error("bound-field-input-size");input.seekg(0);
        std::string bytes(static_cast<std::size_t>(length),'\0');if(!input.read(bytes.data(),length))throw std::runtime_error("bound-field-input-read");
        const auto b=Json::parse(bytes);
        if(!b.is_object()||b.size()!=3||!main.bind(b.at("main"))||!steam.bind(b.at("steam"))||!game.bind(b.at("game")))throw std::runtime_error("bound-field-object-not-current");
        const auto begin=GetTickCount64(),deadline=begin+(continuation?45000:20000);
        const auto current=[&]{return main.current()&&steam.current()&&game.current()&&GetTickCount64()<deadline;};
        ymcc::steamsession::Session expected;
        expected.pid=b.at("steam").at("pid").get<DWORD>();expected.created=std::stoull(b.at("steam").at("birth").get<std::string>());
        const auto steamImage=ymcc::steaminfo::utf16(b.at("steam").at("path").get<std::string>());
        if(ymcc::steamlive::normalizedPath(std::filesystem::path(steamImage).filename().wstring())!=L"steam.exe")throw std::runtime_error("bound-field-steam-image-not-exact");
        expected.root=ymcc::steamlive::normalizedPath(std::filesystem::path(steamImage).parent_path().wstring());
        // The actual public discovery operation returns a UI observation, not
        // a source-account epoch or registration/action permission.
        const auto before=ymcc::steamlive::run(expected.root,{{"operation","sleep.session.inspect"}},{},4000,current,&expected);
        {auto safe=before;safe.erase("observedAccount");report["uiAccountDiscovery"]=std::move(safe);}
        if(!before.value("ok",false)||before.value("mutated",true)||!before.contains("observedAccount")||
            !before.at("observedAccount").is_number_unsigned())throw std::runtime_error("bound-field-ui-account-unavailable");
        const auto account=before.at("observedAccount").get<std::uint64_t>();if(!account||account>MAXDWORD||!current())throw std::runtime_error("bound-field-ui-account-invalid");
        Json calls=Json::array();
        unsigned cursor=24;bool tailComplete=false;
        report["cursorContinuation"]=continuation;
        report["initialCursor"]=continuation?Json(24):Json(nullptr);
        report["crossRequestAtomic"]=false;
        for(unsigned index=0;index<(continuation?8u:2u);++index){
            const char* operation=continuation||index==0?"diagnostics.apps.api.read":"diagnostics.apps.contract.read";
            if(!current())throw std::runtime_error("bound-field-current-revoked");
            const auto policy=ymcc::steamrequest::classify(operation);
            if(!policy.known||!policy.shortRead||policy.setter||policy.coordinatedEffect)throw std::runtime_error("bound-field-fixed-read-policy-missing");
            Json request={{"operation",operation},{"account",account}};
            if(continuation)request["cursor"]=cursor;
            auto result=ymcc::steamlive::run(expected.root,request,{},4000,current,&expected);
            result.erase("observedAccount");
            calls.push_back({{"operation",operation},{"cursor",continuation?Json(cursor):Json(nullptr)},{"result",result}});
            if(!result.value("ok",false)||result.value("mutated",true)){
                report["calls"]=std::move(calls);throw std::runtime_error("bound-field-metadata-read-unconfirmed");
            }
            if(continuation){
                const Json* app=nullptr;
                if(result.contains("surfaces")&&result.at("surfaces").is_array())
                    for(const auto& surface:result.at("surfaces"))
                        if(surface.value("label",std::string{})=="SteamClient.Apps"){app=&surface;break;}
                if(!app||!app->contains("nextCursor")||!app->at("nextCursor").is_number_integer()||
                   !app->contains("hasMore")||!app->at("hasMore").is_boolean()){
                    report["calls"]=std::move(calls);throw std::runtime_error("bound-field-cursor-unavailable");
                }
                const auto next=app->at("nextCursor").get<std::int64_t>();
                const bool more=app->at("hasMore").get<bool>();
                const auto status=app->value("status",std::string{}),reason=app->value("reason",std::string{});
                const bool budget=reason=="namespace-budget"||reason=="catalog-budget"||reason=="sequence-limit";
                if(!((status=="inspected"&&reason.empty())||(status=="partial"&&budget))){
                    report["calls"]=std::move(calls);throw std::runtime_error("bound-field-cursor-sequence-unconfirmed");
                }
                if(next<static_cast<std::int64_t>(cursor)||next>256||(more&&next==cursor)){
                    report["calls"]=std::move(calls);throw std::runtime_error("bound-field-cursor-not-monotonic");
                }
                cursor=static_cast<unsigned>(next);report["lastNextCursor"]=cursor;
                if(!more){tailComplete=true;report["cursorStopReason"]="reported-sequence-end-not-complete-api";break;}
                if(cursor==256){report["cursorStopReason"]="absolute-position-cap";break;}
            }
        }
        if(continuation){report["appsTailReportedEnd"]=tailComplete;
            if(!report.contains("cursorStopReason"))report["cursorStopReason"]="eight-read-cap";}
        auto after=ymcc::steamlive::run(expected.root,{{"operation","sleep.session.inspect"}},{},4000,current,&expected);
        {auto safe=after;safe.erase("observedAccount");report["uiAccountFinalRead"]=std::move(safe);}
        if(!after.value("ok",false)||after.value("mutated",true)||after.value("observedAccount",std::uint64_t{})!=account||!current()){
            report["calls"]=std::move(calls);throw std::runtime_error("bound-field-ui-account-changed");
        }
        report["calls"]=std::move(calls);report["uiAccountEqualAtBothEndpoints"]=true;
        report["accountABAExcluded"]=false;report["status"]="BOUND_PUBLIC_FIXED_API_METADATA_OBSERVED";
        report["elapsedMs"]=GetTickCount64()-begin;report["binding"]=b;
        report["accountReadCallsAreNotBusinessMethodCalls"]=true;code=0;
    }catch(const std::exception& e){report["status"]="UNCONFIRMED";report["reason"]=e.what();}
    const bool mainClosed=main.close(),steamClosed=steam.close(),gameClosed=game.close();
    report["heldObjectsClosed"]={{"main",mainClosed},{"steam",steamClosed},{"game",gameClosed}};
    if(!mainClosed||!steamClosed||!gameClosed)code=3;
    std::cout<<report.dump(2)<<"\n";return code;
}
inline std::optional<int> selected(int argc,char** argv){
    if(argc>1&&(std::string_view(argv[1])=="--live-bound-api-read"||std::string_view(argv[1])=="--live-bound-api-cursor-read")){
        if(argc!=4||std::string_view(argv[2])!="--binding")return 2;
        return runField(argv[3],std::string_view(argv[1])=="--live-bound-api-cursor-read");
    }
    return {};
}
}
