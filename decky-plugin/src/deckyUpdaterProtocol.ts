// Versioned messages on YMCC's existing authenticated mirror, never an arbitrary download API.
export type UpdaterAction='check'|'download'|'install';
export interface UpdaterView {protocol:1;revision:number;hostVersion:string;phase:string;targetVersion:string|null;targetHash:string|null;percent:number|null;canCheck:boolean;canDownload:boolean;canInstall:boolean;gameRunning:boolean;message:string;}
export interface UpdaterRequest {command:'updater.request';runId:string;clientId:string;id:number;action:UpdaterAction;version?:string;sha256?:string;}
const version=/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;
export const validUpdateTarget=(v:unknown,h:unknown)=>typeof v==='string'&&v.length<=40&&version.test(v)&&typeof h==='string'&&/^[a-f0-9]{64}$/i.test(h);
export function validUpdaterView(v:any):v is UpdaterView {return !!v&&v.protocol===1&&Number.isSafeInteger(v.revision)&&v.revision>0&&typeof v.hostVersion==='string'&&v.hostVersion.length<=40&&version.test(v.hostVersion)&&typeof v.phase==='string'&&v.phase.length<=32&&(v.targetVersion===null&&v.targetHash===null||validUpdateTarget(v.targetVersion,v.targetHash))&&(v.percent===null||Number.isFinite(v.percent)&&v.percent>=0&&v.percent<=100)&&['canCheck','canDownload','canInstall','gameRunning'].every(k=>typeof v[k]==='boolean')&&typeof v.message==='string'&&v.message.length<=600;}
export const updateTargetKey=(v:Pick<UpdaterView,'targetVersion'|'targetHash'>)=>v.targetVersion+'|'+v.targetHash?.toLowerCase();
