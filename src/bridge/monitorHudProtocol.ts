// Display-only wire contract shared by YMCC, Steam and the desktop HUD.
// No hardware/setting APIs. Authentication belongs to the native mirror broker.
import { boundedHudData, normalizeMonitorOutput, type HudData, type MonitorOutputPreferences } from './monitorHudModel';
export const HUD_PROTOCOL = 1;
export const HUD_LEASE_MS = 6000;
export interface HudGameBinding {
  appId:number;gameId:string;browserGameId:string;overlayTitle:string;overlayPid:number;
  overlayBrowserId:number;ownerBrowserId:number;overlayBrowserType:number;overlayNativeAppId:number;
}
export interface HudReady {type:'hud-ready';protocol:1;runId:string;}
export interface HudFrame {
  type:'hud';protocol:1;runId:string;session:string;seq:number;operation:number;show:boolean;
  expiresAt:number;game:HudGameBinding|null;preferences:MonitorOutputPreferences;data:HudData;
}
export interface HudClientStatus {
  capable:boolean;game:HudGameBinding|null;shown:boolean;hiddenConfirmed:boolean;session:string|null;operation:number;
  reason:string;
}
export function validHudGameBinding(value:unknown):value is HudGameBinding {
  const b=value as HudGameBinding, integer=(n:unknown,min=1)=>typeof n==='number'&&Number.isSafeInteger(n)&&n>=min&&n<=0xffffffff;
  return !!b && ['appId','overlayPid','overlayBrowserId','ownerBrowserId','overlayNativeAppId'].every(k=>integer(b[k as keyof HudGameBinding])) && integer(b.overlayBrowserType,0) &&
    [b.gameId,b.browserGameId].every(v=>typeof v==='string'&&/^\d{1,20}$/.test(v)&&BigInt(v)>0n&&BigInt(v)<=0xffffffffffffffffn) &&
    b.overlayTitle==='SP Overlay: '+b.overlayPid+'/'+b.overlayBrowserId+'/'+b.overlayBrowserType;
}
export function validHudStatus(value:unknown):value is HudClientStatus {
  const s=value as HudClientStatus;
  return !!s&&typeof s.capable==='boolean'&&typeof s.shown==='boolean'&&typeof s.hiddenConfirmed==='boolean'&&(s.game===null||validHudGameBinding(s.game)) &&
    (s.session===null||typeof s.session==='string'&&/^[a-zA-Z0-9-]{16,80}$/.test(s.session)) &&
    Number.isSafeInteger(s.operation)&&s.operation>=0&&typeof s.reason==='string'&&s.reason.length<=512 && (!s.shown||!!s.game&&!!s.session&&s.capable&&!s.hiddenConfirmed);
}
export function admitHudFrame(raw:unknown, runId:string, now:number, lastSeq:number):HudFrame|null {
  const v=raw as HudFrame;
  if(!v||v.type!=='hud'||v.protocol!==HUD_PROTOCOL||v.runId!==runId||!/^[a-f0-9]{32}$/.test(runId)||
    typeof v.session!=='string'||!/^[a-zA-Z0-9-]{16,80}$/.test(v.session)||!Number.isSafeInteger(v.seq)||v.seq<=lastSeq||
    !Number.isSafeInteger(v.operation)||v.operation<1||typeof v.show!=='boolean'||!Number.isFinite(v.expiresAt)||
    v.expiresAt<=now||v.expiresAt>now+HUD_LEASE_MS+1000||v.data?.source!=='ymcc-uniform'||typeof v.data.stale!=='boolean'||
    !v.data.values||typeof v.data.values!=='object'||Array.isArray(v.data.values)||(v.data.freshUntil!==undefined&&(!v.data.freshUntil||typeof v.data.freshUntil!=='object'||Array.isArray(v.data.freshUntil)))||
    !Array.isArray(v.data.coreUsage)||!Array.isArray(v.data.coreClocks)||!Array.isArray(v.data.frameTimes))return null;
  const preferences=normalizeMonitorOutput(v.preferences,false);
  if(v.show&&(!preferences.enabled||preferences.output!=='ymcc'||!validHudGameBinding(v.game)))return null;
  if(v.game!==null&&!validHudGameBinding(v.game))return null;
  return {type:'hud',protocol:HUD_PROTOCOL,runId,session:v.session,seq:v.seq,operation:v.operation,show:v.show,
    expiresAt:v.expiresAt,game:v.game?{...v.game}:null,preferences,data:boundedHudData(v.data)};
}
export function hudPlacement(width:number,height:number,boardWidth:number,boardHeight:number,position:number,margin=12) {
  const w=Math.max(1,Math.min(Math.floor(width),Math.ceil(boardWidth))), h=Math.max(1,Math.min(Math.floor(height),Math.ceil(boardHeight)));
  const p=[1,2,3,4,5,6].includes(position)?position:1,col=p===1||p===4?0:p===5||p===6?1:2,bottom=p===3||p===4||p===6;
  return {width:w,height:h,left:Math.max(0,col===0?margin:col===1?Math.round((width-w)/2):width-w-margin),
    top:Math.max(0,bottom?height-h-margin:margin)};
}
