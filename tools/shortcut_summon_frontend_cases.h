// Feed a recorded key through the owner's hook and UI drain, not a sampled held key.
static void shortcutFrontendRecordedKey(const json& keyboard) {
    resetRoute(json::array({keyboard}));
    tick(0,10000);
#if defined(YMCC_OEM_CONSUME_SOURCE) && defined(YMCC_SHORTCUT_EDGE_SOURCE)
    oemBackMappingReconcileOnUi();
    tick(0,10001);
    KBDLLHOOKSTRUCT key{};key.vkCode='K';key.scanCode='K';
    ++fixtureTick;oemBackHookProc(HC_ACTION,WM_KEYDOWN,reinterpret_cast<LPARAM>(&key));
    key.flags=LLKHF_UP;
    ++fixtureTick;oemBackHookProc(HC_ACTION,WM_KEYUP,reinterpret_cast<LPARAM>(&key));
    deliverPhysicalReceipts();nativeDesktopFunctionDrain();
#else
    fixtureDesktopDown.insert('K');tick(0,10020);
#endif
    flushJobs();
}

// Original frontend consumer, extended for the common touch/shortcut transport.
static void shortcutSteamMenuCases() {
    for(int button:{1,5})for(const auto* persona:{"disabled","steamdeck","dualsense-edge","elite"})for(bool prepared:{false,true}) {
        auto r=rule("input.frontendButton","");r["params"]={{"button",button==1?"steam":"steamQuickAccess"}};
        resetRoute(json::array({r}),dedicated(baseInput()));fixtureFrontendPersona=persona;g_inputHostBoundPersona="elite";
        g_inputHostPrepared=prepared;g_inputHostEpochMirror=0;if(!prepared)g_inputHostProcess=nullptr;
        const auto before=fixtureInput.dump();tick(0,10000);tick(XINPUT_GAMEPAD_A,10020);flushJobs();
        tick(XINPUT_GAMEPAD_A,10100);flushJobs();
        check(fixtureSteamAttempts==1 && fixtureSteamRequests.size()==1,"held controller action dispatches exactly one native Steam key for every virtual mode");
        check(fixtureDeckyRefreshes==1&&fixtureDeckyChecks==1&&
            fixtureDeckyMenuDiagnostic.value("source","")== (button==1?"ymcc-steam-button":"ymcc-quick-access"),
            "each delivered Steam key queues one shared Decky health check even with no attached frontend");
        const auto& request=fixtureSteamRequests[0];
        check(request.value("operation","")=="menu.button" && request.value("menu","")==(button==1?"main":"quick-access") && !request.contains("qamFollowup"),"controller selects exact shared main/QAM request without touch recovery ticket");
        check(!g_frontendButtonPulse.pending() && fixtureSendInputs.empty() && fixtureOwnActions.empty(),"Steam sends no Guide, Ctrl shortcut, focus mutation or second route");
        check(fixtureInput.dump()==before && fixtureRefreshes==0,"Steam menu never changes global or dedicated settings");
        check(std::any_of(fixtureLogs.begin(),fixtureLogs.end(),[button](const json& row){return row.value("event","")=="shortcut-frontend-button" && row.value("button",0)==button && row.value("ok",false) && row.value("signal","")=="steam-native-button" && row.value("keyDelivered",false) && !row.value("runtimeAccepted",true);}),"receipt distinguishes key delivery from visible menu response");
    }
    for(int button:{1,5}) {
        for(bool newerTouch:{false,true}) {
            resetRoute(json::array());
            {
                std::lock_guard<std::mutex> lock(ymcc::qamfollowup::mutex);
                ymcc::qamfollowup::current.generation=++ymcc::qamfollowup::generation;
                ymcc::qamfollowup::done=false;ymcc::qamfollowup::pending=true;
            }
            nativeYmccShortcutEmit("input.frontendButton",button);
            if(newerTouch) {
                std::lock_guard<std::mutex> lock(ymcc::qamfollowup::mutex);
                ymcc::qamfollowup::current.generation=++ymcc::qamfollowup::generation;
            }
            flushJobs();
            check(ymcc::qamfollowup::done.load()!=newerTouch && ymcc::qamfollowup::pending.load()==newerTouch,
                "new controller menu cancels captured touch recovery but preserves a later touch ticket");
        }
        resetRoute(json::array());fixtureWindowForeground=true;fixtureDesktopDown={VK_SHIFT,VK_LCONTROL,VK_MENU};
        check(nativeYmccShortcutEmit("input.frontendButton",button),"Steam accepts direct route without physical modifier or virtual target gate");flushJobs();
        check(fixtureSteamRequests.size()==1 && fixtureOwnActions.empty() && fixtureWindowForeground,"Steam menu leaves foreground ownership unchanged");
        resetRoute(json::array());fixtureJobAccept=false;
        check(!nativeYmccShortcutEmit("input.frontendButton",button) && fixtureSteamAttempts==0,"queue rejection never sends or falls back");
        for(int gate=0;gate<7;++gate)for(bool during:{false,true}) {
            resetRoute(json::array());
            const auto invalidate=[gate] {
                if(gate==0)g_shortcutRecordingActive=true;
                if(gate==1)g_exitRequested=true;
                if(gate==2)g_inputReady=false;
                if(gate==3)fixtureWriterAllowed=false;
                if(gate==4)++fixturePowerGeneration;
                if(gate==5)++g_oemShortcutEdgeEpoch;
                if(gate==6)++GamepadSerialEvent::ConfigurationRevision;
            };
            check(nativeYmccShortcutEmit("input.frontendButton",button),"Steam request accepted before lifecycle transition");
            if(during)fixtureSteamBeforeSend=invalidate;else invalidate();
            flushJobs();check(fixtureSteamRequests.empty() && fixtureSendInputs.empty() && !g_frontendButtonPulse.pending(),"stale recording/exit/input/power/source/config cancels before final send");
        }
        for(int fault=0;fault<6;++fault) {
            resetRoute(json::array());
            if(fault==0)fixtureSteamResult={{"ok",false},{"mutated",false},{"reason","live-api-unavailable"}};
            if(fault==1)fixtureSteamResult={{"ok",false},{"mutated",true},{"reason","timeout"}};
            if(fault==2)fixtureSteamResult=nullptr;
            if(fault==3)fixtureSteamResult={{"ok","true"}};
            if(fault==4)fixtureSteamResult={{"ok",true},{"mutated","false"}};
            if(fault==5)fixtureSteamThrows=true;
            nativeYmccShortcutEmit("input.frontendButton",button);flushJobs();
            check(fixtureSteamAttempts==1 && fixtureSteamRequests.size()==1 && fixtureSendInputs.empty() && !g_frontendButtonPulse.pending(),"failure/malformed/uncertain transport has one attempt and no fallback");
            check(!fixtureDeckyRefreshes&&!fixtureDeckyChecks,"undelivered or uncertain key does not queue a completed-menu health check");
            const std::string reason=fault==0?"steam-menu-unavailable":"steam-menu-result-uncertain";
            check(std::any_of(fixtureLogs.begin(),fixtureLogs.end(),[&](const json& row){return row.value("event","")=="shortcut-frontend-button" && !row.value("ok",true) && row.value("reason","")==reason;}),"Steam failure settles through existing frontend receipt");
        }
    }
#if defined(YMCC_STEAM_MENU_DIRECT_SOURCE)
    for(bool quick:{false,true}) {
        resetRoute(json::array());const json ticket=quick?json{{"id","original-touch"},{"startedAt",1000}}:json();
        const auto result=nativeSteamMenuButton(quick,[]{return true;},ticket);
        check(result.value("ok",false) && fixtureSteamRequests.size()==1 && fixtureSteamRequests[0].value("operation","")=="menu.button","touch-style call executes the same actual native sender");
        check(quick?fixtureSteamRequests[0]["qamFollowup"]==ticket:!fixtureSteamRequests[0].contains("qamFollowup"),"touch recovery ticket is preserved exactly and only for QAM");
        check(fixtureDeckyRefreshes==1&&fixtureDeckyChecks==1,"touch shares the same once-per-key health completion");
        fixtureSteamRequests.clear();nativeSteamMenuButton(quick,[]{return false;},ticket);
        check(fixtureSteamRequests.empty(),"shared sender rejects invalid touch lifecycle before entering transport");
    }
    for(int boundary=0;boundary<6;++boundary) {
        resetRoute(json::array());
        fixtureSteamAfterSend=[boundary]{
            if(boundary==0)++fixtureSteamSession.created; // PID reuse after dispatch.
            if(boundary==1)++fixturePowerGeneration;
            if(boundary==2)fixtureDeckyEnabled=false;
            if(boundary==3)fixtureDeckyFenced=true;
            if(boundary==4)fixtureDeckyModeDemand=false;
            if(boundary==5)g_aiFanMockSession.isolated=true;
        };
        const auto result=nativeSteamMenuButton(true,[]{return true;});
        check(result==fixtureSteamResult&&fixtureSteamRequests.size()==1&&!fixtureDeckyRefreshes&&!fixtureDeckyChecks,
            "stale or disabled health demand cannot change a delivered QAM receipt or repeat the key");
    }
#endif
    for (unsigned variant=0;variant<5;++variant) {
        resetRoute(json::array());
        const bool quick=variant!=4;
        fixtureSteamResult["runtimeAccepted"]=variant!=1;
        fixtureSteamResult["before"]=variant==2?0:quick?2:1;
        fixtureSteamResult["menu"]=variant==3?2:0;
        const auto reply=nativeSteamMenuButton(quick,[]{return true;});
        check(reply==fixtureSteamResult&&fixtureSteamRequests.size()==1&&fixtureDeckyRefreshes==1&&
              fixtureDeckyChecks==(variant==0?0u:1u)&&fixtureDeckyMenuDiagnostic.value("readReusedHealthy",false)==(variant==0),
              "only a confirmed QAM close may reuse health; all key receipts stay unchanged");
    }
    auto keyboard=rule("input.frontendButton","","press","keyboard","KeyK");keyboard["params"]={{"button","steamQuickAccess"}};
    shortcutFrontendRecordedKey(keyboard);
    check(fixtureSteamRequests.size()==1 && fixtureSteamRequests[0].value("menu","")=="quick-access","recorded keyboard source shares controller and touch Steam channel");
    check(fixtureDeckyRefreshes==1&&fixtureDeckyChecks==1,"recorded keyboard Steam receipt uses the same Decky completion exactly once");
}

// Memory-only coverage of the production dispatcher, shared predicate and pulse.
static void shortcutSummonFrontendCases() {
    for (const bool foreground : {false, true}) {
        for (const bool fullHeight : {false, true}) {
            resetRoute(json::array());
            fixtureWindowForeground=foreground;g_fullHeight=fullHeight;fixtureWindowZoomed=!fullHeight;
            const auto gen=g_summonGeneration;
            check(nativeYmccShortcutEmit("window.summon",0) && fixtureOwnActions==std::vector<std::string>{"window.hideToTray"},
                "summon hides maximized/full-height YMCC independently of foreground");
            check(g_summonGeneration==gen+1,"hide invalidates late summon registration generation");
        }
    }
    for (const int state : {0,1,2}) {
        resetRoute(json::array());
        fixtureWindowZoomed=state!=0;fixtureWindowVisible=state!=1;fixtureWindowIconic=state==2;
        check(nativeYmccShortcutEmit("window.summon",0) && fixtureOwnActions==std::vector<std::string>{"window.summon"},
            "normal/hidden/iconic YMCC restores instead of hiding remembered maximization");
    }
    const HWND savedMainWindow=g_hwnd;
    g_hwnd=nullptr;
    check(!nativeYmccShortcutEmit("window.summon",0),"invalid HWND cannot enter hide or show branch");
    g_hwnd=savedMainWindow;
#if defined(YMCC_SHORTCUT_DEFAULT_OWNER_SOURCE)
    resetRoute(json::array());tick(0,10000);fixtureLegacyReset();fixtureWindowZoomed=true;
    fixtureLegacyEval(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER,10100);
    fixtureLegacyEval(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER,10601);
    fixtureLegacyEval(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER,11200);
    check(fixtureOwnActions==std::vector<std::string>{"window.hideToTray"},"legacy LB+RB shares toggle and long hold fires only once");
    fixtureLegacyEval(0,11300);fixtureWindowVisible=false;
    fixtureLegacyEval(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER,11400);
    fixtureLegacyEval(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER,11901);
    check(fixtureOwnActions.size()==2 && fixtureOwnActions.back()=="window.summon","release/repress LB+RB restores hidden maximized window");
#endif
#if defined(YMCC_ROG_CLICK_SOURCE)
    auto oem=rule("window.summon","","press","oem","lib");resetRoute(json::array({oem}));tick(0,10000);
    fixtureWindowZoomed=true;
    const auto oemGeneration=activateProducer(OemProducerId::RogClick);BYTE lib[]={0x5A,147};
    fixtureTick+=20;rogOemKeyParse(lib,2,oemGeneration);nativeOemProducerDrain();flushJobs();
    check(fixtureOwnActions==std::vector<std::string>{"window.hideToTray"},"dedicated OEM LIB summon uses actual unified hide branch");
#endif
    int delta=0;
    for (const auto& name : {"steam","steamQuickAccess","ps","xbox","gamebar"}) {
        check(nativeYmccShortcutAction("input.frontendButton") && nativeYmccShortcutDelta("input.frontendButton",{{"button",name}},delta),"frontend action parses all five buttons");
    }
    for (const auto& bad : {json::object(),json{{"button","invalid"}},json{{"button",3}}})
        check(!nativeYmccShortcutDelta("input.frontendButton",bad,delta),"frontend parser rejects missing/unknown/non-string parameter");
    for (const auto& entry : std::vector<std::pair<std::string,std::string>>{{"ps","dualsense-edge"},{"xbox","elite"}}) {
        auto r=rule("input.frontendButton","");r["params"]={{"button",entry.first}};
        resetRoute(json::array({r}),dedicated(baseInput()));fixtureFrontendPersona=g_inputHostBoundPersona=entry.second;
        const auto before=fixtureInput.dump();tick(0,10000);tick(XINPUT_GAMEPAD_A,10020);flushJobs();
        check(g_frontendButtonPulse.pending() && g_frontendButtonPulse.sample(10020,1,true)==1,"actual frontend rule queues Guide for matching active persona");
        check(fixtureInput.dump()==before && fixtureRefreshes==0,"frontend action never changes global/dedicated settings or active preset");
        check(g_frontendButtonPulse.sample(10140,1,true)==0,"Guide pulse auto-releases after 120ms without timer");
    }
    for(const auto& persona:std::vector<std::pair<std::string,std::string>>{{"ps","dualsense"},{"xbox","xbox360"}}) {
        resetRoute(json::array());fixtureFrontendPersona=g_inputHostBoundPersona=persona.second;
        check(!nativeYmccShortcutEmit("input.frontendButton",static_cast<int>(ymcc::frontendButton(persona.first))),"unsupported historical PS/Xbox target rejected");
    }
    resetRoute(json::array());fixtureFrontendPersona="dualsense-edge";g_inputHostBoundPersona="elite";
    check(!nativeYmccShortcutEmit("input.frontendButton",2),"PS cannot inject into an old still-bound Xbox target");
    resetRoute(json::array());fixtureFrontendPersona=g_inputHostBoundPersona="dualsense-edge";g_inputHostPrepared=false;
    check(!nativeYmccShortcutEmit("input.frontendButton",2),"unprepared matching PS target reports failure");
    resetRoute(json::array());fixtureFrontendPersona=g_inputHostBoundPersona="disabled";
    check(!nativeYmccShortcutEmit("input.frontendButton",2) && !g_frontendButtonPulse.pending(),"PS explicitly requires matching enabled pad");
    fixtureFrontendPersona=g_inputHostBoundPersona="elite";
    check(!nativeYmccShortcutEmit("input.frontendButton",2) && !g_frontendButtonPulse.pending(),"PS never creates or switches an Xbox target");
    fixtureFrontendPersona=g_inputHostBoundPersona="dualsense-edge";g_inputHostEpochMirror=0;
    check(!nativeYmccShortcutEmit("input.frontendButton",2),"PS requires nonzero live Host generation");
    resetRoute(json::array());fixtureFrontendPersona=g_inputHostBoundPersona="dualsense-edge";
    check(nativeYmccShortcutEmit("input.frontendButton",2),"PS admits matching target before queue");
    ++g_inputHostEpochMirror;flushJobs();check(!g_frontendButtonPulse.pending(),"target changed during queued PS action cancels Guide request");
    for (const int gate : {0,1,2,3}) {
        resetRoute(json::array());fixtureFrontendPersona=g_inputHostBoundPersona="dualsense-edge";
        nativeYmccShortcutEmit("input.frontendButton",2);
        if(gate==0)g_shortcutRecordingActive=true;
        if(gate==1)g_exitRequested=true;
        if(gate==2)g_inputReady=false;
        if(gate==3)fixtureWriterAllowed=false;
        flushJobs();check(!g_frontendButtonPulse.pending(),"queued PS obeys recording/exit/input/power gate");
    }
    resetRoute(json::array());fixtureFrontendPersona=g_inputHostBoundPersona="dualsense-edge";fixtureWindowForeground=true;
    nativeYmccShortcutEmit("input.frontendButton",2);flushJobs();
    check(fixtureOwnActions==std::vector<std::string>{"window.hideToTray"},"PS retains existing focus-return primitive before virtual output");
    shortcutSteamMenuCases();
    resetRoute(json::array());
    check(nativeYmccShortcutEmit("input.frontendButton",4),"Gamebar works without virtual target");flushJobs();
    check(fixtureSendInputs.size()==1 && fixtureSendInputs[0].size()==4,"Gamebar uses one atomic SendInput sequence");
    const auto& inputs=fixtureSendInputs[0];
    check(inputs[0].ki.wVk==VK_LWIN && inputs[1].ki.wVk=='G' && inputs[2].ki.wVk=='G' && inputs[3].ki.wVk==VK_LWIN &&
          inputs[0].ki.dwFlags==0 && inputs[1].ki.dwFlags==0 && inputs[2].ki.dwFlags==KEYEVENTF_KEYUP && inputs[3].ki.dwFlags==KEYEVENTF_KEYUP,
          "Gamebar outputs Win-down G-down G-up Win-up and no persistent device intent");
    resetRoute(json::array());fixtureSendCount=1;nativeYmccShortcutEmit("input.frontendButton",4);flushJobs();
    check(fixtureSendInputs.size()==2 && fixtureSendInputs[1].size()==2 && fixtureSendInputs[1][1].ki.dwFlags==KEYEVENTF_KEYUP,"partial Gamebar injection attempts key-release cleanup");
    auto keyboard=rule("input.frontendButton","","press","keyboard","KeyK");keyboard["params"]={{"button","gamebar"}};
    shortcutFrontendRecordedKey(keyboard);
    check(fixtureSendInputs.size()==1 && fixtureSendInputs[0].size()==4,"keyboard source dispatches actual frontend Gamebar action");
#if defined(YMCC_ROG_CLICK_SOURCE)
    auto frontendOem=rule("input.frontendButton","","press","oem","lib");frontendOem["params"]={{"button","steam"}};
    resetRoute(json::array({frontendOem}));fixtureFrontendPersona=g_inputHostBoundPersona="steamdeck";tick(0,10000);
    const auto frontendOemGeneration=activateProducer(OemProducerId::RogClick);BYTE frontendLib[]={0x5A,147};
    fixtureTick+=20;rogOemKeyParse(frontendLib,2,frontendOemGeneration);nativeOemProducerDrain();flushJobs();
    check(fixtureSteamRequests.size()==1 && fixtureSteamRequests[0].value("menu","")=="main" && !g_frontendButtonPulse.pending(),"dedicated OEM shortcut uses same direct Steam transport via real rule router");
#endif
    ymcc::FrontendButtonPulse pulse;
    check(pulse.sample(0,1,true)==0 && !pulse.request(0,0),"idle/zero-epoch Guide pulse has no output");
    check(pulse.request(10,1) && pulse.sample(30,1,false)==0 && pulse.sample(500,1,true)==1,"Guide waits boundedly for existing neutral admission");
    check(!pulse.request(550,1) && pulse.sample(619,1,true)==1 && pulse.sample(620,1,true)==0,"repeat request cannot extend 120ms pulse");
    pulse.request(1000,1);check(pulse.sample(1750,1,true)==0 && !pulse.pending(),"unadmitted pulse expires at 750ms instead of replaying");
    pulse.request(2000,1);check(pulse.sample(2001,2,true)==0,"Host generation change cancels pending pulse");
    pulse.request(3000,1);pulse.sample(3001,1,true);check(pulse.sample(3002,1,false)==0 && !pulse.pending(),"loss of admission releases already pressed Guide");
    pulse.request(4000,1);check(pulse.request(4001,2) && pulse.sample(4002,2,true)==1,"new epoch may replace stale pending intent without old-target replay");
    pulse.clear();pulse.request(5000,1);check(pulse.sample(4999,1,true)==0,"backward clock invalidates pulse");
    pulse.request(6000,1);check((4u | pulse.sample(6001,1,true))==5u,"Guide bit coexists with existing screen button bits");
    resetRoute(json::array());
}


// Same original fixture: real parser/evaluator/dispatcher -> summon -> actual
// bringToFront with memory window ports -> real UI gates and final frame selector.
// No Host submission, driver writes, Windows windows, timers or processes run.
static json shieldChord(const char* action,const char* trigger,const char* first,const char* second) {
    auto r=rule(action,"",trigger);r["id"]="summon-action-shield";r["origin"]="custom";r["params"]=json::object();
    r["inputs"]=json::array({{{"source","controller"},{"code",first}},{{"source","controller"},{"code",second}}});
    return r;
}
static XINPUT_GAMEPAD shieldPhysicalSample(WORD buttons) {
    XINPUT_GAMEPAD p{};p.wButtons=buttons;p.bLeftTrigger=111;p.bRightTrigger=222;
    p.sThumbLX=-32768;p.sThumbLY=12345;p.sThumbRX=32767;p.sThumbRY=-23456;return p;
}
static void shieldTick(WORD buttons,ULONGLONG now) {
    auto physical=shieldPhysicalSample(buttons);
    // This recognizer requires an exact chord and an initial neutral sample.
    // Nonzero triggers would be extra chord inputs, not merely frame witnesses.
    // shieldState separately exercises full nonneutral trigger/axis/IMU frames.
    physical.bLeftTrigger=physical.bRightTrigger=0;
    const auto before=physical;
    fixtureTick=now;nativeKeyboardShortcutsEvaluate(buttons,physical,now);flushJobs();
    check(std::memcmp(&physical,&before,sizeof physical)==0,"shortcut evaluation preserves physical navigation sample");
}
static void shieldState(bool blocked,const std::string& label,WORD buttons=XINPUT_GAMEPAD_DPAD_DOWN) {
    check(gamepadUiInputEligible()==blocked,label+" actual UI ownership");
    check(msiNativeForegroundUiEligible()==blocked && msiForegroundIsolationUiEligible()==blocked,
        label+" both real physical foreground admission bridges");
    auto physical=shieldPhysicalSample(buttons);const auto original=physical;
    FocusFrameContent live;live.buttons=physical.wButtons;live.lxShort=physical.sThumbLX;live.lyShort=physical.sThumbLY;
    live.rxShort=physical.sThumbRX;live.ryShort=physical.sThumbRY;live.rawLtByte=physical.bLeftTrigger;live.rawRtByte=physical.bRightTrigger;
    live.lt=0.4f;live.rt=0.8f;live.ltRaw=0.3f;live.rtRaw=0.7f;live.backButtons=3;live.screenButtons=7;
    live.imuAdmitted=true;live.gyroX=12;live.gyroY=-3;live.gyroZ=7;live.accelX=1;live.accelY=2;live.accelZ=3;
    live.qw=0.5;live.qx=0.5;live.qy=0.5;live.qz=0.5;
    live.touchpads.present=true;live.touchpads.left.down=true;live.touchpads.left.click=true;live.touchpads.left.x=123;
    live.touchpads.left.y=456;live.touchpads.left.pressure=42;
    const json before=focusIsolationAssembleFrame(live),neutral=focusIsolationAssembleFrame(focusIsolationNeutralContent());
    check(before!=neutral,label+" nonneutral physical witness");
    // Current production catalog has exactly these three output personas. Each is
    // checked through generic output and the applicable Deck/PS5/MSI bound plan.
    for(const char* persona:{"steamdeck","dualsense-edge","elite"}) {
        check(ymcc::controllerPersona::currentOutput(persona),"shield matrix persona remains a current production output");
        for(int route=0;route<3;++route) {
            FocusPlan plan;plan.virtualUiPersona=persona;
            if(route==1){plan.steamDeckTarget=std::string(persona)=="steamdeck";plan.ps5Target=std::string(persona)=="dualsense-edge";}
            if(route==2)plan.msiTarget=true; // Memory bound-plan input, no board/Host probe.
            check(virtualUiOwnsInput(plan)==blocked,label+" generic UI gate "+persona);
            if(plan.steamDeckTarget)check(steamDeckUiOwnsInput(plan)==blocked,label+" Deck gate");
            if(plan.ps5Target)check(ps5UiOwnsInput(plan)==blocked,label+" PS5 gate");
            if(plan.msiTarget)check(msiUiOwnsInput(plan)==blocked,label+" MSI gate");
            check(fixtureShieldOutput(plan,live)==(blocked?neutral:before),label+" production complete final frame "+persona+" route "+std::to_string(route));
        }
    }
    check(focusIsolationAssembleFrame(live)==before && std::memcmp(&physical,&original,sizeof physical)==0,
        label+" output shielding leaves physical UI buttons/axes/triggers/motion/touch unchanged");
}
static void shieldLoad(const json& rules) {
    resetRoute(rules);fixtureWindowVisible=false;fixtureWindowForeground=false;
    shieldTick(0,10000); // Actual revision/parser arm requires a neutral sample.
}
static void shortcutSummonActionShieldCases(int mode=0) {
    // Mutation subset 1: candidate dispatcher/summon disconnect must fail here.
    if(mode!=2) {
        for(int chord=0;chord<2;++chord)for(const char* trigger:{"press","hold","release","double","repeat"}) {
            const WORD bits=chord==0?WORD(XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_A):WORD(XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_Y);
            auto r=shieldChord("window.summon",trigger,chord==0?"back":"x",chord==0?"a":"y");
            shieldLoad(json::array({r}));
            check(g_nativeKeyboardShortcuts.size()==1 && g_nativeKeyboardShortcuts[0].ownAction=="window.summon",
                std::string("custom rule parsed as shared summon: ")+trigger);
            fixtureCapturedTarget={true,static_cast<unsigned long long>(100+chord)};
            g_pendingSummonTarget={true,999}; // Stale previous session must never win.
            shieldState(false,"custom before trigger",bits);
            shieldTick(bits,10100);
            if(std::string(trigger)=="hold" || std::string(trigger)=="repeat") {shieldState(false,"timed trigger below threshold",bits);shieldTick(bits,10599);shieldState(false,"timed trigger 499ms",bits);shieldTick(bits,10600);}
            if(std::string(trigger)=="release") {shieldState(false,"release waits for up",bits);shieldTick(0,10140);}
            if(std::string(trigger)=="double") {shieldState(false,"double first press",bits);shieldTick(0,10140);shieldTick(bits,10200);}
            check(fixtureOwnActions==std::vector<std::string>{"window.summon"},"custom chord emits exactly one shared summon");
            check(g_summonGeneration>0 && fixtureSummonTimerKills==1 && fixtureSummonTimerStarts==0,
                "actual bringToFront reaches successful memory foreground port");
            check(fixturePreparedTargets.size()==1 && fixturePreparedTargets[0].valid &&
                fixturePreparedTargets[0].token==fixtureCapturedTarget.token && g_pendingSummonTarget.token==0,
                "custom summon passes actual captured target into session and consumes stale pending");
            shieldState(true,std::string("custom summon ")+trigger,bits);
            // Releasing the chord and navigating with an unrelated single key
            // must keep shielding; old LB+RB never acts as an extra input gate.
            shieldTick(0,10800);shieldTick(XINPUT_GAMEPAD_DPAD_DOWN,10900);
            shieldState(true,"ordinary single key after custom summon",XINPUT_GAMEPAD_DPAD_DOWN);
            fixtureWindowForeground=false;shieldState(false,"custom focus loss returns current live frame");
            fixtureWindowForeground=true;fixtureWindowZoomed=true;g_pendingSummonTarget={true,444};
            check(nativeYmccShortcutEmit("window.summon",0),"same summon action accepts toggle hide");flushJobs();
            check(g_pendingSummonTarget.token==0 && fixturePreparedTargets.size()==1,"hide clears pending without preparing a new summon session");
            check(!fixtureWindowVisible && !fixtureWindowForeground,"toggle hide updates memory window state");
            shieldState(false,"custom hide returns current live frame");
        }
        // Keyboard/mouse and OEM bindings use the same action, without any
        // shoulder sample or legacy recognizer feeding the ownership decision.
        for(bool desktop:{false,true}) {
            auto r=desktop ? rule("window.summon","","press","keyboard","ControlLeft")
                           : rule("window.summon","","press","oem","lib");
            r["origin"]="custom";r["params"]=json::object();
            if(desktop)r["inputs"].push_back({{"source","mouse"},{"code","MouseRight"}});
            shieldLoad(json::array({r}));fixtureCapturedTarget={true,desktop?700ull:701ull};
            if(desktop){fixtureDesktopDown={VK_LCONTROL,VK_RBUTTON};shieldTick(0,10100);}
            else {
                const auto generation=activateProducer(OemProducerId::RogClick);BYTE lib[]={0x5A,147};
                fixtureTick=10100;rogOemKeyParse(lib,2,generation);nativeOemProducerDrain();flushJobs();
            }
            check(fixtureOwnActions==std::vector<std::string>{"window.summon"},"desktop/OEM custom action reaches the shared summon");
            shieldState(true,"desktop/OEM summon shield without shoulder sample");
            fixtureDesktopDown.clear();fixtureOemMask=0;shieldTick(0,10200);
            shieldState(true,"desktop/OEM trigger release keeps UI ownership");
            fixtureWindowZoomed=true;nativeYmccShortcutEmit("window.summon",0);flushJobs();
            shieldState(false,"desktop/OEM toggle returns game output");
        }
        for(int chord=0;chord<2;++chord) {
            const WORD bits=chord==0?WORD(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER):WORD(XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_A);
            auto r=shieldChord("os.openTouchKeyboard","press",chord==0?"lb":"back",chord==0?"rb":"a");
            shieldLoad(json::array({r}));shieldTick(bits,10100);shieldTick(bits,10700);
#if defined(YMCC_SHORTCUT_DEFAULT_OWNER_SOURCE)
            fixtureLegacyReset();fixtureLegacyEval(bits,10100);fixtureLegacyEval(bits,10700);flushJobs();
#endif
            check(fixtureOwnActions==std::vector<std::string>{"os.openTouchKeyboard"},"rebound shoulder/custom chord executes only its other action");
            shieldState(false,"other action never acquires summon shield",bits);
        }
        auto custom=shieldChord("window.summon","press","back","a");
        shieldLoad(json::array({custom}));shieldTick(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER,10100);
        shieldTick(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER,10700);
#if defined(YMCC_SHORTCUT_DEFAULT_OWNER_SOURCE)
        fixtureLegacyReset();fixtureLegacyEval(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER,10100);
        fixtureLegacyEval(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER,10700);flushJobs();
#endif
        check(fixtureOwnActions.empty(),"old shoulder chord does not match custom summon rule");shieldState(false,"unbound default shoulders");
        custom["enabled"]=false;shieldLoad(json::array({custom}));shieldTick(XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_A,10100);
        check(fixtureOwnActions.empty(),"disabled custom summon does not dispatch");shieldState(false,"disabled custom chord");
        custom["enabled"]=true;shieldLoad(json::array({custom}));shieldTick(XINPUT_GAMEPAD_BACK,10100);
        check(fixtureOwnActions.empty(),"incomplete chord does not dispatch");shieldState(false,"incomplete custom chord");
        shieldLoad(json::array({custom}));fixtureCapturedTarget={false,0};g_pendingSummonTarget={true,777};
        shieldTick(XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_A,10100);
        check(fixturePreparedTargets.size()==1 && !fixturePreparedTargets[0].valid && fixturePreparedTargets[0].token==0,
            "invalid captured target never borrows stale valid pending target");
        check(g_pendingSummonTarget.token==0,"session port consumes pending target after invalid summon");
        shieldState(true,"invalid target still permits YMCC UI shield");
    }
    // Mutation subset 2: a chord-dependent/always-false UI gate must fail while
    // YMCC receives ordinary navigation after the default chord was released.
    if(mode!=1) {
#if defined(YMCC_SHORTCUT_DEFAULT_OWNER_SOURCE)
        auto defaultSummon=shieldChord("window.summon","hold","lb","rb");
        defaultSummon["origin"]="native-default";defaultSummon["defaultKey"]="enabled";
        shieldLoad(json::array({defaultSummon}));fixtureLegacyReset();
        const WORD shoulders=XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER;
        fixtureLegacyEval(shoulders,10100);fixtureLegacyEval(shoulders,10601);flushJobs();
        check(fixtureOwnActions==std::vector<std::string>{"window.summon"},"default shoulders call shared production summon");
        shieldState(true,"default summon shoulders",shoulders);
        fixtureLegacyEval(0,10700);flushJobs();shieldTick(XINPUT_GAMEPAD_A,10800);
        shieldState(true,"default summon ordinary A after shoulders released",XINPUT_GAMEPAD_A);
        fixtureWindowForeground=false;shieldState(false,"default focus loss restores live output");
        fixtureWindowForeground=true;fixtureWindowZoomed=true;
        fixtureLegacyEval(shoulders,11000);fixtureLegacyEval(shoulders,11501);flushJobs();
        check(fixtureOwnActions==std::vector<std::string>({"window.summon","window.hideToTray"}),"default second gesture uses shared toggle hide");
        shieldState(false,"default hidden restores live output");
#endif
        shieldLoad(json::array({shieldChord("window.summon","press","x","y")}));fixtureFocusAllowed=false;
        shieldTick(XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_Y,10100);
        check(fixtureWindowVisible && !fixtureWindowForeground && fixtureSummonTimerStarts==1,"failed foreground handoff records only an inert timer request");
        shieldState(false,"visible but foreground denied cannot claim physical or virtual shield");
    }
    resetRoute(json::array());
}
