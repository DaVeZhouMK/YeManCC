#pragma once
// Event-driven display-stack maintenance shared in spirit with the monitor HUD.
// No process scanning, data cadence, resident polling, input forwarding or focus
// manipulation. Three bounded checks follow a display transition on the HWND owner.
namespace ymcc::screenpads {
inline constexpr UINT kOwnerVisibilityMessage=WM_APP+177;
inline std::array<HWINEVENTHOOK,5> ownerVisibilityHooks{};
inline UINT_PTR ownerVisibilityEpoch=1;
inline bool ownerVisibilityQueued=false,ownerVisibilityRaiseRequested=false,ownerVisibilityArtworkRequested=false;
enum class OwnerVisibilityForceSource : unsigned {Unspecified=0,WindowTransition=1,OwnerRefresh=2};
inline unsigned ownerVisibilityQueuedForceSources=0;
struct OwnerVisibilityWork {
    HWND lastRoot=nullptr;
    DWORD lastPid=0;
    UINT_PTR lastEpoch=0;
    unsigned long long forcedPasses=0,sameRootForcedPasses=0,windowTransitionPasses=0,ownerRefreshPasses=0;
    unsigned long long forcedPairLayerRaises=0,occlusionPairLayerRaises=0;
};
inline OwnerVisibilityWork ownerVisibilityWork;
inline unsigned long long ownerVisibilityArtworkSubmissions=0;
struct OwnerVisibilityRecheck {HWND window=nullptr,foreground=nullptr;UINT_PTR timer=0,serial=0x594d5a00,epoch=0;unsigned step=0;};
inline OwnerVisibilityRecheck ownerVisibilityRecheck;
inline unsigned long long ownerVisibilityDelayedChecks=0;
inline constexpr std::array<UINT,3> kOwnerVisibilityRecheckDelays={200,800,2000};
inline void stopOwnerVisibilityRechecks(){
    auto& r=ownerVisibilityRecheck;const auto window=r.window;const auto timer=r.timer;
    r.timer=0;r.window=nullptr;r.foreground=nullptr;r.epoch=0;r.step=0;
    if(window&&timer)KillTimer(window,timer);
}
inline unsigned long long ownerVisibilityEvents=0,ownerVisibilityChecks=0,ownerVisibilityRaises=0;
inline bool ownerVisibilityEpochCurrent(UINT_PTR epoch) noexcept {
    return initialized&&!inputTeardown&&epoch==ownerVisibilityEpoch&&!displayTransitionFence.pending;
}
inline bool ownerVisibilityActive(){return ownerVisibilityHooks[0]!=nullptr;}
inline bool visibleScreenControls(){
    for(HWND h:windows)if(h&&IsWindowVisible(h))return true;
    return buttonsVisible()||ymcc::customtouch::visible();
}
inline void cancelOwnerVisibilityWork(){
    ++ownerVisibilityEpoch;ownerVisibilityQueued=false;ownerVisibilityRaiseRequested=false;ownerVisibilityArtworkRequested=false;ownerVisibilityQueuedForceSources=0;stopOwnerVisibilityRechecks();
}
inline void stopOwnerVisibility(){
    cancelOwnerVisibilityWork();
    for(auto& hook:ownerVisibilityHooks){if(hook)UnhookWinEvent(hook);hook=nullptr;}
}
inline bool queueOwnerVisibility(bool requestRaise=false,bool requestArtwork=false,OwnerVisibilityForceSource source=OwnerVisibilityForceSource::Unspecified){
    const HWND h=owner.load();
    if(!ownerVisibilityEpochCurrent(ownerVisibilityEpoch)||!sharedInputAllowed()||!ownerVisibilityActive()||!onOwnerThread(h)||!visibleScreenControls())return false;
    ownerVisibilityRaiseRequested=ownerVisibilityRaiseRequested||requestRaise;ownerVisibilityArtworkRequested=ownerVisibilityArtworkRequested||requestArtwork;
    if(requestRaise)ownerVisibilityQueuedForceSources|=static_cast<unsigned>(source);
    if(ownerVisibilityQueued)return true;
    ownerVisibilityQueued=true;
    if(PostMessageW(h,kOwnerVisibilityMessage,ownerVisibilityEpoch,0))return true;
    ownerVisibilityQueued=false;ownerVisibilityRaiseRequested=false;ownerVisibilityArtworkRequested=false;ownerVisibilityQueuedForceSources=0;return false;
}
inline void CALLBACK ownerVisibilityRecheckProc(HWND,UINT,UINT_PTR,DWORD);
inline void armOwnerVisibilityRecheck(){
    auto& r=ownerVisibilityRecheck;
    if(!r.window||r.step>=kOwnerVisibilityRecheckDelays.size())return;
    const auto id=++r.serial;
    if(!SetTimer(r.window,id,kOwnerVisibilityRecheckDelays[r.step],ownerVisibilityRecheckProc)){stopOwnerVisibilityRechecks();return;}
    r.timer=id;
}
inline void beginOwnerVisibilityRechecks(){
    const HWND foreground=GetForegroundWindow(),root=foreground?GetAncestor(foreground,GA_ROOT):nullptr;
    if(!root)return;
    auto& r=ownerVisibilityRecheck;
    // Eligibility only governs a new burst; repeated events cannot extend this one.
    if(r.timer&&r.foreground==root&&r.epoch==ownerVisibilityEpoch)return;
    DWORD pid=0;
    if(!IsWindowVisible(root)||IsIconic(root)||!GetWindowThreadProcessId(root,&pid)||pid==GetCurrentProcessId())return;
    stopOwnerVisibilityRechecks();r.window=owner.load();r.foreground=root;r.epoch=ownerVisibilityEpoch;armOwnerVisibilityRecheck();
}
inline void CALLBACK ownerVisibilityRecheckProc(HWND window,UINT,UINT_PTR id,DWORD){
    auto& r=ownerVisibilityRecheck;
    if(!r.timer||r.window!=window||r.timer!=id)return;
    KillTimer(window,id);r.timer=0;
    const HWND foreground=GetForegroundWindow(),root=foreground?GetAncestor(foreground,GA_ROOT):nullptr;
    if(r.epoch!=ownerVisibilityEpoch||root!=r.foreground||!queueOwnerVisibility(false,r.step+1==kOwnerVisibilityRecheckDelays.size())){stopOwnerVisibilityRechecks();return;}
    ++ownerVisibilityDelayedChecks;++r.step;
    if(r.step<kOwnerVisibilityRecheckDelays.size())armOwnerVisibilityRecheck();else stopOwnerVisibilityRechecks();
}
inline void CALLBACK screenVisibilityObserved(HWINEVENTHOOK,DWORD event,HWND h,LONG object,LONG child,DWORD,DWORD){
    if(!h||child!=CHILDID_SELF||!initialized||!ownerVisibilityActive())return;
    // Actual top-level reorders are also delivered on the desktop CLIENT
    // object, not just OBJID_WINDOW on the game. Do not discard that event.
    const bool desktopOrder=event==EVENT_OBJECT_REORDER&&(h==GetDesktopWindow()||h==GetShellWindow())&&
        (object==OBJID_WINDOW||object==OBJID_CLIENT);
    if(!desktopOrder&&object!=OBJID_WINDOW)return;
    if(!desktopOrder){
        const HWND foreground=GetForegroundWindow();
        if(h!=foreground&&(!foreground||h!=GetAncestor(foreground,GA_ROOT)))return;
    }
    ++ownerVisibilityEvents;
    if(ownerVisibilityQueued){
        // The posted check reads the live stack. A reorder adds neither a
        // flag nor a timer; a same-root location event cannot extend its burst.
        if(event==EVENT_OBJECT_REORDER)return;
        const auto& r=ownerVisibilityRecheck;
        if(event==EVENT_OBJECT_LOCATIONCHANGE&&r.timer&&r.epoch==ownerVisibilityEpoch){
            const HWND foreground=GetForegroundWindow();
            if(foreground&&GetAncestor(foreground,GA_ROOT)==r.foreground)return;
        }
    }
    const bool transition=event==EVENT_SYSTEM_FOREGROUND||event==EVENT_OBJECT_SHOW||event==EVENT_SYSTEM_MINIMIZEEND;
    if(queueOwnerVisibility(transition,false,OwnerVisibilityForceSource::WindowTransition)&&(transition||event==EVENT_OBJECT_LOCATIONCHANGE))beginOwnerVisibilityRechecks();
}
inline void syncOwnerVisibility(){
    if(!initialized||inputTeardown||!sharedInputAllowed()||!onOwnerThread(owner.load())||!visibleScreenControls()){if(ownerVisibilityActive())stopOwnerVisibility();return;}
    if(ownerVisibilityActive()){
        // A settings/rebind refresh can show cached layers under the same game.
        // Recheck that display now, rather than waiting for another game event.
        if(queueOwnerVisibility(true,true,OwnerVisibilityForceSource::OwnerRefresh))beginOwnerVisibilityRechecks();
        return;
    }
    constexpr std::array<DWORD,5> events={EVENT_SYSTEM_FOREGROUND,EVENT_OBJECT_SHOW,EVENT_OBJECT_REORDER,EVENT_OBJECT_LOCATIONCHANGE,EVENT_SYSTEM_MINIMIZEEND};
    for(size_t i=0;i<events.size();++i){
        ownerVisibilityHooks[i]=SetWinEventHook(events[i],events[i],nullptr,screenVisibilityObserved,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        if(!ownerVisibilityHooks[i]){const DWORD error=GetLastError();stopOwnerVisibility();windowError=error?error:ERROR_GEN_FAILURE;return;}
    }
    if(queueOwnerVisibility(true,false,OwnerVisibilityForceSource::OwnerRefresh))beginOwnerVisibilityRechecks();
}
struct ScreenForegroundGeometry {
    HWND window;
    RECT bounds{};
    bool queried=false,eligible=false;
    bool overlaps(HWND layer){
        return layer&&IsWindowVisible(layer)&&overlapsVisible(layer);
    }
    bool overlapsVisible(HWND layer){
        if(!window)return false;
        if(!queried){
            queried=true;
            eligible=IsWindowVisible(window)&&!IsIconic(window)&&GetWindowRect(window,&bounds);
        }
        RECT layerBounds{},intersection{};
        return eligible&&GetWindowRect(layer,&layerBounds)&&IntersectRect(&intersection,&layerBounds,&bounds);
    }
    bool occludes(HWND layer){
        if(!overlaps(layer))return false;
        // The Z-order is still read separately for every checked layer.
        HWND before=GetWindow(layer,GW_HWNDPREV);
        for(int i=0;before&&i<128;++i,before=GetWindow(before,GW_HWNDPREV))if(before==window)return true;
        return false;
    }
};
inline bool screenLayerOverlaps(HWND layer,HWND foreground){return ScreenForegroundGeometry{foreground}.overlaps(layer);}
inline bool screenLayerOccluded(HWND layer,HWND foreground){return ScreenForegroundGeometry{foreground}.occludes(layer);}
enum class ScreenPairInputKind { Unknown, Pad, Button, Custom };
struct ScreenPairInputSlot {
    ScreenPairInputKind kind=ScreenPairInputKind::Unknown;
    size_t index=0;
    const ymcc::customtouch::Item* item=nullptr;
};
struct ScreenPairVisibilityEpoch {
    UINT_PTR visibility=ownerVisibilityEpoch;
    UINT64 pad=padRefreshEpoch,button=buttonRefreshEpoch,custom=ymcc::customtouch::refreshEpoch;
    bool current(ScreenPairInputKind kind) const noexcept {
        if(!ownerVisibilityEpochCurrent(visibility))return false;
        if(kind==ScreenPairInputKind::Pad)return pad==padRefreshEpoch;
        if(kind==ScreenPairInputKind::Button)return button==buttonRefreshEpoch;
        if(kind==ScreenPairInputKind::Custom)return custom==ymcc::customtouch::refreshEpoch;
        return pad==padRefreshEpoch&&button==buttonRefreshEpoch&&custom==ymcc::customtouch::refreshEpoch;
    }
};
inline bool screenPairMouseSafe(HWND input,HWND outline,ScreenPairInputSlot slot={},const ScreenPairVisibilityEpoch& binding={}){
    // A synchronous window callback can replace the input binding without replacing its HWND.
    const auto current=[&]{return binding.current(slot.kind);};
    if(!current())return false;
    bool safe=!outline||ymcc::screeninput::ensureDisplay(outline);
    if(!current())return false;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    if(input&&nativeInputCandidateRequested){
        // Display style repair can re-enter the owner. Treat the caller's slot
        // only as a hint and confirm its current membership and HWND afterwards.
        const auto i=slot.index;
        ymcc::screeninput::Surface* surface=nullptr;
        if(slot.kind==ScreenPairInputKind::Pad&&i<windows.size()&&windows[i]==input)
            surface=&nativeInputSurfaces[i];
        else if(slot.kind==ScreenPairInputKind::Button&&i<buttonWindows.size()&&buttonWindows[i]==input)
            surface=&nativeButtonSurfaces[i];
        else if(slot.kind==ScreenPairInputKind::Custom&&slot.item&&i<ymcc::customtouch::items.size()){
            const auto& item=ymcc::customtouch::items[i];
            if(item.get()==slot.item&&item->window==input)surface=&item->surface;
        }
        // A live HWND has one owner slot. Resolve before calling Win32 again;
        // an item found via fallback may not be in the caller's retained list.
        std::shared_ptr<ymcc::customtouch::Item> fallbackItem;
        if(!surface){
            for(size_t i=0;!surface&&i<windows.size();++i)if(windows[i]==input){
                surface=&nativeInputSurfaces[i];slot={ScreenPairInputKind::Pad,i};
            }
            for(size_t i=0;!surface&&i<buttonWindows.size();++i)if(buttonWindows[i]==input){
                surface=&nativeButtonSurfaces[i];slot={ScreenPairInputKind::Button,i};
            }
            if(!surface)for(const auto& item:ymcc::customtouch::items)if(item&&item->window==input){
                fallbackItem=item;surface=&fallbackItem->surface;slot={ScreenPairInputKind::Custom,0,item.get()};break;
            }
        }
        if(surface){
            if(!current())return false;
            const bool inputSafe=surface->ensureMousePass();
            if(!current())return false;
            if(!inputSafe&&FAILED(surface->error)){
                const bool custom=slot.kind==ScreenPairInputKind::Custom;
                const int index=custom?slot.item->kind:static_cast<int>(slot.index);
                if(!windowError)windowError=(DWORD)surface->error;
                recordNativeInputFailure(slot.kind!=ScreenPairInputKind::Pad,index,*surface,custom);
            }
            safe=inputSafe&&safe;
        }else safe=false;
    }
#endif
    if(!current())return false;
    if(!safe){
        if(input)ShowWindow(input,SW_HIDE);
        if(!current())return false;
        if(outline)ShowWindow(outline,SW_HIDE);
        if(current())release();
    }
    return safe;
}
inline void restoreScreenLayerPair(HWND input,HWND outline,HWND foreground,bool requestRaise=false,ScreenPairInputSlot slot={}){
    const ScreenPairVisibilityEpoch binding;
    const auto current=[&]{return binding.current(slot.kind)&&sharedInputAllowed();};
    if(!current()||!input||!IsWindowVisible(input)||!screenPairMouseSafe(input,outline,slot,binding)||!current())return;
    // Reuse the foreground geometry only for these two read-only checks. A
    // later pair reads afresh after any style repair or SetWindowPos reentry.
    ScreenForegroundGeometry geometry{foreground};
    if(requestRaise){if(!geometry.overlaps(input)&&!geometry.overlaps(outline))return;}
    else if(!geometry.occludes(input)&&!geometry.occludes(outline))return;
    for(HWND layer:{input,outline})if(current()&&layer&&IsWindowVisible(layer)){
        if(SetWindowPos(layer,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)){
            ++ownerVisibilityRaises;
            ++(requestRaise?ownerVisibilityWork.forcedPairLayerRaises:ownerVisibilityWork.occlusionPairLayerRaises);
        }
        else if(binding.current(slot.kind))windowError=GetLastError();
    }
}
inline void restoreScreenFeedbackOrder(){
    const auto epoch=ownerVisibilityEpoch;
    const auto current=[&]{return ownerVisibilityEpochCurrent(epoch)&&sharedInputAllowed();};
    // Raising an input window also raises its owned visual windows; raising
    // its outline afterwards can then put that outline above the contact.
    // Finish the same external-visibility recovery as one ordered stack.
    // These are display-only, already-visible windows: never reshow a hidden
    // contact/cursor, rebuild a bitmap, move input or touch the active contact.
    for(auto& visual:contactVisuals){
        if(!current())return;
        if(!visual.window||!visual.visible||!IsWindowVisible(visual.window))continue;
        const bool safe=ymcc::screeninput::ensureDisplay(visual.window);
        const DWORD error=safe?ERROR_SUCCESS:GetLastError();
        if(!ownerVisibilityEpochCurrent(epoch))return;
        if(!safe){windowError=error?error:ERROR_ACCESS_DENIED;hideContactVisual(visual);continue;}
        if(current()&&visual.visible&&IsWindowVisible(visual.window)){
            if(SetWindowPos(visual.window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE))++ownerVisibilityRaises;
            else windowError=GetLastError();
        }
    }
    if(current()&&cursorWindow&&cursorVisible&&IsWindowVisible(cursorWindow)){
        const bool safe=ymcc::screeninput::ensureDisplay(cursorWindow);
        const DWORD error=safe?ERROR_SUCCESS:GetLastError();
        if(!ownerVisibilityEpochCurrent(epoch))return;
        if(!safe){cursorError=error?error:ERROR_ACCESS_DENIED;cursorHideVisual();return;}
        if(current()&&cursorVisible&&IsWindowVisible(cursorWindow)){
            if(SetWindowPos(cursorWindow,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)){
                ++ownerVisibilityRaises;++cursorStackRaises;
            }else{cursorError=GetLastError();++cursorStackRaiseFailures;}
        }
    }
}
inline bool restoreOwnerArtwork(UINT_PTR epoch,HWND foreground){
    const ScreenPairVisibilityEpoch binding;
    const auto revealSerial=autoHide.revealSerial;
    const auto current=[&]{return epoch==binding.visibility&&binding.current(ScreenPairInputKind::Unknown)&&
        sharedInputAllowed()&&!displayTransitionFence.idleSuspended&&
        !presentationSuspended.load(std::memory_order_acquire)&&autoHide.revealSerial==revealSerial;};
    if(!current())return false;
    const auto cfg=getConfig();
    bool reshown=false;
    const auto submit=[&](HWND input,HWND visual,ScreenPairInputSlot slot,const auto& wanted,const auto& draw){
        if(!current())return false;
        if(!input||!visual||!IsWindowVisible(input))return true;
        const bool hidden=!IsWindowVisible(visual);
        if(hidden&&!wanted())return true;
        if(!ScreenForegroundGeometry{foreground}.overlapsVisible(hidden?input:visual))return true;
        // An isolated failure already hid its own pair. A callback replacing
        // the set, or changing idle visibility, invalidates the remaining work.
        if(!screenPairMouseSafe(input,visual,slot,binding))return current();
        if(!current())return false;
        if(!draw())return current();
        ++ownerVisibilityArtworkSubmissions;
        if(hidden&&current()&&IsWindowVisible(input)&&!IsWindowVisible(visual)){
            ShowWindow(visual,SW_SHOWNOACTIVATE);
            if(current()&&IsWindowVisible(visual)){
                reshown=true;
                // This successful submission can settle an earlier idle-reveal
                // failure for the same layer and binding, but not input errors.
                const auto i=slot.kind==ScreenPairInputKind::Pad?slot.index:
                    slot.kind==ScreenPairInputKind::Button&&slot.index<3?slot.index+2:autoHide.revealErrors.size();
                if(i<autoHide.revealErrors.size()&&autoHide.revealPadEpoch==binding.pad&&autoHide.revealButtonEpoch==binding.button){
                    autoHide.revealErrors[i]=0;autoHide.retryRevealMask&=~autoHideOutlineBits[i];
                }
            }
        }
        return current();
    };
    // One cached frame per eligible layer. A missing frame may be restored
    // only for an already-live input; this does not rebuild input sources.
    for(size_t i=0;i<windows.size();++i){
        if(!submit(windows[i],outlines[i],{ScreenPairInputKind::Pad,i},
            [&]{return !autoHide.hidden&&cfg.masterEnabled&&cfg.enabled&&(cfg.layout!=Layout::Single||i==0)&&permitted(surfaceMode(cfg,static_cast<int>(i)));},
            [&]{return renderOutline(i,cfg.transparency,true);}))return reshown;
    }
    for(size_t i=0;i<buttonWindows.size();++i){
        if(!submit(buttonWindows[i],buttonOutlines[i],{ScreenPairInputKind::Button,i},
            [&]{return (!autoHide.hidden||i>2)&&buttonPermitted(cfg,static_cast<int>(i));},
            [&]{return renderButton(i,cfg.transparency,true);}))return reshown;
    }
    const auto extensions=ymcc::customtouch::items;
    for(size_t i=0;i<extensions.size();++i){
        const auto& item=extensions[i];
        if(!submit(item->window,item->outline,{ScreenPairInputKind::Custom,i,item.get()},
            [&]{return !autoHide.hidden&&ymcc::customtouch::itemWanted(*item);},
            [&]{return ymcc::customtouch::draw(*item);}))return reshown;
    }
    return reshown;
}
inline bool consumeOwnerVisibility(UINT_PTR epoch){
    if(epoch!=ownerVisibilityEpoch||!ownerVisibilityQueued||!initialized||!onOwnerThread(owner.load()))return false;
    const bool requestRaise=ownerVisibilityRaiseRequested,requestArtwork=ownerVisibilityArtworkRequested;
    const unsigned forceSources=ownerVisibilityQueuedForceSources;
    ownerVisibilityQueued=false;ownerVisibilityRaiseRequested=false;ownerVisibilityArtworkRequested=false;ownerVisibilityQueuedForceSources=0;
    if(!ownerVisibilityEpochCurrent(epoch)||!sharedInputAllowed()){stopOwnerVisibilityRechecks();return false;}
    const HWND foreground=GetForegroundWindow(),root=foreground?GetAncestor(foreground,GA_ROOT):nullptr;
    DWORD pid=0;if(!root||!GetWindowThreadProcessId(root,&pid)||pid==GetCurrentProcessId())return false;
    auto& work=ownerVisibilityWork;
    if(requestRaise){
        ++work.forcedPasses;
        if(work.lastEpoch==epoch&&work.lastRoot==root&&work.lastPid==pid)++work.sameRootForcedPasses;
        if(forceSources&static_cast<unsigned>(OwnerVisibilityForceSource::WindowTransition))++work.windowTransitionPasses;
        if(forceSources&static_cast<unsigned>(OwnerVisibilityForceSource::OwnerRefresh))++work.ownerRefreshPasses;
    }
    work.lastEpoch=epoch;work.lastRoot=root;work.lastPid=pid;
    const auto current=[&]{return ownerVisibilityEpochCurrent(epoch)&&sharedInputAllowed();};
    ++ownerVisibilityChecks;const auto raisesBefore=ownerVisibilityRaises;
    for(size_t i=0;i<windows.size();++i){if(!current())return false;restoreScreenLayerPair(windows[i],outlines[i],root,requestRaise,{ScreenPairInputKind::Pad,i});}
    for(size_t i=0;i<buttonWindows.size();++i){if(!current())return false;restoreScreenLayerPair(buttonWindows[i],buttonOutlines[i],root,requestRaise,{ScreenPairInputKind::Button,i});}
    const auto extensions=ymcc::customtouch::items;
    for(size_t i=0;i<extensions.size();++i){
        if(!current())return false;
        const auto& item=extensions[i];
        restoreScreenLayerPair(item->window,item->outline,root,requestRaise,{ScreenPairInputKind::Custom,i,item.get()});
    }
    if(!current())return false;
    const bool reshown=requestArtwork&&restoreOwnerArtwork(epoch,root);
    if(!current())return false;
    if(reshown||ownerVisibilityRaises!=raisesBefore)restoreScreenFeedbackOrder();
    return true;
}
inline Json ownerVisibilityState(){
    const HWND foreground=GetForegroundWindow(),root=foreground?GetAncestor(foreground,GA_ROOT):nullptr;
    DWORD pid=0;if(root)GetWindowThreadProcessId(root,&pid);
    const auto window=[&](HWND h){
        Json j={{"hwnd",(uintptr_t)h},{"visible",bool(h&&IsWindowVisible(h))}};if(!h)return j;
        j["topmost"]=(GetWindowLongPtrW(h,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
        RECT r{};if(GetWindowRect(h,&r))j["rect"]={r.left,r.top,r.right,r.bottom};
        j["foregroundOccludes"]=screenLayerOccluded(h,root);return j;
    };
    Json layers=Json::array();
    for(int i=0;i<2;++i)if(windows[i]||outlines[i])layers.push_back({{"kind","pad"},{"index",i},{"input",window(windows[i])},{"artwork",window(outlines[i])}});
    for(int i=0;i<7;++i)if(buttonWindows[i]||buttonOutlines[i])layers.push_back({{"kind","button"},{"index",i},{"input",window(buttonWindows[i])},{"artwork",window(buttonOutlines[i])}});
    for(const auto& item:ymcc::customtouch::items)layers.push_back({{"kind","custom"},{"id",item->id},{"input",window(item->window)},{"artwork",window(item->outline)}});
    const auto& work=ownerVisibilityWork;
    return {{"version",1},{"scope","explicit-export-current-HWND-stack;not-pixel-or-exclusive-fullscreen-proof"},
        {"restackWork",{{"version",1},{"scope","cumulative;request-sources-can-overlap;forced-does-not-prove-redundancy"},
            {"forcedPasses",work.forcedPasses},{"forcedSameRootAsPreviousPass",work.sameRootForcedPasses},
            {"windowTransitionForcedPasses",work.windowTransitionPasses},{"ownerRefreshForcedPasses",work.ownerRefreshPasses},
            {"forcedPairLayerRaises",work.forcedPairLayerRaises},{"occlusionPairLayerRaises",work.occlusionPairLayerRaises},
            {"feedbackLayerRaises",ownerVisibilityRaises-work.forcedPairLayerRaises-work.occlusionPairLayerRaises}}},
        {"watching",ownerVisibilityActive()},{"events",ownerVisibilityEvents},{"checks",ownerVisibilityChecks},{"raises",ownerVisibilityRaises},
        {"delayedChecks",ownerVisibilityDelayedChecks},{"artworkSubmissions",ownerVisibilityArtworkSubmissions},{"queued",ownerVisibilityQueued},{"raiseRequested",ownerVisibilityRaiseRequested},
        {"recheckTimerArmed",ownerVisibilityRecheck.timer!=0},{"foregroundPid",pid},{"foreground",window(root)},{"layers",std::move(layers)}};
}
} // namespace ymcc::screenpads
