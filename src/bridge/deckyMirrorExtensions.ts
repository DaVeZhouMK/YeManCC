// Multiplex independently owned extensions over the existing authenticated ONE peer.
import type { MirrorPeer, MirrorRequest } from './deckyMirrorRelay';
interface Extension {prefix:string;connected(peer:MirrorPeer):void;request(event:MirrorRequest):void;}
const extensions=new Set<Extension>();let peer:MirrorPeer|null=null;
export function registerDeckyMirrorExtension(extension:Extension):()=>void {
  if([...extensions].some(e=>e.prefix===extension.prefix))throw Error('Mirror extension already owned: '+extension.prefix);
  extensions.add(extension);if(peer)extension.connected(peer);
  return()=>{extensions.delete(extension);};
}
export function dispatchDeckyMirrorPeer(value:MirrorPeer):void {
  peer=value.connected===false?null:value;
  for(const extension of extensions){try{extension.connected(value);}catch{}}
}
export function dispatchDeckyMirrorRequest(event:MirrorRequest):boolean {
  for(const extension of extensions)if(event.request?.command?.startsWith(extension.prefix)){
    try{extension.request(event);}catch{}return true;
  }
  // Extension commands never enter hardware mutation admission, even before
  // the main app has finished starting the display extension.
  return event.request?.command?.startsWith('hud.')===true||event.request?.command?.startsWith('wallpaper.')===true||event.request?.command?.startsWith('updater.')===true;
}
