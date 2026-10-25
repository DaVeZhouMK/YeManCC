#pragma once
// Generated from decky_sidebar_health.js by tools/decky_sidebar_health_embed.mjs.
namespace ymcc::deckymirror {
inline const char* healthScript(){return R"YMCCHEALTH(// YMCC observes the official error boundary; it never replaces Decky functions or clicks actions.
// Official evidence: v3.3.0 components/DeckyErrorBoundary.tsx + utils/errors.ts;
// issues #838/#988 report Steam-update render failures, #966 documents restart loops.
// A plugin-specific failure, a Steam-only error, or a network message is NOT a global fatal signal.
(value => {
  const host=window,key='__YMCC_DECKY_HEALTH__',binding='__YMCC_DECKY_HEALTH_V1';
  if(host!==host.top||host.location.origin!=='https://steamloopback.host'||!/^[a-f0-9]{32}$/.test(value.runId||'')||!/^v\d+\.\d+\.\d+$/.test(value.loaderVersion||''))return;
  const previous=host[key];
  if(previous?.runId===value.runId){previous.scan();return;}
  previous?.dispose?.();
  let stopped=false,reported=false;
  function read(){
    const nodes=host.document.querySelectorAll('.deckyErrorBoundary');
    for(let i=0;i<Math.min(nodes.length,4);i++){
      const node=nodes[i];if(!node.getClientRects().length)continue;
      let hidden=false;
      for(let el=node,depth=0;el&&depth<64;el=el.parentElement,depth++){
        const css=host.getComputedStyle(el);
        if(el.hidden||el.getAttribute('aria-hidden')==='true'||css.visibility==='hidden'||css.visibility==='collapse'||css.display==='none'||css.opacity==='0'){hidden=true;break;}
      }
      if(hidden)continue;
      const source=node.querySelector('.likely-occured-msg')?.textContent?.trim();
      if(source!=='This error likely occurred in the Decky frontend.')continue;
      const text=(node.querySelector('pre code')?.textContent||'').slice(0,2048);
      const version=/Decky Version:\s*(v\d+\.\d+\.\d+)(?:\[U\])?(?=\s|$)/.exec(text)?.[1];
      if(version!==value.loaderVersion||!text.includes('Error Reference:'))continue;
      // v3.3.0 defines a 'trace' class but does not put it on the actual trace.
      // Consume the rendered Trace panel, not an unused source constant.
      const panel=Array.from(node.querySelectorAll('.panel-section')).slice(0,8).find(p=>p.querySelector('.panel-header')?.textContent?.trim()==='Trace');
      const trace=(panel?.querySelector('pre code')?.textContent||'').slice(0,8192);
      // Network rejections can also be TypeError. A visible page alone must not
      // turn a routine failed fetch into a version/Steam compatibility verdict.
      const apiFailure=/TypeError:\s*Cannot read properties of (?:undefined|null)/.test(trace)||/TypeError:[^\r\n]{0,200}\bis not a function\b/.test(trace)||/ReferenceError:[^\r\n]{0,200}\bis not defined\b/.test(trace)||/Minified React error #\d+/.test(trace);
      if(!apiFailure)continue;
      if(/(?:https?:\/\/(?:localhost|127\.0\.0\.1):1337\/plugins\/|decky:\/\/decky\/(?:legacy_)?plugin\/)/.test(trace))continue;
      return 'decky-render-failed';
    }
    return '';
  }
  function scan(){
    if(stopped||reported||host.__YMCC_DECKY_MIRROR__?.runId!==value.runId)return;
    const code=read();if(!code||typeof host[binding]!=='function')return;
    reported=true;host[binding](JSON.stringify({runId:value.runId,code}));
  }
  // Only added/changed nodes under the small official boundary trigger a scan.
  // No timers, global error replacement, network requests or healthy-state polling.
  const observer=new host.MutationObserver(records=>{
    if(records.some(r=>(r.target?.nodeType===1?r.target:r.target?.parentElement)?.closest?.('.deckyErrorBoundary')||(r.type==='attributes'&&r.target?.querySelector?.('.deckyErrorBoundary'))||Array.from(r.addedNodes||[]).some(n=>n.nodeType===1&&(n.matches?.('.deckyErrorBoundary')||n.querySelector?.('.deckyErrorBoundary')))))scan();
  });
  const dispose=()=>{if(stopped)return;stopped=true;observer.disconnect();host.removeEventListener('pagehide',dispose);host.removeEventListener('ymcc-decky-health-stop',dispose);if(host[key]===api)delete host[key];};
  const api={runId:value.runId,read,scan,dispose};
  Object.defineProperty(host,key,{value:api,configurable:true});
  // Context creation may precede <html>; Document itself is always available.
  observer.observe(host.document,{childList:true,subtree:true,characterData:true,attributes:true,attributeFilter:['class','style','hidden','aria-hidden']});
  host.addEventListener('pagehide',dispose);host.addEventListener('ymcc-decky-health-stop',dispose);scan();
})
)YMCCHEALTH";}
}
