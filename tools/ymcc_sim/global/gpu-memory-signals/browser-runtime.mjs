import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
import {digest,hash,key,need,inside,bindGpuOutput,exactFile} from './tool-scope.mjs';
import {checkUiBrowserTerminal} from '../ui-navigation/tool-inputs.mjs';
/** Only a new headless process/profile and a loopback server created by this call. */
export async function launchGpuBrowser(scope,chromium,origin){
 const url=new URL(origin);need(url.protocol==='http:'&&url.hostname==='127.0.0.1'&&url.port&&url.origin===origin,'OWNED_LOOPBACK_ORIGIN_REQUIRED');
 const executable=scope.manifest.browser.executable;exactFile(scope.toolMap.get(key(executable)));
 const args=['--disable-gpu','--disable-gpu-compositing','--disable-software-rasterizer','--disable-features=Vulkan,WebGPU','--disable-background-networking','--disable-component-update','--disable-sync','--disable-extensions','--disable-component-extensions-with-background-pages','--disable-breakpad','--disable-crash-reporter','--no-first-run','--no-default-browser-check','--no-proxy-server','--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE localhost, EXCLUDE 127.0.0.1'];
 const server=await chromium.launchServer({host:'127.0.0.1',port:0,headless:true,executablePath:executable,args,downloadsPath:path.join(scope.out,'browser-downloads'),env:{...process.env,TEMP:path.join(scope.out,'temp'),TMP:path.join(scope.out,'temp')}});
 const child=server.process();const profileArgument=child?.spawnargs.find(x=>x.startsWith('--user-data-dir='));if(!profileArgument||!inside(scope.out,path.resolve(profileArgument.slice('--user-data-dir='.length)))){await server.kill();throw Error('GPU_SCOPE_OWNED_PROFILE_REQUIRED');}need(child?.pid>0&&key(child.spawnfile)===key(executable),'OWNED_BROWSER_PROCESS_REQUIRED');
 const terminal=new Promise(resolve=>{if(child.exitCode!==null)return resolve({exitCode:child.exitCode,signal:child.signalCode});child.once('exit',(exitCode,signal)=>resolve({exitCode,signal}));});
 let browser;try{browser=await chromium.connect(server.wsEndpoint());}catch(error){await server.kill();throw error;}
 const requests=[],contexts=[];const originalContext=browser.newContext.bind(browser),originalPage=browser.newPage.bind(browser),originalClose=browser.close.bind(browser);
 async function guard(context){contexts.push(context);await context.route('**/*',route=>{const request=route.request(),allowed=new URL(request.url()).origin===origin;requests.push({url:request.url(),method:request.method(),allowed});return allowed?route.continue():route.abort('blockedbyclient');});if(context.routeWebSocket)await context.routeWebSocket('**/*',socket=>{requests.push({url:socket.url(),allowed:false,kind:'websocket'});socket.close();});return context;}
 browser.newContext=async options=>guard(await originalContext(options));
 browser.newPage=async options=>{const context=await browser.newContext(options);return context.newPage();};
 let closed=false;browser.close=async()=>{if(closed)return;closed=true;let failure=null,forcedCleanup=false;try{await originalClose();await server.close();}catch(error){failure=error.message;forcedCleanup=true;await server.kill();}
  let timer;const ended=await Promise.race([terminal,new Promise(resolve=>{timer=setTimeout(()=>resolve(null),7000);})]);clearTimeout(timer);if(!ended){forcedCleanup=true;await server.kill();failure??='OWNED_BROWSER_EXIT_TIMEOUT';}
  let identity=null;try{need(ended&&!forcedCleanup&&!failure,'OWNED_BROWSER_CLEAN_EXIT');identity=checkUiBrowserTerminal(scope.uiDeclaration,child,ended);}catch(error){failure??=error.message;}
  const proof={schemaVersion:1,kind:'gpu-owned-headless-browser-runtime',status:failure?'FAIL':'PASS',node:{path:process.execPath,sha256:hash(process.execPath)},browser:{path:executable,sha256:hash(executable),pid:child.pid,args:child.spawnargs,terminal:ended,identity,owned:true,headless:true},ownWebsocketServer:{endpoint:server.wsEndpoint(),closed:true},profile:child.spawnargs.find(x=>x.startsWith('--user-data-dir='))??null,allowOrigin:origin,requests,externalRequestsAllowed:requests.filter(x=>x.allowed&&new URL(x.url).origin!==origin).length,externalRequestsBlocked:requests.filter(x=>!x.allowed).length,forcedCleanup,failure,osNetworkAuditPerformed:false,osLoadedModuleGraphObserved:false};
  const file=path.join(scope.out,'browser-runtime-'+child.pid+'.json');fs.writeFileSync(file,JSON.stringify(proof,null,2));bindGpuOutput(scope,file,'owned browser/runtime/exit evidence');need(!failure&&proof.externalRequestsAllowed===0,'BROWSER_RUNTIME_FAILED:'+failure);
 };
 return browser;
}
