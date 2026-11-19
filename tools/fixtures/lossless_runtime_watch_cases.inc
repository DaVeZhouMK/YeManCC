      ,{"ipc-watch-off-after-queued-on",[]{
        for(int mode=0;mode!=3;++mode){
            beginWatchDispatch();g_asyncMode=mode;{std::lock_guard<std::mutex> lock(powerMutex);nativePowerFact.flags=7;}
            endpoint=std::make_unique<RuntimeMonitor>([]{++endpointDiscoveries;return absent();});installWatch();
            dispatchOriginalWatch(true,false);dispatchOriginalWatch(false,false);
            drainNewestPoolFirst();transition(0,3);deliverCommon();Sleep(80);
            expect(endpointDiscoveries==0&&endpointEmits==0&&opened==closed,"late-native-pool-on-must-not-revive-newer-off");
            expect(configuredCalls==2&&completedIpc.size()==2,"both-original-watch-handlers-and-response-arms-executed");
            for(const auto& response:completedIpc)expect(!response.contains("error")&&response.at("result")==true,"original-watch-response-preserved");
            expect(syncCompletions==2&&asyncCompletions==0,"watch-demand-registered-on-original-receiver-owner");
            for(const auto* command:{"lossless.detect","lossless.getState","lossless.ensureRunning","lossless.ensureScaling","lossless.resetFailed"})
                expect(ipc_cmd_async(command),"bounded-process-and-uia-io-remains-asynchronous");
            endWatchDispatch();
        }
      }}
      ,{"ipc-watch-latest-detail-after-aba",[]{
        for(int mode=0;mode!=3;++mode){
            beginWatchDispatch();g_asyncMode=mode;{std::lock_guard<std::mutex> lock(powerMutex);nativePowerFact.flags=7;}
            endpoint=std::make_unique<RuntimeMonitor>([]{++endpointDiscoveries;return absent();});installWatch();
            dispatchOriginalWatch(true,false);dispatchOriginalWatch(false,false);dispatchOriginalWatch(true,true);
            drainNewestPoolFirst();transition(0,3);deliverCommon();
            expect(until([]{return endpointEmits.load()==1;}),"latest-on-after-aba-resumes-on-shared-ready");
            expect(endpointDiscoveries==1&&opened==closed+1,"latest-on-has-one-current-watch-scope");
            expect(syncCompletions==3&&asyncCompletions==0&&configuredCalls==3,"aba-watch-commands-retain-ingress-order");
            dispatchOriginalWatch(false,false);drainNewestPoolFirst();
            expect(until([]{return opened.load()==closed.load();},700),"later-off-returns-aba-watch-resource-without-shutdown");
            const auto count=endpointDiscoveries.load();transition(1,5);deliverCommon();transition(0,3,true);deliverCommon();Sleep(40);
            expect(endpointDiscoveries==count,"sleep-ready-after-off-cannot-revive-retired-aba-demand");
            endWatchDispatch();
        }
      }}
      ,{"ipc-watch-off-during-blocked-read",[]{
        for(int mode=0;mode!=3;++mode){
            beginWatchDispatch();g_asyncMode=mode;Gate gate;
            endpoint=std::make_unique<RuntimeMonitor>([&]{++endpointDiscoveries;gate.enter();return absent();});installWatch();
            dispatchOriginalWatch(true,false);drainNewestPoolFirst();expect(gate.seen(),"original-observer-blocked-inside-read-leaf");
            const auto start=GetTickCount64();dispatchOriginalWatch(false,false);
            expect(GetTickCount64()-start<500,"off-registration-does-not-wait-for-blocked-uia-or-discovery");
            expect(pendingPool.empty()&&configuredCalls==2,"off-does-not-wait-in-io-pool");
            gate.open();drainNewestPoolFirst();endpoint->stop();
            expect(endpointDiscoveries==1&&endpointEmits==0&&opened==closed,"inflight-old-read-cannot-publish-or-open-after-off");
            endWatchDispatch();
        }
      }}
