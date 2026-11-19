      ,{"runtime-uia-transient-init-self-heals",[]{
        namespace leaf=ymcc::losslessUiaFixture;leaf::reset();leaf::factoryFailures=1;
        FILETIME birth{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user);
        const Identity identity{GetCurrentProcessId(),(std::uint64_t(birth.dwHighDateTime)<<32)|birth.dwLowDateTime,leaf::image};
        std::atomic<unsigned> unreadable{0},readable{0};
        auto value=monitor([&]{return selectRuntime({identity},false);});
        value->configure(true,false,[&](const RuntimeReceipt& r){if(r.readable)++readable;else ++unreadable;});
        expect(until([&]{return unreadable.load()!=0;}),"initial-uia-failure-observed-as-unreadable");
        expect(until([&]{return readable.load()!=0;},6500),"same-live-instance-must-retry-transient-uia-initialization");
        value->stop();
        expect(leaf::factoryCalls==2&&readable==1,"original-cadence-retries-only-missing-uia-client");
        expect(leaf::initCalls==leaf::uninitCalls&&leaf::clientsLive==0&&leaf::elementsLive==0&&leaf::conditionsLive==0,"uia-client-elements-condition-and-com-apartment-returned");
        expect(leaf::subscriptions==2&&leaf::removed==2&&leaf::unexpected==0,"real-automation-listen-and-unlisten-call-both-property-leaves");
        expect(leaf::processOpened==leaf::processClosed,"actual-owned-process-handles-returned-after-uia-heal");
      }}
      ,{"runtime-uia-cancel-during-client-start",[]{
        namespace leaf=ymcc::losslessUiaFixture;leaf::reset();Gate gate;
        FILETIME birth{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user);
        const Identity identity{GetCurrentProcessId(),(std::uint64_t(birth.dwHighDateTime)<<32)|birth.dwLowDateTime,leaf::image};
        leaf::factoryHook=[&]{if(leaf::factoryCalls==1)gate.enter();};std::atomic<unsigned> receipts{0};
        auto value=monitor([&]{return selectRuntime({identity},false);});value->configure(true,false,[&](const RuntimeReceipt&){++receipts;});
        expect(gate.seen(),"actual-worker-blocked-at-client-creation-leaf");transition(1,5);notify(*value);
        expect(leaf::initCalls==1&&leaf::uninitCalls==0,"cancellation-retains-apartment-until-sdk-leaf-actually-returns");
        gate.open();expect(until([&]{return leaf::uninitCalls.load()==1;}),"returned-canceled-client-releases-original-apartment");
        expect(receipts==0&&leaf::subscriptions==0&&leaf::clientsLive==0,"retired-uia-client-cannot-listen-or-publish");
        expect(leaf::processOpened==1,"canceled-client-start-cannot-open-next-process-handle");
        transition(0,3,true);notify(*value);expect(until([&]{return receipts.load()==1;}),"next-ready-automatically-builds-fresh-client-and-observation");value->stop();
        expect(leaf::initCalls==leaf::uninitCalls&&leaf::clientsLive==0&&leaf::elementsLive==0&&leaf::conditionsLive==0,"cancel-and-next-scope-return-all-uia-owned-references");
        expect(leaf::subscriptions==2&&leaf::removed==2&&leaf::processOpened==leaf::processClosed&&leaf::unexpected==0,"successor-subscription-and-process-resources-return");
        leaf::factoryHook={};
      }}
      ,{"runtime-uia-off-returns-client-subscriptions",[]{
        namespace leaf=ymcc::losslessUiaFixture;leaf::reset();
        FILETIME birth{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user);
        const Identity identity{GetCurrentProcessId(),(std::uint64_t(birth.dwHighDateTime)<<32)|birth.dwLowDateTime,leaf::image};std::atomic<unsigned> receipts{0};
        auto value=monitor([&]{return selectRuntime({identity},false);});value->configure(true,false,[&](const RuntimeReceipt& r){if(r.readable)++receipts;});
        expect(until([&]{return receipts.load()==1;}),"live-original-uia-class-publishes-readable-receipt");
        value->configure(false,false,{});expect(until([&]{return leaf::clientsLive.load()==0&&leaf::processOpened.load()==leaf::processClosed.load();},700),"off-returns-client-and-process-without-thread-stop");
        expect(leaf::initCalls==leaf::uninitCalls&&leaf::elementsLive==0&&leaf::conditionsLive==0&&leaf::subscriptions==leaf::removed,"off-unsubscribes-and-returns-apartment-without-new-owner");
        const auto before=leaf::factoryCalls.load();transition(1,5);notify(*value);transition(0,3,true);notify(*value);Sleep(50);
        expect(leaf::factoryCalls==before&&receipts==1,"later-ready-cannot-recreate-off-uia-client");value->stop();expect(leaf::unexpected==0,"uia-test-made-no-unselected-sdk-input-or-process-operation");
      }}
      ,{"runtime-uia-monitor-cancel-between-properties",[]{
        namespace leaf=ymcc::losslessUiaFixture;
        for(const std::string edge:{"power","off-on","stop"}){
          reset();leaf::reset();Gate property;std::atomic<bool> first{true},retired{false},stopReturned{false};std::atomic<unsigned> lateQueries{0},receipts{0};
          FILETIME birth{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user);
          const Identity identity{GetCurrentProcessId(),(std::uint64_t(birth.dwHighDateTime)<<32)|birth.dwLowDateTime,leaf::image};
          leaf::callHook=[&](const char* name){if(retired.load())++lateQueries;if(std::string(name)=="name"&&first.exchange(false))property.enter();};
          leaf::factoryHook=[&]{if(leaf::factoryCalls.load()>1)retired=false;};
          auto value=monitor([&]{return selectRuntime({identity},false);});const auto emit=[&](const RuntimeReceipt& r){if(r.readable)++receipts;};
          value->configure(true,false,emit);expect(property.seen(),"monitor-original-worker-reaches-first-uia-property");
          retired=true;std::thread stopper;
          if(edge=="power"){transition(1,5);notify(*value);}
          else if(edge=="off-on"){value->configure(false,false,emit);value->configure(true,false,emit);}
          else{stopper=std::thread([&]{value->stop();stopReturned=true;});expect(until([&]{return !value->configure(true,false,emit);}),"monitor-stop-retires-original-observer-before-property-return");expect(!stopReturned,"monitor-stop-cannot-return-before-inflight-property");}
          expect(leaf::clientsLive==1&&leaf::initCalls==1&&leaf::uninitCalls==0,"monitor-revocation-retains-real-uia-client-and-apartment-until-property-returns");property.open();
          if(edge=="power"){
            expect(until([&]{return leaf::clientsLive.load()==0&&leaf::processOpened.load()==leaf::processClosed.load();}),"monitor-cancelled-property-returns-old-client-and-process-handles");
            expect(receipts==0&&leaf::subscriptions==leaf::removed,"monitor-retired-property-never-publishes-and-unsubscribes-original-client");retired=false;transition(0,3,true);notify(*value);
            expect(until([&]{return receipts.load()==1;}),"monitor-next-native-ready-self-heals-without-new-configure");
          }else if(edge=="off-on"){
            expect(until([&]{return receipts.load()==1;}),"monitor-new-aba-request-self-heals-after-old-property-return");
          }else{stopper.join();expect(stopReturned&&receipts==0,"monitor-stop-returns-only-after-real-old-uia-worker");transition(0,3,true);notify(*value);expect(!value->configure(true,false,emit),"monitor-late-ready-never-reopens-terminal-observer");}
          value->stop();
          expect(lateQueries==0,"monitor-cancellation-after-first-property-stops-later-uia-query");
          expect(leaf::factoryCalls==(edge=="stop"?1u:2u)&&receipts==(edge=="stop"?0u:1u),"monitor-only-current-successor-publishes-normal-readable-receipt");
          expect(leaf::initCalls==leaf::uninitCalls&&leaf::clientsLive==0&&leaf::elementsLive==0&&leaf::conditionsLive==0,"monitor-property-cancel-and-successor-return-all-real-com-references");
          expect(leaf::subscriptions==leaf::removed&&leaf::processOpened==leaf::processClosed&&leaf::unexpected==0&&opened==closed&&invalid==0,"monitor-property-cancel-returns-subscriptions-native-processes-and-directory-watch");
          std::cerr<<json{{"edge",edge},{"lateQueries",lateQueries.load()},{"propertyNames",leaf::propertyNameCalls.load()},{"roots",leaf::rootCalls.load()},{"finds",leaf::findCalls.load()},{"factories",leaf::factoryCalls.load()},{"receipts",receipts.load()},{"init",leaf::initCalls.load()},{"uninit",leaf::uninitCalls.load()},{"clientsLive",leaf::clientsLive.load()},{"elementsLive",leaf::elementsLive.load()},{"conditionsLive",leaf::conditionsLive.load()},{"subscriptions",leaf::subscriptions.load()},{"removed",leaf::removed.load()},{"processOpened",leaf::processOpened.load()},{"processClosed",leaf::processClosed.load()},{"watchesOpened",opened.load()},{"watchesClosed",closed.load()},{"invalid",invalid.load()},{"unexpected",leaf::unexpected.load()}}.dump()<<'\n';
          leaf::callHook={};leaf::factoryHook={};
        }
      }}
