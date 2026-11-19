"""Compile the exact Steam fallback and real window helper against owned memory ports."""
import argparse
import subprocess
from pathlib import Path

cli = argparse.ArgumentParser()
cli.add_argument('--output', required=True, type=Path)
cli.add_argument('--header', type=Path, default=Path('native/sleep_steam_recovery.h'))
args = cli.parse_args()
root = Path.cwd()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
header = args.header.read_text('utf-8-sig')
start = header.index('static json returnOriginalPidWindow(')
end = header.index('\nstatic json resumeInputContext(', start)
function = header[start:end]

cpp = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <functional>
#include <iostream>
#include <type_traits>
#include "json.hpp"
using json=nlohmann::json;
enum class Race {None, Visible, Iconic, Position, Foreground};
static Race race;
static bool revoked, moved, hidden, iconic, winAccepted, responsive;
static int shows, positions, activations;
static HWND target=reinterpret_cast<HWND>(1), front=reinterpret_cast<HWND>(2), other=reinterpret_cast<HWND>(3);
static BOOL visible(HWND){if(race==Race::Visible)revoked=true;if(race==Race::Foreground)moved=true;return !hidden;}
static BOOL minimized(HWND){if(race==Race::Iconic)revoked=true;return iconic;}
static BOOL show(HWND,int){++shows;return TRUE;}
static BOOL position(HWND,HWND,int,int,int,int,UINT){++positions;if(race==Race::Position)revoked=true;return TRUE;}
static BOOL activate(HWND){++activations;return winAccepted;}
static HWND getForeground(){return moved?other:front;}
static DWORD windowThread(HWND,DWORD*){return 654;}
static LRESULT probe(HWND,UINT,WPARAM,LPARAM,UINT,UINT,PDWORD_PTR){return responsive?1:0;}
#define IsWindowVisible visible
#define IsIconic minimized
#define ShowWindowAsync show
#define SetWindowPos position
#define SetForegroundWindow activate
#define GetForegroundWindow getForeground
#define GetWindowThreadProcessId windowThread
#define SendMessageTimeoutW probe
#include "resume_focus.h"
using namespace ymcc::sleeppower;
static int __cdecl power(SharedPowerV1* value,std::uint32_t bytes) {
 if(!value||bytes!=sizeof(*value))return 0;
 *value={};value->generation=8;value->flags=kKnown|kHardwareAllowed;
 if(revoked){value->observedNotificationSequence=1;value->flags=kKnown|kNotificationPending;}
 return 1;
}
struct Game { DWORD pid=42; ULONGLONG created=14; };
struct Session {};
struct Captured { Game game; Session client; };
struct Foreground { HWND window; std::string role; };
static Foreground foreground(const Game&,const Session&){return {getForeground(),"steam"};}
static bool allowedForeground(const Foreground& f){return f.window!=nullptr;}
static bool sameForeground(const Foreground& f,const Game&,const Session&){return f.window==getForeground();}
static HWND focusFindWindowForPid(DWORD game){return game==42?target:nullptr;}
static bool focusIsRestorableExactWindow(HWND window,DWORD pid){return window==target&&pid==42;}
static bool flag(const json& j,const char* n){return j.value(n,false);}
static unsigned long long number(const json& j,const char* n){return j.value(n,0ULL);}
static std::string pidWindowReturnBlock(const json&,std::uint32_t){return {};}
static const json rows=json::array({{{"gameidString","123"},{"appid",123},{"appType",2}}});
static const json context={{"controllers",json::array({15})}};
static json inspect(unsigned long long,const char*) {
 return {{"steamInputRecovery",{{"repairable",true},{"returnBasis","current-association"}}},
         {"game",{{"pid",42},{"created","14"}}},{"suspendCapture",{{"available",true}}},
         {"steamSessionState",{{"ready",true}}},{"steam",{{"inputContext",context},{"overviews",rows}}}};
}
@PRODUCT@
template<class Function> static json invoke(Function function,const json& receipt,const std::function<bool()>& admitted){
 const Captured original{};const auto expected=foreground(original.game,original.client);
 if constexpr(std::is_invocable_v<Function,const Captured&,unsigned long long,const json&,const Foreground&,const std::function<bool()>&,const std::function<bool()>&>)
     return function(original,8,receipt,expected,admitted,admitted);
 else return function(original,8,receipt,expected,admitted);
}
int main(){
 struct Case{const char* label;Race edge;bool hide,minimize,accept,response,ack;int show,pos,activate;bool attempted,accepted;};
 const Case cases[]={
    {"normal",Race::None,false,false,true,true,true,0,1,1,true,true},
    {"revoked-during-visible",Race::Visible,true,false,true,true,true,0,0,0,true,false},
    {"revoked-during-iconic",Race::Iconic,false,true,true,true,true,0,0,0,true,false},
    {"revoked-after-position",Race::Position,false,false,true,true,true,0,1,0,true,false},
    {"foreground-changed",Race::Foreground,true,false,true,true,true,0,0,0,true,false},
    {"windows-refusal",Race::None,false,false,false,true,true,0,1,1,true,false},
    {"unresponsive",Race::None,false,false,true,false,true,0,0,0,false,false},
    {"no-steam-ack",Race::None,false,false,true,true,false,0,0,0,false,false},
 };
 int failed=0;
 for(const auto& c:cases){
    race=c.edge;hidden=c.hide;iconic=c.minimize;winAccepted=c.accept;responsive=c.response;
    revoked=moved=false;shows=positions=activations=0;
    SharedPowerV1 origin;if(!capturePower(&power,origin))return 3;
    const auto result=invoke(&returnOriginalPidWindow,{{"mutated",true},{"actionAcknowledged",c.ack},
        {"inputContext",context},{"after",{{"overviews",rows}}}},[&]{return powerCurrent(&power,origin);});
    const bool ok=shows==c.show&&positions==c.pos&&activations==c.activate&&
        flag(result,"attempted")==c.attempted&&flag(result,"apiAccepted")==c.accepted&&!flag(result,"gameInputVerified");
    std::cout<<(ok?"PASS ":"FAIL ")<<c.label<<" shows="<<shows<<" positions="<<positions<<" activations="<<activations<<" "<<result.dump()<<"\n";
    if(!ok)++failed;
 }
 return failed?2:0;
}
'''
source = output / 'steam_window.cpp'
source.write_text(cpp.replace('@PRODUCT@', function), encoding='utf-8')
vs = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
batch = output / 'run.cmd'
batch.write_text(f'''@echo off
call "{vs}" 10.0.26100.0 >nul
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /utf-8 /MT /W3 "{source}" /I"{root / 'native'}" /I"{root / 'deps/json'}" /Fo"{output / 'steam_window.obj'}" /Fe"{output / 'steam_window.exe'}" /link user32.lib
if not "%errorlevel%"=="0" exit /b 1
"{output / 'steam_window.exe'}"
exit /b %errorlevel%
''', encoding='ascii')
result = subprocess.run(['cmd.exe', '/d', '/c', str(batch)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(output / 'result.log').write_bytes(result.stdout)
print(result.stdout.decode('utf-8', errors='replace'))
raise SystemExit(result.returncode)
