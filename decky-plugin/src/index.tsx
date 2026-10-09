// YMCC 控制台: one passive mirror. No Python/main.py or own settings.
import { MirrorClient, type Bootstrap, type ClientState, type Choice, type MirrorSnapshot, type MirrorCommand } from './mirrorClient';
import { FRAME_RATE_CEILINGS } from '../../src/bridge/frameRateModel';
import { AnchoredDropdown } from './AnchoredDropdown';
import { PowerChoiceDebounce } from './powerChoiceDebounce';
import { observeSteamRunning } from './steamRunningContext';
import { installConsoleTab } from './qamPlacement';
import { useQuickAccessVisible } from '@decky/api';
declare const SP_REACT: any;
declare const DFL: any;
declare global { interface Window { __YMCC_DECKY_MIRROR__?: Bootstrap; } }
const React = SP_REACT;
const { PanelSection, PanelSectionRow, SliderField, DropdownItem, ToggleField } = DFL;
if (typeof useQuickAccessVisible !== 'function') throw new Error('YMCC QAM 可见性需要 Decky API 2；未启动镜像连接');
if (!SliderField || !DropdownItem || !ToggleField) throw new Error('YMCC 需要 Steam 原生调节控件；未启动镜像连接');
const client = new MirrorClient({ bootstrap: () => window.__YMCC_DECKY_MIRROR__, socket: url => new WebSocket(url),
  schedule: (action, delay) => setTimeout(action, delay), cancel: handle => clearTimeout(handle), clientId: crypto.randomUUID(),steam:() => observeSteamRunning(DFL.Router) });
const powerChoice = new PowerChoiceDebounce({ state: () => client.snapshot(), send: choice => client.mutate('game.setField', {field:choice.field,value:choice.value}, choice.snapshot), schedule:(action,delay) => setTimeout(action,delay), cancel:handle => clearTimeout(handle) });
const bootstrapListener = () => client.bootstrapChanged();
window.addEventListener('ymcc-decky-bootstrap',bootstrapListener);
const directContent = <Content />;
const consoleIcon = <span aria-label="YMCC 控制台" style={{ display: 'inline-flex', alignItems: 'center', justifyContent: 'center', width: '24px', height: '24px', lineHeight: 1, fontSize: '20px', fontWeight: 700 }}>Y</span>;
const placement = installConsoleTab(DFL, window, directContent, consoleIcon);
const fieldLabels: Record<string, string> = { acMode: '交流电方案', dcMode: '电池方案', corePolicyMode: '核心调度',
  hyperThreadPolicy: '超线程', padPersona: '虚拟手柄', gyroEnabled: '陀螺仪', gyroPreset: '陀螺仪预设', speedFactor: '游戏变速', losslessScaling: 'LosslessScaling 插帧' };
const labelStyle = { display: 'flex', justifyContent: 'space-between', alignItems: 'baseline', gap: '8px', width: '100%', fontSize: '12px', lineHeight: '16px' };
const smallLabel = (label: string) => <span style={{ fontSize: '12px' }}>{label}</span>;
const description = (label: string) => <span style={{ fontSize: '11px', lineHeight: '15px' }}>{label}</span>;
const touchpadLabels:Record<string,string>={layout:'屏幕触摸板',singleMode:'触摸板映射',leftMode:'左侧映射',rightMode:'右侧映射',summonPosition:'YMCC呼出',specialMask:'专用按键',rearMask:'背部按键'};
function menuField(snapshot:MirrorSnapshot|undefined|null,key:string) {
  if(key.startsWith('touchpad.'))return snapshot?.touchpads?.fields[key.slice(9)];
  if(key==='acCeiling'||key==='dcCeiling'){
    const side=key==='acCeiling'?'ac':'dc',setting=snapshot?.frames?.pair[side];
    return setting?{value:String(setting.ceiling),choices:FRAME_RATE_CEILINGS.map(value=>({data:String(value),label:value===0?'不锁帧':`${value} FPS`})),supported:!!snapshot?.actions?.frames,inherited:!!snapshot?.game&&!snapshot.frames?.dedicated}:undefined;
  }
  return snapshot?.game?.fields[key];
}
function menuCommand(key:string):MirrorCommand {return key.startsWith('touchpad.')?'touchpad.setField':key==='acCeiling'||key==='dcCeiling'?'frame.setField':'game.setField';}
function Content() {
  const [state, setState] = React.useState(client.snapshot());
  const [powerPending, setPowerPending] = React.useState(powerChoice.snapshot());
  const visible = useQuickAccessVisible();
  const [frameDraft, setFrameDraft] = React.useState(null) as [number|null,(value:number|null)=>void];
  const [menuActive, setMenuActive] = React.useState(false);
  const menu = React.useRef(null) as { current: null | { snapshot: MirrorSnapshot; field: string; selecting: boolean; timer: ReturnType<typeof setTimeout> | null } };
  const sessionVisible = visible || menuActive;
  React.useEffect(() => client.subscribe((next: ClientState) => {
    powerChoice.observe(next);
    const owned=menu.current, current=next.snapshot;
    if(owned && !owned.selecting && (!next.connected || !current?.ready || current.runId!==owned.snapshot.runId || current.revision!==owned.snapshot.revision || current.generation!==owned.snapshot.generation || current.game?.identity!==owned.snapshot.game?.identity)){
      if(owned.timer)clearTimeout(owned.timer); menu.current=null; setMenuActive(false);
    }
    setState(next);
  }), []);
  React.useEffect(() => powerChoice.subscribe(() => setPowerPending(powerChoice.snapshot())), []);
  React.useEffect(() => { if (!visible) { powerChoice.cancel(); const owned=menu.current;
    if(owned && !owned.selecting){if(owned.timer)clearTimeout(owned.timer);menu.current=null;setMenuActive(false);}
  } }, [visible]);
  React.useEffect(() => { client.setVisible(sessionVisible); return () => client.setVisible(false); }, [sessionVisible]);
  React.useEffect(() => () => { if (menu.current?.timer) clearTimeout(menu.current.timer); menu.current = null; powerChoice.cancel(); }, []);
  const snapshot = (state as ClientState).snapshot;
  React.useEffect(()=>setFrameDraft(null),[snapshot?.revision,snapshot?.powerSource,snapshot?.game?.identity,visible]);
  const disabled = !visible || menuActive || !state.connected || !snapshot?.ready || state.busy;
  const apply = (command: MirrorCommand, args: Record<string, unknown>) => {
    if (disabled || !snapshot) return;
    powerChoice.cancel();
    void client.mutate(command, args, snapshot).catch(() => {});
  };
  const cancelMenu = () => {
    if (menu.current?.selecting) return;
    if (menu.current?.timer) clearTimeout(menu.current.timer);
    menu.current = null; setMenuActive(false);
  };
  const openMenu = (fieldName: string, show: () => void) => {
    const current = client.snapshot(), field = menuField(current.snapshot,fieldName);
    if (!visible || menu.current || current.busy || !current.connected || !current.snapshot?.ready || !(menuCommand(fieldName)==='touchpad.setField'?current.snapshot.actions?.touchpads:menuCommand(fieldName)==='frame.setField'?current.snapshot.actions?.frames:current.snapshot.actions?.game) || !field?.supported) return false;
    if (fieldName === 'gyroPreset' && current.snapshot.game?.fields.gyroEnabled?.value !== 'on') return false;
    powerChoice.cancel();
    menu.current = { snapshot: structuredClone(current.snapshot), field: fieldName, selecting: false, timer: null };
    const owned = menu.current;
    owned.timer = setTimeout(() => { if (menu.current === owned && !owned.selecting) cancelMenu(); }, 45000);
    setMenuActive(true);
    try { show(); } catch (error) { cancelMenu(); throw error; }
    // Steam ToggleMenu calls ShowMenu itself unless this hook explicitly returns false.
    return false;
  };
  const selectMenu = (fieldName: string, option: { data: string }) => {
    const owned = menu.current, field = menuField(owned?.snapshot,fieldName);
    if (!owned || owned.field !== fieldName || owned.selecting || !field || !field.choices.some(choice => choice.data === option?.data && !choice.disabled)) return;
    if (option.data === field.value && !field.inherited) { cancelMenu(); return; }
    powerChoice.cancel();
    owned.selecting = true;
    if (owned.timer) { clearTimeout(owned.timer); owned.timer = null; }
    void client.mutate(menuCommand(fieldName), { field: fieldName.startsWith('touchpad.')?fieldName.slice(9):fieldName, value: option.data }, owned.snapshot).catch(() => {}).finally(() => {
      if (menu.current === owned) { menu.current = null; setMenuActive(false); }
    });
  };
  const slider = (label: string, value: string, choices: Choice[], unavailable: boolean, field: string, command: 'game.setField' | 'fan.setPreset' = 'game.setField') => {
    const displayedValue = (field === 'acMode' || field === 'dcMode') && powerPending?.field === field ? powerPending.value : value;
    const index = choices.findIndex(choice => choice.data === displayedValue);
    const blocked = disabled || unavailable || index < 0 || choices.length < 2;
    return <SliderField className="ymcc-decky-slider" label={<span style={labelStyle}>
      <span style={{ flexShrink: 0, whiteSpace: 'nowrap' }}>{label}</span><span style={{ minWidth: 0, fontSize: '11px', fontWeight: 400, textAlign: 'right' }}>{index < 0 ? '不可用' : choices[index].label}</span>
    </span>} layout="below" bottomSeparator="none" value={Math.max(0, index)} min={0} max={Math.max(1, choices.length - 1)} step={1}
      validValues="steps" minimumDpadGranularity={1} notchCount={choices.length} notchTicksVisible={true} showValue={false} editableValue={false}
      disabled={blocked} onChange={(next: number) => {
        if (blocked || !Number.isSafeInteger(next) || next < 0 || next >= choices.length || (next === index && field !== 'acMode' && field !== 'dcMode') || choices[next]?.disabled) return;
        if ((field === 'acMode' || field === 'dcMode') && snapshot) { powerChoice.choose(field, choices[next].data, snapshot); return; }
        apply(command, command === 'fan.setPreset' ? { preset: choices[next].data } : { field, value: choices[next].data });
      }} />;
  };
  const side=snapshot?.powerSource;
  const frameSetting=side?snapshot?.frames?.pair[side]:undefined;
  const frameLabel=side==='ac'?'插电锁帧 AC':'电池锁帧 DC';
  const frameBlocked=disabled || !snapshot?.actions?.frames || !frameSetting || !side;
  const renderDropdown=(key:string,label:string)=>{
    const data=menuField(snapshot,key);if(!data)return null;
    const owned=menu.current?.field===key;
    return <PanelSectionRow key={key}><AnchoredDropdown label={smallLabel(label)} selectedOption={data.value} rgOptions={data.choices}
      opened={menuActive && owned} disabled={(disabled && !owned)||!data.supported || !(menuCommand(key)==='touchpad.setField'?snapshot?.actions?.touchpads:menuCommand(key)==='frame.setField'?snapshot?.actions?.frames:snapshot?.actions?.game)}
      onMenuWillOpen={(show:()=>void)=>openMenu(key,show)} onCancel={cancelMenu} onChange={(option:{data:string})=>selectMenu(key,option)}/></PanelSectionRow>;
  };
  return <div className="ymcc-decky-sidebar">
    <style>{`.ymcc-decky-sidebar .ymcc-decky-slider { padding-top: 4px !important; padding-bottom: 4px !important; }
.ymcc-decky-sidebar [role="combobox"], .ymcc-decky-sidebar [role="combobox"] span { font-size: 12px !important; }
.ymcc-anchored-dropdown { width: 100%; }
.ymcc-anchored-dropdown > div:first-child { display: grid !important; grid-template-columns: 76px minmax(0,1fr); column-gap: 8px; width: 100%; }
.ymcc-anchored-dropdown > div:first-child > div { min-width: 0; width: auto !important; padding-left: 0 !important; padding-right: 0 !important; }
.ymcc-anchored-dropdown > div:first-child > div:last-child > div, .ymcc-anchored-dropdown > div:first-child > div:last-child > div > div { width: 100% !important; min-width: 0; box-sizing: border-box; }
.ymcc-anchored-dropdown [role="combobox"] { width: 100% !important; min-width: 0; box-sizing: border-box; margin: 0 !important; }
.ymcc-anchored-options { margin: 2px 0 6px auto; width: calc(100% - 84px); box-sizing: border-box; max-height: 260px; overflow-y: auto; background: #202a36; border: 1px solid #5b7187; border-radius: 4px; }
.ymcc-anchored-option { box-sizing: border-box; overflow-wrap: anywhere; padding: 7px 10px; min-height: 20px; font-size: 12px; cursor: pointer; }
.ymcc-anchored-option[aria-disabled="true"] { opacity: .45; cursor: default; }
.ymcc-anchored-option:focus, .ymcc-anchored-option.gpfocus { background: #445a70; outline: 2px solid #b4d7f7; outline-offset: -2px; }
.ymcc-anchored-cancel { border-top: 1px solid #5b7187; }
.ymcc-decky-sidebar .ymcc-decky-notice { font-size: 11px; line-height: 15px; overflow-wrap: anywhere; }`}</style>
    <PanelSection>
      <PanelSectionRow><div style={{ fontSize: '12px', lineHeight: '16px', padding: '4px 0 8px' }}>{snapshot?.game?.label ?? 'YMCC 控制台'}</div></PanelSectionRow>
      {snapshot?.game && Object.entries(fieldLabels).map(([field, label]) => {
        const data = snapshot.game!.fields[field];
        if (!data || field === 'acMode' && snapshot.powerSource !== 'ac' || field === 'dcMode' && snapshot.powerSource !== 'dc') return null;
        const unavailable = !snapshot.actions?.game || !data.supported;
        if (field === 'padPersona' || field === 'gyroPreset' || field === 'corePolicyMode' || field === 'speedFactor') return <PanelSectionRow key={field}><AnchoredDropdown label={smallLabel(label)} selectedOption={data.value}
          rgOptions={data.choices} opened={menuActive && menu.current?.field === field} disabled={(disabled && menu.current?.field !== field) || unavailable || field === 'gyroPreset' && snapshot.game!.fields.gyroEnabled?.value !== 'on'} onMenuWillOpen={(show: () => void) => openMenu(field,show)}
          onCancel={cancelMenu} onChange={(option: {data:string}) => selectMenu(field,option)} /></PanelSectionRow>;
        if (field === 'gyroEnabled') return <PanelSectionRow key={field}><ToggleField label={smallLabel(label)} checked={data.value === 'on'}
          bottomSeparator="none" disabled={disabled || unavailable || !['on','off'].includes(data.value)}
          onChange={(enabled:boolean) => { if(!unavailable && ['on','off'].includes(data.value) && typeof enabled === 'boolean' && enabled !== (data.value === 'on')) apply('game.setField',{field,value:enabled?'on':'off'}); }} /></PanelSectionRow>;
        if (field === 'losslessScaling') return <PanelSectionRow key={field}><ToggleField label={smallLabel(label)} checked={data.value === 'on'}
          bottomSeparator="none" disabled={disabled || unavailable || !['on', 'off'].includes(data.value)}
          onChange={(enabled: boolean) => { if (!unavailable && typeof enabled === 'boolean') apply('game.setField', { field, value: enabled ? 'on' : 'off' }); }} /></PanelSectionRow>;
        if (field === 'hyperThreadPolicy') return <PanelSectionRow key={field}><ToggleField label={smallLabel(label)}
          checked={data.value === 'on'}
          bottomSeparator="none" disabled={disabled || unavailable || !['default', 'on', 'off'].includes(data.value)}
          onChange={(enabled: boolean) => { if (!unavailable && ['default', 'on', 'off'].includes(data.value) && typeof enabled === 'boolean') {
            const value = enabled ? 'on' : 'off'; if (value !== data.value) apply('game.setField', { field, value });
          } }} /></PanelSectionRow>;
        return <PanelSectionRow key={field}>{slider(label, data.value, data.choices, unavailable, field)}</PanelSectionRow>;
      })}
      {frameSetting && side && <PanelSectionRow><SliderField className="ymcc-decky-slider" label={<span style={labelStyle}><span>{frameLabel}</span><span style={{fontSize:'11px'}}>{frameSetting.fps===0?'不锁帧':`${frameDraft??frameSetting.fps} FPS`}</span></span>}
        layout="below" bottomSeparator="none" value={frameDraft??frameSetting.fps} min={frameSetting.fps===0?0:20} max={frameSetting.ceiling||20} step={5}
        validValues="steps" minimumDpadGranularity={5} showValue={false} editableValue={false} disabled={frameBlocked||frameSetting.fps===0}
        onChange={(value:number)=>{if(!frameBlocked && frameSetting.fps!==0 && Number.isSafeInteger(value)&&value>=20&&value<=frameSetting.ceiling)setFrameDraft(value);}}
        onChangeComplete={(value:number)=>{setFrameDraft(null);if(!frameBlocked && frameSetting.fps!==0 && Number.isSafeInteger(value)&&value>=20&&value<=frameSetting.ceiling && (value!==frameSetting.fps||!!snapshot?.game&&!snapshot.frames?.dedicated))apply('frame.setField',{field:side+'Fps',value:String(value)});}}/></PanelSectionRow>}
      {frameSetting && side && renderDropdown(side+'Ceiling','锁帧上限')}
      {snapshot?.touchpads && <PanelSectionRow><div style={{fontSize:'12px',padding:'8px 0 4px'}}>触摸板（全局）</div></PanelSectionRow>}
      {snapshot?.touchpads && Object.entries(touchpadLabels).map(([field,label])=>{
        const layout=snapshot.touchpads!.fields.layout?.value;
        if(field==='singleMode'&&layout!=='single'||(field==='leftMode'||field==='rightMode')&&layout!=='dual')return null;
        return renderDropdown('touchpad.'+field,label);
      })}
      <PanelSectionRow><ToggleField label={smallLabel('风扇控制（全局）')} checked={snapshot?.fan.enabled ?? false}
        description={description(`跟随 YMCC 全局挡位：${snapshot?.fan.choices.find(choice => choice.data === snapshot.fan.preset)?.label ?? '待读取'}`)}
        bottomSeparator="none" disabled={disabled || !snapshot?.actions?.fan || !snapshot?.fan.canToggle}
        onChange={(enabled: boolean) => { if (snapshot?.actions?.fan && snapshot.fan.canToggle && typeof enabled === 'boolean' && enabled !== snapshot.fan.enabled) apply('fan.setEnabled', { enabled }); }} /></PanelSectionRow>
      {snapshot && <PanelSectionRow>{slider('风扇挡位（全局）', snapshot.fan.preset, snapshot.fan.choices,
        !snapshot.fan.enabled || !snapshot.fan.supported || !snapshot.actions?.fan, '', 'fan.setPreset')}</PanelSectionRow>}
      <PanelSectionRow><div className="ymcc-decky-notice" role="status">{powerPending ? '电源档位待应用：停止切换 3 秒后生效' : state.notice}</div></PanelSectionRow>
      {!placement.active && <PanelSectionRow><div className="ymcc-decky-notice">当前 Steam/Loader 未提供独立控制台接线，请从 Decky 插件列表打开</div></PanelSectionRow>}
      {snapshot?.steam?.appId && <PanelSectionRow><div className="ymcc-decky-notice">Steam 观测 AppID {snapshot.steam.appId} · 不用于方案匹配</div></PanelSectionRow>}
    </PanelSection>
  </div>;
}
export default () => ({ alwaysRender: true, name: 'YMCC 控制台', titleView: <div style={{ fontSize: '14px' }}>YMCC 控制台</div>, content: placement.active ? null : directContent,
  icon: consoleIcon, onDismount: () => {
    window.removeEventListener('ymcc-decky-bootstrap',bootstrapListener); powerChoice.dispose(); client.dispose(); placement.dispose();
  } });
