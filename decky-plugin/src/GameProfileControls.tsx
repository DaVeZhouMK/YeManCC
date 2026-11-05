import type {MirrorSnapshot} from './mirrorClient';
declare const SP_REACT:any;declare const DFL:any;const React=SP_REACT;
export function GameProfileControls(props:{snapshot:MirrorSnapshot|null;disabled:boolean;pending:boolean;change:(value:'dedicated'|'global',snapshot:MirrorSnapshot)=>void}) {
 const {snapshot,disabled,pending,change}=props,game=snapshot?.game,field=game?.fields.profile;
 const dedicated=field?field.value==='dedicated':snapshot?.performance?.scope==='game';
 const [removal,setRemoval]=React.useState(null as {identity:string;sourceRevision:string}|null);
 const action=React.useRef(null),cancelButton=React.useRef(null),removalOwner=React.useRef(null),alive=React.useRef(true);
 const blocked=disabled||pending||!game||!field?.supported||!field.sourceRevision||!snapshot?.actions?.game;
 const source=JSON.stringify([snapshot?.runId,snapshot?.generation,game?.identity,field?.sourceRevision,field?.value,field?.supported]);
 const latest=React.useRef({snapshot,source,blocked});latest.current={snapshot,source,blocked};
 React.useEffect(()=>{alive.current=true;return()=>{alive.current=false;removalOwner.current=null;};},[]);
 React.useEffect(()=>{removalOwner.current=null;setRemoval(null);},[source,disabled]);
 React.useEffect(()=>{if(removal)cancelButton.current?.focus();},[removal]);
 function cancel(event?:any){event?.stopPropagation?.();event?.preventDefault?.();if(removal&&removalOwner.current!==removal)return true;removalOwner.current=null;setRemoval(null);if(alive.current&&!latest.current.blocked)action.current?.focus();return true;}
 const button=(text:string,handler:()=>void,ref?:any)=><DFL.Focusable ref={ref} className="ymcc-profile-action" role="button" aria-disabled={blocked} focusable={!blocked} tabIndex={blocked?-1:0} onActivate={()=>{if(alive.current&&!blocked&&!latest.current.blocked&&latest.current.source===source)handler();}}>{text}</DFL.Focusable>;
 return <DFL.PanelSectionRow><div className="ymcc-profile-header" data-ymcc-profile-state={dedicated?'dedicated':'global'}>
   <div className="ymcc-profile-title"><strong>{dedicated?'专属模式':'专属配置'}</strong><span className="ymcc-profile-game" title={game?.label}>{game?.label||'未识别到游戏'}</span>
    {button(dedicated?'删除配置':'设为专属',()=>{if(!snapshot||!game||!field)return;if(dedicated){const owned={identity:game.identity,sourceRevision:field.sourceRevision??''};removalOwner.current=owned;setRemoval(owned);}else change('dedicated',latest.current.snapshot!);},action)}</div>
   <div className="ymcc-profile-hint">{pending?'等待当前调整应用后再切换配置':game&&!field?.sourceRevision?'重新打开 YMCC 后，可在此设置专属配置':dedicated?'仅对当前游戏生效':game?'使用全局配置 · 设为专属将导入当前配置':'使用全局配置 · 识别到游戏后可设为专属'}</div>
   {!!removal&&<DFL.Focusable className="ymcc-profile-confirm" focusable={false} flow-children="column" onCancel={cancel}>
    <div>删除当前游戏的专属配置并恢复全局配置？</div>
    <DFL.Focusable className="ymcc-profile-confirm-actions" focusable={false} flow-children="row">
     {button('取消',cancel,cancelButton)}
     {button('删除并恢复全局',()=>{if(removalOwner.current===removal&&snapshot&&game?.identity===removal.identity&&(field?.sourceRevision??'')===removal.sourceRevision){removalOwner.current=null;setRemoval(null);change('global',latest.current.snapshot!);}})}
    </DFL.Focusable>
   </DFL.Focusable>}
  </div></DFL.PanelSectionRow>;
}
