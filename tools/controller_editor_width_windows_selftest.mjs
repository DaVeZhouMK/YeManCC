import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
const sourceRoot=process.cwd().replaceAll(String.fromCharCode(92),'/');
const out=(process.env.YMCC_EDITOR_WIDTH_OUT || path.resolve(sourceRoot,'../../Build/Validation/controller-editor-width')).replaceAll(String.fromCharCode(92),'/');
fs.mkdirSync(out,{recursive:true});
const source=fs.readFileSync(sourceRoot+'/native/main.cpp','utf8').replace(/\r\n/g,'\n');
function slice(start,end){const a=source.indexOf(start),b=source.indexOf(end,a);if(a<0||b<a)throw Error('Production slice missing: '+start);return source.slice(a,b);}
const layout=slice('static bool applyFullHeightLayout(', 'static int  g_effectType');
const ipc=slice('    ipc_on("window.controllerEditorWidth"', '    ipc_on("window.startDrag"');
const hit=slice('    case WM_NCLBUTTONDOWN:\n        // Docked windows remain fixed', '    case WM_SG_S0_INTENT:');
const sizing=slice('    case WM_SIZING:\n        if (g_controllerEditorWidth.active())', '    case WM_SIZE:\n        msiNativeForegroundUiEvent();');
const frame=slice('    case WM_NCCALCSIZE:\n        if (g_frameless', '    case WM_GETMINMAXINFO:');
const generated=`#define UNICODE
#define _UNICODE
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <iostream>
#include <functional>
#include <unordered_map>
#include "${sourceRoot}/native/controller_editor_width.h"
#include "${sourceRoot}/native/window_taskbar_layout.h"
#include "${sourceRoot}/deps/json/json.hpp"
using json=nlohmann::json;
static HWND g_hwnd=nullptr;
static bool g_fullHeight=true,g_frameless=true,dockLeft=false;
static int g_baseW=580,g_baseH=780;
static ymcc::editorwidth::Session g_controllerEditorWidth;
static bool windowPlacementIsLeft(){return dockLeft;}
${layout}
std::unordered_map<std::string,std::function<json(const json&)>> commands;
template<class F>void ipc_on(const char* name,F fn){commands[name]=fn;}
void registerCommands(){${ipc}}
LRESULT CALLBACK WindowProc(HWND h,UINT m,WPARAM w,LPARAM l){switch(m){${frame}${hit}${sizing}}return DefWindowProcW(h,m,w,l);}
int failures=0;json checks=json::array();
void check(bool result,const char* name){checks.push_back({{"name",name},{"pass",result}});if(!result)++failures;}
RECT rect(){RECT r{};GetWindowRect(g_hwnd,&r);return r;}
int width(RECT r){return r.right-r.left;}
int main(){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 WNDCLASSW cls{};cls.lpfnWndProc=WindowProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YMCCEditorWidthVerification";RegisterClassW(&cls);
 g_hwnd=CreateWindowExW(0,cls.lpszClassName,L"YMCC isolated editor width check",WS_POPUP,100,100,580,780,nullptr,nullptr,cls.hInstance,nullptr);
 registerCommands();
 RECT area{};ymcc::windowlayout::safeMonitorArea(MonitorFromWindow(g_hwnd,MONITOR_DEFAULTTONEAREST),area);
 auto request=[&](const char* owner,bool enabled,double scale=2.0){return commands.at("window.controllerEditorWidth")({{"owner",owner},{"enabled",enabled},{"scale",scale}});};
 for(bool left:{false,true}){
  dockLeft=left;check(applyFullHeightLayout(),"normal full-height layout applies");const RECT before=rect();
  POINT edge{left?before.right-1:before.left+1,before.top+20};
  check(SendMessageW(g_hwnd,WM_NCHITTEST,0,MAKELPARAM(edge.x,edge.y))==HTCLIENT,"normal docked window rejects edge resizing");
  auto receipt=request("editor-a",true);const RECT wide=rect();
  check(receipt.value("ok",false),"production IPC enters editor session");
  check(width(wide)==std::min(width(before)*2,static_cast<int>(area.right-area.left)),"real HWND doubles width or clamps to work area");
  check(wide.top==before.top&&wide.bottom==before.bottom,"expansion preserves full height");
  check(left?wide.left==before.left:wide.right==before.right,"expansion preserves docked outer edge");
  check((GetWindowLongPtrW(g_hwnd,GWL_STYLE)&WS_THICKFRAME)!=0,"editor adds a real native resize frame");
  edge={left?wide.right-1:wide.left+1,wide.top+20};
  check(SendMessageW(g_hwnd,WM_NCHITTEST,0,MAKELPARAM(edge.x,edge.y))==(left?HTRIGHT:HTLEFT),"inner edge reports native resize hit");
  check(!request("another-editor",false).value("ok",false),"other editor cannot end owned session");
  check(!request("another-editor",true).value("ok",false),"other editor cannot replace owned session");
  RECT proposed=wide;if(left)proposed.right=proposed.left+width(before)/2;else proposed.left=proposed.right-width(before)/2;
  SendMessageW(g_hwnd,WM_SIZING,left?WMSZ_RIGHT:WMSZ_LEFT,reinterpret_cast<LPARAM>(&proposed));
  check(width(proposed)==width(before),"native sizing prevents shrinking below original width");
  if(left)proposed.right=proposed.left+width(before)*3;else proposed.left=proposed.right-width(before)*3;
  SendMessageW(g_hwnd,WM_SIZING,left?WMSZ_RIGHT:WMSZ_LEFT,reinterpret_cast<LPARAM>(&proposed));
  check(width(proposed)==std::min(width(before)*2,static_cast<int>(area.right-area.left)),"native sizing caps width at twice original");
  const int mid=std::min(static_cast<int>(std::lround(width(before)*1.5)),static_cast<int>(area.right-area.left));
  if(left)proposed.right=proposed.left+mid;else proposed.left=proposed.right-mid;
  SendMessageW(g_hwnd,WM_SIZING,left?WMSZ_RIGHT:WMSZ_LEFT,reinterpret_cast<LPARAM>(&proposed));
  SetWindowPos(g_hwnd,nullptr,proposed.left,proposed.top,width(proposed),proposed.bottom-proposed.top,SWP_NOZORDER|SWP_NOACTIVATE);
  applyFullHeightLayout();check(width(rect())==mid,"display or summon reflow preserves dragged width");
  check(request("editor-a",true,1).value("ok",false)&&width(rect())==width(before),"original-width control resizes real HWND");
  request("editor-a",true,2);check(request("editor-a",false).value("ok",false),"production IPC ends session");
  check(ymcc::windowlayout::sameRect(before,rect()),"closing restores exact normal docked rectangle");
  check((GetWindowLongPtrW(g_hwnd,GWL_STYLE)&WS_THICKFRAME)==0,"closing removes only temporary resize frame");
 }
 g_fullHeight=false;dockLeft=false;SetWindowLongPtrW(g_hwnd,GWL_STYLE,WS_POPUP|WS_THICKFRAME);SetWindowPos(g_hwnd,nullptr,area.left+20,area.top+20,580,600,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
 const RECT ordinary=rect();request("ordinary",true);check(width(rect())==std::min(1160,static_cast<int>(area.right-area.left)),"ordinary main window supports same two-times width");request("ordinary",false);
 check(ymcc::windowlayout::sameRect(ordinary,rect()),"ordinary window restores exact position and size");check((GetWindowLongPtrW(g_hwnd,GWL_STYLE)&WS_THICKFRAME)!=0,"preexisting resize frame remains intact");
 check(ymcc::editorwidth::scaledWidth(700,800,2)==800,"narrow portrait work area is never exceeded");
 check(ymcc::editorwidth::scaledWidth(580,1920,NAN)==580,"invalid scale falls back safely");
 DestroyWindow(g_hwnd);std::cout<<json({{"failures",failures},{"checks",checks},{"workArea",{area.left,area.top,area.right,area.bottom}},{"scope","production IPC, layout and window message handlers extracted verbatim; real hidden Win32 HWND; no installed YMCC or device changes"}}).dump(2);return failures?1:0;
}
`;
fs.writeFileSync(out+'/native-window-fixture.cpp',generated);

// Compile only an isolated hidden-window fixture; never start the installed app.
const recipe=[
  '@echo off',
  'call "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat"',
  'if errorlevel 1 exit /b 1',
  `cd /d "${out}"`,
  'cl /nologo /std:c++20 /utf-8 /EHsc /MT native-window-fixture.cpp /Fe:native-window-fixture.exe /link user32.lib',
  'if errorlevel 1 exit /b 1',
  'native-window-fixture.exe > native-window-results.json',
  'exit /b %errorlevel%',
].join('\r\n');
const recipePath=path.join(out,'run-native-check.cmd');
fs.writeFileSync(recipePath,recipe);
const run=spawnSync('cmd.exe',['/d','/c',recipePath],{encoding:'utf8',windowsHide:true});
fs.writeFileSync(path.join(out,'native-check.log'),(run.stdout||'')+(run.stderr||''));
if(run.status!==0){console.error(run.stdout,run.stderr);process.exit(run.status||1);}
const report=JSON.parse(fs.readFileSync(path.join(out,'native-window-results.json'),'utf8'));
console.log(JSON.stringify({passed:report.checks.filter(check=>check.pass).length,failures:report.failures,report:path.join(out,'native-window-results.json')}));
if(report.failures)process.exit(1);
