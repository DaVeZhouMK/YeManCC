import {admitMouseModePresentation,type MouseModePresentation} from './mouseModePresentation';
import { boundedHudData, normalizeMonitorOutput, type HudData, type MonitorOutputPreferences } from './monitorHudModel';
export interface DesktopHudFrame {
  type:'desktop-hud';protocol:1;hudId:number;nonce:string;seq:number;expiresAt:number;show:boolean;
  preferences:MonitorOutputPreferences;data:HudData;
  mouseMode?:MouseModePresentation|null;
  policyGame?:{pid:number;processCreated:string}|null;
}
export function admitDesktopHudFrame(raw:unknown,id:number,nonce:string,now:number,lastSeq:number):DesktopHudFrame|null {
  const value=raw as DesktopHudFrame;
  if(!value||value.type!=='desktop-hud'||value.protocol!==1||value.hudId!==id||value.nonce!==nonce||
    !Number.isSafeInteger(value.seq)||value.seq<=lastSeq||!Number.isFinite(value.expiresAt)||value.expiresAt<=now||value.expiresAt>now+7000||
    typeof value.show!=='boolean'||value.data?.source!=='ymcc-uniform'||typeof value.data.stale!=='boolean'||!value.data.values||typeof value.data.values!=='object'||Array.isArray(value.data.values)||
    (value.data.freshUntil!==undefined&&(!value.data.freshUntil||typeof value.data.freshUntil!=='object'||Array.isArray(value.data.freshUntil)))||
    !Array.isArray(value.data.coreUsage)||!Array.isArray(value.data.coreClocks)||!Array.isArray(value.data.frameTimes))return null;
  const preferences=normalizeMonitorOutput(value.preferences,false);
  if(value.show&&(!preferences.enabled||preferences.output!=='ymcc'))return null;
  return {type:'desktop-hud',protocol:1,hudId:id,nonce,seq:value.seq,expiresAt:value.expiresAt,show:value.show,
    preferences,data:boundedHudData(value.data),mouseMode:admitMouseModePresentation(value.mouseMode)};
}
