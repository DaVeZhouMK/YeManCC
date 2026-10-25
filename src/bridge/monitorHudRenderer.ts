// Shared production Vue renderer: content-sized panel, optional full-viewport crosshair.
// Both output carriers consume the same already-admitted YMCC values.
import MouseModeWatermark from '../components/MouseModeWatermark.vue';
import type {MouseModePresentation} from './mouseModePresentation';
import type { HudDragPlacement } from './monitorHudDrag';
import { createApp, h, nextTick, shallowRef } from 'vue';
import { ageHudData, createHudAgeLease } from './monitorHudFreshness';
import MonitorHudTemplate from '../components/MonitorHudTemplate.vue';
import MonitorCrosshair from '../components/MonitorCrosshair.vue';
import { hudPlacement } from './monitorHudProtocol';
import { validHudViewport, type HudViewport } from './monitorHudViewport';
import { isHorizontalHudLayout, boundedHudData, hudFromUniformSnapshot, normalizeMonitorOutput, sameHudAppearance, sameHudLayoutAppearance, sameHudDisplayInput, sameHudData, type HudData, type MonitorOutputPreferences } from './monitorHudModel';
export function mountMonitorHud(container:HTMLElement, options:{preferences:MonitorOutputPreferences;data?:HudData;viewport?:HudViewport;mouseMode?:MouseModePresentation|null;onSize?:(width:number,height:number)=>void;onError?:(error:unknown)=>void}) {
  const initialPreferences=normalizeMonitorOutput(options.preferences,false);
  const initialData=boundedHudData(options.data??hudFromUniformSnapshot(null,Date.now(),null,initialPreferences.ymcc.fields.clock === true));
  const preferences=shallowRef(initialPreferences),data=shallowRef(ageHudData(initialData,Date.now()));
  const mouseMode=shallowRef(options.mouseMode??null);
  const mouseStatusEnabled=shallowRef(initialPreferences.mouseStatusEnabled);
  const viewport=shallowRef<HudViewport|null>(validHudViewport(options.viewport)?options.viewport:null),boardSize=shallowRef({width:1,height:1});
  const win=container.ownerDocument.defaultView;
  const ageLease=createHudAgeLease({now:()=>Date.now(),emit:sample=>{if(!sameHudData(data.value,sample,preferences.value.ymcc.fields))data.value=sample;},
    setTimer:(fn,ms)=>win?win.setTimeout(fn,ms):setTimeout(fn,ms),clearTimer:id=>win?win.clearTimeout(id as number):clearTimeout(id as ReturnType<typeof setTimeout>)});
  let disposed=false,observer:ResizeObserver|null=null,observed:Element|null=null;
  container.style.setProperty('pointer-events','none');
  container.style.setProperty('font-family',"'Segoe UI','Microsoft YaHei UI',sans-serif");
  const measure=()=>{
    const el=container.querySelector<HTMLElement>('[data-monitor-hud]');if(!el||disposed)return;
    if(observer&&observed!==el){if(observed)observer.unobserve(observed);observed=el;observer.observe(el);}
    if(isHorizontalHudLayout(preferences.value.ymcc.layout)&&!el.style.width)return;
    const rect=el.getBoundingClientRect(),width=Math.ceil(rect.width),height=Math.ceil(rect.height);
    if(boardSize.value.width!==width||boardSize.value.height!==height)boardSize.value={width,height};
    try{options.onSize?.(width,height);}catch(error){if(options.onError)options.onError(error);else throw error;}
  };
  const app=createApp({setup:()=>()=>{
    const appearance=preferences.value.ymcc;
    const panel=()=>h(MonitorHudTemplate,{appearance,fields:appearance.fields,data:data.value,onLayout:measure});
    const v=viewport.value;
    // A component preview has no authoritative screen geometry: never pretend its panel center is the screen center.
    if(!v||(!appearance.crosshair||appearance.crosshair==='none'))return panel();
    const a=v.workArea??{left:0,top:0,width:v.width,height:v.height};
    const p=hudPlacement(a.width,a.height,boardSize.value.width,boardSize.value.height,appearance.position);
    if(appearance.dragX!==undefined&&appearance.dragY!==undefined){p.left=Math.min(Math.max(0,a.width-boardSize.value.width),Math.round(appearance.dragX*a.width));p.top=Math.min(Math.max(0,a.height-boardSize.value.height),Math.round(appearance.dragY*a.height));}
    return h('div',{'data-monitor-scene':'',style:{position:'relative',width:v.width+'px',height:v.height+'px',pointerEvents:'none'}},[
      h('div',{'data-monitor-panel':'',style:{position:'absolute',left:(a.left+Math.min(p.left,Math.max(0,a.width-boardSize.value.width)))+'px',top:(a.top+Math.min(p.top,Math.max(0,a.height-boardSize.value.height)))+'px',width:a.width+'px',pointerEvents:'none'}},[panel()]),
      h(MonitorCrosshair,{kind:appearance.crosshair}),
      mouseMode.value?h('div',{style:{position:'absolute',left:a.left+'px',top:a.top+'px',width:a.width+'px',height:a.height+'px',pointerEvents:'none'}},[h(MouseModeWatermark,{state:mouseMode.value,enabled:mouseStatusEnabled.value})]):null,
    ]);
  }});
  const css=(MonitorHudTemplate as unknown as {__ymccHudCss?:string}).__ymccHudCss;
  const style=css?container.ownerDocument.createElement('style'):null;
  if(style){style.dataset.ymccHudStyle='owned';style.textContent=css!;container.ownerDocument.head.appendChild(style);}
  app.mount(container);
  if(win?.ResizeObserver&&options.onSize){observer=new win.ResizeObserver(measure);const el=container.querySelector('[data-monitor-hud]');if(el){observed=el;observer.observe(el);}}
  ageLease.accept(initialData);
  void nextTick(measure);
  return {
    update(next:MonitorOutputPreferences,value:HudData){
      if(disposed)return;
      mouseStatusEnabled.value=next.mouseStatusEnabled!==false;
      // Packets are independently admitted at the client boundary. Repeated
      // display inputs need no second reconstruction of the saved preset banks.
      const appearance=sameHudDisplayInput(preferences.value.ymcc,next?.ymcc)?preferences.value:normalizeMonitorOutput(next,false);
      const sample=boundedHudData(value);
      if(!sameHudAppearance(preferences.value.ymcc,appearance.ymcc)){
        const before=preferences.value.ymcc,after=appearance.ymcc;
        // Color, tint and absolute raster opacity cannot change panel geometry.
        // Keep explicit measurement for composition/FPS/hide switches as well
        // as viewport/ResizeObserver events; no observer is re-created here.
        const geometryChanged=!sameHudLayoutAppearance(before,after) ||
          before.hideUnavailable!==after.hideUnavailable || (before.fpsEmphasis!==false)!==(after.fpsEmphasis!==false);
        preferences.value=appearance;if(geometryChanged)void nextTick(measure);
      }
      ageLease.accept(sample);
    },
    setMouseMode(value:MouseModePresentation|null){if(!disposed)mouseMode.value=value;},
    setPlacement(p:HudDragPlacement){
      if(disposed)return;preferences.value={...preferences.value,ymcc:{...preferences.value.ymcc,...p}};
    },
    setViewport(next:HudViewport){
      if(disposed||!validHudViewport(next))return;
      const old=viewport.value,a=old?.workArea,b=next.workArea;
      if(old?.width===next.width&&old?.height===next.height&&a?.left===b?.left&&a?.top===b?.top&&a?.width===b?.width&&a?.height===b?.height)return;
      viewport.value={...next,...(b?{workArea:{...b}}:{})};void nextTick(measure);
    },
    measure,
    dispose(){if(disposed)return;disposed=true;ageLease.dispose();observer?.disconnect();observer=null;app.unmount();style?.remove();},
  };
}
