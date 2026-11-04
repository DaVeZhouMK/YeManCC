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
  let stopped=false,reported=false,accountRevoked=false;
  const uiAccount=value.accountSource==='verified-steam-ui';
  const accountWatch=host.__YMCC_DECKY_ACCOUNT_WATCH__;
  const accountCurrent=()=>!uiAccount||(!accountRevoked&&accountWatch?.version===1&&accountWatch.read(value.account));
  let unsubscribeAccount=null;
  const accountChanged=()=>{
    if(stopped||accountRevoked||!uiAccount||accountCurrent())return;
    accountRevoked=true;
    if(host.__YMCC_DECKY_MIRROR__?.runId===value.runId){
      // Retain only build/run provenance for the native retained-document
      // check. Remove credentials immediately and let the existing client
      // reject pending intents before the parent receives the CDP marker.
      Object.defineProperty(host,'__YMCC_DECKY_MIRROR__',{value:Object.freeze({runId:value.runId,frontendBuildId:value.frontendBuildId,revoked:true}),configurable:true,writable:false});
      host.dispatchEvent(new Event('ymcc-decky-bootstrap'));
      if(typeof host[binding]==='function')host[binding](JSON.stringify({runId:value.runId,code:'decky-account-changed'}));
    }
  };
  if(uiAccount&&accountWatch?.version===1)unsubscribeAccount=accountWatch.subscribe(accountChanged);
  // At most the same four official boundaries read below are retained.
  // Attribute changes in a healthy Steam tree must not scan its descendants.
  let boundaryNodes=[];
  function read(){
    const nodes=host.document.querySelectorAll('.deckyErrorBoundary');
    boundaryNodes=Array.from(nodes).slice(0,4);
    for(let i=0;i<boundaryNodes.length;i++){
      const node=boundaryNodes[i];if(!node.getClientRects().length)continue;
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
      const trace=panel?.querySelector('pre code')?.textContent||'';
      // Truncation can hide a plugin caller and falsely blame the whole Loader.
      // Keep oversized evidence unknown, like the bounded native classifier.
      if(trace.length>8192)continue;
      // Network rejections can also be TypeError. A visible page alone must not
      // turn a routine failed fetch into a version/Steam compatibility verdict.
      const apiFailure=/TypeError:\s*Cannot read properties of (?:undefined|null)/.test(trace)||/TypeError:[^\r\n]{0,200}\bis not a function\b/.test(trace)||/ReferenceError:[^\r\n]{0,200}\bis not defined\b/.test(trace)||/Minified React error #\d+/.test(trace);
      if(!apiFailure)continue;
      if(/(?:https?:\/\/(?:localhost|127\.0\.0\.1):1337\/plugins\/|decky:\/\/decky\/(?:legacy_)?plugin\/)/.test(trace))continue;
      return 'decky-render-failed';
    }
    // A real native readback can observe that the reported fault recovered.
    // Re-arm then, without polling/scanning an already-reported page on mutations.
    reported=false;
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
    const touches=node=>!!node&&boundaryNodes.some(boundary=>boundary===node||boundary.contains(node)||node.contains?.(boundary));
    if(records.some(r=>{
      const target=r.target?.nodeType===1?r.target:r.target?.parentElement;
      // A class mutation can create a boundary on an existing element. Cached
      // containment also catches hidden ancestors and boundary class removal.
      if(target?.matches?.('.deckyErrorBoundary')||touches(target))return true;
      if(r.type!=='childList')return false;
      // Only actual inserted subtrees need discovery. Removal invalidates the
      // small cache so a formerly fifth boundary can become one of the first4.
      return Array.from(r.removedNodes||[]).some(touches)||Array.from(r.addedNodes||[]).some(n=>n.nodeType===1&&(n.matches?.('.deckyErrorBoundary')||n.querySelector?.('.deckyErrorBoundary')));
    }))scan();
  });
  const dispose=()=>{if(stopped)return;stopped=true;unsubscribeAccount?.();observer.disconnect();host.removeEventListener('pagehide',dispose);host.removeEventListener('ymcc-decky-health-stop',dispose);if(host[key]===api)delete host[key];};
  const api={runId:value.runId,read,scan,dispose,accountCurrent};
  Object.defineProperty(host,key,{value:api,configurable:true});
  // Context creation may precede <html>; Document itself is always available.
  observer.observe(host.document,{childList:true,subtree:true,characterData:true,attributes:true,attributeFilter:['class','style','hidden','aria-hidden']});
  host.addEventListener('pagehide',dispose);host.addEventListener('ymcc-decky-health-stop',dispose);scan();accountChanged();
})
)YMCCHEALTH";}
}
