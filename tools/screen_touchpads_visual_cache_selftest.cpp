#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "screen_touchpads.h"
#include <iostream>
#include <dwmapi.h>
#pragma comment(lib,"dwmapi.lib")
using namespace ymcc::screenpads;
static void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE))DispatchMessageW(&m);}
static std::vector<DWORD> captureOwnFrame(HWND h) {
 DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid!=GetCurrentProcessId())return {};
 RECT r{};GetWindowRect(h,&r);const LONG width=r.right-r.left,height=r.bottom-r.top;
 HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
 info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);std::vector<DWORD> pixels;
 if(bitmap&&bits){auto old=SelectObject(memory,bitmap);DwmFlush();
  if(BitBlt(memory,0,0,width,height,screen,r.left,r.top,SRCCOPY|CAPTUREBLT))pixels.assign((DWORD*)bits,(DWORD*)bits+width*height);
  SelectObject(memory,old);}
 if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);if(screen)ReleaseDC(nullptr,screen);return pixels;
}
int main(int argc,char** argv){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
 if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))||!nativeInputRuntime.ensure())return 3;
 setNativeInputCandidate(true);
#endif
 // Warm only the helper's normal Win32 background-paint/capture path before
 // measuring handles; stock DC objects are lazily initialized by USER/GDI.
 const DWORD coldGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 WNDCLASSW warmClass{};warmClass.hInstance=GetModuleHandleW(nullptr);warmClass.lpfnWndProc=DefWindowProcW;
 warmClass.lpszClassName=L"YmccOwnVisualCacheWarmup";warmClass.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);RegisterClassW(&warmClass);
 HWND warm=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,warmClass.lpszClassName,L"",WS_POPUP|WS_VISIBLE,40,40,16,16,nullptr,nullptr,warmClass.hInstance,nullptr);
 UpdateWindow(warm);pump();captureOwnFrame(warm);DestroyWindow(warm);pump();
 const DWORD gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 Json cases=Json::array();auto check=[&](const char* name,bool ok){cases.push_back({{"name",name},{"ok",ok}});};
 Config cfg=profileDefault(1);dryRun=true;setContext("steamdeck",true);initialize(GetModuleHandleW(nullptr),nullptr,cfg,1);refresh();pump();
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
 check("candidate-input-source-and-button-layers-are-actually-active",state().value("nativeMousePassActive",false));
#endif
 const auto before=repaintCount;bool ready=true;
 for(int n=0;n<64;n++)for(int side=0;side<2;side++)ready=renderOutline(side,cfg.transparency)&&ready;
 const auto repeatedUploads=repaintCount-before;
 check("same-native-layer-content-retained-without-128-reuploads",ready&&repeatedUploads==0);
 check("initial-alpha-frames-ready-with-thin-cross",outlineUploads[0].ready&&outlineUploads[1].ready&&!outlineUploads[0].interiorClear&&!outlineUploads[1].interiorClear&&(outlineUploads[0].centre>>24)==25&&(outlineUploads[1].centre>>24)==25);
 const HWND left=outlines[0],right=outlines[1];
 const auto buttonBefore=buttonUploadCount;bool buttonReady=true;
 for(int n=0;n<64;n++)for(int index:{1,2})buttonReady=renderButton(index,cfg.transparency)&&buttonReady;
 check("same-native-button-art-retained-without-128-reuploads",buttonReady&&buttonUploadCount==buttonBefore);
 RECT input{},visual{};GetWindowRect(windows[0],&input);const auto moveUploads=outlineUploadCount;
 SetWindowPos(windows[0],nullptr,input.left+11,input.top+7,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
 check("position-only-move-retains-bitmap",renderOutline(0,cfg.transparency)&&outlineUploadCount==moveUploads);
 GetWindowRect(outlines[0],&visual);check("cached-outline-follows-authoritative-input-window",visual.left==input.left+11-pressGlowPadding(cfg.scale)&&visual.top==input.top+7-pressGlowPadding(cfg.scale));
 const auto forcedOutline=outlineUploadCount,forcedButton=buttonUploadCount;
 refresh(true);pump();check("display-DPI-force-reuploads-all-visible-frames",outlineUploadCount>=forcedOutline+2&&buttonUploadCount>=forcedButton+2);
 const auto normalRefresh=outlineUploadCount,normalButtons=buttonUploadCount;refresh();pump();
 check("normal-refresh-keeps-native-layer-images",outlineUploadCount==normalRefresh&&buttonUploadCount==normalButtons);
 {std::lock_guard<std::mutex> lock(mutex);core.event(0,42,0,30,30,(int)sizes[0].cx,(int)sizes[0].cy,GetTickCount64());}
 const bool held=snapshot().present;cfg.transparency=35;configure(cfg);pump();
 check("transparency-only-keeps-active-native-contact",held&&snapshot().present);
 check("transparency-updates-reuse-visual-HWNDs",outlines[0]==left&&outlines[1]==right&&outlineUploads[0].ready&&outlineUploads[1].ready);
 const auto changedOutline=outlineUploadCount,changedButtons=buttonUploadCount;
 check("transparency-invalidates-both-native-cache-kinds",changedOutline>normalRefresh&&changedButtons>normalButtons);
 cfg.scale=105;configure(cfg);pump();check("geometry-change-still-releases-native-touch",!snapshot().present);
 check("geometry-invalidates-outline-size-cache",outlineUploads[0].width==(int)sizes[0].cx&&outlineUploads[0].height==(int)sizes[0].cy);
 cfg.transparency=100;configure(cfg);pump();check("100-percent-keeps-touch-input-visible-with-zero-alpha-outline",IsWindowVisible(windows[0])&&IsWindowVisible(windows[1])&&outlineUploads[0].edge==0&&outlineUploads[1].edge==0);
 // Actual desktop-composited pixels, cropped to a window owned by THIS helper.
 // No screenshot of the user's full desktop, no mouse/touch injection.
 Config visualCfg=profileDefault(1);setContext("steamdeck",true);configure(visualCfg,1);pump();
 WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=DefWindowProcW;
 cls.lpszClassName=L"YmccOwnVisualCachePixelFrame";cls.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);RegisterClassW(&cls);
 HWND frame=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,L"YMCC 自有绘制验证",WS_POPUP|WS_VISIBLE,40,40,560,320,nullptr,nullptr,cls.hInstance,nullptr);
 UpdateWindow(frame);
 for(int side=0;side<2;side++){SetWindowPos(windows[side],HWND_TOPMOST,60+side*320,60,200,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
 check(side==0?"left-candidate-frame-synchronized":"right-candidate-frame-synchronized",nativeInputSync(side,true,false));
#endif
 renderOutline(side,80,true);}
 pump();Sleep(40);auto pixelBefore=captureOwnFrame(frame);
 unsigned bluePixels=0;for(auto pixel:pixelBefore){const unsigned b=pixel&255,g=(pixel>>8)&255,r=(pixel>>16)&255;if(b>r+8&&g>r)++bluePixels;}
 check("actual-owned-desktop-frame-has-blue-outline-pixels",!pixelBefore.empty()&&bluePixels>150);
 bool centreWhite=pixelBefore.size()==560*320;
 if(centreWhite)centreWhite=(pixelBefore[110*560+110]&0xffffff)==0xffffff && (pixelBefore[110*560+430]&0xffffff)==0xffffff;
 check("actual-compositor-interior-stays-background-transparent",centreWhite);
 const auto retainedUploads=outlineUploadCount;for(int n=0;n<64;n++)for(int side=0;side<2;side++)renderOutline(side,80);
 pump();Sleep(40);auto pixelAfter=captureOwnFrame(frame);
 check("actual-composited-frame-identical-after-128-retained-draws",pixelBefore==pixelAfter&&!pixelBefore.empty()&&outlineUploadCount==retainedUploads);
 const auto captureGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 for(int n=0;n<8;n++)captureOwnFrame(frame);
 check("repeated-owned-pixel-capture-releases-all-temporary-GDI",GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==captureGdi);
 DestroyWindow(frame);
 Config standalone=profileDefault(0);standalone.standaloneSpecialMode=StandaloneSpecialMode::SteamDeck;
 setContext("disabled",false);configure(standalone,0);pump();const auto oldProfile=buttonUploads[1].profile;
 const auto profileUploads=buttonUploadCount;standalone.standaloneSpecialMode=StandaloneSpecialMode::PS5;configure(standalone,0);pump();
 check("independent-Steam-to-PS-change-invalidates-glyph-cache",oldProfile==1&&buttonUploads[1].profile==2&&buttonUploadCount>=profileUploads+2);
 shutdown();pump();check("test-exits-without-system-output",injectedCount==0&&sentKeys==0&&!sentClick&&!windows[0]&&!outlines[0]);
 const DWORD gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 check("native-layer-cache-retains-no-GDI-handles",gdiAfter<=gdiBefore+2);
 bool ok=true;for(const auto& c:cases)ok=ok&&c["ok"].get<bool>();
 Json report={{"ok",ok},{"cases",cases},{"redundantOutlineUploads",repeatedUploads},{"visualPixelFlickerAcceptanceRun",false},{"ownCompositedPixelRetentionChecked",true},{"ownBlueOutlinePixels",bluePixels},{"coldGdi",coldGdi},{"gdiBefore",gdiBefore},{"gdiAfter",gdiAfter},{"virtualTargetStarted",false},{"systemInputInjected",false}};
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
 report["nativeCandidateInputExercised"]=true;setNativeInputCandidate(false);
#else
 report["nativeCandidateInputExercised"]=false;
#endif
 std::ofstream out(argc>1?argv[1]:"visual-cache.json");out<<report.dump(2);std::cout<<report.dump(2)<<"\n";return ok?0:3;
}
