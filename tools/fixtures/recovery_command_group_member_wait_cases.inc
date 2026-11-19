    if(!SteamGroupMemberWait::selected.empty()){
        using namespace SteamGroupMemberWait;
        const bool strict=SteamGroupMemberWait::selected=="strict-unused-launcher-exit";
        const bool cancelRequest=SteamGroupMemberWait::selected=="request-cancel-and-launcher-exit";
        bool memberClosed=false,sourceEnded=false,childrenEnded=false;
        {
            seed();auto observed=owner.observeProcessGroup(source,selectedLease,current);auto lease=captured(observed);
            if(!lease||lease->graph().count!=2||!role(lease,tree.rootPid,tree.shippingPid,roles::State::VerifiedGroupRoles))
                throw std::runtime_error("member wait needs original actual two-process group");
            auto request=RequestCancellation::create();if(!request)throw std::runtime_error("member request event");
            ymcc::steamcommand::Coordinator coordinator;ymcc::steamcommand::Provider provider(coordinator);
            if(!provider)throw std::runtime_error("member Coordinator unavailable");
            const ymcc::steamcommand::Target target{steam.pi.dwProcessId,birth(steam.pi.hProcess),root};
            auto holder=coordinator.reserve(target,ymcc::steamcommand::Plan::Single);
            auto availability=std::make_shared<ymcc::steamcommand::AvailabilityEvent>();
            if(!holder||!availability->open())throw std::runtime_error("member wait setup");
            auto intent=coordinator.prepareIntent(target,availability);
            auto choice=std::make_shared<NativeRoleChoice>(strict?NativeRoleMode::StrictSingle:NativeRoleMode::Group);
            auto action=groupActionCurrent([]{return true;},lease,request,choice);
            auto observation=groupObservationCurrent([]{return true;},lease,request,choice);
            check("member-initial-original-full-or-strict-guard",action()&&observation());
            check("member-slot-remains-owned-by-independent-command",intent&&intent->promote()==ymcc::steamcommand::IntentStatus::Waiting&&holder.held()&&!intent->ticket());
            OwnedHandle started(CreateEventW(nullptr,TRUE,FALSE,nullptr));
            OwnedHandle self(OpenProcess(SYNCHRONIZE,FALSE,GetCurrentProcessId()));
            if(!started.value||!self.value)throw std::runtime_error("member real self/event acquisition");
            waitEntered=started.value;waitCalls=0;nativeWaitCount=0;
            const auto requiredHandles=strict?std::span<const HANDLE>{}:borrowAdditional(lease);
            const auto extraCount=requiredHandles.size();
            const bool exactExtra=requiredHandles.size()==1&&GetProcessId(requiredHandles[0])==tree.rootPid;
            std::atomic<bool> peerOkay{false};
            std::thread peer([&]{
                try{
                    if(WaitForSingleObject(started.value,3000)!=WAIT_OBJECT_0)throw std::runtime_error("member original wait not entered");
                    if(cancelRequest)request->cancel();
                    if(!tree.finishRoot()||WaitForSingleObject(tree.shippingProcess.value,0)!=WAIT_TIMEOUT)
                        throw std::runtime_error("member root did not independently exit");
                    peerOkay.store(true,std::memory_order_release);
                }catch(...){request->cancel();}
            });
            struct Join{std::thread& t;~Join(){if(t.joinable())t.join();}} joined{peer};
            const std::array<HANDLE,4> processes{steam.pi.hProcess,tree.shippingProcess.value,self.value,independent.pi.hProcess};
            const auto began=GetTickCount64();
            const auto waited=invokeWait(&waitWakeObservation,processes,reinterpret_cast<WakeSignal>(availability->event()),
                reinterpret_cast<WakeSignal>(request->event()),requiredHandles);
            const auto elapsed=GetTickCount64()-began;peer.join();waitEntered=nullptr;
            const bool stillOccupied=holder.held()&&intent->status()==ymcc::steamcommand::IntentStatus::Waiting&&!intent->ticket();
            const bool noAvailability=WaitForSingleObject(availability->event(),0)==WAIT_TIMEOUT;
            const bool admitted=action(),observedCurrent=observation();
            std::cout<<"GROUP_MEMBER_WAIT_OBSERVATION "<<json({{"selection",SteamGroupMemberWait::selected},{"actualObserverTrackerGroupLease",true},
                {"originalEngineWaitAndGuards",true},{"completeEngineHandleExecuted",false},{"strictSingle",strict},
                {"extraProcessHandles",extraCount},{"exactExtraLauncherHandle",exactExtra},{"nativeWaitCalls",waitCalls},
                {"nativeWaitCount",nativeWaitCount},{"waited",static_cast<unsigned>(waited)},
                {"expectedProcessCode",static_cast<unsigned>(WakeWait::Process)},{"expectedCancelCode",static_cast<unsigned>(WakeWait::Cancelled)},
                {"elapsedMs",elapsed},{"originalWaitTimeoutMs",1000},{"commandStillOccupied",stillOccupied},{"availabilityUnsignaled",noAvailability},
                {"peerCompleted",peerOkay.load()},{"selectedStillAlive",WaitForSingleObject(tree.shippingProcess.value,0)==WAIT_TIMEOUT},
                {"postwaitAdmitted",admitted},{"observationCurrent",observedCurrent},{"requestCurrent",request->current()},
                {"publisherCalls",0},{"realSteamOperations",0},{"wholeProcessResources","UNKNOWN"}}).dump()<<std::endl;
            check("member-observed-independent-real-launcher-exit",peerOkay.load()&&WaitForSingleObject(tree.shippingProcess.value,0)==WAIT_TIMEOUT);
            check("member-no-availability-or-action-authority-from-process-exit",stillOccupied&&noAvailability&&waitCalls==1);
            check("member-original-wait-role-outcome",waited==(strict?WakeWait::Timeout:cancelRequest?WakeWait::Cancelled:WakeWait::Process));
            check("member-qualified-group-wakes-without-slot-release",strict||(cancelRequest||exactExtra)&&elapsed<500);
            check("member-strict-single-excludes-unused-launcher",!strict||(extraCount==0&&elapsed>=900&&admitted&&observedCurrent&&request->current()));
            check("member-nonexact-refuses-old-request-after-launcher-exit",strict||(!admitted&&!observedCurrent&&!request->current()));
            intent->cancel();intent.reset();holder.parentReturned();holder={};
            action={};observation={};request.reset();choice.reset();lease.reset();
            memberClosed=availability->close();availability.reset();
            memberClosed=started.close()&&self.close()&&memberClosed;
        }
        selectedLease.reset();independent.finish();g_steamSettingsObserver.stop();steam.finish();
        childrenEnded=tree.finish();sourceEnded=WaitForSingleObject(steam.pi.hProcess,0)==WAIT_OBJECT_0&&WaitForSingleObject(independent.pi.hProcess,0)==WAIT_OBJECT_0;
        check("member-original-owned-events-and-children-returned",memberClosed&&sourceEnded&&childrenEnded&&tree.zero&&tree.closed&&!tree.forced);
        std::cout<<"GROUP_MEMBER_WAIT_RESULT "<<json({{"selection",SteamGroupMemberWait::selected},{"checks",checks.size()},{"ownedEventsReturned",memberClosed},
            {"childrenReturned",sourceEnded&&childrenEnded&&tree.zero&&tree.closed&&!tree.forced},{"completeEngineHandleExecuted",false},
            {"sourceReady",false},{"resourcePass",false},{"originalDefaultGroupExecuted",false}}).dump()<<std::endl;
        return;
    }
