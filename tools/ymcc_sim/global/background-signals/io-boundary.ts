// Only IO/transport/settings boundaries are replaced. Product consumers stay real.
const control=()=>{const value=(globalThis as any).__backgroundFixture;if(!value)throw Error('BACKGROUND_FIXTURE_BOUNDARY_NOT_INSTALLED');return value;};
export async function invoke<T>(op:string,args:unknown={},options?:unknown):Promise<T>{return control().invoke(op,args,options);}
export function on(..._args:unknown[]){control().subscriptions++;return()=>{control().subscriptions--;};}
export function getUiSetting(key:string){return control().settings[key];}
export function getUiSettings(){return {...control().settings};}
export function uiSettingsLoaded(){return true;}
export async function setUiSettings(patch:unknown){await control().writeSettings(patch);}
function forbidden(boundary:string):never{control().forbidden.push(boundary);throw Error('FORBIDDEN_REAL_IO:'+boundary);}
export const fs=new Proxy({}, {get:(_target,name)=>()=>forbidden('filesystem.'+String(name))}) as any;
export const registry=new Proxy({}, {get:(_target,name)=>()=>forbidden('registry.'+String(name))}) as any;
export const requestSteamHttp=(..._args:unknown[])=>Promise.reject(Error('FIXTURE_NETWORK_UNAVAILABLE'));
export const requestSteamJson=(..._args:unknown[])=>Promise.reject(Error('FIXTURE_NETWORK_UNAVAILABLE'));
export const power={};
