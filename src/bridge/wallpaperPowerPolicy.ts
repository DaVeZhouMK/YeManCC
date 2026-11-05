import {watch} from 'vue';
import {powerSourceMode, type PowerSourceMode} from './powerSource';
import {getUiSetting} from './uiSettings';

// This is the physical source published by the App's existing native binding,
// never the AC/DC profile being edited. No polling or extra native reader.
export class WallpaperPowerPolicy {
  private preference=false;
  private source:PowerSourceMode|null=null;
  private revision=0;
  update(preference:boolean, source:PowerSourceMode|null){
    const before=this.blocked;
    this.preference=preference;this.source=source;
    if(before!==this.blocked)++this.revision;
  }
  get blocked(){return this.preference&&this.source!=='ac';}
  get reason(){return !this.blocked?'':this.source==='dc'?'wallpaper-on-battery':'wallpaper-power-unknown';}
  capture(){return this.revision;}
  current(revision:number){return !this.blocked&&revision===this.revision;}
}
const policy=new WallpaperPowerPolicy();
function currentPolicy(){policy.update(getUiSetting('steamWallpaperPauseOnBattery'),powerSourceMode.value);return policy;}
export const wallpaperPowerBlocked=()=>currentPolicy().blocked;
export const wallpaperPowerReason=()=>currentPolicy().reason;
export const captureWallpaperPower=()=>currentPolicy().capture();
export const wallpaperPowerCurrent=(revision:number)=>currentPolicy().current(revision);
export function subscribeWallpaperPowerChanged(listener:(blocked:boolean)=>void):()=>void {
  let blocked=currentPolicy().blocked;
  const changed=()=>{const next=currentPolicy().blocked;if(next===blocked)return;blocked=next;listener(next);};
  const stop=watch(powerSourceMode,changed,{flush:'sync'});
  window.addEventListener('ui-settings:changed',changed);
  window.addEventListener('ui-settings:loaded',changed);
  return()=>{stop();window.removeEventListener('ui-settings:changed',changed);window.removeEventListener('ui-settings:loaded',changed);};
}
