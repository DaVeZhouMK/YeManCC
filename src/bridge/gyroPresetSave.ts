import {readGameInputEditor,saveGameInputEditor,type GameInputEditor} from './gameInputEditor';
import type {SettingsCasResult} from './settingsRepository';
interface SavePort {read:()=>Promise<GameInputEditor>;save:typeof saveGameInputEditor}
const sameOwner=(a:GameInputEditor,b:GameInputEditor)=>a.ownerEpoch===b.ownerEpoch&&a.target.exe===b.target.exe&&a.target.pid===b.target.pid&&a.target.processCreated===b.target.processCreated&&a.target.generation===b.target.generation&&!!a.profile===!!b.profile;
/** A native revision advance must not leave the gyro page permanently saving
 * against its original revision. Rebase an explicit edit once, within the
 * identical global owner. Unknown writes, protected starts and per-game edits
 * retain their normal rejection; no retry crosses a game/profile boundary.
 */
export async function saveGyroEditorPatch(editor:GameInputEditor,patch:Record<string,any>,isCurrent:()=>boolean,port:SavePort={read:readGameInputEditor,save:saveGameInputEditor}):Promise<{editor:GameInputEditor;result:SettingsCasResult}>{
 let current=editor;
 let result=await port.save(current,patch,isCurrent);
 if(result.ok===false&&result.reason==='revision-conflict'&&!editor.profile&&isCurrent()){
  const next=await port.read();
  if(!sameOwner(editor,next)||!isCurrent())throw new Error('当前编辑对象已变化，请重新调整');
  current=next;result=await port.save(current,patch,isCurrent);
 }
 // Keep a confirmed conflict snapshot for the next explicit attempt as well.
 // A refused/unknown write never supplies permission to advance the editor.
 if(result.ok||result.ok===false&&result.reason==='revision-conflict')current={...current,input:result.value};
 return {editor:current,result};
}
