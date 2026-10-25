import assert from 'node:assert/strict';
/** Whole-window graph across the actual Vue editor and its actual iframe.
 * Every edge runs through the production native UI dispatcher/postMessage
 * consumers. Reachability paths are replayed without manually focusing targets.
 */
export async function performanceCombinedNavigation({f,page,command,height,mode,task}){
 const nodes=await f.evaluate(()=>{
  const frame=document.querySelector('.performance-simulation-window iframe'),w=frame.contentWindow;
  const parent=[...document.querySelectorAll('.simulation-editor button:not(:disabled),.simulation-editor input:not(:disabled)')].filter(e=>e.getBoundingClientRect().height>0);
  const child=w.YmccSimulationNavigation.collect(null);window.__performanceCombinedNodes=[...parent,...child];
  return window.__performanceCombinedNodes.map((e,i)=>({index:i,document:i<parent.length?'editor':'preview',label:e.getAttribute('aria-label')||e.id||e.textContent?.replace(/\s+/g,' ').trim().slice(0,90),edges:[]}));
 });
 const focus=async index=>{await f.evaluate(i=>{const e=window.__performanceCombinedNodes[i];if(e.ownerDocument!==document)document.querySelector('.performance-simulation-window iframe').focus({preventScroll:true});e.focus({preventScroll:true});},index);};
 const active=()=>f.evaluate(()=>{const a=document.activeElement,e=a?.tagName==='IFRAME'?a.contentDocument.activeElement:a;return window.__performanceCombinedNodes.indexOf(e);});
 const directions=['nav-left','nav-right','nav-up','nav-down'],issues=[];
 for(const node of nodes)for(const action of directions){await focus(node.index);await command(f,action);const to=await active();node.edges.push(to);if(to<0)issues.push({kind:'outside-window-candidate',from:node.label,action});}
 const paths=new Map([[0,[]]]),q=[0];while(q.length){const i=q.shift();for(let d=0;d<4;d++){const j=nodes[i].edges[d];if(j>=0&&!paths.has(j)){paths.set(j,[...paths.get(i),directions[d]]);q.push(j);}}}
 const returns=new Set([0]),rq=[0];while(rq.length){const j=rq.shift();for(let i=0;i<nodes.length;i++)if(nodes[i].edges.includes(j)&&!returns.has(i)){returns.add(i);rq.push(i);}}
 let replayed=0;
 for(const n of nodes){if(!paths.has(n.index)){issues.push({kind:'unreachable',label:n.label});continue;}if(!returns.has(n.index))issues.push({kind:'no-return',label:n.label});await focus(0);for(const action of paths.get(n.index)){await command(f,action);replayed++;}if(await active()!==n.index)issues.push({kind:'replay-mismatch',label:n.label,path:paths.get(n.index)});
  const visible=await f.evaluate(i=>{const e=window.__performanceCombinedNodes[i],r=e.getBoundingClientRect(),w=e.ownerDocument.defaultView;return r.left>=-2&&r.top>=-2&&r.right<=w.innerWidth+2&&r.bottom<=w.innerHeight+2&&!e.closest('[hidden],[inert]');},n.index);if(!visible)issues.push({kind:'clipped-focus',label:n.label});
 }
 const result={name:'performance/simulation/combined-'+mode,height,count:nodes.length,directions:nodes.length*4,replayed,issues,nodes,errors:[],physicalController:false};
 const fs=await import('node:fs');fs.writeFileSync(task+'/performance-combined-'+mode+'-'+height+'.json',JSON.stringify(result,null,2));
 if(issues.length)await page.locator('.screen').screenshot({path:task+'/evidence/performance-combined-failure-'+mode+'-'+height+'.png'});
 return result;
}
