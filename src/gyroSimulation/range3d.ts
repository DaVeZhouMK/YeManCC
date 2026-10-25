/** Optional renderer is loaded only when the gyro mini-game is open. */
import type { RangeState } from './model';
export interface Range3DRenderer {render(ctx:CanvasRenderingContext2D,w:number,h:number,state:RangeState):void;renderScopeFlash(ctx:CanvasRenderingContext2D,w:number,h:number,state:RangeState):void;dispose():void;inspect():Record<string,unknown>}
export async function loadRange3D():Promise<Range3DRenderer>{
 const url=new URL('./gyro-simulation/range3d.js',document.baseURI).href;
 const module=await import(/* @vite-ignore */ url) as {createRange3D():Promise<Range3DRenderer>};
 return module.createRange3D();
}
