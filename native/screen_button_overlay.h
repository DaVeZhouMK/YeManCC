#pragma once
// Included inside ymcc::screenpads after the shared transparent-window helpers.
// Same UI owner as the pads: native personas use BUS; disabled uses direct actions.
// No hook, periodic timer or dedicated worker.
inline std::array<HWND,7> buttonWindows{},buttonOutlines{};
inline UINT64 buttonRefreshEpoch=0; // Invalidates a refresh superseded during dispatcher work.
struct ButtonInputRecovery {unsigned long long aborts=0,retries=0,recovered=0,failed=0;DWORD lastError=0;int lastIndex=-1;};
inline ButtonInputRecovery buttonInputRecovery;
inline std::array<UINT32,7> buttonContacts{};
// Menu keys are momentary actions. Bound their SD output independently of
// Steam/CDP and the HWND owner, so a lost UP cannot pin a native menu signal.
inline constexpr ULONGLONG kMenuSignalMaxMs=3000;
inline std::array<std::atomic<ULONGLONG>,2> menuSignalExpires{};
inline std::array<bool,7> buttonArmed{};
inline std::array<UINT32,7> buttonFeedbackContacts{};
inline std::array<bool,7> buttonFeedbackPressed{};
inline bool renderButton(int index,int transparency,bool force=false);
inline unsigned buttonHapticContacts=0; // Owner-thread only, maintained on contact edges.
inline unsigned buttonFeedbackMask() {return buttonHapticContacts;}
inline unsigned buttonEventCount=0,buttonSummonCount=0,buttonSummonPostCount=0;
inline HWND buttonSummonTestOwner=nullptr; // Developer pointer test only; never set by the app.
struct ButtonUpload {bool ready=false,whiteIcon=false,hasIcon=false;DWORD transparentBackground=1;unsigned maxAlpha=0;RECT ink{};int width=0,height=0,transparency=-1,profile=-1;bool pressed=false;int padding=0;};
inline unsigned long long buttonUploadCount=0,buttonCacheHits=0,buttonPositionMoves=0;
inline std::array<ButtonUpload,7> buttonUploads{},buttonArtwork{};
inline std::array<OutlineBitmap,7> buttonArtBitmaps{};
inline std::array<std::array<OutlineBitmap,2>,7> buttonFrames{};
inline std::filesystem::path buttonArtTestPrefix; // Only the developer CLI sets this.
inline bool buttonHit(float x,float y,int width,int height) {
    if(x<0 || y<0 || x>=width || y>=height)return false;
    const float radius=(float)std::min(10,std::min(width,height)/2);
    const float cx=std::clamp(x,radius,width-radius),cy=std::clamp(y,radius,height-radius);
    return (x-cx)*(x-cx)+(y-cy)*(y-cy)<=radius*radius;
}
inline std::array<RECT,7> buttonRects(const RECT& monitor,int scale,const RECT* workArea=nullptr,SummonPosition summonPosition=SummonPosition::Right,unsigned rearMask=15u) {
    const auto pads=panelRects(monitor,scale,workArea);const auto rows=panelRows(monitor,scale,workArea);
    const LONG gap=rows.gapX,verticalGap=rows.gapY;
    const LONG height=MulDiv(kControlHeight,std::clamp(scale,50,200),100);
    const LONG rearHeight=rows.keyHeight,specialHeight=rows.keyHeight;
    std::array<RECT,7> r{};
    const LONG top=monitor.top+kPanelInsetY;
    if(summonPosition==SummonPosition::Left) r[0]={monitor.left+kPanelInsetX,top,monitor.left+kPanelInsetX+height,top+height};
    else r[0]={monitor.right-kPanelInsetX-height,top,monitor.right-kPanelInsetX,top+height};
    for(int side=0;side<2;side++) {
        const auto pad=pads[side];const LONG rearBottom=pad.top-verticalGap,specialTop=pad.bottom+verticalGap;
        const LONG mid=(pad.left+pad.right)/2;
        r[3+side*2]={pad.left,rearBottom-rearHeight,mid-gap/2,rearBottom};
        r[4+side*2]={mid+(gap+1)/2,rearBottom-rearHeight,pad.right,rearBottom};
        // One special key per side: exactly align to the inward rear-key cell.
        // Left uses the right half, right uses the left half; the outer half is empty.
        const auto inner=r[side==0?4:5];
        r[1+side]={inner.left,specialTop,inner.right,specialTop+specialHeight};
        // A lone key always occupies the inward cell, regardless of its name.
        // Both enabled keys retain their original left/right cells and IDs.
        const unsigned selected=(rearMask>>(side*2))&3u;
        if(selected==1u)r[3+side*2]=inner;
        else if(selected==2u)r[4+side*2]=inner;
    }
    return r;
}
inline bool buttonPermitted(const Config& cfg,int index) {
    if(!cfg.masterEnabled)return false;
    if(index==0)return effectiveSummonPosition(cfg)!=SummonPosition::Off;
    const int profile=activeProfile.load();
    if(profile!=appliedProfile.load())return false;
    // Disabled output has its own explicit, mutually exclusive dedicated pair.
    // "All" is Steam+QAM or PS+Mute; the separate rear-key settings are unchanged.
    if(profile==0)return (index==1 || index==2) && standaloneSpecialProfile(cfg)!=0;
    if(!controlTargetEnabled.load())return false;
    if(index<3) {const unsigned mask=effectiveSpecialMask(cfg);return (mask&(1u<<(index-1)))!=0 && (profile!=3 || index==1);}
    const unsigned mask=effectiveRearMask(cfg);const unsigned bit=1u<<(index-3);return (mask&bit)!=0 && (profile==1 || (profile==2 && controlEdgeEnabled.load()));
}
inline void buttonsReleaseMapped() {
    if(quickAccessEdgeObserver)quickAccessEdgeObserver(-1,false);
    standaloneCancel();screenButtonMask.store(0,std::memory_order_release);
    buttonContacts={};buttonArmed={};
}
inline bool buttonsHaveMappedWork() {
    // Own-UI MOVE can arrive at touch sampling rate. Once the game mapping is
    // revoked, do not retake standaloneMutex or advance its generation again.
    // Keep pending/status and every contact (including summon) in the guard.
    if(screenButtonMask.load(std::memory_order_relaxed) || standalonePending.load(std::memory_order_acquire) ||
       standaloneStatus.load(std::memory_order_acquire)!=StandaloneStatus::Idle)return true;
    for(size_t i=0;i<buttonContacts.size();++i)if(buttonContacts[i] || buttonArmed[i])return true;
    return false;
}
inline void buttonsRelease() {
    buttonsReleaseMapped();const auto pressed=buttonFeedbackPressed;
    buttonFeedbackContacts={};buttonFeedbackPressed={};buttonHapticContacts=0;outputHaptic({});
    const int transparency=getConfig().transparency;
    for(int i=0;i<7;++i)if(pressed[i] && buttonOutlines[i])renderButton(i,transparency);
    autoHideSync();
}
inline void buttonFeedbackAtHit(const Config& cfg,int index,UINT32 id,int phase,bool inside) {
    const bool wasPressed=buttonFeedbackPressed[index];bool pulse=false,changed=false;
    if(phase==0){
        if(!id || buttonFeedbackContacts[index] || !inside)return;
        buttonFeedbackContacts[index]=id;buttonFeedbackPressed[index]=true;buttonHapticContacts|=1u<<(index+2);pulse=true;changed=true;
    }else{
        if(buttonFeedbackContacts[index]!=id || !id)return;
        if(phase==1)buttonFeedbackPressed[index]=inside;
        else {buttonFeedbackContacts[index]=0;buttonFeedbackPressed[index]=false;buttonHapticContacts&=~(1u<<(index+2));changed=true;}
    }
    if(changed){Action feedback;feedback.hapticPulses=pulse?1u:0u;outputHaptic(feedback);}
    if(wasPressed!=buttonFeedbackPressed[index] && buttonOutlines[index])renderButton(index,cfg.transparency);
}
inline void buttonFeedbackEvent(int index,UINT32 id,int phase,float x,float y,int width,int height) {
    buttonFeedbackAtHit(getConfig(),index,id,phase,buttonHit(x,y,width,height));
}
inline unsigned buttonSnapshot() {
    unsigned bits=screenButtonMask.load(std::memory_order_relaxed);if(!bits)return 0;
    if(!controlTargetEnabled.load(std::memory_order_acquire) || activeProfile.load()==0 || activeProfile.load()!=appliedProfile.load())return 0;
    if(activeProfile.load()==1&&(bits&3)) {
        // Only while a menu key is held: one shared tick read in the existing
        // sender, no watchdog thread, polling job, device query or rate change.
        const auto now=GetTickCount64();
        for(unsigned i=0;i<2;++i)if(bits&(1u<<i)) {
            const auto expiry=menuSignalExpires[i].load(std::memory_order_acquire);
            if(!expiry||now>=expiry)bits&=~(1u<<i);
        }
    }
    return bits;
}
inline void handleButtonCaptureLoss(int index,UINT32 id,bool all=false) {
    if(index<0||index>=7)return;
    AutoHideTouchScope activity{false};
    if(index==1 || index==2) {
        auto& contact=autoHide.specialContacts[index-1];
        if(contact && (all || contact==id)){contact=0;activity.enabled=true;}
        activity.enabled=activity.enabled || (buttonContacts[index]!=0 && (all || buttonContacts[index]==id));
    }
    if(buttonFeedbackContacts[index] && (all || buttonFeedbackContacts[index]==id))
        buttonFeedbackEvent(index,buttonFeedbackContacts[index],2,0,0,0,0);
    if((all||buttonContacts[index]==id)&&buttonContacts[index]&&quickAccessEdgeObserver)quickAccessEdgeObserver(-1,false);
    if(all||buttonContacts[index]==id){buttonContacts[index]=0;buttonArmed[index]=false;if(index)screenButtonMask.fetch_and(~(1u<<(index-1)),std::memory_order_release);}
}
// Only a new DOWN may reclaim an expired momentary menu contact. A late UP
// from its old pointer remains unable to clear the new gesture.
inline void retireExpiredMenuContact(int index) {
    if(index<1||index>2||activeProfile.load()!=1||!buttonContacts[index])return;
    const auto expiry=menuSignalExpires[index-1].load(std::memory_order_acquire);
    if(!expiry||GetTickCount64()>=expiry)handleButtonCaptureLoss(index,buttonContacts[index]);
}
inline bool buttonEventAtHit(const Config& cfg,int index,UINT32 id,int phase,bool inside,bool canceled=false) {
    if(index<0 || index>=7)return false;
    if(!buttonPermitted(cfg,index)) {
        // A matching UP/CANCEL still owns its release after the gate changed.
        if(phase!=0&&buttonContacts[index]==id) {
            buttonContacts[index]=0;buttonArmed[index]=false;
            if(index)screenButtonMask.fetch_and(~(1u<<(index-1)),std::memory_order_release);
            if(quickAccessEdgeObserver)quickAccessEdgeObserver(-1,false);
        }
        return false;
    }
    const unsigned bit=index?1u<<(index-1):0;
    if(phase==0) {
        if(!inside)return false;
        retireExpiredMenuContact(index);
        if(buttonContacts[index])return false;
        buttonContacts[index]=id;buttonArmed[index]=true;
        if(bit && activeProfile.load()!=0) {
            if(index<=2&&activeProfile.load()==1)menuSignalExpires[index-1].store(GetTickCount64()+kMenuSignalMaxMs,std::memory_order_release);
            screenButtonMask.fetch_or(bit,std::memory_order_release);
        }
        if(quickAccessEdgeObserver) {
            if(index==2&&activeProfile.load()==1)quickAccessEdgeObserver(0,true);
            else quickAccessEdgeObserver(-1,false);
        }
    } else {
        if(buttonContacts[index]!=id)return false;
        if(phase==1) {buttonArmed[index]=inside;return false;}
        const bool clicked=buttonArmed[index] && !canceled && inside;
        const bool summon=index==0 && clicked;
        buttonContacts[index]=0;buttonArmed[index]=false;
        if(bit)screenButtonMask.fetch_and(~bit,std::memory_order_release);
        if(index==2 && !clicked && quickAccessEdgeObserver)quickAccessEdgeObserver(-1,false);
        if(index && clicked && activeProfile.load()==0)standaloneDispatch(cfg,index);
        return summon;
    }
    return false;
}
inline bool buttonEvent(int index,UINT32 id,int phase,float x,float y,int width,int height,bool canceled=false) {
    return buttonEventAtHit(getConfig(),index,id,phase,buttonHit(x,y,width,height),canceled);
}
inline LRESULT handleButtonPointer(HWND h,int index,UINT message,UINT32 id,float x,float y,bool canceled=false) {
    if(index<0||index>=7||!std::isfinite(x)||!std::isfinite(y))return 0;
    RECT rect{};if(!GetClientRect(h,&rect))return 0;
    // One event-local snapshot and rounded hit result serve wake, feedback and
    // mapping. The mapping helper still rechecks the live target/profile gate.
    const Config cfg=getConfig();const bool inside=buttonHit(x,y,rect.right,rect.bottom);
    const bool allowed=buttonPermitted(cfg,index);
    // Reclaim before wake/feedback so the fresh touch owns those states too.
    if(message==WM_POINTERDOWN&&!canceled&&allowed&&inside)retireExpiredMenuContact(index);
    AutoHideTouchScope activity{false};
    if((index==1 || index==2) && allowed) {
        auto& contact=autoHide.specialContacts[index-1];
        if(message==WM_POINTERDOWN && !canceled && !contact && inside) {
            contact=id;activity.enabled=true;
        } else if(contact==id) {
            activity.enabled=true;if(canceled || message==WM_POINTERUP)contact=0;
        }
    }
    const int phase=canceled?2:message==WM_POINTERDOWN?0:message==WM_POINTERUP?2:1;
    if(allowed)buttonFeedbackAtHit(cfg,index,id,phase,inside);
    // Keep physical feedback in our own UI while suppressing mapped game input.
    if(index&&activeProfile.load()!=0&&!dryRun&&owner.load()==GetForegroundWindow()) {
        if(buttonsHaveMappedWork())buttonsReleaseMapped();
        return 0;
    }
    const bool summon=buttonEventAtHit(cfg,index,id,phase,inside,canceled);
    ++buttonEventCount;
    if(testTrace)testTrace->push_back({{"button",index},{"message",message},{"id",id},{"x",x},{"y",y},{"mask",buttonSnapshot()},{"summon",summon},{"canceled",canceled}});
    if(summon){++buttonSummonCount;if(HWND parent=dryRun?buttonSummonTestOwner:owner.load()) {
        if(PostMessageW(parent,kSummonMessage,0,0))++buttonSummonPostCount;else windowError=GetLastError();}}
    return 0;
}
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
inline std::array<ymcc::screeninput::Surface,7> nativeButtonSurfaces;
inline void nativeButtonSink(HWND root,const ymcc::screeninput::Event& event,void*) {
    if(inputTeardown)return;
    if(event.phase==ymcc::screeninput::Phase::Cancel){handleButtonCaptureLoss(event.surface,event.id);return;}
    const UINT message=event.phase==ymcc::screeninput::Phase::Down?WM_POINTERDOWN:event.phase==ymcc::screeninput::Phase::Up?WM_POINTERUP:WM_POINTERUPDATE;
    handleButtonPointer(root,event.surface,message,event.id,event.x,event.y,event.cancelled);
}
#endif
inline void buttonsNativeInputCancel() {
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    for(auto& surface:nativeButtonSurfaces)surface.cancel();
#endif
}
inline void buttonsNativeInputDisable() {
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    for(auto& surface:nativeButtonSurfaces)surface.close();
#endif
}
inline bool buttonsNativeInputReady() {
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    for(int index=0;index<7;++index)if(buttonWindows[index]&&IsWindowVisible(buttonWindows[index])&&!nativeButtonSurfaces[index].active())return false;
#endif
    return true;
}
inline bool buttonNativeInputSync(int index,bool wanted,bool force=false) {
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    if(index<0||index>=7)return false;auto& surface=nativeButtonSurfaces[index];
    if(!nativeInputCandidateRequested){surface.close();return true;}
    if(!wanted){surface.cancel();return true;}
    const HWND root=buttonWindows[index];const UINT64 epoch=buttonRefreshEpoch;
    const int profile=appliedProfile.load();
    const auto stillWanted=[&]{return initialized && !inputTeardown && nativeInputCandidateRequested &&
        buttonRefreshEpoch==epoch && buttonWindows[index]==root && root && IsWindow(root) &&
        appliedProfile.load()==profile && activeProfile.load()==profile && buttonPermitted(getConfig(),index);};
    if(!stillWanted())return false;
    // ProcessEvents may abort a normal rebind. Recreate only this owned source,
    // once, while the same live selection still authorizes it. No timer/polling.
    for(int attempt=0;attempt<2;++attempt) {
        const bool ok=surface.root()==root?surface.sync(force):surface.attach(nativeInputRuntime,root,index,nativeButtonSink,nullptr);
        // Never close a newer source installed by a nested refresh, or undo exit.
        if(!stillWanted())return false;
        if(ok){if(attempt)++buttonInputRecovery.recovered;return true;}
        const auto failure=surface.error;
        buttonInputRecovery.lastError=(DWORD)failure;buttonInputRecovery.lastIndex=index;
        if(failure==E_ABORT)++buttonInputRecovery.aborts;
        surface.close();if(!stillWanted())return false;
        if(failure==E_ABORT && attempt==0){++buttonInputRecovery.retries;continue;}
        ++buttonInputRecovery.failed;windowError=(DWORD)failure;return false;
    }
    return false;
#else
    return true;
#endif
}
inline LRESULT CALLBACK buttonProc(HWND h,UINT m,WPARAM w,LPARAM lp) {
    if(m==WM_NCCREATE){SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);return TRUE;}
    if(inputTeardown&&(m==WM_POINTERDOWN||m==WM_POINTERUPDATE||m==WM_POINTERUP||m==WM_POINTERCAPTURECHANGED||
       m==WM_CANCELMODE||m==WM_STYLECHANGED||m==WM_ENABLE))return 0;
    const int index=(int)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(m==WM_STYLECHANGED||m==WM_ENABLE){
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
        if(index>=0&&index<7&&nativeButtonSurfaces[index].root()==h&&nativeButtonSurfaces[index].policyEnabled()&&
            !nativeButtonSurfaces[index].ensureMousePass()){
            windowError=(DWORD)nativeButtonSurfaces[index].error;ShowWindow(h,SW_HIDE);
            if(buttonOutlines[index])ShowWindow(buttonOutlines[index],SW_HIDE);buttonsRelease();
        }
#endif
        return 0;}
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(m==WM_POINTERACTIVATE)return PA_NOACTIVATE;
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT){PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    if(m==WM_NCHITTEST){POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&p);RECT r{};GetClientRect(h,&r);
        return buttonHit((float)p.x,(float)p.y,r.right,r.bottom)?HTCLIENT:HTTRANSPARENT;}
    if(m==WM_POINTERDOWN||m==WM_POINTERUPDATE||m==WM_POINTERUP){
        POINTER_INFO info{};const UINT32 id=GET_POINTERID_WPARAM(w);
        if(!GetPointerInfo(id,&info)||(info.pointerType!=PT_TOUCH&&info.pointerType!=PT_PEN))return 0;
        POINT point=info.ptPixelLocation;ScreenToClient(h,&point);
        return handleButtonPointer(h,index,m,id,(float)point.x,(float)point.y,(info.pointerFlags&POINTER_FLAG_CANCELED)!=0);
    }
    if(m==WM_POINTERCAPTURECHANGED||m==WM_CANCELMODE){handleButtonCaptureLoss(index,GET_POINTERID_WPARAM(w),m==WM_CANCELMODE);return 0;}
    return DefWindowProcW(h,m,w,lp);
}
inline const unsigned char* buttonGlyph(int profile,int index) {
    if(index==0)return glyphs::ymcc;
    if(index==1)return profile==1?glyphs::steam:profile==2?glyphs::ps:glyphs::xbox;
    if(index==2)return profile==1?glyphs::dots:glyphs::mute;
    if(profile==2) {switch(index){case 3:return glyphs::lfn;case 4:return glyphs::lb;case 5:return glyphs::rfn;case 6:return glyphs::rb;}}
    return nullptr;
}
inline bool renderButton(int index,int transparency,bool force) {
    const HWND h=buttonOutlines[index],input=buttonWindows[index];if(!h || !input)return false;
    // Owned popup windows do not inherit the owner's screen position/size.
    // The input HWND is authoritative, exactly as in renderOutline for pads.
    RECT rc{};if(!GetWindowRect(input,&rc)){windowError=GetLastError();return false;}
    const int width=rc.right-rc.left,height=rc.bottom-rc.top;if(width<1 || height<1)return false;
    const int applied=appliedProfile.load();const Config cfg=getConfig();
    const int profile=applied==0?standaloneSpecialProfile(cfg):applied;
    const bool pressed=buttonFeedbackPressed[index];transparency=outlineFeedbackTransparency(transparency,pressed);
    const int padding=pressGlowPadding(cfg.scale),frameWidth=width+padding*2,frameHeight=height+padding*2;
    const auto& cached=buttonUploads[index];
    if(!force && !(dryRun && !buttonArtTestPrefix.empty()) && cached.ready && cached.width==width && cached.height==height &&
       cached.transparency==transparency && cached.profile==profile && cached.pressed==pressed && cached.padding==padding) {
        RECT visual{};if(!GetWindowRect(h,&visual)){windowError=GetLastError();return false;}
        if(visual.left!=rc.left-padding || visual.top!=rc.top-padding || visual.right-visual.left!=frameWidth || visual.bottom-visual.top!=frameHeight) {
            if(!SetWindowPos(h,nullptr,rc.left-padding,rc.top-padding,frameWidth,frameHeight,SWP_NOACTIVATE|SWP_NOZORDER)){windowError=GetLastError();return false;}
            ++buttonPositionMoves;
        }
        ++buttonCacheHits;return true;
    }
    auto& art=buttonArtBitmaps[index];
    if(!art.dc || art.width!=width || art.height!=height || buttonArtwork[index].profile!=profile) {
    for(auto& frame:buttonFrames[index])frame.reset();
    if(!allocatePressBitmap(art,width,height))return false;
    HDC memory=art.dc;auto* pixels=art.pixels;constexpr int opacity=255;
    // Only artwork scales down: every HWND/hit target and anchor stays unchanged.
    // Common Y/Steam are font-rendered; QAM is an analytic vector-circle trio.
    const bool textIcon=index==0 || (profile==1 && (index==1 || index>=3));
    const bool circleIcon=profile==1 && index==2;
    const auto* glyph=textIcon || circleIcon?nullptr:buttonGlyph(profile,index);
    if(glyph) {
        int x0=64,y0=64,x1=0,y1=0;
        for(int y=0;y<64;y++)for(int x=0;x<64;x++)if(glyph[y*64+x]){x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x+1);y1=std::max(y1,y+1);}
        const double iconScale=profile==2?.50:1.0;
        const double factor=std::min(width*.72*iconScale/std::max(1,x1-x0),height*.66*iconScale/std::max(1,y1-y0));
        const int gw=std::max(1,(int)std::lround((x1-x0)*factor)),gh=std::max(1,(int)std::lround((y1-y0)*factor));
        const int dx=(width-gw)/2,dy=(height-gh)/2;
        for(int y=0;y<gh;y++)for(int x=0;x<gw;x++) {
            const double sx=std::clamp(x0+(x+.5)*(x1-x0)/gw-.5,0.0,63.0),sy=std::clamp(y0+(y+.5)*(y1-y0)/gh-.5,0.0,63.0);
            const int ax=(int)sx,ay=(int)sy,bx=std::min(63,ax+1),by=std::min(63,ay+1);const double fx=sx-ax,fy=sy-ay;
            const double value=(glyph[ay*64+ax]*(1-fx)+glyph[ay*64+bx]*fx)*(1-fy)+(glyph[by*64+ax]*(1-fx)+glyph[by*64+bx]*fx)*fy;
            const DWORD a=(DWORD)std::lround(value*opacity/255);
            pixels[(dy+y)*width+dx+x]=(a<<24)|(a<<16)|(a<<8)|a;
        }
    } else if(circleIcon) {
        const double diameter=height*.66*.30,radius=diameter/2,step=diameter*1.65;
        for(int dot=-1;dot<=1;dot++) {
            const double cx=width/2.0+dot*step,cy=height/2.0;
            for(int y=std::max(0,(int)std::floor(cy-radius-1));y<std::min(height,(int)std::ceil(cy+radius+1));y++)
                for(int x=std::max(0,(int)std::floor(cx-radius-1));x<std::min(width,(int)std::ceil(cx+radius+1));x++) {
                    const double coverage=std::clamp(radius+.5-std::hypot(x+.5-cx,y+.5-cy),0.0,1.0);
                    const DWORD a=(DWORD)std::lround(opacity*coverage);pixels[y*width+x]=(a<<24)|(a<<16)|(a<<8)|a;
                }
        }
    } else {
        static constexpr const wchar_t* names[]={L"Y",L"STEAM",L"",L"L4",L"L5",L"R4",L"R5"};
        // GDI cap-height is about 72% of its em. Reference glyphs used 66% of
        // button height; match 70% Y / 30% Steam / 80% existing back labels.
        const double fontHeight=index==0?height*.66*.70/.72:index==1?height*.66*.30/.72:height*.52*.80;
        HFONT font=CreateFontW(std::max(1,(int)std::lround(fontHeight)),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        const auto previousFont=SelectObject(memory,font);SetTextColor(memory,RGB(255,255,255));SetBkMode(memory,TRANSPARENT);
        RECT text{0,0,width,height};DrawTextW(memory,names[index],-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        SelectObject(memory,previousFont);DeleteObject(font);GdiFlush();
        int left=width,top=height,right=0,bottom=0;
        for(int y=0;y<height;y++)for(int x=0;x<width;x++)if(pixels[y*width+x]&0xFFFFFF){left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}
        // Centre the actual glyph ink, not the font's ascent/descent box.
        if(right>left && bottom>top) {
            const std::vector<DWORD> ink(pixels,pixels+(size_t)width*height);
            std::fill(pixels,pixels+(size_t)width*height,0u);
            const int dx=(width-(right-left))/2-left,dy=(height-(bottom-top))/2-top;
            for(int y=top;y<bottom;y++)for(int x=left;x<right;x++) {
                const DWORD a=((ink[y*width+x]&255)*opacity+127)/255;const int tx=x+dx,ty=y+dy;
                if(tx>=0 && tx<width && ty>=0 && ty<height)pixels[ty*width+tx]=(a<<24)|(a<<16)|(a<<8)|a;
            }
        }
    }
    ButtonUpload upload;upload.transparency=0;upload.profile=profile;upload.whiteIcon=true;upload.transparentBackground=pixels[std::min(height-1,3)*width+width/2];
    upload.width=width;upload.height=height;upload.ink={width,height,0,0};
    for(int y=0;y<height;y++)for(int x=0;x<width;x++)if(const DWORD pixel=pixels[y*width+x]) {
        const unsigned a=pixel>>24;upload.hasIcon=true;upload.maxAlpha=std::max(upload.maxAlpha,a);
        upload.whiteIcon=upload.whiteIcon && ((pixel&255)==a) && (((pixel>>8)&255)==a) && (((pixel>>16)&255)==a);
        upload.ink.left=std::min(upload.ink.left,(LONG)x);upload.ink.top=std::min(upload.ink.top,(LONG)y);
        upload.ink.right=std::max(upload.ink.right,(LONG)x+1);upload.ink.bottom=std::max(upload.ink.bottom,(LONG)y+1);
    }
    for(int y=0;y<height;y++)for(int x=0;x<width;x++) {
        const DWORD stroke=outlineBorderPixel(x,y,width,height,0);if(stroke)pixels[y*width+x]=stroke;
    }
    buttonArtwork[index]=upload;
    }
    if(!buildPressFrame(art,buttonFrames[index][0],padding,false) || !buildPressFrame(art,buttonFrames[index][1],padding,true))return false;
    auto& frame=buttonFrames[index][pressed?1:0];
    const BYTE alpha=outlineAlpha(transparency);
    if(dryRun && !buttonArtTestPrefix.empty() && buttonPermitted(getConfig(),index)) {
        const auto file=buttonArtTestPrefix.wstring()+L"-"+std::to_wstring(index)+L".bgra";
        // Keep the developer artifact's original input-size format and effective opacity.
        std::vector<DWORD> pixels((size_t)width*height);
        for(size_t p=0;p<pixels.size();++p)pixels[p]=scalePressPixel(art.pixels[p],alpha);
        std::ofstream out(std::filesystem::path(file),std::ios::binary);out.write((const char*)pixels.data(),(std::streamsize)pixels.size()*4);
    }
    POINT destination{rc.left-padding,rc.top-padding},source{};SIZE size{frameWidth,frameHeight};BLENDFUNCTION blend{AC_SRC_OVER,0,alpha,AC_SRC_ALPHA};
    const bool ok=UpdateLayeredWindow(h,nullptr,&destination,&size,frame.dc,&source,0,&blend,ULW_ALPHA)!=FALSE;
    auto upload=buttonArtwork[index];upload.transparency=transparency;upload.pressed=pressed;upload.padding=padding;
    upload.maxAlpha=(upload.maxAlpha*alpha+127)/255;upload.transparentBackground=scalePressPixel(upload.transparentBackground,alpha);
    upload.hasIcon=upload.hasIcon && alpha!=0;upload.ready=ok;buttonUploads[index]=upload;++buttonUploadCount;
    if(!ok){windowError=GetLastError();if(!windowError)windowError=ERROR_INVALID_DATA;}return ok;
}
inline void repairButtonOutline(HWND h,bool contentLost){
    if(!contentLost)return;for(int i=0;i<7;++i)if(buttonOutlines[i]==h){buttonUploads[i]={};
        if(!renderButton(i,getConfig().transparency,true))ShowWindow(h,SW_HIDE);}
}
inline void buttonsRepaint(int transparency) {
    for(int i=0;i<7;i++)if(buttonOutlines[i] && !renderButton(i,transparency))ShowWindow(buttonOutlines[i],SW_HIDE);
}
inline bool buttonsRefresh(const Config& cfg,const RECT& monitor,const RECT& workArea,bool force) {
    const UINT64 epoch=++buttonRefreshEpoch;const int profile=appliedProfile.load();
    const bool target=controlTargetEnabled.load(),edge=controlEdgeEnabled.load();
    const auto current=[&]{return initialized && !inputTeardown && buttonRefreshEpoch==epoch &&
        appliedProfile.load()==profile && activeProfile.load()==profile &&
        controlTargetEnabled.load()==target && controlEdgeEnabled.load()==edge;};
    const auto rects=buttonRects(monitor,cfg.scale,&workArea,effectiveSummonPosition(cfg),effectiveRearMask(cfg));
    for(int i=0;i<7;i++) {
        if(!current())return false;
        const bool show=buttonPermitted(cfg,i);const auto r=rects[i];const int width=r.right-r.left,height=r.bottom-r.top;
        if(show && !buttonWindows[i]) {
            buttonWindows[i]=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,L"YeManScreenButton",L"YeManCC Screen Button",WS_POPUP,0,0,width,height,nullptr,nullptr,instance,(void*)(INT_PTR)i);
            if(!buttonWindows[i])windowError=GetLastError();
        }
        if(!current())return false;
        if(buttonWindows[i] && !buttonOutlines[i]) {
            buttonOutlines[i]=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,L"YeManScreenTouchpadOutline",L"YeManCC Screen Button Icon",WS_POPUP|WS_DISABLED,0,0,width,height,buttonWindows[i],nullptr,instance,nullptr);
            if(!buttonOutlines[i])windowError=GetLastError();
        }
        if(!current())return false;
        const bool positioned=buttonWindows[i] && SetWindowPos(buttonWindows[i],HWND_TOPMOST,r.left,r.top,width,height,SWP_NOACTIVATE);
        if(!current())return false;
        if(buttonWindows[i] && !positioned)windowError=GetLastError();
        const bool inputReady=positioned&&buttonNativeInputSync(i,show,force);
        if(!current())return false;
        const bool ready=inputReady&&buttonOutlines[i]&&ymcc::screeninput::ensureDisplay(buttonOutlines[i])&&renderButton(i,cfg.transparency,force);
        if(!current())return false;
        if(show && !ready && !windowError)windowError=ERROR_NOT_READY;
        if(buttonWindows[i])ShowWindow(buttonWindows[i],show && ready?SW_SHOWNOACTIVATE:SW_HIDE);
        if(buttonOutlines[i])ShowWindow(buttonOutlines[i],show && ready && (!(i==1 || i==2) || !autoHide.hidden)?SW_SHOWNOACTIVATE:SW_HIDE);
    }
    return current();
}
inline bool buttonsNeedRefresh(const Config& cfg) {
    for(int i=0;i<7;++i)if(buttonPermitted(cfg,i)) {
        if(!buttonWindows[i] || !IsWindowVisible(buttonWindows[i]) || !buttonOutlines[i])return true;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
        if(nativeInputCandidateRequested && !nativeButtonSurfaces[i].active())return true;
#endif
        if((!autoHide.hidden || (i!=1 && i!=2)) && !IsWindowVisible(buttonOutlines[i]))return true;
    }
    return false;
}
inline void buttonsInitialize() {
    WNDCLASSW wc{};wc.lpfnWndProc=buttonProc;wc.hInstance=instance;wc.lpszClassName=L"YeManScreenButton";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)windowError=GetLastError();
}
inline bool buttonsVisible(){for(HWND h:buttonWindows)if(h && IsWindowVisible(h))return true;return false;}
// Read existing HWNDs only when state is requested; no polling or device query.
inline Json buttonVisibilityState(const Config& cfg) {
    unsigned permitted=0,input=0,artwork=0;
    for(int i=0;i<7;++i) {
        if(buttonPermitted(cfg,i))permitted|=1u<<i;
        if(buttonWindows[i] && IsWindowVisible(buttonWindows[i]))input|=1u<<i;
        if(buttonOutlines[i] && IsWindowVisible(buttonOutlines[i]))artwork|=1u<<i;
    }
    return {{"version",2},{"permittedMask",permitted},{"inputVisibleMask",input},{"artworkVisibleMask",artwork},
        {"inputRecovery",{{"aborts",buttonInputRecovery.aborts},{"retries",buttonInputRecovery.retries},
            {"recovered",buttonInputRecovery.recovered},{"failed",buttonInputRecovery.failed},
            {"lastError",buttonInputRecovery.lastError},{"lastIndex",buttonInputRecovery.lastIndex}}}};
}
inline unsigned buttonsOpenInputSources(){
    unsigned count=0;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    for(const auto& surface:nativeButtonSurfaces)if(!surface.terminalReleased())++count;
#endif
    return count;
}
inline void buttonsQuiesce(){
    for(HWND h:buttonWindows)rememberExitInputWindow(h);for(HWND h:buttonOutlines)rememberExitInputWindow(h);
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    for(auto& surface:nativeButtonSurfaces){rememberExitInputWindow(surface.child());if(!surface.quiesce()&&!windowError)windowError=ERROR_ACCESS_DENIED;}
#endif
    for(HWND h:buttonWindows)if(h&&IsWindow(h)){ShowWindow(h,SW_HIDE);EnableWindow(h,FALSE);}
    for(HWND h:buttonOutlines)if(h)ShowWindow(h,SW_HIDE);
}
inline void buttonsShutdown(){buttonsNativeInputDisable();buttonsRelease();for(HWND& h:buttonOutlines){if(h)DestroyWindow(h);h=nullptr;}for(HWND& h:buttonWindows){if(h)DestroyWindow(h);h=nullptr;}buttonUploads={};buttonArtwork={};for(auto& art:buttonArtBitmaps)art.reset();for(auto& pair:buttonFrames)for(auto& frame:pair)frame.reset();}
// Driver-free state/geometry tests, folded into the existing main-EXE selftest.
inline Json buttonCoreCases() {
    Json cases=Json::array();auto check=[&](const char* n,bool ok){cases.push_back({{"name",n},{"ok",ok}});};
    const Config saved=getConfig();const int oldActive=activeProfile.load(),oldApplied=appliedProfile.load();
    const bool oldTarget=controlTargetEnabled.load(),oldEdge=controlEdgeEnabled.load();
    const auto defaults=ProfileBank();bool otherDefaultsOff=true;
    for(const auto& c:defaults.slots)otherDefaultsOff=otherDefaultsOff && effectiveSummonPosition(c)==SummonPosition::Off && effectiveRearMask(c)==0;
    check("summon-and-rear-default-off",otherDefaultsOff);
    check("PS-Steam-Xbox-special-buttons-default-all-on",effectiveSpecialMask(defaults.slots[0])==0 &&
        effectiveSpecialMask(defaults.slots[1])==3 && effectiveSpecialMask(defaults.slots[2])==3 && effectiveSpecialMask(defaults.slots[3])==1);
    const ProfileBank savedOff({{"profiles",{{"steamdeck",{{"specialMask",0}}},{"dualsense-edge",{{"specialEnabled",false}}},{"elite",{{"specialMask",0},{"specialEnabled",false}}}}}});
    check("explicit-saved-special-off-overrides-new-all-on-default",effectiveSpecialMask(savedOff.slots[1])==0 && effectiveSpecialMask(savedOff.slots[2])==0 && effectiveSpecialMask(savedOff.slots[3])==0);
    const ProfileBank savedSingle({{"profiles",{{"steamdeck",{{"specialMask",2}}}}}});
    check("saved-partial-special-selection-preserved",effectiveSpecialMask(savedSingle.slots[1])==2);
    Config cfg=profileDefault(1);cfg.summonPosition=SummonPosition::Right;cfg.specialMask=3;cfg.rearMask=15;cfg.summonEnabled=cfg.specialEnabled=cfg.rearEnabled=true;
    const ProfileBank stored({{"profiles",{{"steamdeck",configJson(cfg)}}}});
    check("button-selector-masks-persist-with-existing-profile",stored.slots[1].summonPosition==SummonPosition::Right && stored.slots[1].specialMask==3 && stored.slots[1].rearMask==15 && !stored.slots[2].summonEnabled);
    Config parsed;check("reject-nonboolean-button-toggle",!parseConfig({{"specialEnabled",1}},cfg,parsed));
    check("accept-selector-masks-and-summon-position",parseConfig({{"summonPosition","left"},{"specialMask",2},{"rearMask",6}},cfg,parsed) && parsed.summonPosition==SummonPosition::Left && parsed.specialMask==2 && parsed.rearMask==6);
    const RECT monitors[]={{0,0,1280,720},{0,0,1920,1080},{1920,-1080,4480,360},{0,0,3840,2160}};
    bool geometry=true, rearSpan=true, innerSpecial=true;
    for(const auto& monitor:monitors)for(int scale=50;scale<=200;scale+=5) {
        const auto pads=panelRects(monitor,scale);const auto buttons=buttonRects(monitor,scale);
        for(int side=0;side<2;side++){const auto pad=pads[side],special=buttons[1+side],l=buttons[3+side*2],r=buttons[4+side*2];
            geometry=geometry && special.top>pad.bottom && l.bottom<pad.top &&
                l.left==pad.left && r.right==pad.right && l.right<r.left && l.top>=monitor.top && l.bottom==r.bottom &&
                special.bottom<=monitor.bottom-10;
            rearSpan=rearSpan && l.left==pad.left && r.right==pad.right &&
                (l.right-l.left)+(r.left-l.right)+(r.right-r.left)==pad.right-pad.left &&
                r.left-l.right==MulDiv(10,scale,100) && std::abs((l.right-l.left)-(r.right-r.left))<=1;
            const auto inner=side==0?r:l;
            const POINT outerPoint{side==0?(l.left+l.right)/2:(r.left+r.right)/2,(special.top+special.bottom)/2};
            innerSpecial=innerSpecial && special.left==inner.left && special.right==inner.right &&
                special.right-special.left==inner.right-inner.left && !PtInRect(&special,outerPoint);}
        geometry=geometry && buttons[0].right==monitor.right-kPanelInsetX && buttons[0].top==monitor.top+kPanelInsetY;
    }
    check("rear-above-pad-special-below-pad-all-scales-and-monitors",geometry);
    check("two-rear-cells-total-span-equals-pad-width-all-scales-and-monitors",rearSpan);
    check("single-special-cell-aligns-inner-rear-cell-outer-half-empty-all-scales-and-monitors",innerSpecial);
    bool taskbarSafe=true,gapsDoubled=true,singleMatches=true;
    for(const auto& monitor:monitors)for(int bar:{0,32,48,72,96})for(int scale=50;scale<=200;scale+=5) {
        RECT work=monitor;work.bottom-=bar;
        const auto pads=panelRects(monitor,scale,&work);const auto buttons=buttonRects(monitor,scale,&work,SummonPosition::Right);
        const auto single=singlePanelRect(monitor,scale,&work);const LONG gap=MulDiv(40,scale,100);
        singleMatches=singleMatches && single.bottom==pads[0].bottom &&
            std::abs(single.left+single.right-monitor.left-monitor.right)<=1;
        for(int side=0;side<2;side++) {
            const auto pad=pads[side],special=buttons[1+side],rear=buttons[3+2*side];
            taskbarSafe=taskbarSafe && special.bottom==work.bottom-kControlBottomClearance-panelRows(monitor,scale,&work).lift && rear.top>=work.top &&
                special.top>pad.bottom && rear.bottom<pad.top && special.bottom-special.top==rear.bottom-rear.top;
            gapsDoubled=gapsDoubled && special.top-pad.bottom==gap && pad.top-rear.bottom==gap &&
                buttons[4+2*side].left-buttons[3+2*side].right==MulDiv(10,scale,100);
        }
    }
    check("all-scales-taskbar-heights-special-bottom-above-workarea-and-complete-stack-visible",taskbarSafe);
    check("both-vertical-gaps-doubled-horizontal-gap-unchanged",gapsDoubled);
    check("single-pad-bottom-centre-shares-same-taskbar-safe-row-anchor",singleMatches);
    bool singleRearInner=true,doubleRearFixed=true,otherRowsUnchanged=true;
    for(const auto& monitor:monitors)for(int bar:{0,32,48,72,96})for(int scale=50;scale<=200;scale+=5) {
        RECT work=monitor;work.bottom-=bar;
        const auto full=buttonRects(monitor,scale,&work,SummonPosition::Right,15u);
        // Cover both named pairs and legacy masks: the key ID never chooses its slot.
        for(unsigned mask=0;mask<16;mask++) {
            const auto actual=buttonRects(monitor,scale,&work,SummonPosition::Right,mask);
            for(int n=0;n<3;n++)otherRowsUnchanged=otherRowsUnchanged && EqualRect(&actual[n],&full[n]);
            for(int side=0;side<2;side++) {
                const int first=3+side*2,inner=side==0?4:5;
                const unsigned selected=(mask>>(side*2))&3u;
                if(selected==1u)singleRearInner=singleRearInner && EqualRect(&actual[first],&full[inner]);
                else if(selected==2u)singleRearInner=singleRearInner && EqualRect(&actual[first+1],&full[inner]);
                else if(selected==3u)doubleRearFixed=doubleRearFixed &&
                    EqualRect(&actual[first],&full[first]) && EqualRect(&actual[first+1],&full[first+1]);
            }
        }
    }
    check("either-rear-key-alone-occupies-inner-cell-all-scales-monitors-and-taskbars",singleRearInner);
    check("both-rear-keys-retain-fixed-left-right-cells-all-scales-monitors-and-taskbars",doubleRearFixed);
    check("rear-pair-selection-does-not-move-Y-or-special-row",otherRowsUnchanged);
    activeProfile.store(1);appliedProfile.store(1);controlTargetEnabled.store(true);controlEdgeEnabled.store(false);
    {std::lock_guard<std::mutex> lock(mutex);core.config=cfg;}
    bool independent=true;
    for(int mask=0;mask<8;mask++) {
        Config combination=cfg;combination.enabled=false;combination.summonPosition=(mask&1)?SummonPosition::Right:SummonPosition::Off;
        combination.specialMask=(mask&2)?3u:0u;combination.rearMask=(mask&4)?15u:0u;
        combination.summonEnabled=(mask&1)!=0;combination.specialEnabled=(mask&2)!=0;combination.rearEnabled=(mask&4)!=0;
        independent=independent && buttonPermitted(combination,0)==combination.summonEnabled &&
            buttonPermitted(combination,1)==combination.specialEnabled && buttonPermitted(combination,2)==combination.specialEnabled &&
            buttonPermitted(combination,3)==combination.rearEnabled && buttonPermitted(combination,6)==combination.rearEnabled;
    }
    check("touchpad-off-three-button-categories-have-eight-independent-combinations",independent);
    buttonsRelease();bool bitsCorrect=true;
    for(int i=1;i<7;i++){buttonEvent(i,100+i,0,30,20,100,54);bitsCorrect=bitsCorrect && buttonSnapshot()==(1u<<(i-1));
        buttonEvent(i,900+i,2,30,20,100,54);bitsCorrect=bitsCorrect && buttonSnapshot()==(1u<<(i-1));
        buttonEvent(i,100+i,2,30,20,100,54);bitsCorrect=bitsCorrect && buttonSnapshot()==0;}
    check("six-independent-real-button-bits-hold-release-ignore-other-id",bitsCorrect);
    buttonEvent(1,41,0,30,20,100,54);buttonEvent(3,43,0,30,20,100,54);buttonEvent(5,45,0,30,20,100,54);
    check("special-and-back-buttons-combine",buttonSnapshot()==21);buttonsRelease();check("cancel-clears-all-held-buttons",buttonSnapshot()==0);
    buttonEvent(1,42,0,30,20,100,54);controlTargetEnabled.store(false);check("target-disabled-blocks-held-button-before-UI-refresh",buttonSnapshot()==0);
    buttonsRelease();controlTargetEnabled.store(true);activeProfile.store(3);appliedProfile.store(3);
    check("Xbox-offers-only-guide-no-rear",buttonPermitted(cfg,1) && !buttonPermitted(cfg,2) && !buttonPermitted(cfg,3) && !buttonPermitted(cfg,6));
    activeProfile.store(2);appliedProfile.store(2);controlEdgeEnabled.store(false);
    check("plain-PS5-no-Edge-back-buttons",buttonPermitted(cfg,1) && buttonPermitted(cfg,2) && !buttonPermitted(cfg,3));
    controlEdgeEnabled.store(true);check("PS5-Edge-all-four-Fn-back-buttons",buttonPermitted(cfg,3) && buttonPermitted(cfg,4) && buttonPermitted(cfg,5) && buttonPermitted(cfg,6));
    activeProfile.store(0);appliedProfile.store(0);controlTargetEnabled.store(false);
    check("disabled-persona-common-summon-only",buttonPermitted(cfg,0) && !buttonPermitted(cfg,1));
    Config standalone=profileDefault(0),parsedStandalone;
    check("standalone-special-old-config-defaults-off",standalone.standaloneSpecialMode==StandaloneSpecialMode::Off);
    check("standalone-special-rejects-unknown-or-nonstring-mode",!parseConfig({{"standaloneSpecialMode","elite"}},standalone,parsedStandalone) &&
        !parseConfig({{"standaloneSpecialMode",3}},standalone,parsedStandalone) && !parseConfig({{"standaloneSpecialMode",nullptr}},standalone,parsedStandalone));
    for(auto mode:{StandaloneSpecialMode::SteamDeck,StandaloneSpecialMode::PS5}) {
        standalone.standaloneSpecialMode=mode;
        {std::lock_guard<std::mutex> lock(mutex);core.config=standalone;}
        check(mode==StandaloneSpecialMode::SteamDeck?"disabled-Steam-all-special-pair-permitted":"disabled-PS5-all-special-pair-permitted",
            buttonPermitted(standalone,1) && buttonPermitted(standalone,2) && !buttonPermitted(standalone,3) && !buttonPermitted(standalone,6));
        check(mode==StandaloneSpecialMode::SteamDeck?"disabled-Steam-special-mode-roundtrip":"disabled-PS5-special-mode-roundtrip",
            parseConfig(configJson(standalone),profileDefault(0),parsedStandalone) && parsedStandalone.standaloneSpecialMode==mode);
        buttonEvent(1,81,0,30,20,100,54);buttonEvent(2,82,0,30,20,100,54);
        check(mode==StandaloneSpecialMode::SteamDeck?"disabled-Steam-special-does-not-enable-output":"disabled-PS5-special-does-not-enable-output",
            screenButtonMask.load()==0 && buttonSnapshot()==0 && activeProfile.load()==0 && !controlTargetEnabled.load());
        buttonsRelease();check("standalone-special-release-clears-held-bits",screenButtonMask.load()==0);
        ProfileBank bank;bank.slots[0]=standalone;const ProfileBank restored(bank.json());
        check("standalone-special-stored-in-disabled-slot-only",restored.json()==bank.json() && restored.slots[1].standaloneSpecialMode==StandaloneSpecialMode::Off &&
            restored.slots[2].standaloneSpecialMode==StandaloneSpecialMode::Off);
    }
    standalone.standaloneSpecialMode=StandaloneSpecialMode::Off;
    check("standalone-special-off-hides-both-pairs",!buttonPermitted(standalone,1) && !buttonPermitted(standalone,2));
    {std::lock_guard<std::mutex> lock(mutex);core.config=cfg;}
    buttonEvent(0,66,0,30,20,54,54);check("Y-release-produces-one-summon",buttonEvent(0,66,2,30,20,54,54) && !buttonEvent(0,66,2,30,20,54,54));
    buttonEvent(0,67,0,30,20,54,54);check("Y-canceled-gesture-does-not-summon",!buttonEvent(0,67,2,30,20,54,54,true));
    check("rounded-hit-area-rejects-corners",!buttonHit(0,0,100,54) && buttonHit(50,27,100,54));
    {std::lock_guard<std::mutex> lock(mutex);core.config=saved;}
    buttonsRelease();activeProfile.store(oldActive);appliedProfile.store(oldApplied);controlTargetEnabled.store(oldTarget);controlEdgeEnabled.store(oldEdge);
    return cases;
}
