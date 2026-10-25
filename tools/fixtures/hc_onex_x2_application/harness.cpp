#include "boundary.h"
#include "production.generated.h"
namespace p=ymcc::onex_x2;
using test::check;using test::eq;
static std::vector<unsigned char> raw(unsigned id,bool pressed=true,std::size_t size=64){
    check(size>=16,"raw fixture capacity");std::vector<unsigned char> b(size,0);b[0]=0xA7;b[1]=0xB2;b[2]=0x3F;b[7]=static_cast<unsigned char>(id);b[13]=pressed?1:0;b[size-2]=0x3F;b[size-1]=0xB2;return b;
}
static void ownerAt(ULONGLONG now){check(now>=appmock::now,"monotonic mock clock");appmock::now=now;mock::retained();onexX2OwnerTick();mock::retained();}
static void finish(){
    g_inputCaptureReady=false;onexX2OwnerStop();
    for(auto& io:mock::w.ios)if(!io->observed&&!io->terminal)mock::complete(*io,false);
    ++appmock::now;onexX2OwnerStop();
    check(!g_onexX2Transport.hasDevice()&&g_onexX2Transport.snapshot().phase==p::Phase::Detached,"owner static transport terminal drain closes device");
    eq(g_onexX2RearGeneration,uint64_t{0},"stop revokes rear source");eq(g_onexX2HomeGeneration,uint64_t{0},"stop revokes home source");check(!g_onexX2PublishedReady,"stop withdraws context publication");for(const auto& n:appmock::notifications)check(!n->active,"owner stop unregisters memory device notification");mock::assertClean();
}
static void prepare(const std::string& cls,YmccFamily family=YmccFamily::OneXPlayer,bool present=true,USHORT input=64,USHORT output=65){
    if(!mock::w.objects.empty())finish();mock::reset();appmock::now+=3000;
    for(std::size_t i=0;i<kOemProducerCount;++i){g_oemProducerStates[i]={};g_oemProducerGenerations[i]=0;g_oemProducerAuthorities[i]=0;}
    g_oemProducerEdgeHead=g_oemProducerEdgeCount=0;g_oemProducerEdgeSequence=0;
    g_onexX2SessionGeneration=g_onexX2RearGeneration=g_onexX2HomeGeneration=0;g_onexX2RearReleaseFence=0;g_onexX2PublishedReady=false;g_onexX2HomeDisplayUntil=0;
    g_machineIdentity={};g_machineIdentity.family=family;g_machineIdentity.deviceClass=cls;g_machineIdentity.resolved=true;
    g_inputCaptureReady=true;g_exitRequested=false;g_shortcutRecordingActive=false;g_powerLifecycle=PowerLifecycle::Ready;
    g_oemKeyMask=0;g_oemKeyMaskMsiDinput=0;g_oemKeyMaskMsi=0;g_oemKeyMaskRog=0;g_oemBackState=0;g_oemKbDown=0;g_oemKbDownRaw=0;
    g_oemBackProfileId=static_cast<int>(cls=="OneXPlayerX2MiniPro"?OemChordProfileId::OneXX2MiniPro:OemChordProfileId::OneXX1);
    g_gpdChordPubGen=0;g_gpdBackIfaceGen=0;g_gpdBackIfaceReady=false;
    g_nativeKeyboardShortcuts.clear();g_nativeOemEdgeEpochSeen=0;g_nativeWmiAuthoritySeen=false;g_nativeShortcutLegacyOverrideMask=0;g_oemShortcutEdgePostFailures=0;evaluations.clear();rogReceipts.clear();appmock::jobs.clear();appmock::actions.clear();appmock::logs.clear();appmock::posts=appmock::refreshes=0;
    appmock::diagnostic=false;appmock::postAccepted=appmock::serialAccepted=true;appmock::beforeCompletion={};appmock::afterSourceRevoke={};appmock::beforeEnumeration={};appmock::afterEnumeration={};appmock::afterOwnerOpen={};appmock::afterRead={};appmock::writePermissionOverride={};appmock::hardwareAllowed=true;appmock::permissionChecks=0;appmock::notificationAccepted=true;appmock::registrations=appmock::unregistrations=0;appmock::notifications.clear();
    if(present){auto d=mock::device();d.input=input;d.output=output;mock::w.devices.push_back(d);}
}
static void boot(){ownerAt(appmock::now);eq(g_onexX2Transport.snapshot().phase,p::Phase::Live,"actual owner binds exact supported vendor");eq(mock::w.dataOpens,1u,"one vendor owner handle");eq(mock::w.readsCalled,1u,"actual owner starts reader");}
static void deliver(unsigned id,bool pressed=true){auto& io=mock::pending(false);mock::complete(io,true,io.size,ERROR_SUCCESS,raw(id,pressed,io.size));ownerAt(appmock::now+1);}
static void rule(const std::string& trigger="press",uint64_t mask=kOemBitHome){NativeKeyboardShortcutRule r;r.requiredMask=mask;r.trigger=trigger;r.ownAction="test.home";r.doubleWindowMs=500;r.needsNeutral=false;g_nativeKeyboardShortcuts.push_back(r);}
static void jobs(){while(!appmock::jobs.empty()){auto job=std::move(appmock::jobs.front());appmock::jobs.pop_front();job();}}
static std::size_t logs(const std::string& event){return static_cast<std::size_t>(std::count_if(appmock::logs.begin(),appmock::logs.end(),[&](const auto& l){return l.event==event;}));}
static json ownerLog(){for(auto it=appmock::logs.rbegin();it!=appmock::logs.rend();++it)if(it->event=="onex-x2-owner")return it->fields;test::fail("owner diagnostic missing");}
static void admission(){test::section="owner-admission";
    for(const std::string cls:{"OneXPlayerX2","OneXPlayerX2MiniPro","OneXPlayer3","HandheldCompanion.Devices.OneXPlayerX2","HandheldCompanion.Devices.OneXPlayerX2MiniPro","HandheldCompanion.Devices.OneXPlayer3"}){
        prepare(cls);boot();eq(g_onexX2RearGeneration,uint64_t{0},"bind without read creates no rear producer");eq(g_onexX2HomeGeneration,uint64_t{0},"bind without read creates no home producer");
        const unsigned id=cls.find("Mini")!=std::string::npos?0x24:0x21;deliver(id);check(g_onexX2HomeGeneration!=0,"complete validated Home read starts real new producer");check(oemProducerCurrent(OemProducerId::OneXX2Home,g_onexX2HomeGeneration),"new Home source is current");finish();}
}
static void excludedModels(){test::section="model-boundary";
    const std::vector<std::pair<YmccFamily,std::string>> excluded={{YmccFamily::AsusRogAllyX,"ROGAllyX"},{YmccFamily::MsiClaw,"ClawA1M"},{YmccFamily::Gpd,"GPDWin5"},{YmccFamily::Unknown,"OneXPlayerX2"},{YmccFamily::OneXPlayer,"OneXPlayerX1"},{YmccFamily::OneXPlayer,"OneXPlayerX1AMD"},{YmccFamily::OneXPlayer,"OneXPlayerX1Intel"},{YmccFamily::OneXPlayer,"OneXPlayerX1Mini"},{YmccFamily::OneXPlayer,"OneXPlayerX1Pro"},{YmccFamily::OneXPlayer,"OneXPlayerApex"},{YmccFamily::OneXPlayer,"prefix-OneXPlayerX2"},{YmccFamily::OneXPlayer,"onexplayerx2"}};
    for(const auto& item:excluded){prepare(item.second,item.first);for(unsigned n=0;n<5;++n)ownerAt(appmock::now+3000);
        eq(mock::w.enumerations,0u,"other family/old OneX never enumerates new transport");eq(mock::w.dataOpens,0u,"excluded model never opens vendor");eq(mock::w.readsCalled,0u,"excluded model never reads");eq(mock::w.writesCalled,0u,"excluded model never initializes");
        eq(g_onexX2RearGeneration,uint64_t{0},"excluded model has no new rear source");eq(g_onexX2HomeGeneration,uint64_t{0},"excluded model has no new Home source");finish();}
}
static void readiness(){test::section="owner-readiness";
    for(unsigned failure=0;failure<5;++failure){prepare("OneXPlayerX2");if(failure==0)g_inputCaptureReady=false;if(failure==1)g_exitRequested=true;if(failure==2)g_powerLifecycle=PowerLifecycle::Suspending;if(failure==3)g_powerLifecycle=PowerLifecycle::Suspended;if(failure==4)g_powerLifecycle=PowerLifecycle::Resuming;
        ownerAt(appmock::now);eq(mock::w.enumerations,0u,"backend readiness gate blocks actual bind");check(!g_onexX2PublishedReady,"unready backend cannot publish source");finish();}
}
static void keyboardOwned(){test::section="keyboard-owner";
    for(const std::string cls:{"OneXPlayerX2","OneXPlayerX2MiniPro","OneXPlayer3"}){prepare(cls);boot();deliver(0x20);if(cls=="OneXPlayerX2")deliver(0x24);if(cls=="OneXPlayerX2MiniPro")deliver(0x21);
        eq(g_onexX2RearGeneration,uint64_t{0},"Turbo/Keyboard do not create new rear producer");eq(g_onexX2HomeGeneration,uint64_t{0},"keyboard-owned vendor signals do not create new Home producer");
        eq(oemProducerAuthorityMask(),uint64_t{0},"keyboard-owned vendor events claim no fallback authority");eq(g_oemProducerEdgeCount,std::size_t{0},"ignored vendor signals cannot duplicate UI edges");finish();}
}
static void miniKeyboard(){test::section="mini-rear";prepare("OneXPlayerX2MiniPro");boot();deliver(0x22);deliver(0x23);deliver(0x22,false);deliver(0x23,false);
    eq(g_onexX2RearGeneration,uint64_t{0},"Mini native rear ignored");eq(g_oemProducerEdgeCount,std::size_t{0},"Mini native rear has no UI edges");deliver(0x24);check(g_onexX2HomeGeneration!=0,"Mini Home can coexist with keyboard rear");
    for(uint32_t key:std::array<uint32_t,4>{0u,kVkF15,kVkF16,kVkF15|kVkF16}){g_oemKbDown=key;oemBackHookRecompute();const uint64_t expected=(key&kVkF15?kOemBitL4:0)|(key&kVkF16?kOemBitR4:0);
        eq(g_oemKeyMask.load(),expected,"actual Mini profile maps F15/F16 canonical bits");eq(nativeOemKeyboardActionMask()&(kOemBitL4|kOemBitR4),expected,"new source cannot mask Mini keyboard fallback");eq(nativeOemKeyMask(true)&(kOemBitL4|kOemBitR4),expected,"merged actions retain Mini keyboard rear");
        eq(g_oemBackStateKb.load(),static_cast<int>((key&kVkF15?1:0)|(key&kVkF16?2:0)),"actual recompute preserves semantic Mini left/right");}
    check(!(oemProducerAuthorityMask()&(kOemBitL4|kOemBitR4)),"Mini Home does not claim rear keyboard authority");finish();
}
static void rearUi(){test::section="rear-ui";
    for(const std::string cls:{"OneXPlayerX2","OneXPlayer3"}){prepare(cls);boot();const std::vector<unsigned> ids={0x22,0x23,0x22,0x23};const std::vector<uint64_t> masks={kOemBitL4,kOemBitL4|kOemBitR4,kOemBitR4,0};
        std::vector<ULONGLONG> times;for(std::size_t i=0;i<ids.size();++i){deliver(ids[i],i<2);times.push_back(appmock::now);eq(oemProducerActionMask(),masks[i],"real rear ledger holds exact action state");eq(oemProducerPhysicalMask(),masks[i],"real rear ledger holds exact physical state");eq(oemProducerPhysicalBackState(),static_cast<int>(i==0?1:i==1?3:i==2?2:0),"rear consumer uses semantic state");}
        appmock::now+=1000;nativeOemProducerDrain();eq(evaluations.size(),std::size_t{4},"all real rear transitions survive delayed UI drain");for(std::size_t i=0;i<4;++i){eq(evaluations[i].mask,masks[i],"UI rear snapshot matches captured edge");eq(evaluations[i].capturedAt,times[i],"UI rear uses capture time");eq(evaluations[i].scope,kOemBitL4|kOemBitR4,"new rear scope excludes keyboard signals");check(oemProducerTokensCurrent(evaluations[i].tokens),"rear UI token belongs to live source");}finish();}
}
static void homeDelayed(){test::section="home-ui";
    for(const std::string cls:{"OneXPlayerX2","OneXPlayerX2MiniPro","OneXPlayer3"})for(unsigned id:{0x21u,0x24u}){if((cls=="OneXPlayerX2"&&id!=0x21)||(cls=="OneXPlayerX2MiniPro"&&id!=0x24))continue;
        prepare(cls);boot();rule();deliver(id);const auto at=appmock::now;check(onexX2HomeDisplayMask()==kOemBitHome,"Home has transient display pulse");eq(g_oemProducerEdgeCount,std::size_t{1},"complete click has durable UI receipt");
        const auto r=g_oemProducerEdges[g_oemProducerEdgeHead];eq(r.clickCode,static_cast<BYTE>(id),"original Home vendor ID retained in real receipt");eq(r.capturedAt,at,"capture time retained");check(r.terminalClick,"Home is complete click");
        appmock::now+=1000;eq(onexX2HomeDisplayMask(),uint64_t{0},"display pulse expired before UI consumes");nativeOemProducerDrain();eq(rogReceipts.size(),std::size_t{0},"new Home never enters ROG dispatcher");eq(appmock::jobs.size(),std::size_t{1},"expired display does not lose Home action");jobs();eq(appmock::actions.size(),std::size_t{1},"actual guarded serial job reaches observation sink");
        bool evidence=false;for(const auto& l:appmock::logs)if(l.event=="oem-producer-edge"&&l.fields.value("source","")=="onex-x2-home-click"&&l.fields.contains("code")&&l.fields["code"]==id)evidence=true;check(evidence,"Home log preserves original vendor ID");finish();}
}
static void noHomeRule(){test::section="home-default";prepare("OneXPlayer3");boot();deliver(0x21);deliver(0x24);nativeOemProducerDrain();jobs();check(appmock::actions.empty(),"unconfigured Home cannot trigger default window action");check(rogReceipts.empty(),"unconfigured Home cannot route through legacy ROG defaults");finish();}
static void unsupportedHomeRules(){test::section="home-trigger-boundary";prepare("OneXPlayerX2");boot();for(const auto trigger:{"hold","release","repeat"})rule(trigger);rule("press",kOemBitHome|kOemBitL4);rule();g_nativeKeyboardShortcuts.back().desktopKeys={VK_LWIN};deliver(0x21);nativeOemProducerDrain();jobs();check(appmock::actions.empty()&&appmock::jobs.empty(),"click-only Home cannot fabricate hold release repeat composite desktop action");check(rogReceipts.empty(),"unsupported Home cannot trigger ROG fallback");finish();}
static void homeDouble(){test::section="home-double";prepare("OneXPlayerX2");boot();rule("double");deliver(0x21);const auto first=appmock::now;appmock::now+=299;deliver(0x21);const auto second=appmock::now;eq(second-first,ULONGLONG{300},"capture interval inside double window");appmock::now+=2000;nativeOemProducerDrain();eq(appmock::jobs.size(),std::size_t{1},"double uses capture interval despite delayed UI processing");jobs();eq(appmock::actions.size(),std::size_t{1},"double emits once");deliver(0x21);nativeOemProducerDrain();jobs();eq(appmock::actions.size(),std::size_t{1},"third click starts new double pair");finish();}
static void homeRuleEpoch(){test::section="home-epoch";prepare("OneXPlayerX2");boot();rule("double");deliver(0x21);nativeOemProducerDrain();check(appmock::jobs.empty(),"first double click waits");++g_oemShortcutEdgeEpoch;deliver(0x21);nativeOemProducerDrain();check(appmock::jobs.empty(),"new rule epoch cannot inherit first click");deliver(0x21);nativeOemProducerDrain();jobs();eq(appmock::actions.size(),std::size_t{1},"new rule epoch double starts fresh");finish();}
static void contextFences(){test::section="context-fence";
    for(unsigned changed=0;changed<3;++changed)for(bool serialQueued:{false,true}){prepare("OneXPlayerX2");boot();rule();deliver(0x22);deliver(0x21);const auto rear=g_onexX2RearGeneration,home=g_onexX2HomeGeneration;const auto token=oemProducerTokensFor(kOemBitHome|kOemBitL4);
        if(serialQueued){nativeOemProducerDrain();check(appmock::jobs.size()==1,"real action admitted before context changes");}
        if(changed==0)++g_powerGeneration;if(changed==1)++g_onexX2DeviceEpoch;if(changed==2)g_powerLifecycle=PowerLifecycle::Resuming;
        check(!oemProducerCurrent(OemProducerId::OneXX2Rear,rear)&&!oemProducerCurrent(OemProducerId::OneXX2Home,home),"stale producer rejected before owner ticks");eq(oemProducerAuthorityMask(),uint64_t{0},"stale sources cannot own keyboard authority");eq(oemProducerActionMask(),uint64_t{0},"stale source cannot act");eq(oemProducerPhysicalMask(),uint64_t{0},"stale source cannot publish physical keys");eq(oemProducerPhysicalBackState(),0,"stale rear cannot survive in consumer state");eq(onexX2HomeDisplayMask(),uint64_t{0},"stale Home cannot remain displayed");check(!oemProducerTokensCurrent(token),"captured tokens rejected immediately");
        nativeOemProducerDrain();jobs();check(appmock::actions.empty(),"UI queued or serial queued action cannot survive context fence");finish();}
}
static void ownerLateRead(){test::section="owner-late-read";prepare("OneXPlayerX2");boot();deliver(0x22);deliver(0x21);auto& late=mock::pending(false);mock::complete(late,true,late.size,ERROR_SUCCESS,raw(0x23,true,late.size));++g_powerGeneration;const auto old=g_onexX2Transport.snapshot();bool observedOrdering=false;
    appmock::beforeCompletion=[&]{observedOrdering=true;check(!g_onexX2PublishedReady&&g_onexX2RearGeneration==0&&g_onexX2HomeGeneration==0,"source revocation happens BEFORE old GetOverlappedResult is serviced");eq(g_oemProducerEdgeCount,std::size_t{0},"old UI receipts removed before processing late IO");};
    ownerAt(appmock::now+1);appmock::beforeCompletion={};check(observedOrdering,"late terminal was actually observed through memory API wrapper");
    eq(g_onexX2RearGeneration,uint64_t{0},"owner revokes rear before servicing late old-context packet");eq(g_onexX2HomeGeneration,uint64_t{0},"owner revokes queued Home before old-context packet");check(!g_onexX2PublishedReady&&g_oemProducerEdgeCount==0,"late old packet cannot restore source or queue");check(g_onexX2Transport.snapshot().generation>old.generation,"transport retires old context first");check(!g_onexX2Transport.hasDevice(),"old terminal reader releases old handle");ownerAt(g_onexX2Transport.snapshot().retryAt);eq(g_onexX2Transport.snapshot().validFrames,uint64_t{0},"new binding starts without old frame evidence");check(!g_onexX2PublishedReady,"pending new read is not an actual source");finish();
}
static void stopRetention(){test::section="owner-stop";prepare("OneXPlayerX2");boot();deliver(0x22);deliver(0x21);auto* address=&g_onexX2Transport;g_inputCaptureReady=false;onexX2OwnerStop();check(g_onexX2Transport.hasDevice(),"ownerStop retains static transport with pending IO");eq(mock::w.closeDevices,0u,"ownerStop never frees pending handle");eq(g_onexX2RearGeneration,uint64_t{0},"ownerStop immediately revokes rear");eq(g_onexX2HomeGeneration,uint64_t{0},"ownerStop immediately revokes Home");check(!oemProducerActionMask()&&!oemProducerPhysicalMask()&&!g_oemProducerEdgeCount,"stop withdraws all new source consumers and queue");eq(&g_onexX2Transport,address,"static owner object not replaced");finish();}
static void queueFull(){test::section="queue-full";
    for(bool home:{false,true}){prepare("OneXPlayerX2");boot();for(std::size_t n=0;n<kOemProducerEdgeCapacity;++n)deliver(home?0x21:0x22,home||n%2==0);eq(g_oemProducerEdgeCount,kOemProducerEdgeCapacity,"actual producer FIFO full");deliver(home?0x21:0x22,home||kOemProducerEdgeCapacity%2==0);
        check(g_onexX2Transport.snapshot().phase!=p::Phase::Live,"queuefull refusal stops actual transport");check(!g_onexX2PublishedReady&&g_onexX2RearGeneration==0&&g_onexX2HomeGeneration==0,"queuefull revokes both new sources");eq(g_oemProducerEdgeCount,std::size_t{0},"queuefull revokes stale queued receipts");eq(oemProducerAuthorityMask(),uint64_t{0},"queuefull source releases fallback authority");finish();}
}
static void logEvidence(){test::section="evidence-boundary";prepare("OneXPlayerX2",YmccFamily::OneXPlayer,true,65,64);boot();auto v=ownerLog();eq(v["inputBytes"].get<int>(),65,"log independent input caps");eq(v["outputBytes"].get<int>(),64,"log independent output caps");eq(v["inputLayout"].get<int>(),static_cast<int>(p::InputLayout::ReportIdThenFrame),"log explicit raw layout");check(!v["publicationCurrent"].get<bool>()&&!v["hardwareModeReadbackVerified"].get<bool>(),"caps bind and logs do not claim live physical source/hardware mode readback");eq(v["validFramesThisBinding"].get<int>(),0,"no read completion means no physical evidence");
    auto& r=mock::pending(false);auto bad=raw(0x22,true,r.size);bad[2]=0;mock::complete(r,true,r.size,ERROR_SUCCESS,bad);ownerAt(appmock::now+1);eq(g_onexX2Transport.snapshot().invalidFrames,uint64_t{1},"invalid real read observed but rejected");check(!g_onexX2PublishedReady&&g_onexX2RearGeneration==0&&g_onexX2HomeGeneration==0,"invalid read cannot claim physical input");finish();
}
static void stableLogs(){test::section="log-rate";prepare("OneXPlayerX2");for(unsigned i=0;i<3;++i)mock::w.writes.push_back({mock::Mode::Immediate});boot();const auto insertion=appmock::now;for(unsigned offset:{4000u,4050u,4100u,4150u})ownerAt(insertion+offset);check(g_onexX2Transport.snapshot().initialized,"write terminal init settled");check(!g_onexX2PublishedReady,"completed init alone is not input publication");check(ownerLog()["initializedWritesComplete"].get<bool>()&&!ownerLog()["publicationCurrent"].get<bool>(),"actual log distinguishes write completion from current input source");
    appmock::logs.clear();for(unsigned i=0;i<1000;++i)ownerAt(appmock::now+1);eq(logs("onex-x2-owner"),std::size_t{0},"stable untraced owner does not log per tick");appmock::diagnostic=true;appmock::logs.clear();for(unsigned i=0;i<12000;++i)ownerAt(appmock::now+1);check(logs("onex-x2-owner")<=3,"diagnostic stable sampling bounded to five-second cadence");eq(mock::w.enumerations,1u,"stable owner never re-enumerates");finish();
}
static void oldProducers(){test::section="legacy-ledger";prepare("OneXPlayerX2",YmccFamily::Unknown,false);eq(static_cast<unsigned>(OemProducerId::Legion),0u,"old Legion ID");eq(static_cast<unsigned>(OemProducerId::LegionS),1u,"old LegionS ID");eq(static_cast<unsigned>(OemProducerId::Gpd),2u,"old GPD ID");eq(static_cast<unsigned>(OemProducerId::OneX),3u,"old OneX ID");eq(static_cast<unsigned>(OemProducerId::Msi),4u,"old MSI ID");eq(static_cast<unsigned>(OemProducerId::RogClick),5u,"old ROG ID");eq(static_cast<unsigned>(OemProducerId::OneXX2Rear),6u,"new rear appended");eq(static_cast<unsigned>(OemProducerId::OneXX2Home),7u,"new Home appended");
    g_powerLifecycle=PowerLifecycle::Suspended;
    for(unsigned i=0;i<6;++i){const auto source=static_cast<OemProducerId>(i);check(oemProducerContextCurrent(source),"old source context admission unchanged");const auto gen=oemProducerBegin(source);check(oemProducerCurrent(source,gen),"old source generation accepted under old admission");if(source==OemProducerId::RogClick){check(oemProducerPublishRogClick(gen,56,kOemBitAc),"actual old ROG click publisher");nativeOemProducerDrain();eq(rogReceipts.size(),std::size_t{1},"ROG receipt still delivered to ROG dispatcher stub");}else{check(oemProducerPublish(source,gen,0),"old neutral sample accepted");check(oemProducerPublish(source,gen,kOemProducerMasks[i]),"old held sample accepted");eq(oemProducerPhysicalMask()&(source==OemProducerId::Msi?0:kOemProducerMasks[i]),source==OemProducerId::Msi?uint64_t{0}:kOemProducerMasks[i],"legacy physical source remains available");}check(oemProducerCancel(source,gen),"old source cancellation accepted");check(!oemProducerCurrent(source,gen),"old cancelled generation rejected");}
    finish();
}
static void aggregation(){test::section="aggregation-context";
    for(bool keyboard:{false,true}){prepare("OneXPlayerX2");boot();deliver(0x22);++g_powerGeneration;g_oemKeyMask=keyboard?kOemBitL4:0;const auto gen=oemProducerBegin(OemProducerId::Legion);check(oemProducerPublish(OemProducerId::Legion,gen,0),"independent non-overlapping legacy neutral publisher remains available");const auto r=g_oemProducerEdges[(g_oemProducerEdgeHead+g_oemProducerEdgeCount-1)%kOemProducerEdgeCapacity];eq(r.combinedMask&kOemBitL4,keyboard?kOemBitL4:uint64_t{0},"stale new-source mask and authority cannot contaminate independent aggregate");finish();}
}
static void failedPostRetained(){test::section="post-retained";prepare("OneXPlayerX2");boot();rule();appmock::postAccepted=false;deliver(0x21);
    eq(g_oemProducerEdgeCount,std::size_t{1},"failed UI post retains the complete source receipt");check(g_onexX2PublishedReady,"failed notification does not misclassify accepted receipt as queuefull");appmock::now+=1000;nativeOemProducerDrain();jobs();eq(appmock::actions.size(),std::size_t{1},"retained receipt can be consumed after failed UI post");finish();}
static void queueKeepsOther(){test::section="queue-other-source";prepare("OneXPlayerX2");boot();const auto rog=oemProducerBegin(OemProducerId::RogClick);check(oemProducerPublishRogClick(rog,56,kOemBitAc),"old ROG receipt queued before new Home overflow");
    for(std::size_t n=1;n<kOemProducerEdgeCapacity;++n)deliver(0x21);deliver(0x21);
    check(!g_onexX2PublishedReady&&g_onexX2HomeGeneration==0,"Home queuefull revokes its source");eq(g_oemProducerEdgeCount,std::size_t{1},"new source revocation preserves another source receipt");check(oemProducerCurrent(OemProducerId::RogClick,rog),"new source revocation cannot cancel old ROG generation");nativeOemProducerDrain();eq(rogReceipts.size(),std::size_t{1},"preserved ROG receipt delivered on its old route");finish();}
static void recordingExit(){test::section="action-cancel";
    for(bool queuedSerial:{false,true})for(bool recording:{false,true}){prepare("OneXPlayerX2");boot();rule();deliver(0x21);if(queuedSerial)nativeOemProducerDrain();if(recording)g_shortcutRecordingActive=true;else g_exitRequested=true;
        nativeOemProducerDrain();jobs();check(appmock::actions.empty(),"recording or exit cancels UI or already-queued serial Home action");finish();}
}

static std::vector<BYTE> notice(const std::wstring& path,const GUID& guid){
    const auto bytes=offsetof(DEV_BROADCAST_DEVICEINTERFACE_W,dbcc_name)+(path.size()+1)*sizeof(wchar_t);std::vector<BYTE> buffer(bytes,0);
    auto* n=reinterpret_cast<DEV_BROADCAST_DEVICEINTERFACE_W*>(buffer.data());n->dbcc_size=static_cast<DWORD>(bytes);n->dbcc_devicetype=DBT_DEVTYP_DEVICEINTERFACE;n->dbcc_classguid=guid;
    std::copy(path.c_str(),path.c_str()+path.size()+1,n->dbcc_name);return buffer;
}
static void deviceNotification(){test::section="device-notification";prepare("OneXPlayerX2");boot();deliver(0x22);deliver(0x21);
    check(g_onexX2DeviceNotify!=nullptr&&IsEqualGUID(appmock::registeredGuid,kYeManGuidDeviceInterfaceHid),"actual owner registered exact HID GUID using memory API");
    const auto rear=g_onexX2RearGeneration,home=g_onexX2HomeGeneration,epoch=g_onexX2DeviceEpoch.load();const auto token=oemProducerTokensFor(kOemBitHome|kOemBitL4);
    auto wrong=notice(L"unrelated-device",kYeManGuidDeviceInterfaceHid);onexX2ObserveDeviceChange(DBT_DEVICEREMOVECOMPLETE,reinterpret_cast<LPARAM>(wrong.data()));
    auto wrongGuid=kYeManGuidDeviceInterfaceHid;++wrongGuid.Data1;auto badGuid=notice(L"mock-vendor",wrongGuid);onexX2ObserveDeviceChange(DBT_DEVICEARRIVAL,reinterpret_cast<LPARAM>(badGuid.data()));
    auto matching=notice(L"MOCK-VENDOR",kYeManGuidDeviceInterfaceHid);onexX2ObserveDeviceChange(DBT_DEVNODES_CHANGED,reinterpret_cast<LPARAM>(matching.data()));
    onexX2ObserveDeviceChange(DBT_DEVICEARRIVAL,0);eq(g_onexX2DeviceEpoch.load(),epoch,"unrelated or invalid notification cannot revoke new source");
    const auto io=mock::w.readsCalled+mock::w.writesCalled,enumerations=mock::w.enumerations;
    onexX2ObserveDeviceChange(DBT_DEVICEREMOVECOMPLETE,reinterpret_cast<LPARAM>(matching.data()));
    eq(g_onexX2DeviceEpoch.load(),epoch+1,"exact bound path notification advances dedicated device epoch");
    eq(mock::w.readsCalled+mock::w.writesCalled,io,"device notification callback never performs HID IO");eq(mock::w.enumerations,enumerations,"device notification callback never enumerates");
    check(!oemProducerCurrent(OemProducerId::OneXX2Rear,rear)&&!oemProducerCurrent(OemProducerId::OneXX2Home,home),"matching notification rejects old producer before owner ticks");
    check(!oemProducerTokensCurrent(token)&&!oemProducerActionMask()&&!oemProducerPhysicalMask()&&!onexX2HomeDisplayMask(),"device epoch fences actions physical display and queued tokens immediately");finish();
}
static void independentXinput(){test::section="independent-topology";prepare("OneXPlayerX2");boot();rule();deliver(0x22);deliver(0x21);
    const auto generation=g_onexX2Transport.snapshot().generation,rear=g_onexX2RearGeneration,home=g_onexX2HomeGeneration;const auto token=oemProducerTokensFor(kOemBitHome|kOemBitL4);++g_xinputTopologyEpoch;
    check(oemProducerTokensCurrent(token)&&oemProducerCurrent(OemProducerId::OneXX2Rear,rear)&&oemProducerCurrent(OemProducerId::OneXX2Home,home),"unrelated XInput change does not revoke dedicated physical source");ownerAt(appmock::now+1);
    eq(g_onexX2Transport.snapshot().generation,generation,"virtual/XInput topology dirties never restart new reader");eq(mock::w.enumerations,1u,"unrelated XInput dirties cause no new enumeration");nativeOemProducerDrain();jobs();eq(appmock::actions.size(),std::size_t{1},"Home remains consumable across unrelated XInput change");finish();
}
static void notificationFailure(){test::section="notification-readiness";prepare("OneXPlayerX2");appmock::notificationAccepted=false;ownerAt(appmock::now);
    eq(mock::w.enumerations,0u,"failed notification registration prevents unobservable source binding");check(!g_onexX2PublishedReady&&g_onexX2HomeGeneration==0&&g_onexX2RearGeneration==0,"failed notification has no new sources");
    eq(appmock::registrations,1u,"one registration attempt");ownerAt(appmock::now+1999);eq(appmock::registrations,1u,"notification failure observes retry backoff");appmock::notificationAccepted=true;ownerAt(appmock::now+1);eq(appmock::registrations,2u,"notification can recover at 2000ms");eq(g_onexX2Transport.snapshot().phase,p::Phase::Live,"registered observer permits actual memory vendor bind");finish();
}

static void writeGate(){test::section="write-gate";
    for(bool withdrawAfterPreparation:{false,true}){prepare("OneXPlayerX2");boot();const auto boundAt=appmock::now;
        unsigned checks=0;appmock::writePermissionOverride=[&]{++checks;return withdrawAfterPreparation&&checks==1;};ownerAt(boundAt+4000);appmock::writePermissionOverride={};
        eq(mock::w.writesCalled,0u,"denied hardware gate never submits vendor write");eq(g_onexX2Transport.snapshot().phase,p::Phase::Live,"hardware write denial leaves independent reader live");
        check(g_onexX2Transport.snapshot().readPending&&!g_onexX2Transport.snapshot().writePending,"late permission withdrawal discards prepared write without touching pending read");
        eq(g_onexX2Transport.snapshot().initIndex,std::size_t{0},"denied prepared write does not advance init");eq(checks,withdrawAfterPreparation?2u:1u,"actual submission rechecks permission at preparation and write boundary");
        appmock::hardwareAllowed=false;deliver(0x21);check(g_onexX2HomeGeneration!=0,"Ready plus closed hardware gate still accepts complete independent input read");eq(mock::w.writesCalled,0u,"gate remains closed throughout input read and publication");check(g_onexX2Transport.snapshot().readPending&&!g_onexX2Transport.snapshot().writePending,"closed write gate preserves next independent read");appmock::hardwareAllowed=true;ownerAt(appmock::now+1);
        // After the denied tick the normal gate is restored and the event-owner
        // tick may queue its first write. Its true terminal remains required.
        check(g_onexX2Transport.snapshot().writePending,"restored hardware permission can retry exact pending init page");finish();
    }
}

static void crossGenerationHold(){test::section="held-rebind";
    for(const std::string cls:{"OneXPlayerX2","OneXPlayer3"})for(bool releaseFirst:{false,true}){
        prepare(cls);boot();deliver(0x22);eq(oemProducerActionMask(),kOemBitL4,"initial fresh rear press is accepted");const auto old=g_onexX2RearGeneration;
        ++g_powerGeneration;ownerAt(appmock::now+1);check(g_onexX2RearReleaseFence&kOemBitL4,"revocation retains physical held-key fence across generation");
        auto& r=mock::pending(false);mock::complete(r,false);ownerAt(appmock::now+1);ownerAt(g_onexX2Transport.snapshot().retryAt);
        if(!releaseFirst){deliver(0x22);check(g_onexX2RearGeneration!=old,"rebound rear has fresh source generation");eq(oemProducerActionMask(),uint64_t{0},"inherited held key cannot become new action after rebind");eq(oemProducerPhysicalMask(),kOemBitL4,"held physical evidence remains distinct from armed action");}
        deliver(0x22,false);check(!(g_onexX2RearReleaseFence&kOemBitL4),"actual UP clears cross-generation hold fence including release-first observation");
        eq(oemProducerActionMask(),uint64_t{0},"actual UP is neutral");deliver(0x22);eq(oemProducerActionMask(),kOemBitL4,"new real press after UP is accepted");finish();
    }
}

static void rearRule(uint64_t mask){NativeKeyboardShortcutRule r;r.requiredMask=mask;r.ownAction="test.rear";r.trigger="press";g_nativeKeyboardShortcuts.push_back(r);}
static void evaluateLive(){nativeKeyboardShortcutsEvaluate(g_curW,g_curPad,appmock::now,true,nullptr,false,0,nullptr);}
static void contextRebind(bool evaluateGap=false){
    ++g_powerGeneration;ownerAt(appmock::now+1);auto& r=mock::pending(false);mock::complete(r,false);ownerAt(appmock::now+1);
    if(evaluateGap)evaluateLive();ownerAt(g_onexX2Transport.snapshot().retryAt);
}
static void realFirstRear(){test::section="real-first-rear";
    for(const std::string cls:{"OneXPlayerX2","OneXPlayer3"})for(unsigned id:{0x22u,0x23u})for(bool gap:{false,true}){
        prepare(cls);boot();rearRule(id==0x22?kOemBitL4:kOemBitR4);evaluateLive();check(!g_nativeKeyboardShortcuts[0].needsNeutral,"actual idle evaluator establishes neutral before first vendor source");
        deliver(id);nativeOemProducerDrain();jobs();eq(appmock::actions.size(),std::size_t{1},"first fresh L4/R4 source edge dispatches through real evaluator");
        deliver(id,false);nativeOemProducerDrain();jobs();eq(appmock::actions.size(),std::size_t{1},"physical release does not duplicate press action");contextRebind(gap);
        deliver(id);nativeOemProducerDrain();jobs();eq(appmock::actions.size(),std::size_t{2},"first new press after released rebind can dispatch without an extra artificial UP");finish();
    }
}
static void realHeldRear(){test::section="real-held-rear";
    for(const std::string cls:{"OneXPlayerX2","OneXPlayer3"})for(unsigned id:{0x22u,0x23u})for(bool releaseFirst:{false,true}){
        prepare(cls);boot();rearRule(id==0x22?kOemBitL4:kOemBitR4);evaluateLive();deliver(id);nativeOemProducerDrain();jobs();eq(appmock::actions.size(),std::size_t{1},"initial press acted before held rebind");contextRebind(true);
        if(!releaseFirst){deliver(id);nativeOemProducerDrain();evaluateLive();jobs();eq(appmock::actions.size(),std::size_t{1},"held-across-rebind DOWN cannot create another action in actual evaluator");}
        deliver(id,false);nativeOemProducerDrain();jobs();eq(appmock::actions.size(),std::size_t{1},"actual UP/redundant first UP re-arms without fabricating press");
        deliver(id);nativeOemProducerDrain();jobs();eq(appmock::actions.size(),std::size_t{2},"only actual UP then new DOWN can act after held rebind");finish();
    }
}
static void realRuleEdit(bool reconnect){test::section="real-rule-edit";
    for(unsigned id:{0x22u,0x23u}){prepare("OneXPlayerX2");boot();rearRule(id==0x22?kOemBitL4:kOemBitR4);evaluateLive();deliver(id);nativeOemProducerDrain();jobs();eq(appmock::actions.size(),std::size_t{1},"old rule had one accepted press");
        if(reconnect){deliver(id,false);nativeOemProducerDrain();jobs();}
        ++g_oemShortcutEdgeEpoch;g_nativeKeyboardShortcuts[0].ownAction="test.rear.edited";
        if(reconnect){contextRebind(false);deliver(id);nativeOemProducerDrain();}
        else{evaluateLive();deliver(id);nativeOemProducerDrain();evaluateLive();}
        jobs();eq(appmock::actions.size(),std::size_t{1},"rule change cannot inherit old neutral/press across held state or new source");check(g_nativeKeyboardShortcuts[0].needsNeutral,"edited rule still needs actual neutral UP");
        deliver(id,false);nativeOemProducerDrain();jobs();check(!g_nativeKeyboardShortcuts[0].needsNeutral,"actual UP establishes neutral for edited rule");deliver(id);nativeOemProducerDrain();jobs();eq(appmock::actions.size(),std::size_t{2},"edited rule acts after actual UP and new DOWN");eq(appmock::actions.back(),std::string("test.rear.edited:0"),"updated action owns the new press");finish();
    }
}
static void realRuleEditHeld(){realRuleEdit(false);}
static void realRuleEditRebind(){realRuleEdit(true);}
static void pendingWriteGate(){test::section="pending-write-gate";prepare("OneXPlayerX2");boot();ownerAt(appmock::now+4000);auto& w=mock::pending(true);auto* ov=w.ov;auto* buffer=w.buffer;
    appmock::hardwareAllowed=false;ownerAt(appmock::now+1);check(g_onexX2Transport.snapshot().writePending,"closing gate cannot discard an already-submitted pending write");eq(w.ov,ov,"submitted write retains OVERLAPPED when permission closes");eq(w.buffer,buffer,"submitted write retains buffer when permission closes");eq(mock::w.writesCalled,1u,"closed gate cannot resubmit pending write");check(g_onexX2Transport.hasDevice(),"submitted write keeps actual owner handle until terminal");finish();
}

static void legacyNeutral(){test::section="legacy-neutral";
    for(const auto source:{OemProducerId::OneX,OemProducerId::Gpd,OemProducerId::Legion}){
        prepare("OneXPlayerX1",YmccFamily::OneXPlayer,false);const auto bit=source==OemProducerId::Legion?kOemBitLegionR:kOemBitL4;rearRule(bit);evaluateLive();check(!g_nativeKeyboardShortcuts[0].needsNeutral,"legacy rule has prior idle neutral");
        const auto generation=oemProducerBegin(source);check(oemProducerPublish(source,generation,bit),"legacy source initial held sample accepted by ledger");nativeOemProducerDrain();jobs();check(appmock::actions.empty(),"new rear neutral exception cannot alter legacy first-source held behavior");
        check(oemProducerPublish(source,generation,0),"legacy real neutral accepted");nativeOemProducerDrain();check(oemProducerPublish(source,generation,bit),"legacy new press accepted");nativeOemProducerDrain();jobs();eq(appmock::actions.size(),std::size_t{1},"legacy action still fires after actual neutral and new press");finish();
    }
}

static void exactArrival(){test::section="exact-arrival";prepare("OneXPlayerX2");boot();deliver(0x22);deliver(0x21);
    const auto epoch=g_onexX2DeviceEpoch.load();const auto token=oemProducerTokensFor(kOemBitHome|kOemBitL4);
    for(unsigned fault=0;fault<4;++fault){auto data=notice(L"mock-vendor",kYeManGuidDeviceInterfaceHid);auto* d=reinterpret_cast<DEV_BROADCAST_DEVICEINTERFACE_W*>(data.data());
        if(fault==0)d->dbcc_devicetype=DBT_DEVTYP_OEM;
        if(fault==1)d->dbcc_size=static_cast<DWORD>(offsetof(DEV_BROADCAST_DEVICEINTERFACE_W,dbcc_name));
        if(fault==2)d->dbcc_size=65537;
        if(fault==3){const auto n=(data.size()-offsetof(DEV_BROADCAST_DEVICEINTERFACE_W,dbcc_name))/sizeof(wchar_t);std::fill(d->dbcc_name,d->dbcc_name+n,L'X');}
        onexX2ObserveDeviceChange(DBT_DEVICEARRIVAL,reinterpret_cast<LPARAM>(data.data()));eq(g_onexX2DeviceEpoch.load(),epoch,"malformed size/type/unterminated path cannot dirty device epoch");
    }
    const auto enumCount=mock::w.enumerations,io=mock::w.readsCalled+mock::w.writesCalled;
    auto matching=notice(L"mock-vendor",kYeManGuidDeviceInterfaceHid);onexX2ObserveDeviceChange(DBT_DEVICEARRIVAL,reinterpret_cast<LPARAM>(matching.data()));
    eq(g_onexX2DeviceEpoch.load(),epoch+1,"exact bound HID arrival advances only dedicated device epoch");check(!oemProducerTokensCurrent(token)&&!oemProducerActionMask()&&!oemProducerPhysicalMask(),"arrival fences old source before owner runs");
    eq(mock::w.enumerations,enumCount,"arrival callback performs no enumeration");eq(mock::w.readsCalled+mock::w.writesCalled,io,"arrival callback performs no IO");finish();
}

static std::size_t actionCount(const std::string& name){return static_cast<std::size_t>(std::count(appmock::actions.begin(),appmock::actions.end(),name+":0"));}
static void symmetricRealUp(){test::section="symmetric-real-up";
    for(const std::string cls:{"OneXPlayerX2","OneXPlayer3"})for(unsigned held:{0x22u,0x23u}){
        const auto other=held==0x22?0x23u:0x22u;const auto heldBit=held==0x22?kOemBitL4:kOemBitR4,otherBit=held==0x22?kOemBitR4:kOemBitL4;
        prepare(cls);boot();rearRule(heldBit);g_nativeKeyboardShortcuts.back().ownAction="test.held";rearRule(otherBit);g_nativeKeyboardShortcuts.back().ownAction="test.other";rearRule(heldBit|otherBit);g_nativeKeyboardShortcuts.back().ownAction="test.composite";
        evaluateLive();deliver(held);nativeOemProducerDrain();jobs();eq(actionCount("test.held"),std::size_t{1},"old held side acted before generation change");contextRebind(true);
        deliver(other,false);nativeOemProducerDrain();jobs();check(g_onexX2RearReleaseFence&heldBit,"opposite UP does not clear held side's owner fence");
        deliver(other);nativeOemProducerDrain();jobs();eq(actionCount("test.other"),std::size_t{1},"opposite actual UP/DOWN remains usable");
        deliver(held);nativeOemProducerDrain();jobs();eq(actionCount("test.composite"),std::size_t{0},"opposite UP/DOWN cannot authorize held-side composite action");eq(oemProducerActionMask()&heldBit,uint64_t{0},"session-initialized missing side is not a proven physical UP");eq(actionCount("test.held"),std::size_t{1},"old held side cannot act before its own real UP");
        deliver(held,false);nativeOemProducerDrain();jobs();check(!(g_onexX2RearReleaseFence&heldBit),"own real UP releases only its held fence");deliver(held);nativeOemProducerDrain();jobs();eq(actionCount("test.composite"),std::size_t{1},"own UP then new DOWN can now form a real composite");
        deliver(held,false);deliver(other,false);nativeOemProducerDrain();jobs();deliver(held);nativeOemProducerDrain();jobs();eq(actionCount("test.held"),std::size_t{2},"held side works independently after its own UP");finish();
    }
}
static void clearBindHooks(){appmock::beforeEnumeration={};appmock::afterEnumeration={};appmock::afterOwnerOpen={};appmock::afterRead={};}
static void bindRaces(){test::section="bind-race";
    for(unsigned stage=0;stage<3;++stage){prepare("OneXPlayerX2");rule();mock::Plan immediate;immediate.mode=mock::Mode::Immediate;immediate.frame=raw(0x21);mock::w.reads.push_back(immediate);bool fired=false;
        const auto remove=[&]{if(fired)return;fired=true;auto message=notice(L"mock-vendor",kYeManGuidDeviceInterfaceHid);onexX2ObserveDeviceChange(DBT_DEVICEREMOVECOMPLETE,reinterpret_cast<LPARAM>(message.data()));};
        if(stage==0)appmock::afterEnumeration=remove;if(stage==1)appmock::afterOwnerOpen=remove;if(stage==2)appmock::afterRead=remove;
        ownerAt(appmock::now);clearBindHooks();check(fired,"removal actually injected inside real bind/read boundary");
        if(stage==0){eq(mock::w.dataOpens,0u,"unknown-candidate journal removal blocks before owner open");eq(mock::w.readsCalled,0u,"unknown candidate removal cannot submit first read");}
        if(stage==1){eq(mock::w.dataOpens,1u,"owner-open race occurs on real memory owner handle");eq(mock::w.closeDevices,1u,"post-owner validation closes removed device");eq(mock::w.createEvents,0u,"removed owner cannot allocate read/write events");eq(mock::w.readsCalled,0u,"second bind permission check blocks first IO");}
        if(stage==2)eq(mock::w.readsCalled,1u,"first synchronous read actually completed before late publication fence");
        check(!g_onexX2PublishedReady&&g_onexX2RearGeneration==0&&g_onexX2HomeGeneration==0,"bind-window removal cannot create old producer");nativeOemProducerDrain();jobs();check(appmock::actions.empty(),"bind-window removal cannot dispatch stale Home action");
        check(!g_onexX2BindingObservation&&g_onexX2BindingChanges.empty()&&!g_onexX2BindingOverflow,"owner ends and clears bind observation on normal return");finish();
    }
}
static void bindingJournal(){test::section="bind-journal";
    for(unsigned count:{64u,65u}){prepare("OneXPlayerX2");bool inspected=false;appmock::afterEnumeration=[&]{for(unsigned i=0;i<count;++i){auto data=notice(L"unrelated-"+std::to_wstring(i),kYeManGuidDeviceInterfaceHid);onexX2ObserveDeviceChange(DBT_DEVICEARRIVAL,reinterpret_cast<LPARAM>(data.data()));}eq(g_onexX2BindingChanges.size(),std::size_t{64},"bind notification journal is bounded to 64 entries");eq(g_onexX2BindingOverflow,count>64,"65th notice marks overflow");inspected=true;};
        ownerAt(appmock::now);clearBindHooks();check(inspected,"journal populated before path known");if(count==65){eq(mock::w.dataOpens,0u,"journal overflow rejects new owner open");check(!g_onexX2Transport.hasDevice()&&!g_onexX2PublishedReady,"overflow cannot claim bound input");}
        else{eq(g_onexX2Transport.snapshot().phase,p::Phase::Live,"64 unrelated notices do not deny valid bind");eq(mock::w.enumerations,1u,"unrelated bind notices do not force duplicate enumeration");}
        check(!g_onexX2BindingObservation&&g_onexX2BindingChanges.empty(),"bind journal has no cross-tick history");finish();
    }
}
static void liveUnrelatedJournal(){test::section="live-journal";prepare("OneXPlayerX2");boot();rule();deliver(0x22);const auto generation=g_onexX2Transport.snapshot().generation,epoch=g_onexX2DeviceEpoch.load();
    appmock::beforeCompletion=[&]{for(unsigned i=0;i<65;++i){auto data=notice(L"unrelated-"+std::to_wstring(i),kYeManGuidDeviceInterfaceHid);onexX2ObserveDeviceChange(DBT_DEVICEREMOVECOMPLETE,reinterpret_cast<LPARAM>(data.data()));}check(g_onexX2BindingOverflow,"live tick can observe bounded unrelated overflow");};
    deliver(0x21);appmock::beforeCompletion={};eq(g_onexX2DeviceEpoch.load(),epoch,"unrelated HID journal cannot change current device epoch");eq(g_onexX2Transport.snapshot().generation,generation,"unrelated overflow cannot retire an already bound session");nativeOemProducerDrain();jobs();eq(appmock::actions.size(),std::size_t{1},"live Home survives unrelated journal overflow");finish();
}
static void observationException(){test::section="binding-exception";prepare("OneXPlayerX2");appmock::beforeEnumeration=[]{throw std::runtime_error("memory-only bind fault before allocation");};ownerAt(appmock::now);clearBindHooks();
    check(!g_onexX2BindingObservation&&g_onexX2BindingChanges.empty()&&!g_onexX2BindingOverflow,"owner catch ends bind observation");check(!g_onexX2PublishedReady&&g_onexX2HomeGeneration==0&&g_onexX2RearGeneration==0,"exception cannot publish source");eq(mock::w.enumerations,0u,"injected exception precedes any fake enumeration allocation");finish();
}

static void posttickSourceRevokeRace(){test::section="posttick-source-race";prepare("OneXPlayerX2");rule();mock::Plan first;first.mode=mock::Mode::Immediate;first.frame=raw(0x21);mock::w.reads.push_back(first);
    bool fired=false,pathWasPublished=false;const auto epoch=g_onexX2DeviceEpoch.load();
    appmock::afterSourceRevoke=[&](bool){const auto s=g_onexX2Transport.snapshot();if(fired||s.phase!=p::Phase::Live||s.generation==g_onexX2SessionGeneration||s.validFrames!=1)return;
        fired=true;const auto bound=std::atomic_load_explicit(&g_onexX2BoundDevicePath,std::memory_order_acquire);pathWasPublished=bound&&!bound->empty();
        auto message=notice(L"mock-vendor",kYeManGuidDeviceInterfaceHid);onexX2ObserveDeviceChange(DBT_DEVICEREMOVECOMPLETE,reinterpret_cast<LPARAM>(message.data()));
    };
    ownerAt(appmock::now);appmock::afterSourceRevoke={};check(fired,"matching remove injected in actual posttick new-session source revocation stage");
    eq(mock::w.readsCalled,1u,"first Home really completed synchronously before posttick revoke");eq(g_onexX2Transport.snapshot().validFrames,uint64_t{1},"valid Home was decoded before removal race");
    nativeOemProducerDrain();jobs();check(appmock::actions.empty(),"posttick source revocation cannot hide matching remove and execute old Home");
    check(pathWasPublished,"bind callback path survives posttick action-only revoke");eq(g_onexX2DeviceEpoch.load(),epoch+1,"posttick remove immediately invalidates source context");
    check(!g_onexX2PublishedReady&&g_onexX2HomeGeneration==0&&g_onexX2RearGeneration==0,"posttick removal cannot publish stale source");finish();
}

struct Case{const char* name;void(*run)();};
static const Case cases[]={
    {"posttick-source-revoke-race",posttickSourceRevokeRace},
    {"symmetric-real-up",symmetricRealUp},{"bind-races",bindRaces},{"binding-journal",bindingJournal},{"live-unrelated-journal",liveUnrelatedJournal},{"observation-exception",observationException},
    {"exact-arrival",exactArrival},
    {"legacy-neutral",legacyNeutral},
    {"real-first-rear",realFirstRear},{"real-held-rear",realHeldRear},{"real-rule-edit-held",realRuleEditHeld},{"real-rule-edit-rebind",realRuleEditRebind},{"pending-write-gate",pendingWriteGate},
    {"cross-generation-hold",crossGenerationHold},
    {"write-gate",writeGate},
    {"device-notification",deviceNotification},{"independent-xinput",independentXinput},{"notification-failure",notificationFailure},
    {"failed-post-retained",failedPostRetained},{"queue-keeps-other",queueKeepsOther},{"recording-exit",recordingExit},
    {"admission",admission},{"excluded-models",excludedModels},{"readiness",readiness},{"keyboard-owned",keyboardOwned},{"mini-keyboard",miniKeyboard},{"rear-ui",rearUi},{"home-delayed",homeDelayed},{"home-no-rule",noHomeRule},{"home-unsupported",unsupportedHomeRules},{"home-double",homeDouble},{"home-rule-epoch",homeRuleEpoch},{"context-fences",contextFences},{"owner-late-read",ownerLateRead},{"stop-retention",stopRetention},{"queue-full",queueFull},{"log-evidence",logEvidence},{"stable-logs",stableLogs},{"old-producers",oldProducers},{"aggregation",aggregation}
};
int main(int argc,char** argv){if(argc!=2)return 2;for(const auto& c:cases)if(c.name==std::string(argv[1])){test::scenario=c.name;c.run();std::printf("{\"status\":\"pass\",\"scenario\":\"%s\",\"checks\":%llu,\"realDeviceApiCalls\":0}\n",c.name,static_cast<unsigned long long>(test::checks));return 0;}return 2;}
