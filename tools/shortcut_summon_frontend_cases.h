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
    check(!ymcc::summonPressHides(false,true,false,true,true),"invalid HWND cannot enter hide branch");
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
    for (const auto& name : {"steam","ps","xbox","gamebar"}) {
        check(nativeYmccShortcutAction("input.frontendButton") && nativeYmccShortcutDelta("input.frontendButton",{{"button",name}},delta),"frontend action parses each of four buttons");
    }
    for (const auto& bad : {json::object(),json{{"button","invalid"}},json{{"button",3}}})
        check(!nativeYmccShortcutDelta("input.frontendButton",bad,delta),"frontend parser rejects missing/unknown/non-string parameter");
    for (const auto& entry : std::vector<std::pair<std::string,std::string>>{{"steam","steamdeck"},{"ps","dualsense-edge"},{"ps","dualsense"},{"xbox","elite"},{"xbox","xbox360"}}) {
        auto r=rule("input.frontendButton","");r["params"]={{"button",entry.first}};
        resetRoute(json::array({r}),dedicated(baseInput()));fixtureFrontendPersona=g_inputHostBoundPersona=entry.second;
        const auto before=fixtureInput.dump();tick(0,10000);tick(XINPUT_GAMEPAD_A,10020);flushJobs();
        check(g_frontendButtonPulse.pending() && g_frontendButtonPulse.sample(10020,1,true)==1,"actual frontend rule queues Guide for matching active persona");
        check(fixtureInput.dump()==before && fixtureRefreshes==0,"frontend action never changes global/dedicated settings or active preset");
        check(g_frontendButtonPulse.sample(10140,1,true)==0,"Guide pulse auto-releases after 120ms without timer");
    }
    resetRoute(json::array());fixtureFrontendPersona="steamdeck";g_inputHostBoundPersona="elite";
    check(!nativeYmccShortcutEmit("input.frontendButton",1),"configured Steam persona cannot inject into an old still-bound Xbox target");
    resetRoute(json::array());fixtureFrontendPersona=g_inputHostBoundPersona="steamdeck";g_inputHostPrepared=false;
    check(!nativeYmccShortcutEmit("input.frontendButton",1),"unprepared matching target reports failure instead of pretending to send Guide");
    resetRoute(json::array());fixtureFrontendPersona=g_inputHostBoundPersona="disabled";
    check(!nativeYmccShortcutEmit("input.frontendButton",1) && !g_frontendButtonPulse.pending(),"Steam action fails explicitly when matching pad is disabled");
    fixtureFrontendPersona=g_inputHostBoundPersona="elite";
    check(!nativeYmccShortcutEmit("input.frontendButton",2) && !g_frontendButtonPulse.pending(),"PS action never creates/switches an Xbox virtual target");
    fixtureFrontendPersona=g_inputHostBoundPersona="steamdeck";g_inputHostEpochMirror=0;
    check(!nativeYmccShortcutEmit("input.frontendButton",1),"frontend requires nonzero live Host generation");
    resetRoute(json::array());fixtureFrontendPersona=g_inputHostBoundPersona="steamdeck";
    check(nativeYmccShortcutEmit("input.frontendButton",1),"frontend admits matching target before queue");
    ++g_inputHostEpochMirror;flushJobs();check(!g_frontendButtonPulse.pending(),"target changed during queued action cancels Guide request");
    for (const int gate : {0,1,2,3}) {
        resetRoute(json::array());fixtureFrontendPersona=g_inputHostBoundPersona="steamdeck";
        nativeYmccShortcutEmit("input.frontendButton",1);
        if(gate==0)g_shortcutRecordingActive=true;
        if(gate==1)g_exitRequested=true;
        if(gate==2)g_inputReady=false;
        if(gate==3)fixtureWriterAllowed=false;
        flushJobs();check(!g_frontendButtonPulse.pending(),"queued frontend obeys recording/exit/input/power gate");
    }
    resetRoute(json::array());fixtureFrontendPersona=g_inputHostBoundPersona="steamdeck";fixtureWindowForeground=true;
    nativeYmccShortcutEmit("input.frontendButton",1);flushJobs();
    check(fixtureOwnActions==std::vector<std::string>{"window.hideToTray"},"frontend hides focused YMCC with double-B primitive before external menu output");
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
    resetRoute(json::array({keyboard}));tick(0,10000);fixtureDesktopDown.insert('K');tick(0,10020);flushJobs();
    check(fixtureSendInputs.size()==1 && fixtureSendInputs[0].size()==4,"keyboard source dispatches actual frontend Gamebar action");
#if defined(YMCC_ROG_CLICK_SOURCE)
    auto frontendOem=rule("input.frontendButton","","press","oem","lib");frontendOem["params"]={{"button","steam"}};
    resetRoute(json::array({frontendOem}));fixtureFrontendPersona=g_inputHostBoundPersona="steamdeck";tick(0,10000);
    const auto frontendOemGeneration=activateProducer(OemProducerId::RogClick);BYTE frontendLib[]={0x5A,147};
    fixtureTick+=20;rogOemKeyParse(frontendLib,2,frontendOemGeneration);nativeOemProducerDrain();flushJobs();
    check(g_frontendButtonPulse.pending(),"dedicated OEM shortcut emits frontend Guide via real production rule router");
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
