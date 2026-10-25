// Actual installed Steam focus tree + native direction events. Never activates a setting.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {targets,connect} from './fixtures/decky_cdp.mjs';import {validation} from './fixtures/decky_task_paths.mjs';
const t=(await targets()).find(t=>t.title.startsWith('QuickAccess'));if(!t)throw Error('Actual Steam QAM required');const c=await connect(t.webSocketDebuggerUrl);
const report={actualInstalledSteam:true,nativeDirectionEventReplay:true,physicalControllerTested:false,hardwareWrites:0,settingActivations:0,cases:[]};
try{
 const data=await c.evaluate(`(()=>{
  const root=document.querySelector('.ymcc-decky-sidebar');if(!root)throw Error('Actual product sidebar missing');
  const node=e=>{let f=e?.[Object.keys(e).find(k=>k.startsWith('__reactFiber'))];for(let i=0;f&&i<40;i++,f=f.return){const n=f.memoizedProps?.node;if(n&&n.Element===e)return n;}throw Error('Native focus node missing');};
  const group=root.querySelector('[data-ymcc-performance-row="autoCore"]'),row=node(group),tree=row.Tree;
  const values=()=>[...root.querySelectorAll('[role=slider]')].map(e=>e.getAttribute('aria-valuenow'));
  const label=n=>{const e=n?.Element;return e?document.getElementById(e.getAttribute('aria-labelledby'))?.textContent||e.textContent?.trim():null;};
  const initialValues=values(),initialModes=[...root.querySelectorAll('.ymcc-performance-mode [role=button]')].map(e=>e.getAttribute('aria-pressed'));
  const high=row.m_rgChildren[0].FindFocusableNode(12),low=row.m_rgChildren[1].FindFocusableNode(11);if(!high||!low)throw Error('This live UI does not admit both native focus targets');
  const focus=n=>{n.Element.focus();n.UpdateParentActiveChild();};
  const direction=(n,button)=>{const event=new CustomEvent('vgp_ondirection',{detail:{button,is_repeat:false},bubbles:true,cancelable:true});n.Element.dispatchEvent(event);return {consumed:event.defaultPrevented,active:label(tree.GetLastFocusedNode()),inPair:group.contains(tree.GetLastFocusedNode()?.Element)};};
  focus(high);const right=direction(high,12),left=direction(low,11),up=direction(high,9);focus(low);const down=direction(low,10);
  const mode=root.querySelector('.ymcc-performance-mode'),modeRow=node(mode),auto=node(mode.querySelector('[role=button]'));focus(auto);const modeRight=direction(auto,12),manual=tree.GetLastFocusedNode(),modeLeft=direction(manual,11);
  const saving=[...root.querySelectorAll('.ymcc-anchored-dropdown [role=combobox]')].find(e=>document.getElementById(e.getAttribute('aria-labelledby'))?.textContent==='节能浮动'),smt=[...root.querySelectorAll('.ymcc-anchored-dropdown [role=combobox]')].find(e=>document.getElementById(e.getAttribute('aria-labelledby'))?.textContent==='超线程');
  const ticks=[...root.querySelectorAll('[role=slider]')].map(e=>{const tick=e.querySelector('.SliderControl + div > div > div'),r=tick.getBoundingClientRect();return {label:document.getElementById(e.getAttribute('aria-labelledby'))?.textContent,width:r.width,height:r.height,count:e.children[1]?.children.length};});
  return {rowLayout:row.GetLayout(),rowFocusable:row.m_Properties.focusable,right,left,up,down,modeLayout:modeRow.GetLayout(),modeRight,modeLeft,initialValues,finalValues:values(),initialModes,finalModes:[...mode.querySelectorAll('[role=button]')].map(e=>e.getAttribute('aria-pressed')),savingImmediatelyAboveSmt:saving.closest('.ymcc-anchored-dropdown').parentElement.nextElementSibling.contains(smt),ticks};
 })()`);
 assert.equal(data.rowLayout,2);assert.equal(data.rowFocusable,false);report.cases.push('core pair is registered as one non-focusable native horizontal group');
 assert.equal(data.right.active,'低功耗核心');assert.equal(data.right.inPair,true);assert.equal(data.right.consumed,true);report.cases.push('native RIGHT event moves high-power core focus to low-power core');
 assert.equal(data.left.active,'高功耗核心');assert.equal(data.left.inPair,true);assert.equal(data.left.consumed,true);report.cases.push('native LEFT event moves low-power core focus to high-power core');
 assert.equal(data.up.inPair,false);assert.equal(data.down.inPair,false);assert(data.up.active&&data.down.active);report.cases.push('native UP and DOWN leave the pair for surrounding controls');
 assert.equal(data.modeLayout,2);assert.equal(data.modeRight.active,'手动模式');assert.equal(data.modeLeft.active,'自动模式');report.cases.push('auto/manual mode focus follows the same native horizontal rule');
 assert.deepEqual(data.finalValues,data.initialValues);assert.deepEqual(data.finalModes,data.initialModes);report.cases.push('direction replay changes no slider values or selected mode');
 assert.equal(data.savingImmediatelyAboveSmt,true);report.cases.push('saving is immediately above SMT in the rendered native rows');
 assert(data.ticks.every(t=>Math.abs(t.width-2)<.1&&Math.abs(t.height-6)<.1&&t.count===(t.label==='风扇控制'?4:5)));report.cases.push('actual Steam ticks measure 2px by 6px with the preserved five/four marks');

 const opened=await c.evaluate(`(()=>{const e=[...document.querySelectorAll('.ymcc-anchored-dropdown [role=combobox]')].find(e=>document.getElementById(e.getAttribute('aria-labelledby'))?.textContent==='节能浮动');const value=e.textContent;e.click();return {id:e.id,value};})()`);
 await new Promise(r=>setTimeout(r,150));
 const menu=await c.evaluate(`(()=>{const node=e=>{let f=e?.[Object.keys(e).find(k=>k.startsWith('__reactFiber'))];for(let i=0;f&&i<40;i++,f=f.return){const n=f.memoizedProps?.node;if(n&&n.Element===e)return n;}throw Error('Native option node missing');};const list=document.querySelector('.ymcc-anchored-options'),selected=list.querySelector('[role=option][aria-selected=true]'),n=node(selected),tree=n.Tree;selected.focus();n.UpdateParentActiveChild();const fire=(target,button,type='vgp_ondirection')=>target.dispatchEvent(new CustomEvent(type,{detail:{button,is_repeat:false},bubbles:true,cancelable:true}));const before=selected.textContent;if(node(list).GetActiveDescendant()?.Element!==selected)throw Error('Selected option is not the native active descendant');fire(selected,10);const afterDown=tree.GetLastFocusedNode().Element.textContent;fire(tree.GetLastFocusedNode().Element,9);const afterUp=tree.GetLastFocusedNode().Element.textContent;fire(tree.GetLastFocusedNode().Element,2,'vgp_oncancel');return {layout:node(list).GetLayout(),before,afterDown,afterUp};})()`);
 assert.equal(menu.layout,1);assert.notEqual(menu.afterDown,menu.before);assert.equal(menu.afterUp,menu.before);report.cases.push('native dropdown options keep vertical UP/DOWN focus navigation');
 await new Promise(r=>setTimeout(r,120));
 const cancelled=await c.evaluate(`({open:!!document.querySelector('.ymcc-anchored-options'),focused:document.activeElement?.id===${JSON.stringify(opened.id)},value:document.getElementById(${JSON.stringify(opened.id)})?.textContent})`);
 assert.equal(cancelled.open,false);assert.equal(cancelled.focused,true);assert.equal(cancelled.value,opened.value);report.cases.push('native B cancels the menu, returns focus and preserves the selected saving level');
 report.details={...data,menu,cancelled};report.status='passed';
 fs.writeFileSync(path.join(validation,'PAIR-NATIVE-DIRECTION-REPLAY.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));
}finally{c.close();}
