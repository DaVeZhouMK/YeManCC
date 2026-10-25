// Read-only geometry bridge: size the production editor to the viewport first, then fit the left renderer.
(function(){
'use strict';
if(window.parent===window)return;
window.addEventListener('message',event=>{
 if(event.source!==window.parent||event.origin!==location.origin||event.data?.type!=='ymcc-simulation-editor-layout')return;
 const value=event.data.layout;
 if(!value||!['width','height','left','top','right','gap'].every(key=>Number.isFinite(value[key])&&value[key]>=0&&value[key]<=10000)||value.width<300||value.height<500)return;
 const root=document.documentElement;
 root.style.setProperty('--editor-width',value.width+'px');
 root.style.setProperty('--simulation-workspace-height',value.height+'px');
 root.style.setProperty('--simulation-top-inset',Math.max(0,value.top-48)+'px');
 const leftWidth=Math.max(1,value.left-12-value.gap),leftScale=Math.min(leftWidth/811,value.height/628);
 root.style.setProperty('--simulation-left-scale',String(leftScale));
 root.style.setProperty('--simulation-left-logical-width',leftWidth/leftScale+'px');
 root.style.setProperty('--simulation-left-logical-height',value.height/leftScale+'px');
 root.dataset.editorLayout='measured';
 root.dataset.leftScale=String(leftScale);
 root.dataset.editorHeight=String(value.height);
});
})();
