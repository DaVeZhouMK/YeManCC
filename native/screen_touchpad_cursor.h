#pragma once
// Included inside ymcc::screenpads, after Core/globals. This is a visual-only
// fallback for Windows' CURSOR_SUPPRESSED state during a mouse/Deck gesture.
// No ShowCursor counter changes, mouse injection, hook, timer or worker.
inline constexpr UINT kCursorSyncMessage=WM_APP+174;
inline std::atomic<bool> cursorTracking{false},cursorRetained{false},cursorSyncPending{false};
inline constexpr ULONGLONG kCursorHandoffMs=80,kCursorRetainedPollMs=24;
inline std::atomic<ULONGLONG> cursorNextRetainedPoll{0};
inline bool cursorShowingObserved=false;
inline ULONGLONG cursorShowingSince=0;
inline unsigned cursorShowingSamples=0;
inline std::atomic<HWND> cursorDispatchWindow{nullptr};
inline HWND cursorWindow=nullptr;
inline HCURSOR cursorShape=nullptr;
inline POINT cursorHotspot{};
inline SIZE cursorSize{};
inline bool cursorVisible=false;
inline unsigned long long cursorUploads=0,cursorSyncs=0,cursorShows=0,cursorHides=0,cursorHandoffs=0;
inline DWORD cursorError=0;
inline bool cursorFallbackRequired(bool tracking,DWORD flags,HCURSOR shape) {
    // Do not draw over a visible system cursor or an explicitly hidden one.
    return tracking && shape && (flags&CURSOR_SUPPRESSED) && !(flags&CURSOR_SHOWING);
}
inline LRESULT CALLBACK cursorWindowProc(HWND h,UINT m,WPARAM w,LPARAM lp) {
    switch(m) {
    case WM_SETCURSOR:return TRUE; // Never replace the underlying application cursor.
    case WM_NCHITTEST:return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_POINTERACTIVATE:return PA_NOACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    }return DefWindowProcW(h,m,w,lp);
}
inline void cursorResetHandoff() {cursorShowingObserved=false;cursorShowingSince=0;cursorShowingSamples=0;}
inline void cursorHide() {
    if(cursorWindow && cursorVisible){ShowWindow(cursorWindow,SW_HIDE);++cursorHides;}
    cursorVisible=false;cursorResetHandoff();
    cursorRetained.store(false,std::memory_order_release);cursorNextRetainedPoll.store(0,std::memory_order_relaxed);
    if(!cursorTracking.load(std::memory_order_acquire))cursorDispatchWindow.store(nullptr,std::memory_order_release);
}
inline void cursorStop() {
    cursorTracking.store(false,std::memory_order_release);
    cursorDispatchWindow.store(nullptr,std::memory_order_release);
    cursorSyncPending.store(false,std::memory_order_release);
    cursorHide();
}
inline void cursorShutdown() {
    cursorStop();if(cursorWindow)DestroyWindow(cursorWindow);
    cursorWindow=nullptr;cursorShape=nullptr;cursorSize={};cursorHotspot={};
}
inline void cursorRequestSync() {
    // The existing BUS can call this even in simulated-mouse mode. Never touch
    // HWND/GDI on the BUS thread; coalesce to at most one UI message.
    if(!cursorTracking.load(std::memory_order_acquire)) {
        if(!cursorRetained.load(std::memory_order_acquire))return;
        // A lifted finger may leave Windows suppressed. Keep the last visual,
        // checking only on the existing BUS, throttled while it is passive.
        const auto now=GetTickCount64();if(now<cursorNextRetainedPoll.load(std::memory_order_relaxed))return;
        cursorNextRetainedPoll.store(now+kCursorRetainedPollMs,std::memory_order_relaxed);
    }
    if(cursorSyncPending.exchange(true,std::memory_order_acq_rel))return;
    HWND h=cursorDispatchWindow.load(std::memory_order_acquire);
    if(!h || !PostMessageW(h,kCursorSyncMessage,0,0))cursorSyncPending.store(false,std::memory_order_release);
}
inline bool cursorUpload(const CURSORINFO& ci) {
    ICONINFO icon{};if(!GetIconInfo(ci.hCursor,&icon)){cursorError=GetLastError();return false;}
    BITMAP bitmap{};const HBITMAP source=icon.hbmColor?icon.hbmColor:icon.hbmMask;
    const bool measured=source && GetObjectW(source,sizeof(bitmap),&bitmap);
    const LONG width=measured?bitmap.bmWidth:0,height=measured?(icon.hbmColor?bitmap.bmHeight:bitmap.bmHeight/2):0;
    if(width<=0 || height<=0 || width>512 || height>512) {
        if(icon.hbmColor)DeleteObject(icon.hbmColor);if(icon.hbmMask)DeleteObject(icon.hbmMask);
        cursorError=ERROR_INVALID_DATA;return false;
    }
    if(!cursorWindow) {
        WNDCLASSW wc{};wc.lpfnWndProc=cursorWindowProc;wc.hInstance=instance;wc.lpszClassName=L"YeManScreenTouchpadCursor";
        if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) {
            if(icon.hbmColor)DeleteObject(icon.hbmColor);if(icon.hbmMask)DeleteObject(icon.hbmMask);
            cursorError=GetLastError();return false;
        }
        cursorWindow=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,
            wc.lpszClassName,L"YeManCC Touchpad Cursor",WS_POPUP,0,0,width,height,nullptr,nullptr,instance,nullptr);
        if(!cursorWindow) {if(icon.hbmColor)DeleteObject(icon.hbmColor);if(icon.hbmMask)DeleteObject(icon.hbmMask);cursorError=GetLastError();return false;}
    }
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr),blackDC=screen?CreateCompatibleDC(screen):nullptr,whiteDC=screen?CreateCompatibleDC(screen):nullptr;
    void *blackBits=nullptr,*whiteBits=nullptr;
    HBITMAP black=screen?CreateDIBSection(screen,&info,DIB_RGB_COLORS,&blackBits,nullptr,0):nullptr;
    HBITMAP white=screen?CreateDIBSection(screen,&info,DIB_RGB_COLORS,&whiteBits,nullptr,0):nullptr;
    bool ok=false;HGDIOBJ oldBlack=nullptr,oldWhite=nullptr;
    if(screen && blackDC && whiteDC && black && white && blackBits && whiteBits) {
        oldBlack=SelectObject(blackDC,black);oldWhite=SelectObject(whiteDC,white);
        auto b=static_cast<DWORD*>(blackBits),w=static_cast<DWORD*>(whiteBits);
        const size_t count=(size_t)width*height;std::fill_n(b,count,0u);std::fill_n(w,count,0x00ffffffu);
        // Rendering against black/white reconstructs premultiplied alpha for
        // both modern alpha cursors and classic monochrome AND/XOR masks.
        if(DrawIconEx(blackDC,0,0,ci.hCursor,width,height,0,nullptr,DI_NORMAL) &&
           DrawIconEx(whiteDC,0,0,ci.hCursor,width,height,0,nullptr,DI_NORMAL)) {
            GdiFlush();
            for(size_t i=0;i<count;i++) {
                int difference=0;for(int shift:{0,8,16})difference=std::max(difference,(int)((w[i]>>shift)&255)-(int)((b[i]>>shift)&255));
                b[i]=(b[i]&0x00ffffffu)|((DWORD)(255-difference)<<24);
            }
            POINT destination{ci.ptScreenPos.x-(LONG)icon.xHotspot,ci.ptScreenPos.y-(LONG)icon.yHotspot},origin{};
            SIZE size{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
            ok=UpdateLayeredWindow(cursorWindow,screen,&destination,&size,blackDC,&origin,0,&blend,ULW_ALPHA)!=FALSE;
            if(ok){cursorShape=ci.hCursor;cursorHotspot={(LONG)icon.xHotspot,(LONG)icon.yHotspot};cursorSize=size;++cursorUploads;cursorError=0;}
        }
    }
    if(!ok){cursorError=GetLastError();if(!cursorError)cursorError=ERROR_NOT_ENOUGH_MEMORY;}
    if(oldBlack)SelectObject(blackDC,oldBlack);if(oldWhite)SelectObject(whiteDC,oldWhite);
    if(black)DeleteObject(black);if(white)DeleteObject(white);
    if(blackDC)DeleteDC(blackDC);if(whiteDC)DeleteDC(whiteDC);if(screen)ReleaseDC(nullptr,screen);
    if(icon.hbmColor)DeleteObject(icon.hbmColor);if(icon.hbmMask)DeleteObject(icon.hbmMask);
    return ok;
}
inline void cursorSyncInfo(CURSORINFO ci,ULONGLONG now=GetTickCount64()) {
    ++cursorSyncs;
    const bool active=cursorTracking.load(std::memory_order_acquire);
    const bool retained=cursorRetained.load(std::memory_order_acquire);
    if(!active && !retained){cursorHide();return;}
    const bool suppressed=(ci.flags&CURSOR_SUPPRESSED) && !(ci.flags&CURSOR_SHOWING);
    if(suppressed)cursorResetHandoff();
    else if(ci.flags&CURSOR_SHOWING) {
        if(!cursorVisible){cursorHide();return;}
        // A mouse output can flip SHOWING for a single sample, then the next
        // touch flips it back. Keep the same HWND at the current hotspot until
        // SHOWING is stable. This affects drawing only, never input latency.
        // Once the finger is lifted, a real mouse gets an immediate handoff.
        if(!active){++cursorHandoffs;cursorHide();return;}
        if(!cursorShowingObserved){cursorShowingObserved=true;cursorShowingSince=now;cursorShowingSamples=1;}
        else if(cursorShowingSamples<2)++cursorShowingSamples;
        if(cursorShowingSamples>=2 && now-cursorShowingSince>=kCursorHandoffMs) {
            ++cursorHandoffs;cursorHide();return;
        }
    } else {cursorHide();return;} // Respect explicit application hiding (flags=0).
    // Suppression can clear hCursor. During a handoff keep the cached bitmap
    // instead of blinking just because Windows momentarily returns NULL.
    if(!ci.hCursor){ci.hCursor=cursorShape;if(!ci.hCursor)ci.hCursor=GetCursor();if(!ci.hCursor)ci.hCursor=LoadCursorW(nullptr,IDC_ARROW);}
    if(!ci.hCursor){cursorHide();return;}
    if((!cursorWindow || cursorShape!=ci.hCursor) && !cursorUpload(ci)) {
        // A transient shape/upload failure must not erase the last good frame.
        // A later sync retries; explicit hide/stop always wins over this cache.
        if(!cursorWindow || !cursorShape){cursorHide();return;}
    }
    RECT current{};const LONG x=ci.ptScreenPos.x-cursorHotspot.x,y=ci.ptScreenPos.y-cursorHotspot.y;
    if(!cursorVisible || !GetWindowRect(cursorWindow,&current) || current.left!=x || current.top!=y) {
        if(!SetWindowPos(cursorWindow,HWND_TOPMOST,x,y,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW)) {
            cursorError=GetLastError();return; // Retain last valid visual if possible.
        }
    }
    if(!cursorVisible)++cursorShows;
    cursorVisible=true;
}
inline void cursorSync() {
    CURSORINFO ci{sizeof(ci)};if(!GetCursorInfo(&ci))return; // Transient reads do not clear the last good frame.
    cursorSyncInfo(ci);
}
inline bool cursorRetainAfterUp(HWND dispatch,const CURSORINFO& ci) {
    if(!cursorTracking.load(std::memory_order_acquire) || !(ci.flags&CURSOR_SUPPRESSED) || (ci.flags&CURSOR_SHOWING))return false;
    cursorTracking.store(false,std::memory_order_release);cursorRetained.store(true,std::memory_order_release);
    cursorDispatchWindow.store(dispatch,std::memory_order_release);cursorNextRetainedPoll.store(0,std::memory_order_relaxed);
    cursorSyncInfo(ci);cursorRequestSync();return true;
}
inline void cursorContactChanged(HWND dispatch,bool normalUp=false) {
    bool tracking=false;{std::lock_guard<std::mutex> lock(mutex);
        for(int i=0;i<2;i++)tracking=tracking || (core.pads[i].down && (core.mode(i)==Mode::Mouse || core.mode(i)==Mode::Deck));}
    if(!tracking) {
        CURSORINFO ci{sizeof(ci)};
        if(normalUp && GetCursorInfo(&ci) && cursorRetainAfterUp(dispatch,ci))return;
        cursorStop();return;
    }
    cursorRetained.store(false,std::memory_order_release);cursorNextRetainedPoll.store(0,std::memory_order_relaxed);
    cursorDispatchWindow.store(dispatch,std::memory_order_release);cursorTracking.store(true,std::memory_order_release);
    cursorSync();cursorRequestSync();
}


