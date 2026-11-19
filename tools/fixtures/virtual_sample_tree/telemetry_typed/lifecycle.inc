static json typedLifecycleReceipts=json::array();
static void typedLifecycleCases(const json& hosts) {
    const auto input=active(hosts,"steamdeck",true);
    const auto originalPower=g_gamepadSerialActionPower;
    const auto originalBound=GamepadSerialEvent::BoundRevision;
    const auto resetContext=[&]{g_gamepadSerialActionPower=0;GamepadSerialEvent::BoundRevision=0;};
    {
        ++cases;Fixture a(input),b(input);a.fixturePowerGeneration=b.fixturePowerGeneration=7;
        g_gamepadSerialActionPower=7;
        a.beforePost();b.candidatePost();
        check(a.posted&&b.posted&&b.posted->gyroTelemetry,"typed power test actually queued record");
        check(a.posted->actionPower==7&&b.posted->actionPower==7,"original action power copied");
        a.fixturePowerGeneration=b.fixturePowerGeneration=8;
        a.consumePosted();b.consumePosted();
        check(a.delivered.empty()&&b.delivered.empty()&&!a.posted&&!b.posted,"retired power cancels both event representations");
        typedLifecycleReceipts.push_back({{"name","retired-power"},{"queuedTyped",true},{"delivered",false}});resetContext();
    }
    {
        ++cases;Fixture a(input),b(input);
        GamepadSerialEvent::BoundRevision=GamepadSerialEvent::ConfigurationRevision.load();
        a.beforePost();b.candidatePost();
        check(a.posted&&b.posted&&b.posted->gyroTelemetry,"typed revision test actually queued record");
        {
            GamepadSerialEvent::ConfigurationWrite writer(true);
            a.consumePosted();b.consumePosted();
        }
        check(a.delivered.empty()&&b.delivered.empty()&&!a.posted&&!b.posted,"configuration writer revokes both event representations");
        typedLifecycleReceipts.push_back({{"name","configuration-writer"},{"queuedTyped",true},{"delivered",false}});resetContext();
    }
    {
        ++cases;Fixture a(input),b(input);resetContext();
        a.beforePost();b.candidatePost();
        check(a.posted&&b.posted&&a.posted->actionPower==0&&b.posted->actionRevision==0,"state notifications keep zero context");
        a.fixturePowerGeneration=b.fixturePowerGeneration=9;
        {GamepadSerialEvent::ConfigurationWrite writer(true);a.consumePosted();b.consumePosted();}
        check(!a.delivered.empty()&&a.delivered==b.delivered,"zero state notification is not a stale user action");
        typedLifecycleReceipts.push_back({{"name","zero-state-notification"},{"delivered",true}});
    }
    for(const bool windowLost:{false,true}) {
        ++cases;Input configured=input;
        configured.postSuccess=windowLost;configured.dropWindowDuringSnapshot=windowLost;
        Fixture f(configured);resetContext();liveBalance=0;liveEnabled=true;
        try{f.candidatePost();}catch(...){liveEnabled=false;throw;}
        liveEnabled=false;
        check(!f.posted&&!f.postedToken&&liveBalance==0,"typed failed post/window loss releases every owned allocation");
        typedLifecycleReceipts.push_back({{"name",windowLost?"window-lost-during-snapshot":"failed-post"},{"liveAllocationBalance",liveBalance},{"postCalls",f.postCalls}});
    }
    {
        ++cases;Fixture f(input);resetContext();f.holdPostedToken=true;
        liveBalance=0;liveEnabled=true;
        try{
            f.candidatePost();
            check(f.postedToken!=0&&!f.posted&&liveBalance>0,"typed event genuinely remains owned in mailbox");
            g_gamepadSerialEventMailbox->retire();
        }catch(...){liveEnabled=false;throw;}
        liveEnabled=false;
        const auto cancelled=f.postedToken;f.postedToken=0;
        check(liveBalance==0,"terminal retire destroys pending typed record and complete Host");
        check(!g_gamepadSerialEventMailbox->take(cancelled),"late wake cannot recover retired typed payload");
        typedLifecycleReceipts.push_back({{"name","terminal-retire-pending"},{"liveAllocationBalance",liveBalance},{"lateTakeEmpty",true}});
    }
    {
        ++cases;resetContext();Fixture oldPath(input),typedPath(input);
        liveBalance=0;liveEnabled=true;
        try{oldPath.beforePost();typedPath.candidatePost();}catch(...){liveEnabled=false;throw;}
        liveEnabled=false;
        check(!oldPath.posted&&!typedPath.posted&&!oldPath.postedToken&&!typedPath.postedToken,"terminal mailbox rejects both late producers");
        check(oldPath.postCalls==0&&typedPath.postCalls==0&&liveBalance==0,"late producer never wakes or leaks after terminal retire");
        typedLifecycleReceipts.push_back({{"name","terminal-late-producers"},{"oldPostCalls",oldPath.postCalls},{"typedPostCalls",typedPath.postCalls},{"liveAllocationBalance",liveBalance}});
    }
    g_gamepadSerialActionPower=originalPower;GamepadSerialEvent::BoundRevision=originalBound;
}
