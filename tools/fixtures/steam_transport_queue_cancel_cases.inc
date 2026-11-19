static int transportQueueCancelFixture(std::string_view selected) {
 if(selected!="queue-source-revoke"&&selected!="queue-owner-revoke"&&selected!="queue-owner-throws"&&selected!="queue-inherited-deadline")return 2;
 const bool sourceRevoke=selected=="queue-source-revoke",throws=selected=="queue-owner-throws",deadline=selected=="queue-inherited-deadline";
 ymcc::steamsession::SourceOwner source;
 if(!source.claim())throw std::runtime_error("queue fixture own source claim failed");
 source.resumeReads();
 struct SourcePort {ymcc::steamsession::SourceOwner& owner;auto sourceRead(){return owner.read();}} port{source};
 std::unique_lock held(transportMutex);
 std::atomic<bool> valid{true};std::atomic<unsigned> callbacks{0},admissions{0};
 std::promise<void> entered;auto ready=entered.get_future();
 admissionProbe=true;admissionPortReads=0;
 const auto http=dispatchHttpOpen,sends=dispatchSends,opens=processIdentityOpens;
 auto request=std::async(std::launch::async,[&]{
   const auto invoke=[&]{return runOwned(port,L"S:\\YMCC-owned-offline-fixture",{{"operation","settings.get"}},
     [&]{++admissions;return true;},0,[&]{
       if(++callbacks==2)entered.set_value();
       if(!valid.load()&&throws)throw std::runtime_error("fixture owner retired");
       return valid.load();
     });};
   if(deadline){listenersource::Deadline parentDeadline(GetTickCount64()+100);return invoke();}
   return invoke();
 });
 const bool reached=ready.wait_for(std::chrono::seconds(2))==std::future_status::ready;
 const auto began=std::chrono::steady_clock::now();
 if(sourceRevoke)source.suspendReads();else if(!deadline)valid=false;
 const bool returnedHeld=request.wait_for(std::chrono::milliseconds(200))==std::future_status::ready;
 const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
 std::future<void> drainage;
 bool drainedHeld=false;
 if(sourceRevoke){drainage=std::async(std::launch::async,[&]{source.drain();});drainedHeld=drainage.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;}
 // Controlled release always occurs before assertions, including old failure.
 const bool ownerStillHeld=held.owns_lock();held.unlock();
 const auto receipt=request.get();if(drainage.valid())drainage.get();source.drain();
 const bool closed=source.close();admissionProbe=false;
 std::cout<<"TRANSPORT_QUEUE_CANCEL_OBSERVATION "<<Json{{"selection",selected},{"reachedOriginalQueue",reached},
   {"ownerStillHeldAtObservation",ownerStillHeld},{"returnedBeforeOwnerRelease",returnedHeld},{"drainedBeforeOwnerRelease",drainedHeld},
   {"elapsedMs",elapsed},{"limitMs",200},{"callbacks",callbacks.load()},{"admissions",admissions.load()},
   {"portReads",admissionPortReads.load()},{"httpOpens",dispatchHttpOpen-http},{"sends",dispatchSends-sends},
   {"processOpens",processIdentityOpens-opens},{"sourceClosed",closed},{"receipt",receipt},
   {"originalDefaultGroupExecuted",false},{"realSteamOperations",0},{"wholeProcessResourcePass",false}}.dump()<<"\n";
 expect(reached&&ownerStillHeld,"queue cancellation observed while actual original transport owner remains held");
 expect(returnedHeld&&(!sourceRevoke||drainedHeld),"cancelled shared request returns and source drains before transport owner releases");
 expect(receipt==Json{{"ok",false},{"reason","live-action-cancelled"},{"mutated",false},{"stage","native-live-admission"},{"requestDispatched",false}},"queue revocation keeps exact original unwritten cancellation receipt");
 expect(admissions==0&&admissionPortReads==0&&dispatchHttpOpen==http&&dispatchSends==sends&&processIdentityOpens==opens,"cancelled queued request does not acquire process TCP HTTP or final admission");
 expect(closed,"original request source count returns for ordinary owner close");
 return 0;
}
