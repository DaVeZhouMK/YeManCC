#pragma once
// Included inside ymcc::screenpads after the shared transparent-window helpers.
// Same UI owner and BUS submitter as the touchpads; no hook, timer or worker.
inline std::array<HWND,7> buttonWindows{},buttonOutlines{};
inline std::array<UINT32,7> buttonContacts{};
inline std::array<bool,7> buttonArmed{};
inline unsigned buttonEventCount=0,buttonSummonCount=0,buttonSummonPostCount=0;
inline HWND buttonSummonTestOwner=nullptr; // Developer pointer test only; never set by the app.
struct ButtonUpload {bool ready=false,whiteIcon=false,hasIcon=false;DWORD transparentBackground=1;unsigned maxAlpha=0;RECT ink{};int width=0,height=0;};
inline std::array<ButtonUpload,7> buttonUploads{};
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
    if(index==0)return effectiveSummonPosition(cfg)!=SummonPosition::Off;
    const int profile=activeProfile.load();
    if(!controlTargetEnabled.load() || profile!=appliedProfile.load() || profile==0)return false;
    if(index<3) {const unsigned mask=effectiveSpecialMask(cfg);return (mask&(1u<<(index-1)))!=0 && (profile!=3 && !ps4Target.load() || index==1);}
    const unsigned mask=effectiveRearMask(cfg);const unsigned bit=1u<<(index-3);return (mask&bit)!=0 && (profile==1 || (profile==2 && controlEdgeEnabled.load()));
}
inline void buttonsRelease() {
    screenButtonMask.store(0,std::memory_order_release);
    buttonContacts={};buttonArmed={};
}
inline unsigned buttonSnapshot() {
    const unsigned bits=screenButtonMask.load(std::memory_order_relaxed);if(!bits)return 0;
    if(!controlTargetEnabled.load(std::memory_order_acquire) || activeProfile.load()!=appliedProfile.load())return 0;
    return bits;
}
inline bool buttonEvent(int index,UINT32 id,int phase,float x,float y,int width,int height,bool canceled=false) {
    if(index<0 || index>=7)return false;
    const Config cfg=getConfig();
    if(!buttonPermitted(cfg,index))return false;
    const unsigned bit=index?1u<<(index-1):0;
    if(phase==0) {
        if(buttonContacts[index] || !buttonHit(x,y,width,height))return false;
        buttonContacts[index]=id;buttonArmed[index]=true;
        if(bit)screenButtonMask.fetch_or(bit,std::memory_order_release);
    } else {
        if(buttonContacts[index]!=id)return false;
        if(phase==1) {buttonArmed[index]=buttonHit(x,y,width,height);return false;}
        const bool summon=index==0 && buttonArmed[index] && !canceled && buttonHit(x,y,width,height);
        buttonContacts[index]=0;buttonArmed[index]=false;
        if(bit)screenButtonMask.fetch_and(~bit,std::memory_order_release);
        return summon;
    }
    return false;
}
inline LRESULT CALLBACK buttonProc(HWND h,UINT m,WPARAM w,LPARAM lp) {
    if(m==WM_NCCREATE){SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);return TRUE;}
    const int index=(int)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(m==WM_POINTERACTIVATE)return PA_NOACTIVATE;
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT){PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    if(m==WM_NCHITTEST){POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&p);RECT r{};GetClientRect(h,&r);
        return buttonHit((float)p.x,(float)p.y,r.right,r.bottom)?HTCLIENT:HTTRANSPARENT;}
    if(m==WM_POINTERDOWN || m==WM_POINTERUPDATE || m==WM_POINTERUP) {
        POINTER_INFO pi{};const UINT32 id=GET_POINTERID_WPARAM(w);
        if(!GetPointerInfo(id,&pi) || (pi.pointerType!=PT_TOUCH && pi.pointerType!=PT_PEN))return 0;
        if(index && !dryRun && owner.load()==GetForegroundWindow()){buttonsRelease();return 0;}
        POINT p=pi.ptPixelLocation;ScreenToClient(h,&p);RECT r{};GetClientRect(h,&r);
        const bool canceled=(pi.pointerFlags&POINTER_FLAG_CANCELED)!=0;
        const bool summon=buttonEvent(index,id,canceled?2:m==WM_POINTERDOWN?0:m==WM_POINTERUP?2:1,(float)p.x,(float)p.y,r.right,r.bottom,canceled);
        ++buttonEventCount;
        if(testTrace)testTrace->push_back({{"button",index},{"message",m},{"id",id},{"x",p.x},{"y",p.y},{"mask",buttonSnapshot()},{"summon",summon},{"canceled",canceled}});
        if(summon) {
            ++buttonSummonCount;
            // Same asynchronous owner-UI route in production and the isolated test sink.
            if(HWND parent=dryRun?buttonSummonTestOwner:owner.load()) {
                if(PostMessageW(parent,kSummonMessage,0,0))++buttonSummonPostCount;
                else windowError=GetLastError();
            }
        }
        return 0;
    }
    if(m==WM_POINTERCAPTURECHANGED || m==WM_CANCELMODE) {
        if(m==WM_CANCELMODE || buttonContacts[index]==GET_POINTERID_WPARAM(w)) {
            buttonContacts[index]=0;buttonArmed[index]=false;
            if(index)screenButtonMask.fetch_and(~(1u<<(index-1)),std::memory_order_release);
        }return 0;
    }
    return DefWindowProcW(h,m,w,lp);
}
inline const unsigned char* buttonGlyph(int profile,int index) {
    if(index==0)return glyphs::ymcc;
    if(index==1)return profile==1?glyphs::steam:profile==2?glyphs::ps:glyphs::xbox;
    if(index==2)return profile==1?glyphs::dots:glyphs::mute;
    if(profile==2) {switch(index){case 3:return glyphs::lfn;case 4:return glyphs::lb;case 5:return glyphs::rfn;case 6:return glyphs::rb;}}
    return nullptr;
}
inline bool renderButton(int index,int transparency) {
    const HWND h=buttonOutlines[index],input=buttonWindows[index];if(!h || !input)return false;
    // Owned popup windows do not inherit the owner's screen position/size.
    // The input HWND is authoritative, exactly as in renderOutline for pads.
    RECT rc{};if(!GetWindowRect(input,&rc)){windowError=GetLastError();return false;}
    const int width=rc.right-rc.left,height=rc.bottom-rc.top;if(width<1 || height<1)return false;
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);BITMAPINFO bi{};
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=width;bi.bmiHeader.biHeight=-height;
    bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap || !memory || !bits){if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);if(screen)ReleaseDC(nullptr,screen);windowError=ERROR_NOT_ENOUGH_MEMORY;return false;}
    const auto prior=SelectObject(memory,bitmap);auto* pixels=(DWORD*)bits;
    std::fill(pixels,pixels+(size_t)width*height,0u);
    const int profile=appliedProfile.load(),opacity=outlineAlpha(transparency);
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
    ButtonUpload upload;upload.whiteIcon=true;upload.transparentBackground=pixels[std::min(height-1,3)*width+width/2];
    upload.width=width;upload.height=height;upload.ink={width,height,0,0};
    for(int y=0;y<height;y++)for(int x=0;x<width;x++)if(const DWORD pixel=pixels[y*width+x]) {
        const unsigned a=pixel>>24;upload.hasIcon=true;upload.maxAlpha=std::max(upload.maxAlpha,a);
        upload.whiteIcon=upload.whiteIcon && ((pixel&255)==a) && (((pixel>>8)&255)==a) && (((pixel>>16)&255)==a);
        upload.ink.left=std::min(upload.ink.left,(LONG)x);upload.ink.top=std::min(upload.ink.top,(LONG)y);
        upload.ink.right=std::max(upload.ink.right,(LONG)x+1);upload.ink.bottom=std::max(upload.ink.bottom,(LONG)y+1);
    }
    for(int y=0;y<height;y++)for(int x=0;x<width;x++) {
        const DWORD stroke=outlinePixel(x,y,width,height,transparency);if(stroke)pixels[y*width+x]=stroke;
    }
    if(dryRun && !buttonArtTestPrefix.empty() && buttonPermitted(getConfig(),index)) {
        const auto file=buttonArtTestPrefix.wstring()+L"-"+std::to_wstring(index)+L".bgra";
        std::ofstream out(std::filesystem::path(file),std::ios::binary);out.write((const char*)pixels,(std::streamsize)width*height*4);
    }
    POINT destination{rc.left,rc.top},source{};SIZE size{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    const bool ok=UpdateLayeredWindow(h,screen,&destination,&size,memory,&source,0,&blend,ULW_ALPHA)!=FALSE;
    upload.ready=ok;buttonUploads[index]=upload;
    if(!ok){windowError=GetLastError();if(!windowError)windowError=ERROR_INVALID_DATA;}
    SelectObject(memory,prior);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);return ok;
}
inline void buttonsRepaint(int transparency) {
    for(int i=0;i<7;i++)if(buttonOutlines[i] && !renderButton(i,transparency))ShowWindow(buttonOutlines[i],SW_HIDE);
}
inline void buttonsRefresh(const Config& cfg,const RECT& monitor,const RECT& workArea) {
    const auto rects=buttonRects(monitor,cfg.scale,&workArea,effectiveSummonPosition(cfg),effectiveRearMask(cfg));
    for(int i=0;i<7;i++) {
        const bool show=buttonPermitted(cfg,i);const auto r=rects[i];const int width=r.right-r.left,height=r.bottom-r.top;
        if(show && !buttonWindows[i]) {
            buttonWindows[i]=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,L"YeManScreenButton",L"YeManCC Screen Button",WS_POPUP,0,0,width,height,nullptr,nullptr,instance,(void*)(INT_PTR)i);
            if(!buttonWindows[i])windowError=GetLastError();
        }
        if(buttonWindows[i] && !buttonOutlines[i]) {
            buttonOutlines[i]=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,L"YeManScreenTouchpadOutline",L"YeManCC Screen Button Icon",WS_POPUP,0,0,width,height,buttonWindows[i],nullptr,instance,nullptr);
            if(!buttonOutlines[i])windowError=GetLastError();
        }
        const bool positioned=buttonWindows[i] && SetWindowPos(buttonWindows[i],HWND_TOPMOST,r.left,r.top,width,height,SWP_NOACTIVATE);
        if(buttonWindows[i] && !positioned)windowError=GetLastError();
        const bool ready=positioned && buttonOutlines[i] && renderButton(i,cfg.transparency);
        if(buttonWindows[i])ShowWindow(buttonWindows[i],show && ready?SW_SHOWNOACTIVATE:SW_HIDE);
        if(buttonOutlines[i])ShowWindow(buttonOutlines[i],show && ready?SW_SHOWNOACTIVATE:SW_HIDE);
    }
}
inline void buttonsInitialize() {
    WNDCLASSW wc{};wc.lpfnWndProc=buttonProc;wc.hInstance=instance;wc.lpszClassName=L"YeManScreenButton";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)windowError=GetLastError();
}
inline bool buttonsVisible(){for(HWND h:buttonWindows)if(h && IsWindowVisible(h))return true;return false;}
inline void buttonsShutdown(){buttonsRelease();for(HWND& h:buttonOutlines){if(h)DestroyWindow(h);h=nullptr;}for(HWND& h:buttonWindows){if(h)DestroyWindow(h);h=nullptr;}}
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
        const auto single=singlePanelRect(monitor,scale,&work);const LONG gap=MulDiv(20,scale,100);
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
    buttonEvent(0,66,0,30,20,54,54);check("Y-release-produces-one-summon",buttonEvent(0,66,2,30,20,54,54) && !buttonEvent(0,66,2,30,20,54,54));
    buttonEvent(0,67,0,30,20,54,54);check("Y-canceled-gesture-does-not-summon",!buttonEvent(0,67,2,30,20,54,54,true));
    check("rounded-hit-area-rejects-corners",!buttonHit(0,0,100,54) && buttonHit(50,27,100,54));
    {std::lock_guard<std::mutex> lock(mutex);core.config=saved;}
    buttonsRelease();activeProfile.store(oldActive);appliedProfile.store(oldApplied);controlTargetEnabled.store(oldTarget);controlEdgeEnabled.store(oldEdge);
    return cases;
}
