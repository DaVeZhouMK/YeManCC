// Ephemeral observation of the official @decky/ui Router getters; no AppID->PID claim.
// Source reference is pinned in Archives (decky-frontend-lib 4b68ef7287d8).
export interface SteamRunningObservation {
  availability: 'observed' | 'none' | 'unavailable' | 'ambiguous';
  appId?: string;
  gameId?: string;
  runningCount?: number;
}
function decimalId(value: unknown, maximum: bigint): string | undefined {
  if(typeof value==='number' && (!Number.isSafeInteger(value)||value<=0))return;
  if(typeof value!=='string'&&typeof value!=='number')return;
  const text=String(value);
  if(text.length>20||!/^\d+$/.test(text))return;
  const number=BigInt(text);if(number<=0n||number>maximum)return;
  return number.toString();
}
export function validSteamObservation(value:unknown):value is SteamRunningObservation {
  if(!value||typeof value!=='object'||Array.isArray(value))return false;
  const observation=value as SteamRunningObservation;
  if(!['observed','none','unavailable','ambiguous'].includes(observation.availability))return false;
  if(Object.keys(value).some(key=>!['availability','appId','gameId','runningCount'].includes(key)))return false;
  if(observation.appId!==undefined && (typeof observation.appId!=='string'||decimalId(observation.appId,4294967295n)!==observation.appId))return false;
  if(observation.gameId!==undefined && (typeof observation.gameId!=='string'||decimalId(observation.gameId,18446744073709551615n)!==observation.gameId))return false;
  if(observation.runningCount!==undefined && (!Number.isSafeInteger(observation.runningCount)||observation.runningCount<0||observation.runningCount>256))return false;
  if(observation.availability==='none')return observation.appId===undefined&&observation.gameId===undefined&&observation.runningCount===0;
  if(observation.availability==='unavailable')return observation.appId===undefined&&observation.gameId===undefined&&observation.runningCount===undefined;
  return !!observation.appId;
}
export function observeSteamRunning(router:unknown):SteamRunningObservation {
  try {
    if(!router||typeof router!=='object'||!('MainRunningApp' in router))return {availability:'unavailable'};
    const source=router as {MainRunningApp?:{appid?:unknown;gameid?:unknown};RunningApps?:unknown[]};
    const app=source.MainRunningApp;
    const apps=source.RunningApps;
    const count=Array.isArray(apps)&&apps.length<=256?apps.length:undefined;
    if(!app)return count===0?{availability:'none',runningCount:0}:{availability:'unavailable'};
    const appId=decimalId(app.appid,4294967295n);if(!appId)return {availability:'unavailable'};
    const gameId=decimalId(app.gameid,18446744073709551615n);
    return {availability:count!==undefined&&count!==1?'ambiguous':'observed',appId,...(gameId?{gameId}:{}),...(count!==undefined?{runningCount:count}:{})};
  }catch{return {availability:'unavailable'};}
}
