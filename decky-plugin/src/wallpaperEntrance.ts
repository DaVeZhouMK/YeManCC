export type WallpaperEntrance = 'fade'|'drift'|'reveal';
export const wallpaperEntranceMs=(style:WallpaperEntrance)=>style==='fade'?420:style==='drift'?520:600;
// One finite compositor animation. No frame callback, canvas copy or timer.
export function enterWallpaper(node:HTMLVideoElement,style:WallpaperEntrance='fade'):Animation|null {
  if(typeof node.animate!=='function'||node.ownerDocument.defaultView?.matchMedia?.('(prefers-reduced-motion: reduce)').matches)return null;
  const opacity=node.style.opacity||'1';
  const frames:Keyframe[]=style==='drift'?[{opacity:0,transform:'scale(1.025) translateY(8px)'},{opacity,transform:'scale(1) translateY(0)'}]:style==='reveal'?[{opacity:0,clipPath:'inset(0 100% 0 0)'},{opacity,clipPath:'inset(0 0% 0 0)'}]:[{opacity:0},{opacity}];
  return node.animate(frames,{duration:wallpaperEntranceMs(style),easing:'cubic-bezier(.2,.7,.2,1)',fill:'none'});
}
