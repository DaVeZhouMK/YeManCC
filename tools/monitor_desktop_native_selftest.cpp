#define NOMINMAX
#include "../native/monitor_desktop_hud.h"
#include <fstream>
#include <iostream>
#include <chrono>
using namespace ymcc::desktophud;
struct Shared {HWND target;volatile LONG clicks;};
static Shared* shared=nullptr;
static LRESULT CALLBACK targetProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_LBUTTONDOWN){InterlockedIncrement(&shared->clicks);InvalidateRect(h,nullptr,FALSE);return 0;}
 if(m==WM_PAINT){PAINTSTRUCT p{};HDC dc=BeginPaint(h,&p);RECT r{};GetClientRect(h,&r);HBRUSH b=CreateSolidBrush(RGB(74,92,112));FillRect(dc,&r,b);DeleteObject(b);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(245,245,245));const wchar_t* text=L"YMCC HUD TEST ONLY — synthetic readings / own isolated target window";TextOutW(dc,24,r.bottom-60,text,(int)wcslen(text));EndPaint(h,&p);return 0;}
 if(m==WM_CLOSE){DestroyWindow(h);return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);
}
static void pump(DWORD ms){const ULONGLONG end=GetTickCount64()+ms;while(GetTickCount64()<end){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(5);}}
static void screenshot(const std::wstring& file,RECT r){
 int w=r.right-r.left,h=r.bottom-r.top;HDC screen=GetDC(nullptr),mem=CreateCompatibleDC(screen);HBITMAP bmp=CreateCompatibleBitmap(screen,w,h);auto old=SelectObject(mem,bmp);BitBlt(mem,0,0,w,h,screen,r.left,r.top,SRCCOPY|CAPTUREBLT);SelectObject(mem,old);
 BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
 std::vector<unsigned char> pixels((size_t)w*h*4);GetDIBits(mem,bmp,0,h,pixels.data(),&bi,DIB_RGB_COLORS);BITMAPFILEHEADER f{};f.bfType=0x4d42;f.bfOffBits=sizeof(f)+sizeof(bi.bmiHeader);f.bfSize=f.bfOffBits+(DWORD)pixels.size();
 std::ofstream out(file,std::ios::binary);out.write((char*)&f,sizeof(f));out.write((char*)&bi.bmiHeader,sizeof(bi.bmiHeader));out.write((char*)pixels.data(),pixels.size());DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,screen);
}
int wmain(int argc,wchar_t** argv){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 if(argc>=3&&std::wstring(argv[1])==L"--target"){
   HANDLE map=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,argv[2]);if(!map)return 2;shared=(Shared*)MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared));
   WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=targetProc;c.lpszClassName=L"YMCC_HUD_TEST_TARGET";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&c);
   HWND h=CreateWindowW(c.lpszClassName,L"TEST ONLY: YMCC native HUD / isolated cross-process target",WS_OVERLAPPEDWINDOW,140,130,900,700,nullptr,nullptr,c.hInstance,nullptr);shared->target=h;ShowWindow(h,SW_SHOW);UpdateWindow(h);
   MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}UnmapViewOfFile(shared);CloseHandle(map);return 0;
 }
 if(argc<4){std::cerr<<"usage: harness WEB_DIR OUT_DIR FRAME_FILE [--click-own-target]";return 2;}
 const std::wstring web=argv[1],out=argv[2],fixture=argv[3];const bool doClick=argc>=5&&std::wstring(argv[4])==L"--click-own-target";
 Json report={{"actualNativeWebView2",true},{"syntheticDataBoundary",true},{"physicalMousePassVerified",false},{"actualGame",false},{"installedRuntimeChanged",false},{"cpuBenchmark",false}};
 std::vector<std::string> passed;HWND before=GetForegroundWindow();POINT cursor{};GetCursorPos(&cursor);PROCESS_INFORMATION process{};HANDLE mapping=nullptr;ComPtr<ICoreWebView2Environment> env;Carrier carrier;bool com=false;
 try{
   if(FindWindowW(L"YMCC_DesktopMonitorHUD",nullptr))throw std::runtime_error("Another native HUD already present — test refuses interference");
   com=SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));if(!com)throw std::runtime_error("COM init failed");
   std::wstring name=L"Local\\YMCC_HUD_OWNED_TEST_"+std::to_wstring(GetCurrentProcessId());mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name.c_str());shared=(Shared*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared));*shared={};
   wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);std::wstring command=L"\""+std::wstring(exe)+L"\" --target "+name;STARTUPINFOW si{sizeof(si)};
   if(!CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&process))throw std::runtime_error("Owned target process start failed");
   for(int n=0;n<400&&!shared->target;n++)pump(10);if(!shared->target)throw std::runtime_error("Owned test target not ready");
   FILETIME birth{},e{},k{},u{};GetProcessTimes(process.hProcess,&birth,&e,&k,&u);const ULONGLONG created=((ULONGLONG)birth.dwHighDateTime<<32)|birth.dwLowDateTime;
   bool envDone=false;HRESULT envHr=E_PENDING;std::wstring profile=out+L"\\webview-profile";
   HRESULT hr=CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.c_str(),nullptr,Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([&](HRESULT h,ICoreWebView2Environment* value)->HRESULT{envHr=h;env=value;envDone=true;return S_OK;}).Get());
   if(FAILED(hr))throw std::runtime_error("WebView environment request failed");for(int n=0;n<3000&&!envDone;n++)pump(10);if(!envDone||FAILED(envHr)||!env)throw std::runtime_error("WebView environment not ready");
   bool ready=false,painted=false,expired=false;std::vector<Json> events;
   carrier.initialize(env.Get(),{[&](ICoreWebView2* v){ComPtr<ICoreWebView2_3> v3;if(FAILED(v->QueryInterface(IID_PPV_ARGS(&v3))))return false;return SUCCEEDED(v3->SetVirtualHostNameToFolderMapping(L"app.localhost",web.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS));},
     [&](const std::string& ev,const Json& data){events.push_back({{"event",ev},{"data",data}});report["events"]=events;if(ev=="desktopHud.ready")ready=true;if(ev=="desktopHud.painted")painted=true;if(ev=="desktopHud.expired")expired=true;}});
   const auto own=carrier.create(process.dwProcessId,created,"native-desktop-test-00000001");int id=own["id"].get<int>();std::string nonce=own["nonce"].get<std::string>();
   for(int n=0;n<1600&&!ready;n++)pump(10);report["ready"]=ready;report["events"]=events;if(!ready)throw std::runtime_error("Production lightweight renderer did not handshake");passed.push_back("actual dedicated H5 entry / native display-only receiver ready");
   SetForegroundWindow(shared->target);pump(100);
   if(GetForegroundWindow()!=shared->target&&doClick){
     // Obtain test focus through a real click only on our own verified visible
     // target. Do NOT AttachThreadInput to the user's foreground application.
     SetWindowPos(shared->target,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);pump(50);
     RECT wr{};GetWindowRect(shared->target,&wr);POINT point{wr.left+70,wr.top+70};HWND hit=WindowFromPoint(point);DWORD pid=0;GetWindowThreadProcessId(hit,&pid);
     if(pid!=process.dwProcessId)throw std::runtime_error("Focus click target is not the owned test window; no click sent");
     SetCursorPos(point.x,point.y);INPUT events[2]{};events[0].type=INPUT_MOUSE;events[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;events[1].type=INPUT_MOUSE;events[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
     if(SendInput(2,events,sizeof(INPUT))!=2)throw std::runtime_error("Owned focus click refused");pump(100);SetCursorPos(cursor.x,cursor.y);
   }
   if(GetForegroundWindow()!=shared->target){DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);report["focusFailurePid"]=foregroundPid;report["ownTargetPid"]=process.dwProcessId;report["focusClickCount"]=(int)shared->clicks;throw std::runtime_error("Owned test target could not become foreground; HUD click not sent");}
   RECT targetRect{};GetClientRect(shared->target,&targetRect);POINT origin{};ClientToScreen(shared->target,&origin);OffsetRect(&targetRect,origin.x,origin.y);screenshot(out+L"\\before.bmp",targetRect);
   std::ifstream input(fixture);Json frame;input>>frame;frame["hudId"]=id;frame["nonce"]=nonce;frame["seq"]=1;frame["expiresAt"]=wallNow()+6000;
   if(!carrier.frame(id,nonce,frame))throw std::runtime_error("Production frame admission rejected synthetic test boundary");
   for(int n=0;n<300&&!painted;n++)pump(10);pump(250);report["painted"]=painted;report["status"]=carrier.status();if(!painted||!report["status"].value("visible",false))throw std::runtime_error("H5 HUD did not actually paint/show over owned target");passed.push_back("actual production renderer painted over external normal window without focus steal");
   HWND hud=FindWindowW(L"YMCC_DesktopMonitorHUD",nullptr);if(!hud||GetWindow(hud,GW_OWNER))throw std::runtime_error("HUD must not have a Win32 owner");
   RECT hudRect{};GetWindowRect(hud,&hudRect);POINT click{hudRect.left+std::min(35L,hudRect.right-hudRect.left-1),hudRect.top+std::min(35L,hudRect.bottom-hudRect.top-1)};
   HWND hit=WindowFromPoint(click);DWORD hitPid=0;GetWindowThreadProcessId(hit,&hitPid);report["hitPid"]=hitPid;report["targetPid"]=process.dwProcessId;report["hudRect"]={hudRect.left,hudRect.top,hudRect.right,hudRect.bottom};
   if(hitPid!=process.dwProcessId)throw std::runtime_error("Actual layered WebView HUD did not pass Windows hit-testing to external target");passed.push_back("cross-process WindowFromPoint at opaque HUD content hits underlying target");screenshot(out+L"\\visible.bmp",targetRect);
   // Require real glyph pixels: DOM/layout ACK and a black rectangle are NOT
   // visual acceptance. Test only within the owned HUD rectangle.
   std::ifstream captured(out+L"\\visible.bmp",std::ios::binary);BITMAPFILEHEADER capturedFile{};BITMAPINFOHEADER capturedHeader{};
   captured.read((char*)&capturedFile,sizeof(capturedFile));captured.read((char*)&capturedHeader,sizeof(capturedHeader));
   const int imageWidth=capturedHeader.biWidth,imageHeight=std::abs(capturedHeader.biHeight);std::vector<unsigned char> capturedPixels((size_t)imageWidth*imageHeight*4);captured.read((char*)capturedPixels.data(),capturedPixels.size());
   int glyphs=0,black=0,total=0;
   for(int y=std::max(0L,hudRect.top-targetRect.top+4);y<std::min((LONG)imageHeight,hudRect.bottom-targetRect.top-4);y++)
    for(int x=std::max(0L,hudRect.left-targetRect.left+4);x<std::min((LONG)imageWidth,hudRect.right-targetRect.left-4);x++){
     const auto* c=&capturedPixels[((size_t)y*imageWidth+x)*4];++total;
     if(c[2]>120&&c[1]>130&&c[0]>140)++glyphs;if(c[2]<3&&c[1]<3&&c[0]<3)++black;
   }
   report["visibleGlyphPixels"]=glyphs;report["blackPixels"]=black;report["testedPixels"]=total;
   if(glyphs<60||black>total/2)throw std::runtime_error("Actual HUD glyph/transparency pixels absent; black or empty surface is not H5 acceptance");
   passed.push_back("actual H5 glyph pixels visible; black-rectangle false positive rejected");
   if(doClick){
     const LONG beforeClicks=shared->clicks;SetCursorPos(click.x,click.y);INPUT clicks[2]{};clicks[0].type=INPUT_MOUSE;clicks[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;clicks[1].type=INPUT_MOUSE;clicks[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
     if(SendInput(2,clicks,sizeof(INPUT))!=2)throw std::runtime_error("Owned-target SendInput refused");for(int n=0;n<60&&shared->clicks==beforeClicks;n++)pump(10);
     report["targetClicks"]=(int)shared->clicks;report["foregroundStayedTarget"]=GetForegroundWindow()==shared->target;
     if(shared->clicks!=beforeClicks+1||GetForegroundWindow()!=shared->target)throw std::runtime_error("Physical click did not pass HUD or focus was stolen");report["physicalMousePassVerified"]=true;passed.push_back("actual SendInput click delivered once beneath real WebView2 HUD with target focus preserved");SetCursorPos(cursor.x,cursor.y);
   }
   // The same production renderer must preserve position IDs and the three
   // templates in the actual native carrier, not just in a browser mock.
   unsigned long long nextSeq=2;Json matrix=Json::array();
   for(const auto& item:std::vector<std::pair<std::string,int>>{{"corner",5},{"capsule",2},{"bar",6},{"corner",3},{"corner",4},{"corner",1}}){
     report["matrixAttempt"]={{"layout",item.first},{"position",item.second}};painted=false;frame["preferences"]["ymcc"]["layout"]=item.first;frame["preferences"]["ymcc"]["position"]=item.second;
     frame["preferences"]["ymcc"]["opacity"]=item.first=="capsule"?0.35:0.86;
     frame["seq"]=nextSeq++;frame["expiresAt"]=wallNow()+6000;
     if(!carrier.frame(id,nonce,frame))throw std::runtime_error("Native layout/position frame rejected");
     for(int n=0;n<150&&!painted;n++)pump(10);pump(80);
     report["matrixNativeStatus"]=carrier.status();if(!painted||!carrier.status().value("visible",false))throw std::runtime_error("Template did not repaint in native carrier");
     RECT board{};GetWindowRect(hud,&board);const int vw=targetRect.right-targetRect.left,vh=targetRect.bottom-targetRect.top,bw=board.right-board.left,bh=board.bottom-board.top;
     const int col=item.second==1||item.second==4?0:item.second==5||item.second==6?1:2;
     const bool bottom=item.second==3||item.second==4||item.second==6;
     const int expectedX=targetRect.left+std::max(0,col==0?12:col==1?(vw-bw)/2:vw-bw-12),expectedY=targetRect.top+std::max(0,bottom?vh-bh-12:12);
     if(board.left!=expectedX||board.top!=expectedY)throw std::runtime_error("Native position disagrees with Steam/YMCC UI position ID");
     const auto file=out+L"\\native-"+wide(item.first)+L"-position-"+std::to_wstring(item.second)+L".bmp";screenshot(file,targetRect);
     matrix.push_back({{"layout",item.first},{"position",item.second},{"bounds",{board.left,board.top,board.right,board.bottom}}});
   }
   report["nativeLayoutPositionMatrix"]=matrix;passed.push_back("all six Steam/YMCC position IDs and all three shared H5 templates repaint in native carrier");
   ShowWindow(shared->target,SW_MINIMIZE);pump(150);if(carrier.status().value("visible",true))throw std::runtime_error("Minimized target HUD stayed visible");passed.push_back("target minimize event hides the native HUD");
   ShowWindow(shared->target,SW_RESTORE);SetForegroundWindow(shared->target);pump(150);if(!carrier.status().value("visible",false))throw std::runtime_error("Target restore failed to restore active HUD");passed.push_back("target restore repositions active HUD without recreating WebView");
   if(carrier.close(id,"wrong-nonce"))throw std::runtime_error("Wrong nonce closed own HUD");passed.push_back("native close refuses another owner's nonce");
   for(int n=0;n<750&&!expired;n++)pump(10);if(!expired||carrier.status().value("visible",true))throw std::runtime_error("Native data deadman failed");passed.push_back("native lease timer hides stale HUD independently of renderer/main refresh");
   if(!carrier.close(id,nonce)||carrier.status().value("id",-1)!=0||FindWindowW(L"YMCC_DesktopMonitorHUD",nullptr))throw std::runtime_error("Owned HUD/window failed to close");passed.push_back("owned native WebView/HWND/hooks cleanup confirmed");report["passed"]=true;
 }catch(const std::exception& e){report["passed"]=false;report["error"]=e.what();}
 carrier.shutdown();env.Reset();if(shared&&shared->target)PostMessageW(shared->target,WM_CLOSE,0,0);
 if(process.hProcess){if(WaitForSingleObject(process.hProcess,3000)!=WAIT_OBJECT_0){report["targetCleanupConfirmed"]=false;}else report["targetCleanupConfirmed"]=true;CloseHandle(process.hProcess);}if(process.hThread)CloseHandle(process.hThread);
 SetCursorPos(cursor.x,cursor.y);if(before&&IsWindow(before))SetForegroundWindow(before);if(shared){UnmapViewOfFile(shared);shared=nullptr;}if(mapping)CloseHandle(mapping);if(com)CoUninitialize();
 report["cases"]=passed;report["scope"]="Actual production native Carrier + dedicated production H5 renderer and separate target process. Sensor/Steam boundary synthetic; no installed YMCC/Steam/game changes. Physical input only when explicitly passed --click-own-target, at owned test window.";
 std::ofstream evidence(out+L"\\NATIVE-DESKTOP-SELFTEST.json");evidence<<report.dump(2);std::cout<<report.dump(2)<<std::endl;return report.value("passed",false)?0:1;
}
