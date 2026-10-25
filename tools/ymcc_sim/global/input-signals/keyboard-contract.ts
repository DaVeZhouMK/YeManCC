/** Independent keyboard identity contract: Windows KeyboardEvent.code names,
 * Windows virtual-key values and shift/extended semantics; no input APIs. */
import { actionById, CONTROLLER_SHORTCUT_ACTIONS, defaultParameters,
  newControllerShortcutRule, readControllerShortcutRules, validateControllerShortcutRules } from '../../../../src/bridge/controllerShortcutRules';
const series=(prefix:string,first:number,last:number)=>Array.from({length:last-first+1},(_,i)=>prefix+(i+first));
export const KEYBOARD_GROUPS = [
 {id:'keyboard.numeric',defaultKey:'Digit1',codes:[...series('Digit',0,9),...series('F',1,24),...series('Numpad',0,9),'NumLock','NumpadDivide','NumpadMultiply','NumpadSubtract','NumpadAdd','NumpadDecimal','NumpadEnter']},
 {id:'keyboard.symbol',defaultKey:'Backquote',codes:['Backquote',...series('Digit',0,9),'Minus','Equal','BracketLeft','BracketRight','Backslash','Semicolon','Quote','Comma','Period','Slash']},
 {id:'keyboard.letter',defaultKey:'KeyA',codes:Array.from({length:26},(_,i)=>'Key'+String.fromCharCode(65+i))},
 {id:'keyboard.leftFunction',defaultKey:'Enter',codes:['Tab','Space','Backquote','Minus','Equal','BracketLeft','BracketRight','Backslash','Semicolon','Quote','Comma','Period','Slash','ShiftLeft','AltLeft','Enter','MetaLeft','CapsLock','Escape','ControlLeft','Backspace']},
 {id:'keyboard.rightFunction',defaultKey:'PrintScreen',codes:['ShiftRight','AltRight','ControlRight','MetaRight','ContextMenu','ArrowUp','ArrowDown','ArrowLeft','ArrowRight','PrintScreen','ScrollLock','Pause','Insert','Home','PageUp','Delete','End','PageDown']},
] as const;
const VK:Record<string,number>={Backquote:0xc0,Minus:0xbd,Equal:0xbb,BracketLeft:0xdb,BracketRight:0xdd,Backslash:0xdc,Semicolon:0xba,Quote:0xde,Comma:0xbc,Period:0xbe,Slash:0xbf,
 Tab:9,Space:32,ShiftLeft:0xa0,ShiftRight:0xa1,AltLeft:0xa4,AltRight:0xa5,ControlLeft:0xa2,ControlRight:0xa3,MetaLeft:0x5b,MetaRight:0x5c,Enter:13,Backspace:8,CapsLock:0x14,Escape:0x1b,
 ContextMenu:0x5d,ArrowUp:0x26,ArrowDown:0x28,ArrowLeft:0x25,ArrowRight:0x27,PrintScreen:0x2c,ScrollLock:0x91,Pause:0x13,Insert:0x2d,Home:0x24,PageUp:0x21,Delete:0x2e,End:0x23,PageDown:0x22,
 NumLock:0x90,NumpadDivide:0x6f,NumpadMultiply:0x6a,NumpadSubtract:0x6d,NumpadAdd:0x6b,NumpadDecimal:0x6e,NumpadEnter:13};
const extended=new Set(['ControlRight','AltRight','ArrowUp','ArrowDown','ArrowLeft','ArrowRight','PrintScreen','Insert','Home','PageUp','Delete','End','PageDown','NumLock','NumpadDivide','NumpadEnter']);
export function keyboardMappingCases() {
 return KEYBOARD_GROUPS.flatMap(group=>group.codes.map(code=>({actionId:group.id,code,
  virtualKey:/^Digit[0-9]$/.test(code)?code.charCodeAt(5):/^Key[A-Z]$/.test(code)?code.charCodeAt(3):/^F\d+$/.test(code)?0x70+Number(code.slice(1))-1:/^Numpad[0-9]$/.test(code)?0x60+Number(code.slice(6)):VK[code],
  requiresShift:group.id==='keyboard.symbol',extended:extended.has(code)})));
}
export function inspectKeyboardActionContracts(){
 return KEYBOARD_GROUPS.map(group=>{
  const action=actionById(group.id),parameter=action?.parameters?.[0],values=parameter?.options?.map(o=>o.value)??[];
  const rule=newControllerShortcutRule('contract-default-'+group.id,group.id);
  return {actionId:group.id,count:values.length,expectedCount:group.codes.length,defaultKey:parameter?.default,
   defaultInOptions:values.includes(String(parameter?.default)),defaultProblems:validateControllerShortcutRules([rule]),
   missing:group.codes.filter(code=>!values.includes(code)),unexpected:values.filter(code=>!group.codes.includes(code)),
   defaultMatches:parameter?.default===group.defaultKey};
 });
}
export function assertKeyboardActionContract(){
 const check=(ok:unknown,message:string)=>{if(!ok)throw Error(message);};
 check(CONTROLLER_SHORTCUT_ACTIONS.filter(a=>a.id.startsWith('keyboard.')).map(a=>a.id).sort().join('|')===KEYBOARD_GROUPS.map(g=>g.id).sort().join('|'),'keyboard action identities changed');
 let checked=0;
 for(const group of KEYBOARD_GROUPS){
  const action=actionById(group.id),parameter=action?.parameters?.[0],values=parameter?.options?.map(o=>o.value)??[];
  check(action?.available&&action.scope==='native-global'&&parameter?.presentation==='key-grid',group.id+' key-grid metadata');
  check(values.length===new Set(values).size,group.id+' duplicate key identity');
  check(values.slice().sort().join('|')===group.codes.slice().sort().join('|'),group.id+' keyboard identity set changed');
  check(parameter?.default===group.defaultKey&&values.includes(group.defaultKey),group.id+' default must use an admitted code');
  for(const code of group.codes){
   const rule={...newControllerShortcutRule('contract-'+group.id+'-'+code,group.id),params:{key:code}};
   check(rule.trigger==='press'&&!validateControllerShortcutRules([rule]).length,group.id+' valid code rejected: '+code);
   const read=readControllerShortcutRules({shortcutRules:[rule]});
   check(read.length===1&&read[0].params.key===code,group.id+' code lost or renamed on reload: '+code);checked++;
  }
  const created=newControllerShortcutRule('default-'+group.id,group.id);
  check(created.params.key===group.defaultKey&&defaultParameters(group.id).key===group.defaultKey&&!validateControllerShortcutRules([created]).length,group.id+' default rule rejected');
  for(const code of ['InvalidKey','Tilde','F25','Digit10','Numpad10']){
   const rule={...created,params:{key:code}};check(validateControllerShortcutRules([rule]).length>0,group.id+' invalid code accepted: '+code);
  }
 }
 return {groups:KEYBOARD_GROUPS.length,validMappings:checked,negativeCodesPerGroup:5,status:'PASS'};
}
