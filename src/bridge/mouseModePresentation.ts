export type MousePolicyMode='auto'|'on'|'off';
export interface MouseModePresentation {
 mode:MousePolicyMode;on:boolean;stateKnown:boolean;provider:'ymcc'|'steam';
 controlAllowed:boolean;mappingVerified:boolean;verification:string;scene:string;
 pending:boolean;ok:boolean;sequence:number;policyReason:string;ymccVisitRevision?:number;error?:string;
}
export function admitMouseModePresentation(raw:unknown):MouseModePresentation|null {
 const s=raw as Partial<MouseModePresentation>|null;
 if(!s||!['auto','on','off'].includes(s.mode??'')||typeof s.on!=='boolean'||typeof s.stateKnown!=='boolean'||!['ymcc','steam'].includes(s.provider??''))return null;
 return {mode:s.mode!,on:s.on,stateKnown:s.stateKnown,provider:s.provider!,controlAllowed:s.controlAllowed===true,mappingVerified:s.mappingVerified===true,verification:typeof s.verification==='string'?s.verification:'unobserved',scene:typeof s.scene==='string'?s.scene:'unknown',pending:s.pending===true,ok:s.ok===true,sequence:Number.isSafeInteger(s.sequence)?s.sequence!:0,policyReason:typeof s.policyReason==='string'?s.policyReason:'',ymccVisitRevision:Number.isSafeInteger(s.ymccVisitRevision)&&s.ymccVisitRevision!>=0?s.ymccVisitRevision!:0,...(typeof s.error==='string'?{error:s.error}:{})};
}
export function mouseModeText(s:MouseModePresentation|null):{title:string;detail:string;icon:string} {
 if(!s)return {title:'状态待确认',detail:'模拟鼠标',icon:'mouse'};
 if(s.provider==='steam')return {title:'Steam 管理',detail:'只读',icon:'steam'};
 if(s.pending)return {title:'正在切换',detail:{auto:'自动',on:'开启',off:'关闭'}[s.mode],icon:'mouse'};
 if(!s.ok||!s.stateKnown)return {title:'状态待确认',detail:s.error||'模拟鼠标',icon:'warning'};
 if(s.on)return {title:'鼠标模式',detail:(s.mode==='on'?'手动开启':'自动 · 桌面')+(s.verification==='process-liveness'?' · 映射待确认':''),icon:'mouse'};
 if(s.mode==='auto'&&s.scene==='ymcc')return {title:'手柄模式',detail:'自动暂停 · YMCC',icon:'gamepad'};
 if(s.mode==='auto')return {title:'手柄模式',detail:s.scene==='unknown'?'前台待确认':'自动 · 非桌面',icon:'gamepad'};
 return {title:'已关闭',detail:'模拟鼠标',icon:'mouse'};
}
export function manualSidebarMouse(s:MouseModePresentation|null):boolean {return !!s&&s.mode==='on'&&s.on&&s.stateKnown&&s.ok&&!s.pending&&s.provider==='ymcc'&&s.controlAllowed;}
