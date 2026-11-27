// Memory settings and the external IPC port only; semantic API wrappers are real.
const control=()=>{const value=(globalThis as any).__backgroundFixture;if(!value)throw Error('BACKGROUND_FIXTURE_BOUNDARY_NOT_INSTALLED');return value;};
export async function invoke<T>(op:string,args:unknown={},options?:unknown):Promise<T>{return control().invoke(op,args,options);}
export function on(name:string,listener:(value:unknown)=>void){const fixture=control();fixture.subscriptions++;const stop=fixture.listen?.(name,listener);return()=>{fixture.subscriptions--;stop?.();};}
export function getUiSetting(key:string){return control().settings[key];}
export function getUiSettings(){return {...control().settings};}
export function uiSettingsLoaded(){return true;}
export async function setUiSettings(patch:unknown){await control().writeSettings(patch);}
