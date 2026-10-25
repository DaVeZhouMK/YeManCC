// Strict display placement contract; no input hooks, clocks or device access.
export interface HudDragPlacement {dragLocked:boolean;dragRevision:number;dragX?:number;dragY?:number;}
export function admitHudDragPlacement(raw:unknown):HudDragPlacement|null {
 if(!raw||typeof raw!=='object')return null;const p=raw as Record<string,unknown>;
 if(typeof p.dragLocked!=='boolean'||typeof p.dragRevision!=='number'||!Number.isSafeInteger(p.dragRevision)||p.dragRevision<0)return null;
 const has=p.dragX!==undefined||p.dragY!==undefined;
 if(has&&!(typeof p.dragX==='number'&&Number.isFinite(p.dragX)&&p.dragX>=0&&p.dragX<=1&&typeof p.dragY==='number'&&Number.isFinite(p.dragY)&&p.dragY>=0&&p.dragY<=1))return null;
 return {dragLocked:p.dragLocked,dragRevision:p.dragRevision,...(has?{dragX:p.dragX as number,dragY:p.dragY as number}:{})};
}
