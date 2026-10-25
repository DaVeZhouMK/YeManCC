import type {MouseModePresentation} from './mouseModePresentation';

export const MOUSE_STATUS_HINT_MS=2500;
// Observe policy state, not packet identity or its periodic sequence number.
export function createMouseStatusHint(io:{emit:(state:MouseModePresentation|null)=>void;setTimer:(fn:()=>void,ms:number)=>unknown;clearTimer:(id:unknown)=>void}) {
 let timer:unknown=null,disposed=false,initialized=false,enabled=true,sequence=0,ymccRevision=0;
 let selected:string|null=null,settledKey:string|null=null,enteredAuto=false,suppressSettlement=false,ymccVisit=false;
 const hide=()=>{if(timer!==null){io.clearTimer(timer);timer=null;}io.emit(null);};
 return {
  accept(state:MouseModePresentation|null,nextEnabled=true){
   if(disposed)return;
   const enableChanged=enabled!==nextEnabled;enabled=nextEnabled;
   if(enableChanged)hide();
   if(!state){hide();return;}
   if(state.sequence>0&&sequence>0&&state.sequence<sequence)return;
   if(state.sequence>0)sequence=state.sequence;
   const selection=state.mode+'|'+state.provider;
   if(initialized&&selected!==selection){enteredAuto=state.mode==='auto'&&state.provider==='ymcc';suppressSettlement=false;}
   selected=selection;
   const auto=state.mode==='auto'&&state.provider==='ymcc'&&state.controlAllowed;
   const eligible=auto&&state.ok&&state.stateKnown&&!state.pending;
   // Every YMCC foreground visit and its eventual return receipt are silent,
   // including pending backend work between hide/minimize and desktop restore.
   if(state.scene==='ymcc'||(state.ymccVisitRevision??0)>ymccRevision)ymccVisit=true;
   ymccRevision=Math.max(ymccRevision,state.ymccVisitRevision??0);
   if(!auto&&state.scene!=='ymcc')ymccVisit=false;
   if(enableChanged){enteredAuto=false;suppressSettlement=!eligible;}
   if(!enabled||!auto){enteredAuto=false;hide();}
   if(!eligible){
    hide();
    // First receipt may be pending; its eventual result is a quiet baseline.
    if(!initialized&&auto)suppressSettlement=true;
    initialized=true;return;
   }
   const key=state.on?'mouse':'controller';
   const changed=initialized&&(enteredAuto||(settledKey!==null&&settledKey!==key));
   settledKey=key;initialized=true;enteredAuto=false;
   if(ymccVisit){
    hide();suppressSettlement=false;
    if(state.scene!=='ymcc')ymccVisit=false;
    return;
   }
   if(!enabled||enableChanged||suppressSettlement){suppressSettlement=false;return;}
   if(!changed)return;
   hide();io.emit(state);
   timer=io.setTimer(()=>{timer=null;if(!disposed)io.emit(null);},MOUSE_STATUS_HINT_MS);
  },
  dispose(){if(disposed)return;disposed=true;hide();},
 };
}
