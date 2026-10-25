import fs from 'node:fs';import assert from 'node:assert/strict';
const file='native/monitor_desktop_hud.h',s=fs.readFileSync(file,'utf8');const cases=[];
function check(name,test){assert(test,name);cases.push(name)}
check('global viewport fixed to display, no foreground/client tracking',s.includes('if(s.global){')&&s.includes('r=info.rcWork;')&&!s.includes('foregroundAnchor')&&!s.includes('externalClient'));
check('global no longer discards all foreground events',!s.includes('if(s->global||!s->hwnd'));
check('both local-global and game-bound carrier install event hooks',s.includes('try{hook();}')&&!s.includes('try{if(!global)hook();}'));
check('own HUD feedback events excluded',s.includes('hwnd==s->hwnd||IsChild(s->hwnd,hwnd)')&&s.includes('WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS'));
check('event changes coalesced into one window message',s.includes('if(!s->queued){s->queued=true;if(!PostMessageW'));
check('topmost same-geometry foreground switch stays non-activating',s.includes('SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW'));
check('same-geometry topmost raise is event or confirmed late global occlusion only',s.includes('else if(hudplacement::needsRaise(s->global,raise,foregroundOccludes(*s,foreground)))')&&s.includes('s->queuedRaise=false;'));
check('repair checks overlapping topmost foreground and bounded native Z-order, no target binding',s.includes('GetWindowLongPtrW(root,GWL_EXSTYLE)&WS_EX_TOPMOST')&&s.includes('IntersectRect(&intersection,&hud,&game)')&&s.includes('before&&count<128')&&s.includes('GW_HWNDPREV'));
check('window geometry never becomes a new telemetry collector',!s.includes('HWiNFO')&&!s.includes('GetSystemTimes')&&!s.includes('PdhCollectQueryData'));
check('no polling added for foreground follow',(s.match(/SetTimer\(/g)||[]).length===2&&s.includes('static constexpr UINT_PTR leaseTimer'));
const report={passed:cases.length,cases,actualGameAccepted:false,actualNativeWindowAccepted:false,cpuBenchmark:false};console.log(JSON.stringify(report,null,2));fs.writeFileSync(process.env.YMCC_PLACEMENT_TEST_REPORT||'G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/global-game-visibility-20261011/NATIVE-SOURCE-SELFTEST.json',JSON.stringify(report,null,2));