import { invoke } from './ipc';
export interface SteamMonitorValues { position:number;detail:number;scale:number;saturation:number;opacity:number; }
export interface SteamMonitorState extends SteamMonitorValues {ok:boolean;available:boolean;pending:boolean;restartRequired?:boolean;reason?:string;via?:string;}
// One native entry for monitor + the two legacy Steam adapters. Native owns
// serialization/account binding and never edits Steam files while it is live.
export const steamSettings = {
  get<T>(scope:'monitor'|'mouse'|'overlay'):Promise<T>{return invoke<T>('steam.settings.get',{scope});},
  set<T>(patch:{monitor?:Partial<SteamMonitorValues>;mousePercent?:number;overlayOffFix?:boolean}):Promise<T>{return invoke<T>('steam.settings.set',patch);},
};
export const STEAM_MONITOR_POSITIONS = [
  {value:0,label:'关闭监控'},{value:1,label:'左上角'},{value:5,label:'顶部居中'},
  {value:2,label:'右上角'},{value:3,label:'右下角'},{value:6,label:'底部居中'},{value:4,label:'左下角'},
];
export const STEAM_MONITOR_DETAILS = [
  {value:1,label:'FPS 单个值'},{value:2,label:'FPS 详情'},
  {value:3,label:'FPS详情、CPU及GPU利用率'},{value:4,label:'FPS、CPU、GPU和RAM完整详情'},
];
export function steamMonitorMessage(state:Partial<SteamMonitorState>):string {
  if(state.pending && state.restartRequired)return '设置已排队，请退出并重新开启 Steam；待修改项目会统一合并写入。';
  if(state.pending)return '设置已排队，正在通过 Steam 联动统一应用。';
  if(state.ok && state.via==='live')return '已通过 Steam 实时应用。';
  if(state.ok && state.via==='file')return '已保存 Steam 设置，下次启动 Steam 生效。';
  switch(state.reason){
    case 'localconfig-not-found':return '未找到可读取的 Steam 账号配置。';
    case 'steam-account-changed':return 'Steam 账号已改变，未将上一个账号的设置写入当前账号。';
    case 'localconfig-changed':return 'Steam 配置已被其它操作修改，未覆盖；请重试。';
    case 'settings-write-failed':case 'write-failed':return '保存失败，请检查文件权限后重试。';
    case 'backup-failed':return '原配置备份失败，未修改 Steam 配置。';
    case 'live-transport-uncertain':case 'live-timeout':case 'live-readback-failed':return 'Steam 调用结果未确认，未追加文件写入；请在 Steam 设置中核对。';
    case 'invalid-monitor-settings':return '参数超出 Steam 支持范围，未修改配置。';
    default:return state.reason?'Steam 设置暂不可用：'+state.reason:'Steam设置－游戏中－性能监控；无法实时应用时需重启 Steam。';
  }
}
