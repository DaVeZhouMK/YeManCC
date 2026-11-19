#pragma once
// Included after screen_touchpads.h. All windows and keyboard ownership stay on
// its existing UI thread; the existing BUS consumes one epoch-bound POD sample.
#include "custom_touch_config.h"
#include "custom_touch_glyphs.generated.h"
#include <memory>
namespace ymcc::customtouch {
inline constexpr UINT kEditorMessage=WM_APP+194;
struct Item {
    std::string id,label;int kind=13;std::vector<WORD> keys;HWND window=nullptr,outline=nullptr;
    ymcc::screenpads::OutlineBitmap bitmap;std::array<ymcc::screenpads::OutlineBitmap,2> frames;
    RECT rect{};UINT32 contact=0;bool pressed=false,feedbackOnly=false;
    ymcc::touchhaptic::Motion haptic;
    int artworkTemplate=-1,artworkScale=0,artworkKind=-1,padding=0;std::string artworkLabel;
    bool artworkReady=false;float x=0,y=0;unsigned long long generation=0;bool retiring=false;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    ymcc::screeninput::Surface surface;
#endif
};
inline std::vector<std::shared_ptr<Item>> items;
inline std::vector<std::shared_ptr<Item>> retired;
inline std::array<bool,256> sent{};
inline bool ownsKey(WORD key) noexcept{return key<sent.size()&&sent[key];}
inline HWND toolbar=nullptr,editorLabel=nullptr;
inline HFONT editorFont=nullptr,editorTitleFont=nullptr;inline int editorDpi=96;
struct EditorColors {COLORREF panel=RGB(13,19,28),input=RGB(21,30,43),text=RGB(233,238,245),dim=RGB(139,150,168),accent=RGB(46,166,255),line=RGB(43,48,56);};
inline EditorColors editorColors;
inline void setEditorTheme(const std::string& theme){
    editorColors={};
    if(theme=="red-black")editorColors={RGB(24,18,25),RGB(40,28,37),RGB(246,237,241),RGB(187,168,178),RGB(241,107,130),RGB(53,46,53)};
    else if(theme=="cyberpunk")editorColors={RGB(23,21,16),RGB(40,36,25),RGB(244,240,228),RGB(190,179,154),RGB(239,198,91),RGB(52,49,44)};
}
inline std::atomic<HWND> editorWindow{nullptr};
inline unsigned long editorTicket=0;inline bool editorArming=true;inline WORD editorPrevious=0;inline int editorDirection=0;inline ULONGLONG editorRepeatAt=0;
inline Json originalPositions;
inline RECT monitor{};inline unsigned long long refreshEpoch=0;
inline DWORD error=0;inline std::string selected,dragId;inline UINT32 dragContact=0;inline POINT dragOffset{};
inline bool (*save)(const Json&,const Json&)=nullptr;
inline void (*changed)()=nullptr;inline void (*showOwner)(bool)=nullptr;
inline bool (*resumeDisplayAfterEditing)()=nullptr;
inline bool dryRun=false;
inline const char* glyphIds[]={"face-Y","face-X","face-B","face-A","gamepad-LT","gamepad-LB","gamepad-RB","gamepad-RT","gamepad-back","gamepad-start","gamepad-dpad"};
inline const char* legacyId(int index){static const char* ids[]={"summon-y","native-steam","native-qam","native-rear-1","native-rear-2","native-rear-3","native-rear-4"};return index>=0&&index<7?ids[index]:"";}
// The BUS reads only the parent's atomic/public power contract. HWND teardown
// and display-fence fields remain exclusive to its original owner thread.
inline bool inheritedInputAllowed(){return live.load()&&inheritedMasterEnabled.load(std::memory_order_acquire)&&!editing.load()&&ymcc::screenpads::sharedInputAllowed()&&ymcc::screenpads::activeProfile.load()==ymcc::screenpads::appliedProfile.load();}
inline bool outputAllowed(){return inheritedInputAllowed()&&!ymcc::screenpads::inputTeardown&&!ymcc::screenpads::displayTransitionFence.pending;}
inline bool targetAllowed(){return inheritedInputAllowed()&&ymcc::screenpads::controlTargetEnabled.load()&&ymcc::screenpads::activeProfile.load()>0;}
inline unsigned heldKeys(){return (unsigned)std::count(sent.begin(),sent.end(),true);}
inline bool sendChangedKeys(const std::array<bool,256>& desired){
    // A virtual key can enter at most one pass. Initialize only emitted inputs.
    std::array<INPUT,256> inputs;std::array<unsigned,256> codes;UINT n=0;
    // Release first, in reverse modifier order. Only successfully inserted downs
    // are owned; a short release remains owned for the next lifecycle edge.
    for(int pass=0;pass<4;++pass)for(unsigned index=0;index<256;++index){const bool down=pass>=2;const unsigned vk=down?index:255-index;
        if(sent[vk]==desired[vk]||desired[vk]!=down)continue;
        const bool modifier=(vk>=VK_SHIFT&&vk<=VK_MENU)||(vk>=VK_LSHIFT&&vk<=VK_RMENU)||vk==VK_LWIN||vk==VK_RWIN;
        if(modifier!=(pass==1||pass==2))continue;
        if(ymcc::screenpads::mappedKeyHeld((WORD)vk)){sent[vk]=desired[vk];continue;}
        auto& input=inputs[n];input={};input.type=INPUT_KEYBOARD;input.ki.wVk=(WORD)vk;
        input.ki.dwFlags=(keyFlags?keyFlags((WORD)vk):0)|(down?0:KEYEVENTF_KEYUP);input.ki.dwExtraInfo=ymcc::screenpads::kInjectedTag;codes[n++]=vk;
    }
    if(!n)return true;const UINT inserted=dryRun?n:SendInput(n,inputs.data(),sizeof(INPUT));const DWORD last=GetLastError();
    for(UINT i=0;i<std::min(inserted,n);++i)sent[codes[i]]=(inputs[i].ki.dwFlags&KEYEVENTF_KEYUP)==0;
    if(inserted!=n)error=last?last:ERROR_ACCESS_DENIED;
    if(const auto observer=ymcc::screenpads::outputEvidenceObserver)observer(inputs.data(),n,inserted,inserted==n?0:last,dryRun);
    SetLastError(last);
    return inserted==n;
}
inline bool sendDesired(const std::array<bool,256>& desired){
    // Compare against accepted ownership, not the last requested state: a
    // short SendInput still leaves a difference to reconcile.
    return sent==desired||sendChangedKeys(desired);
}
inline bool draw(Item&);
inline bool contactHeld(){for(const auto& item:items)if(item->contact)return true;return false;}
inline void syncFeedback(bool pulse=false){feedbackContact.store(contactHeld(),std::memory_order_relaxed);ymcc::screenpads::Action action;action.hapticPulses=pulse?1u:0u;ymcc::screenpads::outputHaptic(action);}
inline void releaseMapped(){
    if(!live.load()&&!heldKeys())return;
    invalidate();for(auto& item:items)if(item->contact)item->feedbackOnly=true;sendDesired({});
}
inline void release(){
    if(!live.load()&&std::none_of(sent.begin(),sent.end(),[](bool b){return b;}))return;
    invalidate();const auto current=items;for(auto& item:current){const bool pressed=item->contact!=0;item->contact=0;item->pressed=false;item->feedbackOnly=false;item->haptic={};item->x=item->y=0;if(pressed)draw(*item);}
    sendDesired({});syncFeedback();
}
inline Pad sample(unsigned long long epoch,bool ready){
    if(!live.load(std::memory_order_acquire)||!gamepadEnabled.load(std::memory_order_acquire))return {};
    ready=ready&&epoch&&targetAllowed();std::lock_guard<std::mutex> lock(padMutex);
    if(epoch!=transportEpoch||ready!=transportReady){pad={};hasPadInput.store(false);powerBound=false;++inputGeneration;transportEpoch=epoch;transportReady=ready;}
    if(ready&&hasPadInput.load(std::memory_order_relaxed)&&powerBound&&(!powerCurrent||!powerCurrent(touchPower))){pad={};hasPadInput.store(false);powerBound=false;++inputGeneration;}
    return ready?pad:Pad{};
}
inline void rebuildPadLocked(){
    pad={};for(const auto& item:items){if(!item->contact||item->feedbackOnly||item->generation!=inputGeneration||!item->pressed)continue;
        switch(item->kind){case 0:pad.buttons|=0x8000;break;case 1:pad.buttons|=0x4000;break;case 2:pad.buttons|=0x2000;break;case 3:pad.buttons|=0x1000;break;
        case 4:pad.lt=255;break;case 5:pad.buttons|=0x0100;break;case 6:pad.buttons|=0x0200;break;case 7:pad.rt=255;break;
        case 8:pad.buttons|=0x0020;break;case 9:pad.buttons|=0x0010;break;
        case 10:if(item->x<-.3f)pad.buttons|=4;if(item->x>.3f)pad.buttons|=8;if(item->y<-.3f)pad.buttons|=2;if(item->y>.3f)pad.buttons|=1;break;
        case 11:pad.lx=(short)std::lround(item->x*32767);pad.ly=(short)std::lround(item->y*32767);break;
        case 12:pad.rx=(short)std::lround(item->x*32767);pad.ry=(short)std::lround(item->y*32767);break;default:break;}}
    hasPadInput.store(pad.buttons||pad.lt||pad.rt||pad.lx||pad.ly||pad.rx||pad.ry,std::memory_order_release);
}
inline bool draw(Item& item){
    using namespace ymcc::screenpads;
    if(!item.window||!item.outline)return false;RECT r{};if(!GetWindowRect(item.window,&r))return false;
    const int width=r.right-r.left,height=r.bottom-r.top;if(width<1||height<1)return false;
    const auto native=getConfig();const int artworkTemplate=config["template"]=="ps5"?1:config["template"]=="xbox"?2:0;
    const int scale=MulDiv(native.scale,config[item.kind==11||item.kind==12?"stickScale":"buttonScale"].get<int>(),100);
    const int padding=pressGlowPadding(scale);auto& image=item.bitmap;
    const bool rebuild=!item.artworkReady||image.width!=width||image.height!=height||item.artworkKind!=item.kind||
        item.artworkTemplate!=artworkTemplate||item.artworkLabel!=item.label||item.artworkScale!=scale;
    if(rebuild){
        item.artworkReady=false;for(auto& frame:item.frames)frame.reset();
        if(!allocatePressBitmap(image,width,height)){error=windowError;return false;}
        const auto ink=[&](int x,int y,unsigned a){if(x>=0&&y>=0&&x<width&&y<height)image.pixels[(size_t)y*width+x]=(a<<24)|(a<<16)|(a<<8)|a;};
        if(item.kind<11){const auto mask=assets::masks[artworkTemplate][item.kind];
            // Match the preview img object-fit:contain; Steam square artwork must not stretch.
            const int side=std::min(width,height),left=(width-side)/2,top=(height-side)/2;
            for(int y=0;y<side;++y)for(int x=0;x<side;++x)ink(x+left,y+top,mask[(size_t)std::min(63,y*64/side)*64+std::min(63,x*64/side)]);
        }else if(item.kind<13){
            const double cx=width*.5,cy=height*.5,radius=std::min(width,height)*73/150.0;
            for(int y=0;y<height;++y)for(int x=0;x<width;++x){const double distance=std::abs(std::hypot(x+.5-cx,y+.5-cy)-radius);unsigned a=(unsigned)std::lround(std::clamp(1.0-distance,0.0,1.0)*255);
                const int inset=std::max(1,(int)std::lround(std::min(width,height)*17/150.0));
                if((x==width/2&&y>=inset&&y<height-inset)||(y==height/2&&x>=inset&&x<width-inset))a=std::max(a,127u);ink(x,y,a);}
        }else{
            HFONT font=CreateFontW(-std::max(11,MulDiv(16,scale,100)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            if(!font){error=ERROR_NOT_ENOUGH_MEMORY;return false;}HGDIOBJ previous=SelectObject(image.dc,font);SetTextColor(image.dc,RGB(255,255,255));SetBkMode(image.dc,TRANSPARENT);RECT textRect{2,2,width-2,height-2};
            const int length=MultiByteToWideChar(CP_UTF8,0,item.label.data(),(int)item.label.size(),nullptr,0);std::wstring label(length,L'\0');MultiByteToWideChar(CP_UTF8,0,item.label.data(),(int)item.label.size(),label.data(),length);DrawTextW(image.dc,label.c_str(),length,&textRect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(image.dc,previous);DeleteObject(font);
            for(int y=0;y<height;++y)for(int x=0;x<width;++x){const DWORD text=image.pixels[(size_t)y*width+x];const DWORD a=std::max<DWORD>({text&255u,(text>>8)&255u,(text>>16)&255u,outlineBorderPixel(x,y,width,height,0)>>24});ink(x,y,a);}
        }
        item.artworkTemplate=artworkTemplate;item.artworkScale=scale;item.artworkKind=item.kind;item.artworkLabel=item.label;item.padding=padding;item.artworkReady=true;
    }
    const bool pressed=item.contact!=0;auto& frame=item.frames[pressed?1:0];
    if(!buildPressFrame(image,frame,padding,pressed,item.kind==11||item.kind==12)||!ymcc::screeninput::ensureDisplay(item.outline)){error=windowError?windowError:ERROR_ACCESS_DENIED;return false;}
    POINT destination{r.left-padding,r.top-padding},source{};SIZE size{frame.width,frame.height};
    BLENDFUNCTION blend{AC_SRC_OVER,0,outlineAlpha(outlineFeedbackTransparency(native.transparency,pressed)),AC_SRC_ALPHA};
    if(!UpdateLayeredWindow(item.outline,nullptr,&destination,&size,frame.dc,&source,0,&blend,ULW_ALPHA)){error=GetLastError();return false;}return true;
}
inline void repaint(){for(auto& item:items)if(!draw(*item)&&item->outline)ShowWindow(item->outline,SW_HIDE);}
inline bool visible(){for(const auto& item:items)if(item->window&&IsWindowVisible(item->window))return true;return false;}
inline bool itemWanted(const Item& item){
    using namespace ymcc::screenpads;
    if(!live.load()||inputTeardown||displayTransitionFence.pending||presentationSuspended.load())return false;
    if(editing.load())return true;
    if(!inheritedMasterEnabled.load()||activeProfile.load()!=appliedProfile.load())return false;
    return item.kind==13||(controlTargetEnabled.load()&&activeProfile.load()>0);
}
inline void syncVisibility(bool hidden){
    for(const auto& item:items)if(item->outline){const bool show=!hidden&&itemWanted(*item)&&item->window&&IsWindowVisible(item->window);
        ShowWindow(item->outline,show&&draw(*item)?SW_SHOWNOACTIVATE:SW_HIDE);}
}
inline HWND findWindow(const std::string& id){
    using namespace ymcc::screenpads;if(id=="pad-left"||id=="pad-single")return windows[0];if(id=="pad-right")return windows[1];
    for(int i=0;i<7;++i)if(id==legacyId(i))return buttonWindows[i];for(const auto& item:items)if(item->id==id)return item->window;return nullptr;
}
inline void labelSelection(){if(editorLabel){
    std::string name=selected;for(const auto& item:items)if(item->id==selected){name=item->label;break;}
    if(selected=="summon-y")name="Y 呼出键";else if(selected=="pad-left")name="左触摸板";else if(selected=="pad-right")name="右触摸板";
    const int length=MultiByteToWideChar(CP_UTF8,0,name.data(),(int)name.size(),nullptr,0);std::wstring text(length,L'\0');
    MultiByteToWideChar(CP_UTF8,0,name.data(),(int)name.size(),text.data(),length);RECT r{};
    if(GetWindowRect(findWindow(selected),&r))text+=L"   X "+std::to_wstring(r.left-monitor.left)+L"   Y "+std::to_wstring(r.top-monitor.top);
    SetWindowTextW(editorLabel,text.c_str());InvalidateRect(editorLabel,nullptr,FALSE);
}}
inline void moveWindow(const std::string& id,LONG x,LONG y){
    const HWND h=findWindow(id);RECT r{};if(!h||!GetWindowRect(h,&r))return;const LONG width=r.right-r.left,height=r.bottom-r.top;
    x=std::clamp(x,monitor.left,std::max(monitor.left,monitor.right-width));y=std::clamp(y,monitor.top,std::max(monitor.top,monitor.bottom-height));
    config["positions"][resolution(monitor)][id]={{"x",(double)(x-monitor.left)/std::max(1L,monitor.right-monitor.left)},{"y",(double)(y-monitor.top)/std::max(1L,monitor.bottom-monitor.top)}};
    SetWindowPos(h,HWND_TOPMOST,x,y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
    using namespace ymcc::screenpads;for(int i=0;i<2;++i)if(h==windows[i])renderOutline(i,getConfig().transparency);
    for(int i=0;i<7;++i)if(h==buttonWindows[i])renderButton(i,getConfig().transparency);for(auto& item:items)if(h==item->window)draw(*item);
}
inline bool layoutPointer(HWND h,const char* id,UINT message,UINT32 contact,float x,float y,bool canceled){
    if(!editing.load())return false;if(canceled){if(dragContact==contact){dragContact=0;dragId.clear();}return true;}
    if(message==WM_POINTERDOWN){if(dragContact)return true;selected=id;dragId=id;dragContact=contact;dragOffset={(LONG)std::lround(x),(LONG)std::lround(y)};labelSelection();}
    if(dragContact==contact&&dragId==id){if(message==WM_POINTERUPDATE){POINT p{(LONG)std::lround(x),(LONG)std::lround(y)};ClientToScreen(h,&p);moveWindow(id,p.x-dragOffset.x,p.y-dragOffset.y);}else if(message==WM_POINTERUP){dragContact=0;dragId.clear();}}
    return true;
}
inline bool itemWanted(const Item&);
inline void input(Item& item,UINT message,UINT32 contact,float x,float y,bool canceled=false,DWORD sourceTime=0,bool timeKnown=false){
    using namespace ymcc::screenpads;
    if(item.retiring||!live.load()||inputTeardown||!std::isfinite(x)||!std::isfinite(y)||!contact)return;
    if(layoutPointer(item.window,item.id.c_str(),message,contact,x,y,canceled))return;
    if(!outputAllowed()){release();return;}
    RECT r{};if(!GetClientRect(item.window,&r))return;const float width=(float)r.right,height=(float)r.bottom;
    const bool stick=item.kind==11||item.kind==12;
    const bool inside=x>=0&&y>=0&&x<width&&y<height&&(!stick||std::hypot(x-width*.5f,y-height*.5f)<=std::min(width,height)*73.f/150.f);
    const bool ownUi=ymcc::screenpads::dryRun&&feedbackOnlyForegroundFixture?*feedbackOnlyForegroundFixture:(!ymcc::screenpads::dryRun&&owner.load()==GetForegroundWindow());
    const bool wasPressed=item.contact!=0;bool pulse=false;
    const auto tick=GetTickCount64(),now=timeKnown?pointerEventTick(sourceTime,tick):tick;
    const int strength=touchFeedbackStrength(getConfig().touchFeedback);
    if(canceled||message==WM_POINTERUP){if(item.contact!=contact)return;item.contact=0;item.pressed=false;item.feedbackOnly=false;item.haptic={};}
    else{
        if(message==WM_POINTERDOWN){
            if(item.contact||!inside||!itemWanted(item))return;
            bool mapped=item.kind==13;
            if(item.kind<13&&targetAllowed()&&!ownUi){ymcc::sleeppower::SharedPowerV1 power;
                if(capturePower&&powerCurrent&&capturePower(power)){std::lock_guard<std::mutex> lock(padMutex);mapped=transportReady;
                    if(mapped){if(powerBound&&!powerCurrent(touchPower)){pad={};hasPadInput.store(false);++inputGeneration;}touchPower=power;powerBound=true;item.generation=inputGeneration;}}}
            item.contact=contact;item.feedbackOnly=ownUi||!mapped;item.haptic.begin(x,y,(int)width,(int)height,now);pulse=strength>0;
        }else if(item.contact!=contact)return;
        else if(strength)pulse=item.haptic.move(x,y,now);
        // A gesture that starts on YMCC remains feedback-only through UP.
        if(ownUi){for(auto& key:items)if(key->contact)key->feedbackOnly=true;invalidate();sendDesired({});}
        item.pressed=item.kind==11||item.kind==12||inside;
        if(item.kind<13){
            item.x=std::clamp((x-width*.5f)/(width*.42f),-1.f,1.f);item.y=std::clamp(-(y-height*.5f)/(height*.42f),-1.f,1.f);
            const float length=std::hypot(item.x,item.y);if(length>1){item.x/=length;item.y/=length;}
        }
    }
    if(item.kind<13){std::lock_guard<std::mutex> lock(padMutex);rebuildPadLocked();}
    else{std::array<bool,256> desired{};for(const auto& key:items)if(key->kind==13&&key->contact&&key->pressed&&!key->feedbackOnly)for(WORD vk:key->keys)desired[vk]=true;
        if(!sendDesired(desired)){for(auto& key:items)if(key->kind==13){key->contact=0;key->pressed=false;}sendDesired({});}}
    ++eventCount;syncFeedback(pulse);if(wasPressed!=(item.contact!=0))draw(item);autoHideTouch(message==WM_POINTERDOWN&&!canceled);
}
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
inline void nativeSink(HWND,const ymcc::screeninput::Event& event,void* context){auto* item=(Item*)context;if(!live.load()||!item)return;
    input(*item,event.phase==ymcc::screeninput::Phase::Down?WM_POINTERDOWN:event.phase==ymcc::screeninput::Phase::Up?WM_POINTERUP:WM_POINTERUPDATE,event.id,event.x,event.y,event.cancelled||event.phase==ymcc::screeninput::Phase::Cancel,event.sourceTime,event.timeKnown);}
#endif
inline LRESULT CALLBACK windowProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_NCCREATE){SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);return TRUE;}auto* item=(Item*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_POINTERACTIVATE)return PA_NOACTIVATE;if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT){PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    if(!item)return DefWindowProcW(h,m,w,l);
    if(m==WM_CANCELMODE||m==WM_POINTERCAPTURECHANGED){input(*item,WM_POINTERUP,item->contact,0,0,true);return 0;}
    if(m==WM_STYLECHANGED||m==WM_ENABLE){
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
        if(item->surface.active()&&!item->surface.ensureMousePass()){ymcc::screenpads::recordNativeInputFailure(true,item->kind,item->surface,true);input(*item,WM_POINTERUP,item->contact,0,0,true);ShowWindow(h,SW_HIDE);if(item->outline)ShowWindow(item->outline,SW_HIDE);error=(DWORD)item->surface.error;}
#endif
        return 0;}
    if(m==WM_POINTERDOWN||m==WM_POINTERUPDATE||m==WM_POINTERUP){POINTER_INFO info{};const UINT32 id=GET_POINTERID_WPARAM(w);if(!GetPointerInfo(id,&info)||(info.pointerType!=PT_TOUCH&&info.pointerType!=PT_PEN))return 0;POINT p=info.ptPixelLocation;ScreenToClient(h,&p);input(*item,m,id,(float)p.x,(float)p.y,(info.pointerFlags&POINTER_FLAG_CANCELED)!=0,info.dwTime,true);return 0;}
    return DefWindowProcW(h,m,w,l);
}
inline void dispose(const std::shared_ptr<Item>& item){
    item->retiring=true;
    for(HWND h:{item->window,item->outline})if(h){ShowWindow(h,SW_HIDE);EnableWindow(h,FALSE);}
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    item->surface.close();
#endif
    for(HWND* h:{&item->outline,&item->window})if(*h){if(DestroyWindow(*h))*h=nullptr;else error=GetLastError();}item->bitmap.reset();for(auto& frame:item->frames)frame.reset();item->artworkReady=false;
}
inline std::array<RECT,13> gamepadDefaultRects(const std::array<RECT,2>& pads,int buttons,int sticks,int nativeScale){
    std::array<RECT,13> result{};const LONG gap=std::max(6,MulDiv(12,nativeScale,100));
    const LONG side=MulDiv(132,sticks,100),face=MulDiv(52,buttons,100),offset=MulDiv(50,buttons,100);
    const LONG shoulderWidth=MulDiv(76,buttons,100),shoulderHeight=MulDiv(44,buttons,100),smallWidth=MulDiv(44,buttons,100),smallHeight=MulDiv(38,buttons,100),dpad=MulDiv(98,buttons,100);
    const auto rect=[](LONG x,LONG y,LONG w,LONG h){return RECT{x,y,x+w,y+h};};
    for(int n=0;n<2;++n){const auto& pad=pads[n];const LONG center=(pad.left+pad.right)/2;
        result[11+n]=rect(n==0?pad.right+gap:pad.left-gap-side,(pad.top+pad.bottom-side)/2,side,side);
        const LONG x=center-(2*shoulderWidth+gap)/2,y=pad.top-gap-shoulderHeight;
        result[n==0?4:6]=rect(x,y,shoulderWidth,shoulderHeight);result[n==0?5:7]=rect(x+shoulderWidth+gap,y,shoulderWidth,shoulderHeight);
    }
    const LONG cx=result[12].left-gap-offset-face/2,cy=(pads[1].top+pads[1].bottom)/2;
    const int delta[4][2]={{0,-1},{-1,0},{1,0},{0,1}};
    for(int i=0;i<4;++i)result[i]=rect(cx+delta[i][0]*offset-face/2,cy+delta[i][1]*offset-face/2,face,face);
    result[8]=rect(pads[0].right+gap,pads[0].top-gap-smallHeight,smallWidth,smallHeight);
    result[9]=rect(pads[1].left-gap-smallWidth,pads[1].top-gap-smallHeight,smallWidth,smallHeight);
    result[10]=rect(result[11].right+gap,(pads[0].top+pads[0].bottom-dpad)/2,dpad,dpad);return result;
}
inline void refresh(){
    if(!live.load()||ymcc::screenpads::inputTeardown||ymcc::screenpads::displayTransitionFence.pending)return;const auto epoch=++refreshEpoch;const auto old=items;
    MONITORINFO mi{sizeof(mi)};if(!GetMonitorInfoW(MonitorFromWindow(ymcc::screenpads::owner.load(),MONITOR_DEFAULTTOPRIMARY),&mi)){error=GetLastError();return;}monitor=mi.rcMonitor;
    using namespace ymcc::screenpads;const auto native=getConfig();auto position=effectiveSummonPosition(native);if(position==SummonPosition::Off)position=summonWindowOnLeft.load()?SummonPosition::Right:SummonPosition::Left;
    const auto anchor=buttonRects(monitor,native.scale,&mi.rcWork,position)[0];const LONG ox=monitor.left,oy=monitor.top;
    const int buttons=MulDiv(native.scale,config["buttonScale"].get<int>(),100),sticks=MulDiv(native.scale,config["stickScale"].get<int>(),100);std::vector<std::shared_ptr<Item>> next;
    auto add=[&](const std::string& id,const std::string& label,int kind,LONG x,LONG y,int width,int height,const std::vector<WORD>& keys=std::vector<WORD>{}){
        auto found=std::find_if(old.begin(),old.end(),[&](const auto& item){return item->id==id&&!item->retiring;});auto item=found==old.end()?std::make_shared<Item>():*found;item->id=id;item->label=label;item->kind=kind;item->keys=keys;item->rect=place(id.c_str(),monitor,{x,y,x+width,y+height});next.push_back(item);};
    if(config["enabled"].get<bool>()){size_t index=0;for(const auto& key:config["keys"]){std::vector<WORD> keys;for(const auto& entry:key["keys"]){WORD vk=0;bool shift=false;resolveKey(entry["actionId"].get<std::string>(),entry["code"].get<std::string>(),vk,shift);if(shift&&std::find(keys.begin(),keys.end(),VK_LSHIFT)==keys.end())keys.push_back(VK_LSHIFT);if(std::find(keys.begin(),keys.end(),vk)==keys.end())keys.push_back(vk);}
            const int width=MulDiv(66,buttons,100),height=MulDiv(54,buttons,100),gap=10;const LONG x=position==SummonPosition::Left?anchor.right+gap+(LONG)(index%4)*(width+gap):anchor.left-gap-width-(LONG)(index%4)*(width+gap);
            add(key["id"].get<std::string>(),key["label"].get<std::string>(),13,std::clamp(x,ox,std::max(ox,monitor.right-width)),std::min(anchor.top+(LONG)(index/4)*(height+gap),std::max(oy,monitor.bottom-height)),width,height,keys);++index;}}
    if(config["gamepad"].get<bool>()){
        auto pads=panelRects(monitor,native.scale,&mi.rcWork);pads[0]=place("pad-left",monitor,pads[0]);pads[1]=place("pad-right",monitor,pads[1]);
        const auto positions=gamepadDefaultRects(pads,buttons,sticks,native.scale);
        for(int i=0;i<13;++i){auto r=positions[i];const LONG width=r.right-r.left,height=r.bottom-r.top;
            const LONG x=std::clamp(r.left,mi.rcWork.left,std::max(mi.rcWork.left,mi.rcWork.right-width));
            const LONG y=std::clamp(r.top,mi.rcWork.top,std::max(mi.rcWork.top,mi.rcWork.bottom-height));
            add(i<11?glyphIds[i]:i==11?"stick-left":"stick-right",i<11?glyphIds[i]:i==11?"左摇杆":"右摇杆",i,x,y,width,height);
        }
    }
    items=next;for(const auto& item:old)if(std::find(next.begin(),next.end(),item)==next.end()){dispose(item);
        bool pending=item->window||item->outline;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
        pending=pending||!item->surface.terminalReleased();
#endif
        if(pending)retired.push_back(item);
    }
    
    for(const auto& item:next){if(refreshEpoch!=epoch||!live.load()||displayTransitionFence.pending)return;const bool show=itemWanted(*item);const auto r=item->rect;
        if(!item->window)item->window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,L"YeManCustomTouch",L"YMCC 自定义触控",WS_POPUP,r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,item.get());
        if(!item->window){error=GetLastError();continue;}if(!item->outline)item->outline=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,L"YeManScreenTouchpadOutline",L"YMCC 自定义触控图形",WS_POPUP|WS_DISABLED,r.left,r.top,r.right-r.left,r.bottom-r.top,item->window,nullptr,instance,nullptr);
        EnableWindow(item->window,TRUE);SetWindowPos(item->window,HWND_TOPMOST,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE);
        if(item->kind==11||item->kind==12){HRGN circle=CreateEllipticRgn(0,0,r.right-r.left,r.bottom-r.top);if(!circle||!SetWindowRgn(item->window,circle,FALSE)){if(circle)DeleteObject(circle);error=GetLastError();}}
        bool ready=true;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
        if(nativeInputCandidateRequested&&show){for(int attempt=0;attempt<2;++attempt){ready=item->surface.root()==item->window?item->surface.sync():item->surface.attach(nativePadInputRuntime,item->window,item->kind,nativeSink,item.get());if(refreshEpoch!=epoch||!live.load())return;if(ready)break;const auto failure=item->surface.error;if(failure!=E_ABORT||attempt)recordNativeInputFailure(true,item->kind,item->surface,true);item->surface.close();if(refreshEpoch!=epoch||!live.load())return;if(failure!=E_ABORT||attempt){error=(DWORD)failure;break;}}}
        else item->surface.close();
#endif
        if(refreshEpoch!=epoch||!live.load()||displayTransitionFence.pending)return;const bool visualReady=draw(*item);
        if(show&&(!ready||!visualReady)){if(!error)error=ERROR_NOT_READY;windowError=error;}
        ShowWindow(item->window,show&&ready?SW_SHOWNOACTIVATE:SW_HIDE);if(item->outline)ShowWindow(item->outline,show&&ready&&visualReady&&(!autoHide.hidden||editing.load())?SW_SHOWNOACTIVATE:SW_HIDE);
    }
}
inline unsigned openSources(){unsigned n=0;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    for(const auto& group:{&items,&retired})for(const auto& item:*group)if(!item->surface.terminalReleased())++n;
#endif
    return n;}
inline bool needsRefresh(){
    if(!wantsInput())return false;
    if(items.empty())return inheritedMasterEnabled.load()&&!ymcc::screenpads::presentationSuspended.load()&&
        ((config["enabled"].get<bool>()&&!config["keys"].empty())||ymcc::screenpads::controlTargetEnabled.load());
    for(const auto& item:items)if(itemWanted(*item)){
        if(!item->window||!IsWindowVisible(item->window)||!item->outline||!IsWindow(item->outline)||!item->artworkReady||
           (!ymcc::screenpads::autoHide.hidden&&!IsWindowVisible(item->outline)))return true;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
        if(ymcc::screenpads::nativeInputCandidateRequested&&(item->surface.root()!=item->window||!item->surface.active()))return true;
#endif
    }return false;
}
inline bool inputReady(){
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    for(const auto& item:items)if(item->window&&IsWindowVisible(item->window))
        if(item->surface.root()!=item->window||!item->surface.active())return false;
#endif
    return true;
}
inline void cancelNativeInputs(){
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    const auto current=items;for(const auto& item:current)item->surface.cancel();
#endif
}
inline void hideForTransition(){
    for(const auto& item:items){if(item->window)ShowWindow(item->window,SW_HIDE);if(item->outline)ShowWindow(item->outline,SW_HIDE);}
}
template<class Current> inline bool retireOwnedResources(Current&& current){
    const auto epoch=refreshEpoch;const auto present=items,previous=retired;
    hideForTransition();release();if(!current()||epoch!=refreshEpoch)return false;
    for(const auto& group:{&present,&previous})for(const auto& item:*group){dispose(item);if(!current()||epoch!=refreshEpoch)return false;}
    if(openSources()||liveWindows()||heldKeys())return false;
    items.clear();retired.clear();return true;
}
template<class Current> inline bool probeControls(Current&& current,bool artwork){
    const auto epoch=refreshEpoch;const auto present=items;
    for(const auto& item:present)if(itemWanted(*item)){
        if(!current()||epoch!=refreshEpoch)return false;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
        if(ymcc::screenpads::nativeInputCandidateRequested){const bool ready=item->surface.sync();if(!current()||epoch!=refreshEpoch)return false;
            if(!ready){error=(DWORD)item->surface.error;ymcc::screenpads::windowError=error;ymcc::screenpads::recordNativeInputFailure(true,item->kind,item->surface,true);return false;}}
#endif
        if(artwork&&!ymcc::screenpads::autoHide.hidden&&!draw(*item))return false;
    }return current()&&epoch==refreshEpoch&&!needsRefresh();
}
template<class Current> inline bool detachInputsCurrent(const Current& stillCurrent){
    if(!stillCurrent())return false;
    release();if(!stillCurrent())return false;
    const auto epoch=refreshEpoch;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    for(const auto& group:{&items,&retired}){const auto current=*group;for(const auto& item:current){
        if(!stillCurrent())return false;
        item->surface.close();if(refreshEpoch!=epoch||!live.load()||!stillCurrent())return false;
    }}
#endif
    return openSources()==0;
}
inline bool detachInputs(){return detachInputsCurrent([]{return true;});}
inline unsigned liveWindows(){unsigned n=toolbar&&IsWindow(toolbar)?1:0;for(const auto& group:{&items,&retired})for(const auto& item:*group)n+=(item->window&&IsWindow(item->window)?1:0)+(item->outline&&IsWindow(item->outline)?1:0);return n;}
inline void retireEditorToolbar(){
    const HWND h=toolbar;if(!h)return;
    if(DestroyWindow(h)){if(toolbar==h)toolbar=nullptr;}
    else error=GetLastError();
}
inline void refreshAfterEditing(){if(!resumeDisplayAfterEditing||!resumeDisplayAfterEditing())ymcc::screenpads::refresh();}
inline bool finish(bool commit,bool restoreOwner=true){
    if(!editing.load())return false;Json expected=config;expected["positions"]=originalPositions;
    if(commit&&save&&!save(config,expected)){error=ERROR_WRITE_FAULT;if(editorLabel)SetWindowTextW(editorLabel,L"保存失败，请重试或取消");return false;}
    if(!commit)config["positions"]=originalPositions;
    editing.store(false);editorWindow.store(nullptr);++editorTicket;dragContact=0;dragId.clear();release();
    retireEditorToolbar();editorLabel=nullptr;
    refreshAfterEditing();if(restoreOwner&&showOwner)showOwner(true);if(changed)changed();return true;
}
inline void editorAction(const char* action){
    if(!editing.load()||!action)return;const std::string value(action);const WPARAM code=value=="confirm"?1:value=="back"?2:value=="edit-game"?3:value=="page-prev"?4:value=="page-next"?5:value=="nav-left"?6:value=="nav-right"?7:value=="nav-up"?8:value=="nav-down"?9:0;
    if(code)if(HWND h=editorWindow.load())PostMessageW(h,kEditorMessage,code,(LPARAM)editorTicket);
}
inline void editorButtons(WORD buttons,short x,short y,ULONGLONG now){
    if(!editing.load())return;
    if(ymcc::screenpads::inputPowerAllowed&&!ymcc::screenpads::inputPowerAllowed())return;
    if(editorArming){editorPrevious=buttons;if(!buttons&&std::abs((int)x)<14000&&std::abs((int)y)<14000)editorArming=false;return;}
    const WORD pressed=buttons&~editorPrevious;editorPrevious=buttons;
    if(pressed&0x1000)editorAction("confirm");else if(pressed&0x2000)editorAction("back");
    else if(pressed&0x4000)editorAction("edit-game");else if(pressed&0x0100)editorAction("page-prev");else if(pressed&0x0200)editorAction("page-next");
    const int direction=(buttons&4)||x< -14000?6:(buttons&8)||x>14000?7:(buttons&1)||y>14000?8:(buttons&2)||y< -14000?9:0;
    if(direction&&(direction!=editorDirection||now>=editorRepeatAt)){if(HWND h=editorWindow.load())PostMessageW(h,kEditorMessage,direction,(LPARAM)editorTicket);editorRepeatAt=now+(direction!=editorDirection?300:90);}editorDirection=direction;
}
inline void editorCommand(unsigned command){
    if(!editing.load())return;if(command==1){finish(true);return;}if(command==2){finish(false);return;}
    if(command==3){config["positions"]=Json::object();ymcc::screenpads::refresh();return;}
    std::vector<std::string> ids;for(const char* id:{"summon-y","pad-left","pad-right","native-steam","native-qam"})if(HWND h=findWindow(id))if(IsWindowVisible(h))ids.emplace_back(id);for(const auto& item:items)ids.push_back(item->id);if(ids.empty())return;
    auto found=std::find(ids.begin(),ids.end(),selected);size_t index=found==ids.end()?0:(size_t)std::distance(ids.begin(),found);
    if(command==4||command==5){index=(index+ids.size()+(command==4?-1:1))%ids.size();selected=ids[index];labelSelection();return;}
    selected=ids[index];RECT rect{};if(GetWindowRect(findWindow(selected),&rect))moveWindow(selected,rect.left+(command==6?-3:command==7?3:0),rect.top+(command==8?-3:command==9?3:0));labelSelection();
}
// The same YMCC tokens used by the reviewed preview, drawn on the existing UI thread.
inline int editorLayoutDpi(const RECT& work,UINT dpi){return std::max(48,std::min((int)std::max(96u,dpi),MulDiv(std::max(1L,work.right-work.left-48),96,760)));}
inline RECT editorBounds(const RECT& work,UINT dpi){
    const int d=editorLayoutDpi(work,dpi),margin=MulDiv(24,d,96);
    const LONG width=std::min((LONG)MulDiv(760,d,96),std::max(1L,work.right-work.left-2*margin));
    const LONG height=std::min((LONG)MulDiv(68,d,96),std::max(1L,work.bottom-work.top-2*margin));
    const LONG x=work.left+(work.right-work.left-width)/2,y=std::max(work.top+margin,work.bottom-margin-height);
    return {x,y,x+width,y+height};
}
inline void editorRoundBox(HDC dc,const RECT& r,COLORREF fill,COLORREF line,int radius,int border=1){
    HBRUSH brush=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,border,line);
    const auto previousBrush=SelectObject(dc,brush),previousPen=SelectObject(dc,pen);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
    SelectObject(dc,previousPen);SelectObject(dc,previousBrush);DeleteObject(pen);DeleteObject(brush);
}
inline void arrangeEditor(){
    if(!toolbar)return;MONITORINFO mi{sizeof(mi)};
    if(!GetMonitorInfoW(MonitorFromWindow(ymcc::screenpads::owner.load(),MONITOR_DEFAULTTOPRIMARY),&mi))return;
    const UINT dpi=GetDpiForWindow(ymcc::screenpads::owner.load());const int d=editorLayoutDpi(mi.rcWork,dpi);
    if(d!=editorDpi||!editorFont||!editorTitleFont){
        if(editorFont)DeleteObject(editorFont);if(editorTitleFont)DeleteObject(editorTitleFont);editorDpi=d;
        editorFont=CreateFontW(-MulDiv(12,d,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        editorTitleFont=CreateFontW(-MulDiv(15,d,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    }
    const auto r=editorBounds(mi.rcWork,dpi);const int width=r.right-r.left,height=r.bottom-r.top;
    SetWindowPos(toolbar,HWND_TOPMOST,r.left,r.top,width,height,SWP_NOACTIVATE);
    const auto px=[&](int value){return MulDiv(value,d,96);};const int labelWidth=width-px(420);
    MoveWindow(editorLabel,px(16),px(10),std::max(1,labelWidth),height-px(20),TRUE);
    int x=px(28)+labelWidth;
    for(int id:{3,2,1}){const int w=px(id==3?164:94);HWND control=GetDlgItem(toolbar,id);MoveWindow(control,x,px(14),w,height-px(28),TRUE);SendMessageW(control,WM_SETFONT,(WPARAM)editorFont,FALSE);x+=w+px(12);}
    HRGN region=CreateRoundRectRgn(0,0,width+1,height+1,px(20),px(20));if(region&&!SetWindowRgn(toolbar,region,TRUE))DeleteObject(region);
    InvalidateRect(toolbar,nullptr,TRUE);
}
inline LRESULT CALLBACK editorProc(HWND h,UINT message,WPARAM w,LPARAM l){
    if(message==WM_COMMAND){editorCommand(LOWORD(w));return 0;}if(message==kEditorMessage){if((unsigned long)l==editorTicket)editorCommand((unsigned)w);return 0;}
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PRINTCLIENT){RECT r{};GetClientRect(h,&r);editorRoundBox((HDC)w,r,editorColors.panel,editorColors.line,MulDiv(20,editorDpi,96));return 0;}
    if(message==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);editorRoundBox(dc,r,editorColors.panel,editorColors.line,MulDiv(20,editorDpi,96));EndPaint(h,&ps);return 0;}
    if(message==WM_DRAWITEM){const auto& item=*(DRAWITEMSTRUCT*)l;const auto dc=item.hDC;RECT r=item.rcItem;SetBkMode(dc,TRANSPARENT);
        const auto previous=SelectObject(dc,editorFont);wchar_t text[512]{};GetWindowTextW(item.hwndItem,text,512);
        if(item.CtlType==ODT_STATIC){
            SetDCBrushColor(dc,editorColors.panel);FillRect(dc,&r,(HBRUSH)GetStockObject(DC_BRUSH));
            RECT title=r;title.bottom=title.top+MulDiv(23,editorDpi,96);SelectObject(dc,editorTitleFont);SetTextColor(dc,editorColors.text);DrawTextW(dc,L"调整触控位置",-1,&title,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            SelectObject(dc,editorFont);r.top=title.bottom;SetTextColor(dc,editorColors.dim);DrawTextW(dc,text,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }else{
            const bool focus=(item.itemState&ODS_FOCUS)!=0,pressed=(item.itemState&ODS_SELECTED)!=0,saveButton=item.CtlID==1;
            const COLORREF border=focus||saveButton?editorColors.accent:editorColors.line;
            const COLORREF fill=pressed?editorColors.panel:editorColors.input;
            SetDCBrushColor(dc,editorColors.panel);FillRect(dc,&r,(HBRUSH)GetStockObject(DC_BRUSH));
            editorRoundBox(dc,r,fill,border,MulDiv(14,editorDpi,96),focus?2:1);SetTextColor(dc,saveButton?editorColors.accent:editorColors.text);
            DrawTextW(dc,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        SelectObject(dc,previous);return TRUE;
    }
    if(message==WM_SETTINGCHANGE||message==WM_DPICHANGED){arrangeEditor();return 0;}
    if(message==WM_NCDESTROY&&h==toolbar){
        toolbar=nullptr;editorLabel=nullptr;
        if(editorFont)DeleteObject(editorFont);if(editorTitleFont)DeleteObject(editorTitleFont);editorFont=editorTitleFont=nullptr;
    }
    if(message==WM_CLOSE){finish(false);return 0;}if(message==WM_KEYDOWN){const unsigned action=w==VK_RETURN?1:w==VK_ESCAPE?2:w=='R'?3:w==VK_PRIOR?4:w==VK_NEXT?5:w==VK_LEFT?6:w==VK_RIGHT?7:w==VK_UP?8:w==VK_DOWN?9:0;if(action){editorCommand(action);return 0;}}
    return DefWindowProcW(h,message,w,l);
}
inline bool begin(){
    if(!live.load()||editing.load()||ymcc::screenpads::inputTeardown||!ymcc::screenpads::sharedInputAllowed())return false;
    originalPositions=config["positions"];ymcc::screenpads::release();editing.store(true);ymcc::screenpads::autoHideSync();ymcc::screenpads::refresh();++editorTicket;editorArming=true;editorPrevious=0;editorDirection=0;selected=config["keys"].empty()?"summon-y":config["keys"][0]["id"].get<std::string>();
    WNDCLASSW wc{};wc.lpfnWndProc=editorProc;wc.hInstance=ymcc::screenpads::instance;wc.lpszClassName=L"YeManCustomTouchEditor";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){const auto failure=GetLastError();finish(false,false);error=failure;return false;}
    toolbar=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,wc.lpszClassName,L"调整触控位置",WS_POPUP,0,0,1,1,nullptr,nullptr,wc.hInstance,nullptr);
    if(!toolbar){const DWORD failure=GetLastError();editing.store(false);refreshAfterEditing();error=failure;return false;}editorWindow.store(toolbar);
    editorLabel=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_OWNERDRAW,0,0,1,1,toolbar,(HMENU)(INT_PTR)100,wc.hInstance,nullptr);
    const wchar_t* labels[]={L"A 保存",L"B 取消",L"X 重置默认位置"};bool controlsReady=editorLabel!=nullptr;
    for(int i=0;i<3;++i)controlsReady=CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,toolbar,(HMENU)(INT_PTR)(i+1),wc.hInstance,nullptr)!=nullptr&&controlsReady;
    if(!controlsReady){const auto failure=GetLastError();finish(false,false);error=failure?failure:ERROR_NOT_ENOUGH_MEMORY;return false;}
    arrangeEditor();labelSelection();if(showOwner)showOwner(false);ShowWindow(toolbar,SW_SHOWNORMAL);if(changed)changed();return true;
}
inline Json state(){const bool target=targetAllowed();bool ready=false;{std::lock_guard<std::mutex> lock(padMutex);ready=transportReady;}return {{"ok",error==0&&live.load()},{"scope","global"},{"config",config},{"editing",editing.load()},
    {"outputReady",target&&ready},{"settingsSaved",false},{"error",error},{"reason",error?"native-custom-touch-failed":""}};}
inline bool configure(const Json& value){
    if(!live.load()||ymcc::screenpads::inputTeardown||editing.load())return false;Json next;if(!parse(value,defaults(),next))return false;
    Json appearance=next;appearance["template"]=config["template"];if(next["template"]!=config["template"]&&appearance==config){config=std::move(next);error=0;for(auto& item:items)draw(*item);return true;}
    release();config=std::move(next);error=0;gamepadEnabled.store(config["gamepad"].get<bool>(),std::memory_order_release);refresh();ymcc::screenpads::autoHideSync(true);if(ymcc::screenpads::visibilityRefreshObserver)ymcc::screenpads::visibilityRefreshObserver();return true;
}
inline void initialize(const Json& value){
    WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=ymcc::screenpads::instance;wc.lpszClassName=L"YeManCustomTouch";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){error=GetLastError();return;}live.store(true);if(!configure(value)){config=defaults();config["enabled"]=false;error=ERROR_INVALID_DATA;}
}
inline void quiesce(){
    // Editor dismissal and key release may re-enter the owner. Make their
    // windows inert before either can run.
    if(toolbar&&IsWindow(toolbar)){ShowWindow(toolbar,SW_HIDE);EnableWindow(toolbar,FALSE);}
    const auto current=items;
    for(const auto& item:current){
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
        item->surface.quiesce();
#endif
        if(item->window&&IsWindow(item->window)){ShowWindow(item->window,SW_HIDE);EnableWindow(item->window,FALSE);}
        if(item->outline)ShowWindow(item->outline,SW_HIDE);
    }
    if(editing.load())finish(false,false);
    release();live.store(false);gamepadEnabled.store(false);
}
inline void drain(){if(live.load())return;sendDesired({});retireEditorToolbar();for(const auto& group:{&items,&retired}){const auto current=*group;for(const auto& item:current)dispose(item);}if(!liveWindows()&&!openSources()){items.clear();retired.clear();}}
inline void shutdown(){quiesce();drain();}
}
