// Steam owns the controls and focus tree; the shared owner supplies all values.
import {normalizeSliderValue,visiblePerformanceControls,cpuGpuPreferenceText,type PerformanceMirrorView,type PerformanceScalar,type PerformanceControl} from './performanceModel';
import {AnchoredDropdown} from './AnchoredDropdown';
declare const SP_REACT:any;declare const DFL:any;const React=SP_REACT;
export function PerformanceControls(props:{view:PerformanceMirrorView;disabled:boolean;draft:Record<string,PerformanceScalar>;change:(field:string,value:PerformanceScalar)=>void;begin?:()=>void;end?:()=>void;status?:string}) {
  const {PanelSectionRow,SliderField,ToggleField}=DFL,{view,disabled,draft,change}=props;
  const [menu,setMenu]=React.useState(null as string|null);
  React.useEffect(()=>{setMenu(null);},[view.side,view.scope,view.mode,view.sourceRevision,disabled]);
  function render(c:PerformanceControl){const value=draft[c.key]??c.value,blocked=disabled||!view.enabled||!c.enabled;
    // Cap only the low-power slider; rendering never normalizes the saved bank.
    if(c.kind==='slider'){const preference=c.key==='bias',max=c.key==='coreSplit'?Math.min(25,c.max):c.max;const sliderValue=Math.max(c.min,Math.min(Number(value),max));return <div className={preference?'ymcc-performance-bias':undefined}><SliderField className={preference?'ymcc-decky-slider ymcc-decky-bias-slider':'ymcc-decky-slider'} label={<span className="ymcc-performance-label"><span>{preference?'CPU / GPU 优先倾向':c.label}</span><span>{preference?cpuGpuPreferenceText(Number(value)):draft[c.key]===undefined&&c.display?c.display:String(value)+(c.suffix??'')}</span></span>}
      layout="below" bottomSeparator="none" min={c.min} max={max} step={c.step} value={sliderValue} notchCount={Math.min(5,Math.floor((max-c.min)/c.step)+1)} notchTicksVisible={true} validValues="steps" minimumDpadGranularity={c.step} showValue={false} editableValue={false} disabled={blocked||c.min===max} onChangeStart={()=>{if(!blocked)props.begin?.();}} onChangeComplete={()=>props.end?.()} onChange={(next:number)=>{const value=normalizeSliderValue(next,c.min,max,c.step);if(!blocked&&value!==null)change(c.key,value);}}/>{preference&&<div className="ymcc-bias-directions" aria-hidden="true"><span>CPU 优先</span><span>均衡</span><span>GPU 优先</span></div>}</div>;}
    // Steam's native CheckboxBase supports controlled=true. Keep its focus/node
    // identity across unrelated TDP saves; props alone now handle failed rollback.
    if(c.kind==='toggle')return <ToggleField key={c.key} controlled={true} label={c.label} checked={value===true} bottomSeparator="none" disabled={blocked} onChange={(next:boolean)=>{if(!blocked)change(c.key,next);}}/>;
    if(c.presentation==='buttons')return <DFL.Focusable className="ymcc-performance-mode" role="group" aria-label={c.label} focusable={false} flow-children="row">{c.choices.map(o=><DFL.Focusable key={o.data} role="button" tabIndex={blocked||o.disabled?-1:0} focusable={!blocked&&!o.disabled} aria-disabled={blocked||!!o.disabled} aria-pressed={value===o.data} onActivate={()=>{if(!blocked&&!o.disabled)change(c.key,o.data);}}>{o.label}</DFL.Focusable>)}</DFL.Focusable>;
    return <AnchoredDropdown label={c.label} selectedOption={String(value)} rgOptions={c.choices} disabled={blocked} opened={menu===c.key} onMenuWillOpen={()=>{if(blocked)return false;setMenu(c.key);return false;}} onCancel={()=>setMenu(null)} onChange={(next:{data:string})=>{setMenu(null);if(!blocked)change(c.key,next.data);}}/>;
  }
  // Decky projects the owner's settings. Hiding coreHigh must never reset or
  // submit it; defaults and existing per-game values remain owned by YMCC.
  const schedulingKeys=view.mode==='auto'?['coreSplit','coreLow','hyperThreadMode']:['coreMode','hyperThreadMode'];
  const visible=visiblePerformanceControls(view).filter(c=>c.key!=='coreHigh');
  const coreControls=schedulingKeys.flatMap(key=>visible.filter(c=>c.key===key));
  const controls=visible.filter(c=>!schedulingKeys.includes(c.key));
  const savingIndex=controls.findIndex(c=>c.key==='saving');
  if(savingIndex>=0&&controls.some(c=>c.key==='bias')){
    const [saving]=controls.splice(savingIndex,1);
    controls.splice(controls.findIndex(c=>c.key==='bias')+1,0,saving);
  }
  function rows(items:PerformanceControl[]){const seen=new Set<string>();return items.map(c=>{if(c.group){if(seen.has(c.group))return null;seen.add(c.group);const members=items.filter(x=>x.group===c.group);return <PanelSectionRow key={c.group}>{members.length===1?render(members[0]):<DFL.Focusable className="ymcc-performance-pair" data-ymcc-performance-row={c.group} role="group" focusable={false} flow-children="row">{members.map(x=><div key={x.key}>{render(x)}</div>)}</DFL.Focusable>}</PanelSectionRow>;}return <PanelSectionRow key={c.key}>{render(c)}</PanelSectionRow>;});}
  return <><PanelSectionRow><div className="ymcc-performance-context"><span>{view.scope==='game'?'游戏专属':'全局配置'} · {view.side==='ac'?'交流电 AC':view.side==='dc'?'电池 DC':'供电待确认'}</span><span className="ymcc-performance-status" role="status" aria-live="polite">{props.status??''}</span></div></PanelSectionRow>
    {rows(controls)}
    {coreControls.length>0&&<PanelSectionRow><div className="ymcc-section-heading">核心调度</div></PanelSectionRow>}
    {rows(coreControls)}
    {!!view.feedback&&<PanelSectionRow><div className="ymcc-decky-notice">{view.feedback}</div></PanelSectionRow>}</>;
}
