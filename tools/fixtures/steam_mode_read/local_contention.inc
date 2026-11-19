        if(retryOnly){
        run("localbusy-exact-receipt-keeps-startup-retry",[&](Case& c){
            DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
            {
                steamui::ModeObserver observer;Event first,ready;std::atomic<unsigned> reads{0},notifications{0};
                steamui::ModeObserverConfig config;config.debounceMs=0;config.minimumGapMs=5000;
                const auto session=syntheticSession();
                config.read=[&](const Session& s){if(++reads==1){first.signal();return Json{{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},{"mode","unknown"},{"reason","live-busy"},{"localReadBusy",true}};}return sample(s);};
                config.changed=[&](const Session&,const Json&){++notifications;ready.signal();};
                c.check("startup-initialized",observer.initialize(config));observer.setDemand(steamui::ModeConsumer::Css,true);observer.observeSteam(session);
                const bool entered=first.wait()==WAIT_OBJECT_0;const auto arrived=ready.wait(1800);observer.shutdown();
                c.check("exact-local-receipt-keeps-finite-fast-retry",entered&&arrived==WAIT_OBJECT_0&&reads==2&&notifications==1,{{"reads",reads.load()},{"notifications",notifications.load()},{"readyWait",arrived}});
                const auto resources=observer.sourceEventDiagnostics();c.check("source-events-returned",resources.created==resources.closed&&resources.closeFailed==0);
            }
            GetProcessHandleCount(GetCurrentProcess(),&after);c.check("owned-handles-returned",before==after,{{"before",before},{"after",after}});
        });
        run("localbusy-nonlocal-receipts-spend-ordinary-retry",[&](Case& c){
            for(unsigned variant=0;variant<12;++variant){
                DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
                unsigned observedReads=0,observedNotifications=0;DWORD unexpected=WAIT_FAILED;
                {
                    steamui::ModeObserver observer;Event first,second;std::atomic<unsigned> reads{0},notifications{0};
                    steamui::ModeObserverConfig config;config.debounceMs=0;config.minimumGapMs=5000;const auto session=syntheticSession();
                    Json busy{{"known",true},{"running",true},{"pid",session.pid},{"birth",std::to_string(session.created)},{"mode","unknown"},{"reason","live-busy"},{"localReadBusy",true}};
                    switch(variant){case 0:busy.erase("localReadBusy");break;case 1:busy["localReadBusy"]=false;break;case 2:busy["localReadBusy"]="true";break;case 3:busy["localReadBusy"]=1;break;case 4:busy["pid"]=double(session.pid);break;case 5:busy["birth"]="wrong-birth";break;case 6:busy["observedAccount"]=202;break;case 7:busy["known"]=false;break;case 8:busy["running"]=false;break;case 9:busy["reason"]="different-reason";break;case 10:busy["known"]="true";break;case 11:busy["pid"]=session.pid+1;break;}
                    config.read=[&](const Session&){const auto call=++reads;if(call==1)first.signal();else second.signal();return busy;};
                    config.changed=[&](const Session&,const Json&){++notifications;};
                    c.check("rejection-initialized-"+std::to_string(variant),observer.initialize(config));observer.setDemand(steamui::ModeConsumer::Css,true);observer.observeSteam(session);
                    const bool entered=first.wait()==WAIT_OBJECT_0;unexpected=second.wait(900);observer.shutdown();observedReads=reads.load();observedNotifications=notifications.load();
                    c.check("rejection-enters-original-worker-"+std::to_string(variant),entered);
                    c.metadata["variant-"+std::to_string(variant)]={{"envelope",busy},{"reads",observedReads},{"notifications",observedNotifications},{"secondWait",unexpected}};
                }
                GetProcessHandleCount(GetCurrentProcess(),&after);c.check("rejection-owned-handles-returned-"+std::to_string(variant),before==after,{{"before",before},{"after",after}});
                if(!c.check("nonlocal-keeps-five-second-gap-"+std::to_string(variant),unexpected==WAIT_TIMEOUT&&observedReads==1&&observedNotifications==0))return;
            }
        });
        run("localbusy-existing-read-window-finite-credit",[&](Case& c){steamui::ModeReadWindow w(10,5000,30000);ULONGLONG now=10000;w.beginStartup(now);w.request(now);for(unsigned i=0;i<6;++i){now+=w.timeout(now);c.check("finite-startup-slot-"+std::to_string(i),w.take(now));w.finish(now,false,true,true);}c.check("ordinary-gap-after-startup",w.timeout(now)==5000);now+=w.timeout(now);w.take(now);w.finish(now,false,true,true);now+=w.timeout(now);w.take(now);w.finish(now,false,true,true);c.check("three-attempts-park",w.timeout(now)==INFINITE);w.request(now+1,true);c.check("failure-cooldown-not-refilled",w.timeout(now+1)>=29999);});
        } else {
        run("localbusy-five-retains-warm-source", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                h.beforeInitialize=[](auto& x){x.minimumGapMs=550;};
                if(!warm(c,h))return;
                auto lease=h.observer.observedSource(h.session);const auto event=lease.cancellationEvent();const auto revision=lease.snapshot().modeRevision;const auto facts=lease.snapshot().modeFacts;
                std::vector<std::shared_ptr<Gate>> gates;
                const auto busy=Json{{"known",true},{"running",true},{"pid",h.session.pid},{"birth",std::to_string(h.session.created)},{"mode","unknown"},{"reason","live-busy"},{"localReadBusy",true}};
                for(unsigned i=0;i<5;++i)gates.push_back(h.queue(busy));
                auto healthy=h.queue(sample(h.session));
                c.check("noninvalidating-health-request",h.observer.requestRefresh(h.session,false));
                for(unsigned i=0;i<5;++i){
                    c.check("busy-gate-entered-"+std::to_string(i+1),gates[i]->entered.wait()==WAIT_OBJECT_0);
                    gates[i]->release();
                    const auto next=i<4?gates[i+1]:healthy;
                    if(!c.check("busy-completed-next-read-held-"+std::to_string(i+1),next->entered.wait()==WAIT_OBJECT_0))return;
                    auto now=h.observer.observedSource(h.session);
                    if(!c.check("tagged-busy-preserves-original-source-"+std::to_string(i+1),now&&now.current()&&lease.current()&&now.cancellationEvent()==event&&now.snapshot().modeRevision==revision&&now.snapshot().modeFacts==facts&&WaitForSingleObject(event,0)==WAIT_TIMEOUT))return;
                    c.check("busy-has-no-confirmation-"+std::to_string(i+1),h.sourceCount()==1&&h.invalidationCount()==0&&h.legacyCount()==1&&h.stable==0);
                    c.check("busy-pending-snapshot-"+std::to_string(i+1),h.observer.snapshot(h.session).value("mode",std::string{})=="unknown"&&h.observer.passiveSnapshot(h.session).value("mode",std::string{})=="unknown");
                }
                healthy->release();c.check("fresh-healthy-completes",h.wait([&]{return h.legacyCount()==2;}));
                auto same=h.observer.observedSource(h.session);
                c.check("fresh-healthy-does-not-republish-source",same&&same.current()&&same.cancellationEvent()==event&&same.snapshot().modeRevision==revision&&same.snapshot().modeFacts==facts&&h.sourceCount()==1&&h.invalidationCount()==0);
            });
        });
        if(selection!="--mode-source-local-contention-control") {
        run("localbusy-cold-does-not-create-source",[&](Case& c){withHarness(c,[&](Harness& h){
            auto busy=Json{{"known",true},{"running",true},{"pid",h.session.pid},{"birth",std::to_string(h.session.created)},{"mode","unknown"},{"reason","live-busy"},{"localReadBusy",true}};
            auto first=h.queue(busy),next=h.queue(busy);c.check("cold-initializes",h.start());c.check("cold-first-read",first->entered.wait()==WAIT_OBJECT_0);first->release();c.check("cold-completed",next->entered.wait()==WAIT_OBJECT_0);
            c.check("cold-busy-no-source-or-confirmation",!h.observer.observedSource(h.session)&&h.sourceCount()==0&&h.invalidationCount()==0&&h.legacyCount()==0&&h.stable==0&&h.observer.snapshot(h.session).value("mode",std::string{})=="unknown");
        });});
        run("localbusy-malformed-envelopes-revoke",[&](Case& c){
            for(unsigned variant=0;variant<12;++variant)withHarness(c,[&](Harness& h){
                if(!warm(c,h))return;auto old=h.observer.observedSource(h.session);
                auto b=Json{{"known",true},{"running",true},{"pid",h.session.pid},{"birth",std::to_string(h.session.created)},{"mode","unknown"},{"reason","live-busy"},{"localReadBusy",true}};
                switch(variant){case 0:b.erase("localReadBusy");break;case 1:b["localReadBusy"]=false;break;case 2:b["localReadBusy"]="true";break;case 3:b["localReadBusy"]=1;break;case 4:b["pid"]=double(h.session.pid);break;case 5:b["birth"]="wrong-birth";break;case 6:b["observedAccount"]=202;break;case 7:b["known"]=false;break;case 8:b["running"]=false;break;case 9:b["reason"]="different-reason";break;case 10:b["known"]="true";break;case 11:b["pid"]=h.session.pid+1;break;}
                auto read=h.queue(b);h.queue(b);c.check("malformed-refresh-"+std::to_string(variant),h.observer.requestRefresh(h.session,false)&&read->entered.wait()==WAIT_OBJECT_0);read->release();
                c.check("malformed-revokes-"+std::to_string(variant),h.wait([&]{return !old.current()&&h.invalidationCount()==1;})&&WaitForSingleObject(old.cancellationEvent(),0)==WAIT_OBJECT_0&&!h.observer.observedSource(h.session));
                c.metadata["variant-"+std::to_string(variant)]=b;
            });
        });
        run("localbusy-lifecycle-and-owned-cancel-revoke",[&](Case& c){for(unsigned variant=0;variant<6;++variant)withHarness(c,[&](Harness& h){
            if(!warm(c,h))return;auto old=h.observer.observedSource(h.session);auto b=Json{{"known",true},{"running",true},{"pid",h.session.pid},{"birth",std::to_string(h.session.created)},{"mode","unknown"},{"reason","live-busy"},{"localReadBusy",true}};
            auto read=h.queue(b);h.queue(b);c.check("lifecycle-read-held-"+std::to_string(variant),h.observer.requestRefresh(h.session,false)&&read->entered.wait()==WAIT_OBJECT_0);
            if(variant==0)h.observer.requestRefresh(h.session,true);
            if(variant==1||variant==2){auto other=h.session;if(variant==1)++other.created;else ++other.account;h.observer.observeSteam(other);}
            if(variant==3)h.observer.setDemand(steamui::ModeConsumer::Decky,false);
            if(variant==4){Event done;JoiningThread t([&]{h.observer.shutdown();done.signal();},[&]{read->release();});c.check("shutdown-revokes-before-join",WaitForSingleObject(old.cancellationEvent(),waitLimit)==WAIT_OBJECT_0&&done.wait(0)==WAIT_TIMEOUT);read->release();t.join();}
            if(variant==5){read->release();h.finish();}
            c.check("lifecycle-sticky-revocation-"+std::to_string(variant),!old.current()&&WaitForSingleObject(old.cancellationEvent(),0)==WAIT_OBJECT_0);
        });
        withHarness(c,[&](Harness& h){std::atomic<bool> cancelNext{false};h.beforeInitialize=[&](auto& cfg){auto leaf=cfg.read;cfg.readOwned=[&,leaf](const Session& s,const steamui::ModeReadCurrent& current){if(!current()||cancelNext)return Json{{"known",false},{"reason","shared-mode-read-cancelled"}};return leaf(s);};};if(!warm(c,h))return;auto old=h.observer.observedSource(h.session);cancelNext=true;c.check("owned-cancel-refresh",h.observer.requestRefresh(h.session,false));c.check("owned-cancel-revokes",h.wait([&]{return !old.current();})&&WaitForSingleObject(old.cancellationEvent(),0)==WAIT_OBJECT_0);});});
        run("localbusy-inflight-hint-no-late-restore",[&](Case& c){withHarness(c,[&](Harness& h){if(!warm(c,h))return;auto old=h.observer.observedSource(h.session);auto b=Json{{"known",true},{"running",true},{"pid",h.session.pid},{"birth",std::to_string(h.session.created)},{"mode","unknown"},{"reason","live-busy"},{"localReadBusy",true}};auto first=h.queue(b),next=h.queue(b);c.check("hint-busy-held",h.observer.requestRefresh(h.session,false)&&first->entered.wait()==WAIT_OBJECT_0);c.check("strong-hint-admitted",h.observer.requestRefresh(h.session,true));c.check("hint-synchronous-revoke",!old.current()&&WaitForSingleObject(old.cancellationEvent(),0)==WAIT_OBJECT_0);first->release();c.check("hint-followup-held",next->entered.wait()==WAIT_OBJECT_0);c.check("late-busy-cannot-restore",!old.current()&&!h.observer.observedSource(h.session)&&h.sourceCount()==1&&h.invalidationCount()==1);});});
        run("localbusy-read-window-budget-unchanged",[&](Case& c){steamui::ModeReadWindow w(10,5000,30000);ULONGLONG now=10000;w.beginStartup(now);w.request(now);for(unsigned i=0;i<6;++i){now+=w.timeout(now);c.check("finite-startup-slot-"+std::to_string(i),w.take(now));w.finish(now,false,true,true);}c.check("ordinary-gap-after-startup",w.timeout(now)==5000);now+=w.timeout(now);w.take(now);w.finish(now,false,true,true);now+=w.timeout(now);w.take(now);w.finish(now,false,true,true);c.check("three-attempts-park",w.timeout(now)==INFINITE);w.request(now+1,true);c.check("failure-cooldown-not-refilled",w.timeout(now+1)>=29999);});
        run("producer-native-trylock-and-owner-cancel",[&](Case& c){producerChecks(c);});
        }
        }
