#pragma once
// Included inside screenpads after its drawing/input state declarations.
// At most two cached visual-only windows: no timer, input injection or hit testing.
// User reference: Windows ring is about 80px. The visible ring is 130% of
// that diameter at 100% pad scale; only its outside has a soft halo.
inline constexpr int kContactCoreDiameter=104,kContactGlowDiameter=144;
struct ContactVisual {
    HWND window=nullptr;OutlineBitmap image{};int surface=-1,diameter=0,alpha=-1;
    POINT position{};bool uploaded=false,visible=false;
};
inline std::array<ContactVisual,2> contactVisuals{};
inline unsigned long long contactVisualUploads=0,contactVisualMoves=0;
// Owner-thread root geometry only. Visibility remains a live check on every
// dispatch. The root window procedure invalidates this on geometry/lifecycle
// notifications; force refresh always reads the authoritative HWND rectangle.
struct ContactGeometry {HWND input=nullptr;RECT rect{};bool valid=false;};
inline std::array<ContactGeometry,2> contactGeometries{};
inline unsigned long long contactGeometryReads=0,contactGeometryHits=0,contactGeometryInvalidations=0;
inline void invalidateContactGeometry(HWND input) {
    for(auto& geometry:contactGeometries)if(geometry.input==input&&geometry.valid){geometry.valid=false;++contactGeometryInvalidations;}
}
inline bool contactGeometryRect(int side,HWND input,RECT& rect,bool force) {
    auto& geometry=contactGeometries[side];
    if(force||!geometry.valid||geometry.input!=input){
        RECT measured{};++contactGeometryReads;
        if(!GetWindowRect(input,&measured)){geometry.valid=false;return false;}
        geometry={input,measured,true};
    }else ++contactGeometryHits;
    rect=geometry.rect;return true;
}
inline LRESULT CALLBACK contactVisualProc(HWND h,UINT message,WPARAM w,LPARAM lp) {
    if(message==WM_NCCREATE) {
        SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);return TRUE;
    }
    switch(message) {
    case WM_STYLECHANGED:case WM_ENABLE: {
        auto* visual=reinterpret_cast<ContactVisual*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        const bool lost=message==WM_STYLECHANGED && w==GWL_EXSTYLE && lp &&
            !(reinterpret_cast<const STYLESTRUCT*>(lp)->styleNew&WS_EX_LAYERED);
        if(lost && visual)visual->uploaded=false;
        if(!ymcc::screeninput::ensureDisplay(h) || lost) {
            ShowWindow(h,SW_HIDE);if(visual)visual->visible=false;
        }
        return 0;
    }
    case WM_NCDESTROY: {
        auto* visual=reinterpret_cast<ContactVisual*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(visual){visual->window=nullptr;visual->uploaded=false;visual->visible=false;}
        SetWindowLongPtrW(h,GWLP_USERDATA,0);break;
    }
    case WM_NCHITTEST:return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_POINTERACTIVATE:return PA_NOACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    }
    return DefWindowProcW(h,message,w,lp);
}
inline void hideContactVisual(ContactVisual& visual) {
    if(visual.visible && visual.window)ShowWindow(visual.window,SW_HIDE);
    visual.visible=false;
}
inline void closeContactVisual(ContactVisual& visual) {
    hideContactVisual(visual);
    if(visual.window)DestroyWindow(visual.window);
    visual.window=nullptr;visual.image.reset();visual.uploaded=false;
    visual.surface=-1;visual.diameter=0;visual.alpha=-1;
}
inline void shutdownContactVisuals() {for(auto& visual:contactVisuals)closeContactVisual(visual);contactGeometries={};}
inline bool buildContactVisual(ContactVisual& visual,int diameter) {
    auto& image=visual.image;
    if(image.dc && image.width==diameter)return true;
    image.reset();image.dc=CreateCompatibleDC(nullptr);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=diameter;
    info.bmiHeader.biHeight=-diameter;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    if(image.dc)image.bitmap=CreateDIBSection(image.dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!image.dc || !image.bitmap || !bits){image.reset();return false;}
    const auto previous=SelectObject(image.dc,image.bitmap);
    if(!previous || previous==HGDI_ERROR){image.reset();return false;}
    image.previous=previous;image.pixels=static_cast<DWORD*>(bits);image.width=image.height=diameter;
    const double radius=diameter/2.0,ringRadius=radius*kContactCoreDiameter/kContactGlowDiameter;
    const double haloWidth=radius-ringRadius;
    const double ringHalfWidth=std::max(1.0,1.5*diameter/kContactGlowDiameter);
    for(int y=0;y<diameter;++y)for(int x=0;x<diameter;++x) {
        const double dx=x+.5-radius,dy=y+.5-radius,dist=std::sqrt(dx*dx+dy*dy);
        // Only the ring and its outside halo have alpha; the centre stays clear.
        const double glow=dist>=ringRadius?std::clamp((radius-dist)/haloWidth,0.0,1.0):0.0;
        const double ring=std::clamp(ringHalfWidth+.5-std::abs(dist-ringRadius),0.0,1.0);
        const DWORD alpha=(DWORD)std::lround(std::max(210*glow*glow*(3-2*glow),245*ring));
        image.pixels[y*diameter+x]=(alpha<<24)|(alpha<<16)|(alpha<<8)|alpha;
    }
    visual.uploaded=false;return true;
}
inline void syncContactVisuals(int surface,bool force) {
    struct Point {bool active=false;int surface=0;float x=0,y=0;};
    std::array<Point,2> points{};Config cfg;
    {std::lock_guard<std::mutex> l(mutex);cfg=core.config;
        const bool shared=cfg.layout==Layout::Single && cfg.single==Mode::DualSense;
        if(cfg.enabled && !autoHide.hidden)for(int i=0;i<2;++i) {
            const auto& p=core.pads[i];const auto& feedback=feedbackOnlyContacts.contacts[i];
            if(p.down)points[i]={true,shared?0:i,p.x,p.y};
            else if(feedback.active)points[i]={true,feedback.surface,feedback.x,feedback.y};
        }
    }
    const int alpha=outlineAlpha(outlineFeedbackTransparency(cfg.transparency,true));
    const int diameter=MulDiv(kContactGlowDiameter,std::clamp(cfg.scale,50,200),100);
    // Two DualSense contacts can share one input surface. Query visibility
    // once per dispatch; stable geometry follows the root's event invalidation.
    struct SurfaceView {RECT rect{};bool checked=false,ready=false;};
    std::array<SurfaceView,2> surfaces{};
    for(int i=0;i<2;++i) {
        auto& visual=contactVisuals[i];const auto& point=points[i];
        if(surface>=0 && point.surface!=surface && visual.surface!=surface)continue;
        const int side=point.surface;
        if(!point.active || side<0 || side>1 || !alpha || !permitted(surfaceMode(cfg,side))) {hideContactVisual(visual);continue;}
        auto& view=surfaces[side];
        if(!view.checked) {
            view.checked=true;
            view.ready=windows[side] && outlines[side] && IsWindowVisible(windows[side]) &&
                IsWindowVisible(outlines[side]) && contactGeometryRect(side,windows[side],view.rect,force);
        }
        if(!view.ready){hideContactVisual(visual);continue;}
        const auto& rect=view.rect;
        if(rect.right-rect.left<diameter || rect.bottom-rect.top<diameter){hideContactVisual(visual);continue;}
        const int px=std::clamp((int)std::lround(point.x)-diameter/2,0,(int)(rect.right-rect.left)-diameter);
        const int py=std::clamp((int)std::lround(point.y)-diameter/2,0,(int)(rect.bottom-rect.top)-diameter);
        POINT position{rect.left+px,rect.top+py};
        if(visual.window && visual.surface!=side)closeContactVisual(visual);
        if(!visual.window) {
            WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=contactVisualProc;wc.lpszClassName=L"YeManScreenTouchContact";
            if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){windowError=GetLastError();continue;}
            visual.surface=side;
            visual.window=CreateWindowExW(WS_EX_TOPMOST|ymcc::screeninput::displayExStyle,wc.lpszClassName,L"",
                WS_POPUP|WS_DISABLED,position.x,position.y,diameter,diameter,windows[side],nullptr,instance,&visual);
            if(!visual.window){windowError=GetLastError();continue;}
        }
        const bool needsUpload=force || !visual.uploaded || visual.diameter!=diameter || visual.alpha!=alpha;
        const bool needsShow=!visual.visible || force || surface<0;
        // WM_STYLECHANGED / WM_ENABLE repair the inert display policy immediately.
        // Revalidate before showing, uploading or explicitly raising it; ordinary
        // position-only MOVE does not need the same User32 style queries again.
        if(((needsUpload || needsShow) && !ymcc::screeninput::ensureDisplay(visual.window)) ||
            (needsUpload && !buildContactVisual(visual,diameter))) {
            windowError=GetLastError();if(!windowError)windowError=ERROR_NOT_ENOUGH_MEMORY;
            hideContactVisual(visual);continue;
        }
        if(needsUpload) {
            POINT origin{};SIZE size{diameter,diameter};BLENDFUNCTION blend{AC_SRC_OVER,0,(BYTE)alpha,AC_SRC_ALPHA};
            if(!UpdateLayeredWindow(visual.window,nullptr,&position,&size,visual.image.dc,&origin,0,&blend,ULW_ALPHA)) {
                windowError=GetLastError();visual.uploaded=false;hideContactVisual(visual);continue;
            }
            visual.position=position;visual.diameter=diameter;visual.alpha=alpha;visual.uploaded=true;++contactVisualUploads;
        } else if(visual.position.x!=position.x || visual.position.y!=position.y) {
            if(!SetWindowPos(visual.window,nullptr,position.x,position.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE)) {
                windowError=GetLastError();hideContactVisual(visual);continue;
            }
            visual.position=position;++contactVisualMoves;
        }
        // A full surface refresh may have raised its sibling outline. Restore
        // this marker above it without doing z-order work on each MOVE.
        if(!visual.visible || force || surface<0) {
            if(!SetWindowPos(visual.window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW)) {
                windowError=GetLastError();continue;
            }
            visual.visible=true;
            // Showing an owned marker can raise its input/outline group above
            // a retained cursor even when the cursor's XY did not change.
            // The existing contact sync follows this call. Invalidate only on
            // a successful show/raise, so it rechecks that known stack change;
            // ordinary MOVE keeps its position-only cache and original budget.
            if(cursorVisible)cursorPositionValid=false;
        }
    }
}