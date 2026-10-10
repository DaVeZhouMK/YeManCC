// Global touchpad profile slots only. Never uses an EXE or game-override persona.
import { SCREEN_TOUCHPAD_LAYOUTS, SCREEN_SUMMON_POSITIONS, LEFT_TOUCHPAD_MODES, RIGHT_TOUCHPAD_MODES, SINGLE_TOUCHPAD_MODES, STANDALONE_SPECIAL_MODES,
  screenTouchpadProfile, type ScreenTouchpadState, type ScreenTouchpadConfig } from './screenTouchpads';
import type { MirrorField } from '../../decky-plugin/src/mirrorClient';
import type { PowerLifecycleState } from './api';
import type { RelayMutationContext } from './deckyMirrorRelay';
export interface TouchpadMirrorRead {persona:string;virtualEnabled:boolean;state:ScreenTouchpadState;source:string;fields:Record<string,MirrorField>;}
export function touchpadMirrorFields(persona:string,virtualEnabled:boolean,state:ScreenTouchpadState):Record<string,MirrorField> {
  const profile=screenTouchpadProfile(persona),ps4=persona==='dualshock4';
  const ps=profile==='dualsense-edge'&&virtualEnabled&&(state.ps5Available===true||state.ps4Available===true);
  const deck=profile==='steamdeck'&&virtualEnabled&&state.steamDeckAvailable===true;
  const supported=state.ok===true&&state.available===true;
  const modes=(options:readonly {value:string;label:string}[])=>options.filter(option=>(option.value!=='steamdeck'||profile==='steamdeck')&&(option.value!=='dualsense'||profile==='dualsense-edge'))
    .map(option=>({data:option.value,label:ps4?option.label.replace('PS5','PS4'):option.label,disabled:option.value==='steamdeck'&&!deck||option.value==='dualsense'&&!ps}));
  const specials=profile==='steamdeck'?[['0','关闭'],['1','只开启 Steam'],['2','只开启三点'],['3','都开启']]:ps4?[['0','关闭'],['1','都开启（PS）']]:profile==='dualsense-edge'?[['0','关闭'],['1','只开启 PS'],['2','只开启静音'],['3','都开启']]:profile==='elite'?[['0','关闭'],['1','都开启（Xbox）']]:[['0','关闭']];
  const rear=profile==='steamdeck'?[['10','L5+R5'],['5','L4+R4'],['15','全开启'],['0','全关闭']]:profile==='dualsense-edge'?[['5','LFN+RFN'],['10','LB+RB'],['15','全开启'],['0','全关闭']]:[['0','全关闭']];
  const field=(value:string,choices:MirrorField['choices'],allowed=true):MirrorField=>({value,choices,supported:supported&&allowed});
  return {
    layout:field(state.layout,SCREEN_TOUCHPAD_LAYOUTS.map(o=>({data:o.value,label:o.label}))),
    singleMode:field(state.singleMode,modes(SINGLE_TOUCHPAD_MODES),state.layout==='single'),
    leftMode:field(state.leftMode,modes(LEFT_TOUCHPAD_MODES),state.layout==='dual'),
    rightMode:field(state.rightMode,modes(RIGHT_TOUCHPAD_MODES),state.layout==='dual'),
    summonPosition:field(state.summonPosition,SCREEN_SUMMON_POSITIONS.map(o=>({data:o.value,label:o.label}))),
    specialMask:field(String(profile==='elite'||ps4?state.specialMask&1:state.specialMask),specials.map(([data,label])=>({data,label})),profile!=='disabled'),
    rearMask:field(String(state.rearMask),rear.map(([data,label])=>({data,label})),profile==='steamdeck'||profile==='dualsense-edge'),
    // The disabled slot uses its original independent-key setting, never a virtual button mask.
    ...(profile==='disabled'?{standaloneSpecialMode:field(state.standaloneSpecialMode??'off',
      STANDALONE_SPECIAL_MODES.map(o=>({data:o.value,label:o.label})),STANDALONE_SPECIAL_MODES.some(o=>o.value===state.standaloneSpecialMode))}:{}),
  };
}
export function touchpadMirrorSource(persona:string,virtualEnabled:boolean,state:ScreenTouchpadState):string {
  // Exclude runtime event/repaint/visible counters, include only saved config and capability gates.
  return JSON.stringify({persona,virtualEnabled,layout:state.layout,singleMode:state.singleMode,leftMode:state.leftMode,rightMode:state.rightMode,
    summonPosition:state.summonPosition,standaloneSpecialMode:state.standaloneSpecialMode,specialMask:state.specialMask,rearMask:state.rearMask,scale:state.scale,transparency:state.transparency,mouseSensitivity:state.mouseSensitivity,
    enabled:state.enabled,summonEnabled:state.summonEnabled,specialEnabled:state.specialEnabled,rearEnabled:state.rearEnabled,
    ok:state.ok,available:state.available,deck:state.steamDeckAvailable,ps5:state.ps5Available,ps4:state.ps4Available});
}
export function createTouchpadMirrorActions(deps:{allowed:()=>boolean;read:()=>Promise<TouchpadMirrorRead>;power:()=>Promise<PowerLifecycleState>;
  acquire:(owner:string)=>(()=>void)|null;set:(patch:Partial<ScreenTouchpadConfig>,persona:ReturnType<typeof screenTouchpadProfile>)=>Promise<ScreenTouchpadState>}) {
 return {async execute(args:Record<string,unknown>,context:RelayMutationContext){
  if(Object.keys(args).length!==2 || typeof args.field!=='string' || !['layout','singleMode','leftMode','rightMode','summonPosition','specialMask','rearMask','standaloneSpecialMode'].includes(args.field) || typeof args.value!=='string')throw Error('TOUCHPAD_MIRROR_INVALID_REQUEST');
  const checkpoint=()=>{context.checkpoint();if(!deps.allowed())throw Error('TOUCHPAD_MIRROR_NOT_READY');};checkpoint();
  const source=context.controlAdmission?.touchpads;if(typeof source!=='string')throw Error('TOUCHPAD_MIRROR_SOURCE_REQUIRED');
  const release=deps.acquire('decky-global-touchpad');if(!release)throw Error('TOUCHPAD_MIRROR_BUSY');
  const power=async()=>{checkpoint();const state=await deps.power();checkpoint();if(state.phase!=='ready'||!state.hardwareWritesAllowed||state.generation!==context.generation)throw Error('TOUCHPAD_MIRROR_POWER_NOT_READY');};
  try {await power();const read=await deps.read();checkpoint();if(read.source!==source)throw Error('TOUCHPAD_MIRROR_SOURCE_CHANGED');
    const field=read.fields[args.field],choice=field?.choices.find(o=>o.data===args.value);
    if(!field?.supported||!choice||choice.disabled)throw Error('TOUCHPAD_MIRROR_UNSUPPORTED');
    if(field.value===args.value)return {saved:false,applied:false,notice:'全局触摸板设置未改变'};
    const profile=screenTouchpadProfile(read.persona),value=args.value;
    const patch:Partial<ScreenTouchpadConfig>={};
    if(args.field==='layout'){
      patch.layout=value as ScreenTouchpadConfig['layout'];patch.enabled=value!=='off';
      if(value==='dual'&&profile==='dualsense-edge'){
        if(read.state.leftMode==='dualsense'&&read.fields.leftMode.choices.find(o=>o.data==='dualsense')?.disabled)patch.leftMode='wasd';
        if(read.state.rightMode==='dualsense'&&read.fields.rightMode.choices.find(o=>o.data==='dualsense')?.disabled)patch.rightMode='mouse';
      }
      if(value==='single'&&read.state.singleMode==='dualsense'&&read.fields.singleMode.choices.find(o=>o.data==='dualsense')?.disabled)patch.singleMode='mouse';
    } else if(args.field==='summonPosition'){patch.summonPosition=value as ScreenTouchpadConfig['summonPosition'];patch.summonEnabled=value!=='off';}
    else if(args.field==='standaloneSpecialMode'){patch.standaloneSpecialMode=value as ScreenTouchpadConfig['standaloneSpecialMode'];}
    else if(args.field==='specialMask'){patch.specialMask=Number(value);patch.specialEnabled=Number(value)!==0;}
    else if(args.field==='rearMask'){patch.rearMask=Number(value);patch.rearEnabled=Number(value)!==0;}
    else Object.assign(patch,{[args.field]:value});
    const final=await deps.read();checkpoint();if(final.source!==source)throw Error('TOUCHPAD_MIRROR_SOURCE_CHANGED');await power();
    const result=await deps.set(patch,profile);
    if(!result.ok)throw Error('TOUCHPAD_MIRROR_SAVE_FAILED');
    return {saved:true,applied:result.available===true,notice:'已保存到原全局触摸板设置；未创建游戏专属'};
  } finally {release();}
 }};
}
