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
  const current=()=>!stopped&&!accountRevoked&&host.__YMCC_DECKY_MIRROR__?.runId===value.runId&&host.__YMCC_DECKY_MIRROR__?.revoked!==true;
  // One currently observed official socket, no reconnect/auth/network action.
  // Read only CloseEvent primitives: its reason and URL may contain credentials.
  let socket=null,router=null,socketOpen=null,socketClose=null,socketOpened=false;
  const socketFacts=new Set();
  function releaseSocket(){
    const prior=socket,opened=socketOpen,closed=socketClose;
    socket=router=socketOpen=socketClose=null;socketOpened=false;
    try{if(prior&&opened)prior.removeEventListener('open',opened);}catch{}
    try{if(prior&&closed)prior.removeEventListener('close',closed);}catch{}
  }
  function observeSocket(){
    try{
      if(stopped||host.__YMCC_DECKY_MIRROR__?.runId!==value.runId||host.__YMCC_DECKY_MIRROR__?.revoked===true){releaseSocket();return;}
      const nextRouter=host.DeckyBackend,next=nextRouter?.ws;
      if(next===socket&&nextRouter===router)return;
      releaseSocket();
      if(!next||typeof next.addEventListener!=='function'||typeof next.removeEventListener!=='function')return;
      socket=next;router=nextRouter;socketOpened=next.readyState===1;
      const current=()=>!stopped&&socket===next&&router===nextRouter&&host.DeckyBackend===nextRouter&&nextRouter.ws===next&&
        host.__YMCC_DECKY_MIRROR__?.runId===value.runId&&host.__YMCC_DECKY_MIRROR__?.revoked!==true;
      socketOpen=()=>{try{if(current())socketOpened=true;}catch{}};
      socketClose=event=>{
        try{
          if(!current())return;
          const rawCode=event?.code,clean=event?.wasClean;
          const codeKnown=Number.isInteger(rawCode)&&rawCode>=0&&rawCode<=65535,cleanKnown=typeof clean==='boolean';
          const code=codeKnown?rawCode:0,wasClean=cleanKnown?clean:false;
          const signature=[codeKnown,code,cleanKnown,wasClean,socketOpened].join(':');
          if(socketFacts.has(signature)||socketFacts.size>=16)return;
          socketFacts.add(signature);
          let pendingCallsKnown=false,pendingCalls=0;
          try{
            const own=Object.getOwnPropertyDescriptor(nextRouter,'runningCalls');
            if(own&&Object.prototype.hasOwnProperty.call(own,'value')){
              const count=Object.getOwnPropertyDescriptor(Map.prototype,'size').get.call(own.value);
              if(Number.isSafeInteger(count)&&count>=0&&count<=0xffffffff){pendingCallsKnown=true;pendingCalls=count;}
            }
          }catch{} // Missing/non-Map sources remain unknown, never a zero count.
          const pendingFacts=pendingCallsKnown?{pendingCallsKnown,pendingCalls}:{pendingCallsKnown:false};
          try{host.console.info('[YMCC:DeckyHealth] '+JSON.stringify({schema:1,stage:'official-socket-close',ordinal:socketFacts.size,
            codeKnown,code,cleanKnown,wasClean,opened:socketOpened,...pendingFacts}));}catch{}
          // Reuse the existing same-run binding so optional close facts survive
          // Steam exports that omit console.info. This is never a health verdict.
          try{if(current()&&typeof host[binding]==='function')host[binding](JSON.stringify({runId:value.runId,code:'decky-socket-close',
            ordinal:socketFacts.size,codeKnown,closeCode:code,cleanKnown,wasClean,opened:socketOpened,...pendingFacts}));}catch{}
        }catch{} // Observation never affects official reconnect or native recovery.
      };
      next.addEventListener('open',socketOpen);next.addEventListener('close',socketClose);
    }catch{releaseSocket();}
  }

  // Existing health reads may explain an incomplete own frontend load. These
  // bounded scalars never prove readiness or grant another import/recovery.
  const frontendFacts=new Set();
  const frontendStamp=typeof value.frontendBuildId==='string'&&/^[a-f0-9]{64}$/.test(value.frontendBuildId)?value.runId+':'+value.frontendBuildId:null;
  function observeFrontendLoad(){
    try{
      if(stopped||!frontendStamp||frontendFacts.size>=16)return;
      const bridge=host.__YMCC_DECKY_MIRROR__;
      if(bridge?.runId!==value.runId||bridge.revoked===true||bridge.frontendBuildId!==value.frontendBuildId)return;
      const frontend=host.__YMCC_DECKY_FRONTEND__;
      if(frontend?.active===true&&frontend.buildId===value.frontendBuildId)return;
      const boot=host.__YMCC_DECKY_FRONTEND_BOOT__,update=host.__YMCC_DECKY_FRONTEND_UPDATE__,facts={};
      if(boot?.stamp===frontendStamp&&['waiting-loader','reading-enabled-plugin','waiting-receipt','official-load-in-progress','absent-or-disabled','invalid-plugin-contract','current','failed','canceled'].includes(boot.phase)&&
         Number.isInteger(boot.probes)&&boot.probes>=0&&boot.probes<=3&&typeof boot.dispatched==='boolean'){
        facts.bootPhase=boot.phase;facts.bootProbes=boot.probes;facts.bootDispatched=boot.dispatched;
      }
      if(update?.stamp===frontendStamp&&['pending','current','failed','canceled'].includes(update.phase)&&
         Number.isInteger(update.attempts)&&update.attempts>=0&&update.attempts<=3&&typeof update.pending==='boolean'){
        facts.updatePhase=update.phase;facts.updateAttempts=update.attempts;facts.updatePending=update.pending;
      }
      if(!Object.keys(facts).length)return;
      const signature=JSON.stringify(facts);if(frontendFacts.has(signature))return;
      frontendFacts.add(signature);
      host.console.info('[YMCC:DeckyHealth] '+JSON.stringify({schema:1,stage:'frontend-load-state',ordinal:frontendFacts.size,...facts}));
    }catch{} // Optional state/log failures never affect the original health read.
  }

  const uiAccount=value.accountSource==='verified-steam-ui';
  const accountWatch=host.__YMCC_DECKY_ACCOUNT_WATCH__;
  const accountCurrent=()=>!uiAccount||(!accountRevoked&&accountWatch?.version===1&&accountWatch.read(value.account));
  let unsubscribeAccount=null;
  const accountChanged=()=>{
    if(stopped||accountRevoked||!uiAccount||accountCurrent())return;
    accountRevoked=true;
    // Retain accountCurrent() for the native revocation check, not live observers.
    stopObserving();
    if(host.__YMCC_DECKY_MIRROR__?.runId===value.runId){
      // Retain only build/run provenance for the native retained-document
      // check. Remove credentials immediately and let the existing client
      // reject pending intents before the parent receives the CDP marker.
      Object.defineProperty(host,'__YMCC_DECKY_MIRROR__',{value:Object.freeze({runId:value.runId,frontendBuildId:value.frontendBuildId,revoked:true}),configurable:true,writable:false});
      host.dispatchEvent(new Event('ymcc-decky-bootstrap'));
      if(typeof host[binding]==='function')host[binding](JSON.stringify({runId:value.runId,code:'decky-account-changed'}));
    }
  };
  // At most the same four official boundaries read below are retained.
  // Attribute changes in a healthy Steam tree must not scan its descendants.
  let boundaryNodes=[];
  // 2026-11-11 Win5: WS/hooks were healthy before a manual page reset, while
  // Steam exported only "Caught an error [object Object]". Preserve a bounded
  // visible-boundary classification in the existing browser export. This is
  // evidence only: it cannot disable/restart Decky or change recovery budgets.
  const boundaryFacts=new Set();
  const noteBoundary=(source,version,trace)=>{
    if(stopped||host.__YMCC_DECKY_MIRROR__?.runId!==value.runId)return;
    const attribution=source==='This error likely occurred in the Decky frontend.'?'decky':source==='This error likely occurred in Steam.'?'steam':source?'plugin':'unknown';
    const fault=trace.length>8192?'oversized':/(?:Failed to fetch|Load failed|NetworkError|HTTP (?:429|5\d\d))/.test(trace)?'network':/Minified React error #\d+/.test(trace)?'react':/(?:TypeError:|ReferenceError:)/.test(trace)?'script':'unknown';
    const loader=version===value.loaderVersion?'current':version?'other':'unknown';
    const signature=attribution+':'+fault+':'+loader;
    if(boundaryFacts.has(signature)||boundaryFacts.size>=16)return;
    boundaryFacts.add(signature);
    // No source text, stack, URL, account, credentials or game identity.
    try{host.console.info('[YMCC:DeckyHealth] '+JSON.stringify({schema:1,stage:'visible-boundary',ordinal:boundaryFacts.size,attribution,fault,loader}));}catch{}
  };
  function readHealth(discoverBoundaries){
    if(!current()){stopObserving();return '';}
    observeSocket();observeFrontendLoad();
    if(discoverBoundaries){
      const nodes=host.document.querySelectorAll('.deckyErrorBoundary');
      const hadBoundary=boundaryNodes.length>0;
      boundaryNodes=Array.from(nodes).slice(0,4);
      if(hadBoundary!==(boundaryNodes.length>0))watchBoundaries(boundaryNodes.length>0);
    }
    for(let i=0;i<boundaryNodes.length;i++){
      const node=boundaryNodes[i];if(!node.getClientRects().length)continue;
      let hidden=false;
      for(let el=node,depth=0;el&&depth<64;el=el.parentElement,depth++){
        const css=host.getComputedStyle(el);
        if(el.hidden||el.getAttribute('aria-hidden')==='true'||css.visibility==='hidden'||css.visibility==='collapse'||css.display==='none'||css.opacity==='0'){hidden=true;break;}
      }
      if(hidden)continue;
      const source=node.querySelector('.likely-occured-msg')?.textContent?.trim();
      const text=(node.querySelector('pre code')?.textContent||'').slice(0,2048);
      const version=/Decky Version:\s*(v\d+\.\d+\.\d+)(?:\[U\])?(?=\s|$)/.exec(text)?.[1];
      // v3.3.0 defines a 'trace' class but does not put it on the actual trace.
      // Consume the rendered Trace panel, not an unused source constant.
      const panel=Array.from(node.querySelectorAll('.panel-section')).slice(0,8).find(p=>p.querySelector('.panel-header')?.textContent?.trim()==='Trace');
      const trace=panel?.querySelector('pre code')?.textContent||'';
      noteBoundary(source,version,trace);
      if(source!=='This error likely occurred in the Decky frontend.'||version!==value.loaderVersion||!text.includes('Error Reference:'))continue;
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
  const read=()=>readHealth(true);
  function scanHealth(discoverBoundaries){
    if(reported||!current())return;
    const code=readHealth(discoverBoundaries);if(!code||typeof host[binding]!=='function')return;
    reported=true;host[binding](JSON.stringify({runId:value.runId,code}));
  }
  const scan=()=>scanHealth(true);
  // Only added/changed nodes under the small official boundary trigger a scan.
  // No timers, global error replacement, network requests or healthy-state polling.
  const observer=new host.MutationObserver(records=>{
    if(reported||!current())return;
    // Small append-only leaf batches cannot change the boundary list. An
    // ancestor can still reveal a boundary via CSS (e.g. :has), so recheck it.
    if(boundaryNodes.length&&records.length===1){
      const r=records[0],p=r.target,added=r.addedNodes;
      if(r.type==='childList'&&p.nodeType===1&&!r.removedNodes.length&&added.length&&added.length<=16&&
         !p.matches('.deckyErrorBoundary')&&boundaryNodes.some(n=>p.contains(n))){
        let unchanged=true;
        for(const n of added)if(n.nodeType===1&&(n.firstElementChild||n.matches('.deckyErrorBoundary'))){unchanged=false;break;}
        if(unchanged){scanHealth(false);return;}
      }
    }
    const touches=node=>!!node&&boundaryNodes.some(boundary=>boundary===node||boundary.contains(node)||node.contains?.(boundary));
    if(records.some(r=>{
      const target=r.target?.nodeType===1?r.target:r.target?.parentElement;
      // A class mutation can create a boundary on an existing element. Cached
      // containment also catches hidden ancestors and boundary class removal.
      if(target?.matches?.('.deckyErrorBoundary')||touches(target))return true;
      if(r.type!=='childList')return false;
      // Only actual inserted subtrees need discovery. Removal invalidates the
      // small cache so a formerly fifth boundary can become one of the first4.
      // Match an inserted leaf itself, but only search descendants when it has children.
      for(const n of r.removedNodes)if(touches(n))return true;
      for(const n of r.addedNodes)if(n.nodeType===1&&(n.matches('.deckyErrorBoundary')||(n.firstElementChild&&n.querySelector('.deckyErrorBoundary'))))return true;
      return false;
    }))scan();
  });
  function watchBoundaries(detailed){
    if(!current())return;
    // With no boundary, only insertion or a class change can discover one.
    // Keep the same observer and enable visibility/text records when found.
    observer.observe(host.document,{childList:true,subtree:true,characterData:detailed,attributes:true,
      attributeFilter:detailed?['class','style','hidden','aria-hidden']:['class']});
  }
  // A real QAM opening is an observation request, never a healthy verdict or
  // launch permission. Reuse the current authenticated watch and native ladder.
  let checkQueued=false;
  const checkConnection=event=>{
    if(checkQueued||!current())return;
    const code=event.type==='ymcc-decky-connection-result'?'decky-connection-result':'decky-connection-check';
    checkQueued=true;Promise.resolve().then(()=>{
      checkQueued=false;
      if(!current())return;
      if(typeof host[binding]==='function')host[binding](JSON.stringify({runId:value.runId,code}));
    });
  };
  function stopObserving(){
    releaseSocket();observer.disconnect();boundaryNodes=[];
    const unsubscribe=unsubscribeAccount;unsubscribeAccount=null;
    try{unsubscribe?.();}catch{}
  }
  const dispose=()=>{if(stopped)return;stopped=true;stopObserving();host.removeEventListener('pagehide',dispose);host.removeEventListener('ymcc-decky-health-stop',dispose);host.removeEventListener('ymcc-decky-connection-check',checkConnection);host.removeEventListener('ymcc-decky-connection-result',checkConnection);if(host[key]===api)delete host[key];};
  const api={runId:value.runId,read,scan,dispose,accountCurrent};
  if(uiAccount&&accountWatch?.version===1){
    unsubscribeAccount=accountWatch.subscribe(accountChanged);
    if(accountRevoked)stopObserving(); // The subscription may invoke its callback inline.
  }
  Object.defineProperty(host,key,{value:api,configurable:true});
  // Context creation may precede <html>; Document itself is always available.
  watchBoundaries(false);
  host.addEventListener('pagehide',dispose);host.addEventListener('ymcc-decky-health-stop',dispose);host.addEventListener('ymcc-decky-connection-check',checkConnection);host.addEventListener('ymcc-decky-connection-result',checkConnection);scan();accountChanged();
})
)YMCCHEALTH";}
}
