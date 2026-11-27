      ,{"runtime-read-detect-pending",[]{
        beginRead();{std::lock_guard<std::mutex> lock(powerMutex);nativePowerFact.flags=7;}
        const auto r=callRead("lossless.detect");expect(unavailableRead("lossless.detect",r),"detect-pending-must-return-unknown");
        expect(readDiscoveries==0&&ymcc::losslessUiaFixture::factoryCalls==0,"detect-pending-must-not-start-process-or-uia-read");expectReadResources();
      }}
      ,{"runtime-read-state-pending",[]{
        beginRead();{std::lock_guard<std::mutex> lock(powerMutex);nativePowerFact.flags=7;}
        const auto r=callRead("lossless.getState");expect(unavailableRead("lossless.getState",r),"state-pending-must-return-unavailable");
        expect(ymcc::losslessUiaFixture::processOpened==0&&ymcc::losslessUiaFixture::factoryCalls==0,"state-pending-must-not-start-process-or-uia-read");expectReadResources();
      }}
      ,{"runtime-read-normal-publication-and-schema",[]{
        for(const auto* command: {"lossless.detect","lossless.getState"}){
            beginRead();std::atomic<bool> first{true};ymcc::losslessUiaFixture::callHook=[&](const char* stage){if(std::string(stage)=="name"&&first.exchange(false)){std::lock_guard<std::mutex> lock(powerMutex);++nativePowerFact.publicationRevision;}};
            const auto r=callRead(command);expect(readableRead(command,r),"diagnostic-publication-keeps-current-read-valid");expectReadResources();
            if(std::string(command)=="lossless.detect")expect(r.value("powerGeneration",0ULL)==nativePowerFact.generation&&
                r.value("powerNotificationSequence",0ULL)==nativePowerFact.observedNotificationSequence&&
                r.contains("powerSuspendSerial")&&r.value("powerSuspendSerial",0ULL)==nativePowerFact.suspendSerial,
                "detect-positive-reply-carries-original-shared-power-scope");
            bool rejected=false;try{readEntries.at(command)(json{{"unexpected",true}});}catch(const std::runtime_error&){rejected=true;}
            expect(rejected,"read-preserves-original-invalid-schema-rejection");
            for(unsigned stateIndex=0;stateIndex!=4;++stateIndex){reset();beginRead();{std::lock_guard<std::mutex> lock(powerMutex);if(stateIndex==0)known=false;else if(stateIndex==1)nativePowerFact.phase=1;else if(stateIndex==2)nativePowerFact.flags=1;else g_exitRequested=true;}
                expect(unavailableRead(command,callRead(command)),"unknown-nonready-closed-or-exit-refuses-read");expect(ymcc::losslessUiaFixture::factoryCalls==0&&ymcc::losslessUiaFixture::processOpened==0,"refused-read-performs-no-new-process-or-uia-work");expectReadResources();}
            reset();beginRead();
        }
        discoverAbsent=true;const auto absent=callRead("lossless.detect");expect(absent.at("known")==true&&absent.at("running")==false&&ymcc::losslessUiaFixture::factoryCalls==0,"current-complete-absence-is-not-power-unknown");
      }}
      ,{"runtime-read-cancel-between-properties",[]{
        for(const auto* command: {"lossless.detect","lossless.getState"})for(unsigned change=0;change!=7;++change){
            reset();beginRead();std::atomic<bool> first{true};ymcc::losslessUiaFixture::callHook=[&](const char* stage){if(std::string(stage)!="name"||!first.exchange(false))return;
                {std::lock_guard<std::mutex> lock(powerMutex);if(change==0)++nativePowerFact.generation;else if(change==1)++nativePowerFact.suspendSerial;else if(change==2)++nativePowerFact.observedNotificationSequence;else if(change==3)nativePowerFact.phase=1;else if(change==4)nativePowerFact.flags=7;else if(change==5)known=false;else g_exitRequested=true;}
                revocations.revoke();};
            const auto r=callRead(command);expect(unavailableRead(command,r),"changed-public-proof-cannot-return-stale-readable-result");
            expect(ymcc::losslessUiaFixture::propertyNameCalls==1&&ymcc::losslessUiaFixture::rootCalls==1,"cancellation-after-first-property-stops-later-uia-query");expectReadResources();
        }
        reset();beginRead();
      }}
      ,{"runtime-read-inflight-return-and-fresh-ready",[]{
        for(const auto* command: {"lossless.detect","lossless.getState"}){
            reset();beginRead();Gate gate;std::atomic<bool> first{true};ymcc::losslessUiaFixture::callHook=[&](const char* stage){if(std::string(stage)=="find"&&first.exchange(false))gate.enter();};json result;
            std::thread request([&]{result=callRead(command);});const bool entered=gate.seen();expect(entered,"read-is-held-inside-original-uia-find");
            const auto borrowed=borrowedCancellation.load();HANDLE duplicate=nullptr;const bool copied=borrowed&&DuplicateHandle(GetCurrentProcess(),borrowed,GetCurrentProcess(),&duplicate,SYNCHRONIZE,FALSE,0);expect(copied,"inflight-read-holds-real-public-cancellation-registration");
            transition(1,5);expect(ymcc::losslessUiaFixture::clientsLive==1&&ymcc::losslessUiaFixture::initCalls==ymcc::losslessUiaFixture::uninitCalls+1,"cancel-does-not-falsely-return-client-or-apartment-before-sdk-returns");
            gate.open();request.join();expect(unavailableRead(command,result),"inflight-canceled-read-returns-unavailable");
            expect(ymcc::losslessUiaFixture::propertyNameCalls==0,"canceled-find-does-not-start-property-get");
            expect(duplicate&&WaitForSingleObject(duplicate,0)==WAIT_OBJECT_0,"borrowed-public-event-remains-signaled-after-read-resource-return");if(duplicate)CloseHandle(duplicate);expectReadResources();
            transition(0,3,true);ymcc::losslessUiaFixture::callHook={};expect(readableRead(command,callRead(command)),"fresh-request-on-current-ready-self-heals-without-old-result-replay");expectReadResources();
        }
      }}
