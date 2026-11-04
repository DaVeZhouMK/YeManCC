    // Actual native discovery/lifecycle + exact suspended managed fixture child.
    // The real machine selector, sole catalog and shipping helper parser are used.
    {
        auto check=[&](bool ok,const std::string& id){
            ++checks;if(!ok){std::cerr<<"SUPERJOY_COMPOSITION_FAILED: "<<id<<"\n";throw std::runtime_error(id);}
        };
        const auto originalFamily=g_machineIdentity.family;
        const auto originalHelper=kLightSetterExe;
        g_sjMetadataFail=false;g_sjResumeFail=false;
        g_machineIdentity.deviceClass="HandheldCompanion.Devices.AYANEO3";
        g_machineIdentity.family=YmccFamily::Unknown;
        superJoyLightingOwnerResume();
        check(superJoyLightingEnsureNotification(),"superjoy:explicit-notification-port-ready");
        const std::wstring path=L"\\\\?\\hid#vid_1c4f&pid_0002#software-vendor-interface";
        const SuperJoyFixtureCandidate good{path};
        std::wstring selected;std::string reason;
        g_sjCandidates={good};
        check(superJoyLightingSelectInterface(selected,reason)&&selected==path,"superjoy:exact-descriptor-selected");
        for(int field=0;field<6;field++){
            auto bad=good;
            if(field==0)bad.vendor=0x1234;if(field==1)bad.product=3;if(field==2)bad.page=1;
            if(field==3)bad.usage=2;if(field==4)bad.input=64;if(field==5)bad.output=64;
            g_sjCandidates={bad};
            check(!superJoyLightingSelectInterface(selected,reason)&&selected.empty(),"superjoy:wrong-descriptor-field-"+std::to_string(field));
        }
        auto other=good;other.path+=L"-other";g_sjCandidates={good,other};
        check(!superJoyLightingSelectInterface(selected,reason)&&reason=="superjoy-endpoint-ambiguous","superjoy:two-exact-endpoints-not-guessed");
        auto unreadable=good;unreadable.readable=false;g_sjCandidates={unreadable,good};
        check(!superJoyLightingSelectInterface(selected,reason)&&reason=="superjoy-endpoint-observation-failed","superjoy:unobserved-candidate-no-fallback");
        g_sjCandidates={good};g_sjDiscoveryMutation=[] {g_superJoyLightingTopology.fetch_add(1);};
        check(!superJoyLightingSelectInterface(selected,reason)&&reason=="superjoy-owner-context-changed","superjoy:topology-change-in-discovery-refused");
        g_sjDiscoveryMutation=[] {g_sjFixturePower.fetch_add(1);};
        check(!superJoyLightingSelectInterface(selected,reason)&&reason=="superjoy-owner-context-changed","superjoy:power-change-in-discovery-refused");g_sjFixturePower=7;
        auto notify=[&](const std::wstring& device){
            std::vector<BYTE> buffer(offsetof(DEV_BROADCAST_DEVICEINTERFACE_W,dbcc_name)+(device.size()+1)*sizeof(wchar_t));
            auto* detail=reinterpret_cast<DEV_BROADCAST_DEVICEINTERFACE_W*>(buffer.data());
            detail->dbcc_size=static_cast<DWORD>(buffer.size());detail->dbcc_devicetype=DBT_DEVTYP_DEVICEINTERFACE;detail->dbcc_classguid=kYeManGuidDeviceInterfaceHid;
            std::copy(device.begin(),device.end(),detail->dbcc_name);detail->dbcc_name[device.size()]=L'\0';
            superJoyLightingObserveDeviceChange(DBT_DEVICEREMOVECOMPLETE,reinterpret_cast<LPARAM>(detail));
        };
        const auto epoch=g_superJoyLightingTopology.load();notify(L"\\\\?\\hid#vid_045e&pid_0001#unrelated");
        check(g_superJoyLightingTopology==epoch,"superjoy:unrelated-device-event-does-not-revoke");
        notify(path);check(g_superJoyLightingTopology==epoch+1,"superjoy:matching-device-event-advances-owner-topology");
        kLightSetterExe=superJoyManagedFixture;SetEnvironmentVariableW(L"YMCC_SUPERJOY_COMPOSITION_FIXTURE",L"1");
        SetEnvironmentVariableW(L"YMCC_SJ_COMPOSITION_EVIDENCE_ROOT",dir.c_str());
        auto childFile=[&](const std::string& name){return dirN+"\\\\superjoy-"+name+"-child.json";};
        auto invokeSuperJoy=[&](const std::string& mode,const std::string& name,bool enabled=true){
            SetEnvironmentVariableW(L"YMCC_SJ_COMPOSITION_MODE",U2W(mode).c_str());
            SetEnvironmentVariableW(L"YMCC_SJ_CHILD_RESULT",U2W(childFile(name)).c_str());
            return ymccLightSetterApply(enabled,"solid",7,101,254,99,1,200,50,33);
        };
        auto readChild=[&](const std::string& name){std::ifstream input(childFile(name));json child;input>>child;return child.at("result");};
        for(const auto* model:{"HandheldCompanion.Devices.AYANEO3","HandheldCompanion.Devices.AYANEONEXT2"}){
            MachineIdentity selectedIdentity;selectedIdentity.manufacturer="AYADEVICE";
            selectedIdentity.product=std::string(model).find("NEXT2")!=std::string::npos?"NEXT II":"AYANEO 3";
            ymccResolveFamily(selectedIdentity);
            check(selectedIdentity.resolved&&selectedIdentity.deviceClass==model&&selectedIdentity.family==YmccFamily::Ayanero,"superjoy:actual-machine-selector-to-catalog");
            g_machineIdentity=selectedIdentity;
            check(ymcc::lighting::sourceAdapter(model)==ymcc::lighting::SourceAdapter::SuperJoyCec&&superJoyLightingRouteAdmitted(),"superjoy:typed-sole-catalog-authority");
            for(bool enabled:{true,false}){
                const auto name=std::string(model).substr(25)+(enabled?"-on":"-off");
                auto value=invokeSuperJoy("ok",name,enabled);record(name.c_str(),value);
                check(value.value("ok",false)&&value.value("childOwnerAccepted",false)&&value.value("ownerGenerationStable",false)&&
                    value.value("ownerChildReturned",false)&&value.value("ownerLeaseReleased",false)&&!value.value("ownerPendingDebt",true),"superjoy:actual-parent-and-real-managed-composition-"+name);
                const auto child=readChild(name);
                check(child.value("hidWrites",0)==3&&child.value("hidReads",0)==3&&child.value("ecWrites",0)==(std::string(model).find("NEXT2")!=std::string::npos?42:22),"superjoy:model-specific-combined-protocol-counts-"+name);
                check(child.value("shippingParserAndCatalogUsed",false)&&child.value("ecDisposed",false)&&child.value("ecThreadCount",0)==1&&!child.value("hardwareIo",true)&&!child.value("productMainInvoked",true),"superjoy:joint-consumer-disposal-and-scope-"+name);
            }
        }
        g_machineIdentity.deviceClass="HandheldCompanion.Devices.AYANEO3";
        for(const auto* mode:{"missing-handback","wrong-owner-generation"}){
            auto value=invokeSuperJoy(mode,mode);record(mode,value);
            check(!value.value("ok",true)&&!value.value("resultAccepted",true)&&!value.value("childOwnerAccepted",true)&&
                value.value("ownerChildReturned",false)&&value.value("ownerLeaseReleased",false)&&value.value("reason",std::string())=="light-setter-owner-receipt-mismatch",std::string("superjoy:receipt-refusal-")+mode);
        }
        {auto value=invokeSuperJoy("cec-fail","cec-fail");const auto child=readChild("cec-fail");
         check(!value.value("ok",true)&&child.value("handbackCompleted",false)&&child.value("ecDisposed",false)&&
            child.value("hidWrites",0)==1&&child.at("ecWritesObserved").back()==json::array({191,0}),"superjoy:actual-cross-process-EC-failure-still-hands-back");}
        for(const auto* action:{"stop","power","topology"}){
            const auto name=std::string("blocked-")+action;
            const auto readyName=L"Local\\YMCC-SuperJoy-NativePending-"+std::to_wstring(GetCurrentProcessId())+L"-"+U2W(action);
            HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,readyName.c_str());
            SetEnvironmentVariableW(L"YMCC_SJ_READY_EVENT",readyName.c_str());
            std::atomic<bool> issued{false},stopped{false};
            std::thread revoke([&]{
                issued=WaitForSingleObject(ready,3000)==WAIT_OBJECT_0;
                if(!issued)return;
                if(std::string(action)=="stop")stopped=superJoyLightingOwnerStop(GetTickCount64()+1200);
                else if(std::string(action)=="power"){g_sjFixturePower.fetch_add(1);superJoyLightingOwnerTick();}
                else notify(path);
            });
            const auto value=invokeSuperJoy("blocked",name);revoke.join();CloseHandle(ready);SetEnvironmentVariableW(L"YMCC_SJ_READY_EVENT",nullptr);
            const auto child=readChild(name);
            check(issued&&!value.value("ok",true)&&!value.value("ownerGenerationStable",true)&&value.value("ownerChildReturned",false)&&value.value("ownerLeaseReleased",false),"superjoy:actual-parent-cancellation-and-return-"+name);
            check(child.value("pendingReadIssued",false)&&child.value("pendingReadCancelled",false)&&child.value("leaseRevoked",false)&&
                child.value("handbackCompleted",false)&&child.value("ecDisposed",false)&&child.value("hidWrites",0)==1,"superjoy:actual-pending-pipe-cancelled-before-next-phase-"+name);
            if(std::string(action)=="stop")check(stopped,"superjoy:stop-observes-joint-child-return");
            superJoyLightingOwnerResume();g_sjFixturePower=7;check(superJoyLightingEnsureNotification(),"superjoy:notification-rearmed-after-retirement");
            auto successor=invokeSuperJoy("ok",name+"-successor");check(successor.value("ok",false),"superjoy:independent-successor-after-return-"+name);
        }
        g_sjResumeCalls=0;g_sjMetadataFail=true;
        auto metadataFailure=invokeSuperJoy("ok","metadata-failed-before-resume");g_sjMetadataFail=false;
        check(!metadataFailure.value("ok",true)&&metadataFailure.value("ownerChildReturned",false)&&metadataFailure.value("ownerLeaseReleased",false)&&
            !metadataFailure.value("ownerPendingDebt",true)&&g_sjResumeCalls==0,"superjoy:identity-query-failure-cleans-exact-suspended-child-without-resume");
        g_sjResumeCalls=0;g_sjResumeFail=true;
        auto resumeFailure=invokeSuperJoy("ok","resume-failed");g_sjResumeFail=false;
        check(!resumeFailure.value("ok",true)&&resumeFailure.value("ownerChildReturned",false)&&resumeFailure.value("ownerLeaseReleased",false)&&
            !resumeFailure.value("ownerPendingDebt",true)&&g_sjResumeCalls==1,"superjoy:resume-failure-cleans-exact-suspended-child");
        kLightSetterExe=dir+L"\\bad-helper.exe";
        auto spawnFailure=ymccLightSetterApply(true,"solid",1,2,3,4,5,6,50,33);
        check(!spawnFailure.value("ok",true)&&spawnFailure.value("reason",std::string())=="light-setter-spawn-failed"&&
            superJoyLightingOwner().observation().phase==ymcc::superjoy_owner::Phase::Idle,"superjoy:failed-spawn-cancels-unspawned-admission");
        const auto idleReads=g_sjFixturePowerReads.load();superJoyLightingOwnerTick();
        check(g_sjFixturePowerReads==idleReads,"superjoy:idle-owner-tick-does-not-build-context-or-heartbeat");
        MachineIdentity unknown=g_machineIdentity;unknown.manufacturer="UNREVIEWED";unknown.product="UNREVIEWED";
        ymccResolveFamily(unknown);
        check(!unknown.resolved&&unknown.deviceClass.empty(),"superjoy:unknown-machine-cannot-retain-previous-admission");
        check(superJoyLightingOwnerStop(GetTickCount64()+100),"superjoy:final-idle-stop");
        kLightSetterExe=originalHelper;g_machineIdentity.family=originalFamily;
        SetEnvironmentVariableW(L"YMCC_SJ_COMPOSITION_EVIDENCE_ROOT",nullptr);SetEnvironmentVariableW(L"YMCC_SUPERJOY_COMPOSITION_FIXTURE",nullptr);SetEnvironmentVariableW(L"YMCC_SJ_CHILD_RESULT",nullptr);SetEnvironmentVariableW(L"YMCC_SJ_COMPOSITION_MODE",nullptr);
    }
