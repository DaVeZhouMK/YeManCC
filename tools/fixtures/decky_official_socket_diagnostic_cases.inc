// Optional boundary of the original actual BootstrapWatch consumer.
static int officialSocketObservationOnly(const char* output){
    unsigned checks=0;const auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);++checks;};
    Json raw={{"selection","--official-socket-observation-only"},{"actualBootstrapWatch",true},{"realSteam",false},{"newCanonicalIds",0}};
    DWORD handlesBefore=0,handlesAfter=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 2;
    try{
        {
            WatchFixture f;check(f.startFailure.empty()&&f.stage("context-attached"),"original watched context not ready");
            const auto observations=[&](){std::lock_guard lock(f.mutex);return static_cast<unsigned>(std::count_if(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="official-socket-close";}));};
            check(observations()==0,"healthy startup emitted optional socket facts");
            const auto packet=[&](Json value,int context=101){return Json{{"method","Runtime.bindingCalled"},{"params",{{"name","__YMCC_DECKY_HEALTH_V1"},{"executionContextId",context},{"payload",value.dump()}}}};};
            const auto value=Json{{"runId",f.cdp.run},{"code","decky-socket-close"},{"ordinal",1},{"codeKnown",true},{"closeCode",1006},{"cleanKnown",true},{"wasClean",false},{"opened",true},{"pendingCallsKnown",true},{"pendingCalls",3}};
            const auto initialReads=f.cdp.healthReads.load();const auto initialEvaluations=f.cdp.count();
            f.cdp.send(packet(value));
            check(f.stage("official-socket-close",600),"current socket close facts never reached original native diagnostic sink");
            check(observations()==1,"first optional socket observation duplicated");
            {
                std::lock_guard lock(f.mutex);const auto found=std::find_if(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="official-socket-close";});
                const auto logged=ymcc::deckydiag::record("bootstrap-watch",*found);
                check(logged.contains("officialSocketClose"),"actual log field filter lost official close facts");
                check(logged.at("officialSocketClose").at("closeCode")==1006,"actual log changed official close code");
                check(logged.at("officialSocketClose").at("pendingCallsKnown")==true&&logged.at("officialSocketClose").at("pendingCalls")==3,"actual log lost pending official replies");
                check(!logged.contains("runId")&&!logged.contains("token")&&!logged.contains("endpoint"),"optional log exposed binding identity");
            }
            for(unsigned n=0;n<1000;++n)f.cdp.send(packet(value));
            auto other=value;other["runId"]=std::string(32,'a');f.cdp.send(packet(other));f.cdp.send(packet(value,999));
            for(const auto& field:{"ordinal","codeKnown","closeCode","cleanKnown","wasClean","opened","pendingCallsKnown","pendingCalls"}){other=value;other[field]="private-token";f.cdp.send(packet(other));}
            other=value;other["closeCode"]=-1;f.cdp.send(packet(other));other["closeCode"]=65536;f.cdp.send(packet(other));
            other=value;other["ordinal"]=0;f.cdp.send(packet(other));other["ordinal"]=17;f.cdp.send(packet(other));
            other=value;other["codeKnown"]=false;f.cdp.send(packet(other));other=value;other["secret"]="do-not-log";f.cdp.send(packet(other));
            other=value;other["code"]="decky-render-failed";f.cdp.send(packet(other));
            for(const auto bad:Json::array({-1,4294967296ull,1.5,nullptr})){other=value;other["pendingCalls"]=bad;f.cdp.send(packet(other));}
            other=value;other["pendingCallsKnown"]=false;f.cdp.send(packet(other));
            other=value;other.erase("pendingCalls");f.cdp.send(packet(other));
            f.cdp.created(105);check(f.cdp.wait(105),"invalid observation drain barrier absent");
            {std::unique_lock lock(f.mutex);check(f.changed.wait_for(lock,std::chrono::seconds(4),[&]{return std::any_of(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="context-attached"&&row.value("contextGeneration",0u)==2;});}),"drain attachment not complete");}
            check(observations()==1,"duplicate or malformed observation entered diagnostic sink");
            check(f.cdp.healthReads==initialReads+1&&f.cdp.count()==initialEvaluations+1,"passive observation caused additional CDP reads");
            check(f.cdp.accountChecks==0&&f.cdp.teardowns==0,"passive socket facts became an account or fatal action");
            for(unsigned n=0;n<30;++n){other=value;other["ordinal"]=1+n%16;other["closeCode"]=2000+n;if(n==0){other["pendingCallsKnown"]=false;other.erase("pendingCalls");}else if(n==1)other["pendingCalls"]=0;else if(n==2)other["pendingCalls"]=4294967295ull;f.cdp.send(packet(other,105));}
            f.cdp.created(106);check(f.cdp.wait(106),"cap drain barrier absent");
            {std::unique_lock lock(f.mutex);check(f.changed.wait_for(lock,std::chrono::seconds(4),[&]{return std::any_of(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="context-attached"&&row.value("contextGeneration",0u)==3;});}),"cap attachment not complete");}
            check(observations()==16,"optional native observation ceiling changed");
            {std::lock_guard lock(f.mutex);bool unknown=false,zero=false,maximum=false;for(const auto& row:f.records){if(row.value("bootstrapStage",std::string{})!="official-socket-close")continue;const auto facts=ymcc::deckydiag::record("bootstrap-watch",row).value("officialSocketClose",Json{});if(facts.value("pendingCallsKnown",true)==false&&!facts.contains("pendingCalls"))unknown=true;if(facts.value("pendingCallsKnown",false)&&facts.value("pendingCalls",-1ll)==0)zero=true;if(facts.value("pendingCallsKnown",false)&&facts.value("pendingCalls",0ull)==4294967295ull)maximum=true;}check(unknown&&zero&&maximum,"native log must distinguish unknown/known-zero/known-maximum pending calls");}
            raw["recordedUnique"]=observations();raw["duplicatePackets"]=1000;
            check(f.watch.stop(),"original watch retirement incomplete");
            const auto end=GetTickCount64()+1500;while((!f.observedSocket.expired()||!f.observedSession.expired()||!f.observedConnection.expired())&&GetTickCount64()<end)Sleep(1);
            check(f.observedSocket.expired()&&f.observedSession.expired()&&f.observedConnection.expired(),"original callback parents not returned");
            raw["callbackParentsReturned"]=true;
        }
        raw["passed"]=true;
    }catch(const std::exception& error){raw["passed"]=false;raw["error"]=error.what();}
    WSACleanup();GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);
    raw["checks"]=checks;raw["handlesBefore"]=handlesBefore;raw["handlesAfter"]=handlesAfter;
    raw["processHandleScope"]="cold-before-first-WinHTTP-vs-after;ownership-and-cause-unknown";
    const auto text=raw.dump(2);const HANDLE file=CreateFileA(output,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return 2;DWORD written=0;const bool wrote=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)!=FALSE&&written==text.size();
    const bool closed=CloseHandle(file)!=FALSE;return wrote&&closed?(raw.value("passed",false)?0:1):2;
}

// Only previously unexecuted optional-log failure/re-arm boundary.
static int officialSocketObservationContinuation(const char* output){
    Json raw={{"selection","--official-socket-observation-continuation"},{"actualBootstrapWatch",true},{"realSteam",false},{"newCanonicalIds",0}};
    unsigned checks=0;const auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);++checks;};
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 2;
    try{
        Json counts=Json::array();
        for(unsigned round=0;round<2;++round){
            WatchFixture f;check(f.startFailure.empty()&&f.stage("context-attached"),"new original watch attachment missing");
            const auto packet=[&](Json value){return Json{{"method","Runtime.bindingCalled"},{"params",{{"name","__YMCC_DECKY_HEALTH_V1"},{"executionContextId",101},{"payload",value.dump()}}}};};
            auto value=Json{{"runId",f.cdp.run},{"code","decky-socket-close"},{"ordinal",1},{"codeKnown",true},{"closeCode",1006},{"cleanKnown",true},{"wasClean",false},{"opened",true}};
            f.failOptionalLog=true;f.cdp.send(packet(value));value["ordinal"]=2;value["closeCode"]=1001;f.cdp.send(packet(value));
            check(f.stage("official-socket-close"),"optional sink failure stopped the existing watch");
            {
                std::lock_guard lock(f.mutex);const auto rows=std::count_if(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="official-socket-close";});
                check(rows==1,"failed optional diagnostic was replayed");
                const auto found=std::find_if(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="official-socket-close";});
                check(found->at("officialSocketClose").at("ordinal")==2,"next actual diagnostic not retained");
                check(std::none_of(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="failed";}),"optional diagnostic failure became a transport failure");
                const auto detail=*found;check(!ymcc::deckydiag::record("runtime",detail).contains("officialSocketClose"),"passive socket facts escaped event allowlist");
                auto bad=detail;bad["officialSocketClose"]["token"]="secret";
                check(!ymcc::deckydiag::record("bootstrap-watch",bad).contains("officialSocketClose"),"log sink retained an arbitrary field");
            }
            check(f.cdp.enables==1&&f.cdp.accountChecks==0&&f.cdp.teardowns==0&&f.cdp.healthReads==1,"optional log failure adds reads or action");
            check(f.watch.stop(),"continuation watch cleanup incomplete");
            const auto end=GetTickCount64()+1500;while((!f.observedSocket.expired()||!f.observedSession.expired()||!f.observedConnection.expired())&&GetTickCount64()<end)Sleep(1);
            check(f.observedSocket.expired()&&f.observedSession.expired()&&f.observedConnection.expired(),"continuation original callback parents not returned");
            DWORD count=0;GetProcessHandleCount(GetCurrentProcess(),&count);counts.push_back(count);
        }
        raw.update({{"passed",true},{"rounds",2},{"callbackParentsReturned",true},{"processHandlesAfterEachStoppedWatch",counts},{"processHandleScope","WinHTTP process pool not attributed; exact own callback parents returned"}});
    }catch(const std::exception& error){raw["passed"]=false;raw["error"]=error.what();}
    raw["checks"]=checks;WSACleanup();const auto text=raw.dump(2);
    const HANDLE file=CreateFileA(output,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return 2;
    DWORD written=0;const bool wrote=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)!=FALSE&&written==text.size();const bool closed=CloseHandle(file)!=FALSE;
    return wrote&&closed?(raw.value("passed",false)?0:1):2;
}

// Current facts may arrive while an original health command is waiting.
static int officialSocketObservationReplay(const char* output){
    Json raw={{"selection","--official-socket-observation-replay"},{"actualBootstrapWatch",true},{"realSteam",false},{"newCanonicalIds",0}};
    unsigned checks=0;const auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);++checks;};
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 2;
    try{
        {
            WatchFixture f;check(f.startFailure.empty()&&f.stage("context-attached"),"replay original initial attachment missing");
            callbackReplayMode=6;f.cdp.created(105);check(f.cdp.wait(105),"real health command not reached");
            {std::unique_lock lock(f.mutex);check(f.changed.wait_for(lock,std::chrono::seconds(4),[&]{return std::any_of(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="context-attached"&&row.value("contextGeneration",0u)==2;});}),"interleaved observations blocked original command reply");
                check(std::count_if(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="official-socket-close";})==1,"interleaved observations not deduplicated");
                check(std::none_of(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="failed";}),"passive facts exhausted original bounded event queue");}
            check(f.cdp.enables==1&&f.cdp.count()==2&&f.cdp.healthReads==2,"interleaved diagnostics created extra subscriptions or commands");
            check(f.cdp.accountChecks==0&&f.cdp.teardowns==0,"interleaved diagnostics acquired fatal/account authority");
            check(f.watch.stop(),"replay original watch cleanup incomplete");
            const auto end=GetTickCount64()+1500;while((!f.observedSocket.expired()||!f.observedSession.expired()||!f.observedConnection.expired())&&GetTickCount64()<end)Sleep(1);
            check(f.observedSocket.expired()&&f.observedSession.expired()&&f.observedConnection.expired(),"replay original callback parents not returned");
        }
        raw.update({{"passed",true},{"interleavedPackets",1000},{"callbackParentsReturned",true}});
    }catch(const std::exception& error){raw["passed"]=false;raw["error"]=error.what();}
    raw["checks"]=checks;WSACleanup();const auto text=raw.dump(2);const HANDLE file=CreateFileA(output,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return 2;
    DWORD written=0;const bool wrote=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)!=FALSE&&written==text.size();const bool closed=CloseHandle(file)!=FALSE;return wrote&&closed?(raw.value("passed",false)?0:1):2;
}
