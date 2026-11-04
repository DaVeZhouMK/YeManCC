import {onBeforeUnmount,onMounted,readonly,shallowRef} from 'vue';
import {invoke,on} from './ipc';
import {isControllerPersona,type ControllerPersona} from './controllerPersona';

export interface InputShortcutRuntimeSnapshot {
  active:boolean;persona:ControllerPersona;virtualEnabled:boolean;gyroEnabled:boolean;
}
const state=shallowRef<InputShortcutRuntimeSnapshot|null>(null);
let owners=0,generation=0,revision=0,stop:(()=>void)|undefined;
function accept(raw:unknown):void {
  const value=raw as Partial<InputShortcutRuntimeSnapshot>|null;
  if(!value||!isControllerPersona(value.persona)||typeof value.virtualEnabled!=='boolean'||typeof value.gyroEnabled!=='boolean')return;
  revision++;state.value={active:value.active===true,persona:value.persona,virtualEnabled:value.virtualEnabled,gyroEnabled:value.gyroEnabled};
}
// The sidebar and its page share one initial read and the existing native event.
// No timer, process detector or settings polling belongs to this presentation.
export function useInputShortcutRuntime(){
  let retained=false;
  onMounted(()=>{
    if(retained)return;retained=true;if(++owners!==1)return;
    const epoch=++generation,priorRevision=revision;
    stop=on('input.shortcutRuntime',accept);
    void invoke<InputShortcutRuntimeSnapshot>('input.shortcutRuntime.get',{}).then(value=>{
      if(owners&&generation===epoch&&revision===priorRevision)accept(value);
    }).catch(()=>{});
  });
  onBeforeUnmount(()=>{
    if(!retained)return;retained=false;if(--owners)return;
    ++generation;stop?.();stop=undefined;state.value=null;
  });
  return readonly(state);
}
