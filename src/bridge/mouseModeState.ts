import {shallowRef} from 'vue';
import {admitMouseModePresentation,type MouseModePresentation} from './mouseModePresentation';
export const mouseModeState=shallowRef<MouseModePresentation|null>(null);
export function acceptMouseModeState(raw:unknown):void {
 const next=admitMouseModePresentation(raw);if(!next)return;
 const old=mouseModeState.value;if(old&&next.sequence>0&&old.sequence>next.sequence)return;
 // Carry the observed visit across throttled HUD frames. The HUD may see only
 // the return state after a quick summon/minimize pair.
 next.ymccVisitRevision=Math.max(old?.ymccVisitRevision??0,next.ymccVisitRevision??0)+(next.scene==='ymcc'&&old?.scene!=='ymcc'?1:0);
 mouseModeState.value=next;
}
