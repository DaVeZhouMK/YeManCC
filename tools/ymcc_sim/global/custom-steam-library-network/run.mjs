// Offline production-consumer regression. No product process or live provider I/O.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {spawn} from 'node:child_process';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
const here=path.dirname(fileURLToPath(import.meta.url));
const repo=path.resolve(here,'../../../..');
const build=path.resolve(repo,'../../Build');
const worker='native/custom-steam-library/steam_artwork_lab.cpp';
const tests=[{suite:'lifecycle',file:'tools/custom_steam_library_worker_lifecycle_selftest.cpp'},
 {suite:'artwork',file:'tools/custom_steam_library_artwork_tier_selftest.cpp'},
 {suite:'http-transport',file:'tools/custom_steam_library_http_transport_selftest.cpp'}];
const inputs=[worker,...tests.map(t=>t.file),'tools/custom_steam_library_test_runtime.h','tools/custom_steam_library_storage_isolation.h',
 'native/process_efficiency.h','native/game_detection_blacklist_entries.h','deps/json/json.hpp',
 ...fs.readdirSync(path.join(repo,'native/custom-steam-library')).filter(n=>n.endsWith('.h')).map(n=>'native/custom-steam-library/'+n),
 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/ymcc_sim/global/custom-steam-library-network/run.mjs'];
const sha=b=>createHash('sha256').update(b).digest('hex');
const readJson=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const save=(p,value)=>fs.writeFileSync(p,JSON.stringify(value,null,2)+'\n');
function one(source,before,after){
 if(source.split(before).length!==2)throw Error('Private fault anchor is missing or ambiguous: '+before.slice(0,90));
 return source.replace(before,after);
}
function privateFault(bytes){
 let s=bytes.toString('utf8').replace(/\r\n/g,'\n');
 const assetsStart=s.indexOf('static json steamLibraryAssetsResponse(');
 const assetsEnd=s.indexOf('\nstatic std::map<std::string,json> g_steamLibraryAssetsSession;',assetsStart);
 if(assetsStart<0||assetsEnd<0)throw Error('StoreBrowse consumer fault anchor missing');
 s=one(s,s.slice(assetsStart,assetsEnd),`static json steamLibraryAssetsResponse(const json& response, int appId,
    const std::string&, json&) {
    const auto envelope=response.find("response");
    if(envelope==response.end()||!envelope->is_object())throw std::runtime_error("Steam library assets response is invalid");
    const auto items=envelope->find("store_items");
    if(items==envelope->end()||!items->is_array())throw std::runtime_error("Steam library assets inventory is invalid");
    for(const auto& item:*items) {
        if(!item.is_object()||positiveJsonInt(item,"appid")!=appId||positiveJsonInt(item,"success")!=1)continue;
        const auto value=item.find("assets");
        if(value==item.end()||!value->is_object())continue;
        return *value;
    }
    return json::object();
}
`);

 s=one(s,`static void requireCompleteHttpStatus(DWORD status) {
    if (status == 206)
        throw HttpTransportError("Unexpected partial HTTP response for a complete request",
            ERROR_WINHTTP_INVALID_SERVER_RESPONSE, true);
}`, `static void requireCompleteHttpStatus(DWORD status) { (void)status; }`);
 s=one(s,`            if (GetTickCount64() < effectiveHttpDeadline(deadline)) return true;`,
 `            if (true) return true;`);

 s=one(s,`    if (g_fastMetadataMode) rememberMetadataBackoff(url, 0, 15000);
    if (attemptLog) attemptLog->push_back({
        {"stage", stage}, {"provider", provider}, {"url", url}, {"attempt", 0},
        {"freshSession", false}, {"ok", false}, {"retryable", true},
        {"reason", reason}, {"identityAuthority", false}});
    throw HttpTransportError(provider + " returned unusable metadata: " + reason,
        ERROR_INVALID_DATA, true);`,
 `    throw std::runtime_error(provider + " returned unusable metadata: " + reason);`);
 s=one(s,`        if (!body.is_null() && !usableIgdbResponse(url, body, result))
            throwMetadataSemanticFailure(url, provider, "igdb-search-envelope", "invalid-provider-envelope", attempts);`,'');
 s=one(s,`        if (!usable(data))
            throwMetadataSemanticFailure(url, "steam", "steam-search-envelope", "invalid-provider-envelope", attempts);`,'');
 s=one(s,`    if (evidence.retryableFailures > 0) {
        failures.push_back({{"stage", "artwork-discovery"}`,`    if (false && evidence.retryableFailures > 0) {
        failures.push_back({{"stage", "artwork-discovery"}`);
 s=one(s,`    const auto cooldown = loadMetadataBackoff(url);
    if (!cooldown.empty() && (fastMetadata || imageRequest || metadataProviderRetryAt(cooldown) > unixTimeMs())) {`,`    const auto cooldown = fastMetadata ? loadMetadataBackoff(url) : json::object();
    if (!cooldown.empty() && (fastMetadata || imageRequest || metadataProviderRetryAt(cooldown) > unixTimeMs())) {`);
 s=one(s,`static bool deferHttpProviderWait(const std::string& url, const HttpResponse& response, json& trace) {
    if (response.status != 429 &&
        (!transientHttpStatus(response.status) || response.serverRetryAfterMs == 0)) return false;
    const auto delay = (std::max)(response.status == 429 ? DWORD{30000} : DWORD{0},
        (std::max)(response.retryAfterMs, response.serverRetryAfterMs));
    rememberMetadataBackoff(url, response.status, delay, true);
    trace["retryable"] = true;
    trace["retryDeferred"] = true;
    trace["retryAfterMs"] = delay;
    trace["retryAt"] = unixTimeMs() + delay;
    return true;
}`, `static bool deferHttpProviderWait(const std::string&, const HttpResponse&, json&) { return false; }`);
 s=one(s,`    if (started >= deadline) {
        trace["retryable"] = true;`, `    if (false && started >= deadline) {
        trace["retryable"] = true;`);
 s=one(s,`    const auto cooldown = loadMetadataBackoff(url);
    if (!cooldown.empty() && ((g_fastMetadataMode && !imageRequest) || imageRequest ||`,
 `    const auto cooldown = json::object();
    if (!cooldown.empty() && ((g_fastMetadataMode && !imageRequest) || imageRequest ||`);
 s=one(s,`        trace["error"] = error.what();
        trace["win32Error"] = error.code();
        trace["retryable"] = error.retryable();`,
 `        trace["error"] = error.what();
        trace["retryable"] = false;`);
 s=one(s,`                    if (!info && response.status >= 200 && response.status < 300)
                        throwArtworkDecodeFailure(response.body, artworkRequestUrl, candidate.provider,
                            &attempt["networkAttempts"]);`, '');
 s=one(s,`            if (error.retryable()) resetReusableHttpSession();`, '');
 s=one(s,`        case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:\n`, '');
 s=one(s,`        if (response.status >= 200 && response.status < 300 && response.expectedBytes > maxBytes) {`,
 `        if (response.expectedBytes > maxBytes) {`);
 s=one(s,`            // A successful zero-byte read is EOF, including chunked bodies
            // without Content-Length. Do not spin on an availability hint.
            break;`, '');
 s=one(s,`static void requireHttpBudget(ULONGLONG deadline) {
    if (GetTickCount64() >= effectiveHttpDeadline(deadline))
        throw HttpTransportError("HTTP operation budget expired", ERROR_WINHTTP_TIMEOUT, true);
}`, `static void requireHttpBudget(ULONGLONG deadline) { (void)deadline; }`);
 s=one(s,`    if (!WinHttpSetTimeouts(handle, resolveTimeout, connectTimeout, sendTimeout, receiveTimeout))
        throwWinHttp("WinHttpSetTimeouts");`,
 `    WinHttpSetTimeouts(handle, resolveTimeout, connectTimeout, sendTimeout, receiveTimeout);`);
 s=one(s,`    const DWORD bodyBytes = static_cast<DWORD>(requestBody.size());
    setHttpTimeouts(request, deadline, acceleratorRoute);`,
 `    const DWORD bodyBytes = static_cast<DWORD>(requestBody.size());`);
 s=one(s,`    // DNS/connect/TLS/send may have consumed most of the original allowance.
    // Refresh the receive timeout from the remaining budget at this boundary.
    setHttpTimeouts(request, deadline, acceleratorRoute);`, '');
 return Buffer.from(s.replace(/\n/g,'\r\n'));
}
async function command(exe,args,cwd,log,timeoutMs){
 const started=Date.now();let child;let timer;let timedOut=false;
 const output=fs.openSync(log,'wx');
 try{
  return await new Promise((resolve,reject)=>{
   child=spawn(exe,args,{cwd,windowsHide:true,stdio:['ignore',output,output],env:{...process.env,YMCC_STEAM_DATA_CASES:'',YMCC_LIBRARY_NETWORK_CASES:''}});
   timer=setTimeout(()=>{timedOut=true;child.kill();},timeoutMs);
   child.on('error',reject);
   child.on('close',(code,signal)=>resolve({exe,args,cwd,pid:child.pid,exitCode:code,signal,timedOut,elapsedMs:Date.now()-started,log}));
  });
 }finally{clearTimeout(timer);fs.closeSync(output);}
}
export async function runNetworkRegression(root){
 root=path.resolve(root);
 if(!root.toLowerCase().startsWith((build+path.sep).toLowerCase())||fs.existsSync(root))throw Error('Use a fresh evidence directory inside the existing Build root');
 if(/["%\r\n]/.test(root)||/["%\r\n]/.test(repo))throw Error('Unsupported compiler path');
 fs.mkdirSync(root,{recursive:true});
 const original=new Map(inputs.map(p=>[p,fs.readFileSync(path.join(repo,p))]));
 const hashes=Object.fromEntries([...original].map(([p,b])=>[p,sha(b)]));
 save(path.join(root,'source-manifest.json'),hashes);
 const source=new Map([...original].filter(([p])=>!p.endsWith('.mjs')));
 const cases=[];const commands=[];const caseSets=new Map();
 try{
  for(const kind of ['positive','fault']){
   const dir=path.join(root,kind),snap=path.join(dir,'snapshot');fs.mkdirSync(snap,{recursive:true});
   for(const [rel,bytes] of source){const target=path.join(snap,rel);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,kind==='fault'&&rel===worker?privateFault(bytes):bytes);}
   // Preserve the fixture's original __FILE__ only for its existing Build-root
   // admission. All includes still resolve to this frozen source snapshot.
   for(const {suite,file:test} of tests){
   const suiteDir=path.join(dir,suite);fs.mkdirSync(suiteDir);
   const fixture=path.join(snap,test);
   fs.writeFileSync(fixture,'#line 1 '+JSON.stringify(path.join(repo,test).replace(/\\/g,'/'))+'\r\n'+source.get(test).toString('utf8').replace(/^\uFEFF/,''));
   const executable=path.join(suiteDir,'network-selftest.exe');
   const batch=path.join(suiteDir,'compile.cmd');
   const preamble=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');
   fs.writeFileSync(batch,preamble+`cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE /FC "${fixture}" /I"${path.join(snap,'deps/json')}" /Fo"${path.join(suiteDir,'fixture.obj')}" /Fe"${executable}" /link /SUBSYSTEM:CONSOLE /MACHINE:x64 winhttp.lib iphlpapi.lib version.lib bcrypt.lib advapi32.lib shell32.lib user32.lib gdiplus.lib ole32.lib ws2_32.lib\r\nexit /b %errorlevel%\r\n`);
   const compile=await command(process.env.ComSpec||'C:/Windows/System32/cmd.exe',['/d','/c',batch],repo,path.join(suiteDir,'compile.log'),180000);commands.push(compile);
   if(compile.exitCode!==0||compile.timedOut||!fs.existsSync(executable))throw Error(kind+' compilation failed; see '+compile.log);
   const fixtures=path.join(suiteDir,'cases');
   const run=await command(executable,[fixtures],repo,path.join(suiteDir,'run.log'),120000);commands.push(run);
   if(run.timedOut||!fs.existsSync(path.join(fixtures,'summary.json')))throw Error(kind+' fixture did not produce a terminal summary');
   const summary=readJson(path.join(fixtures,'summary.json'));
   const rows=summary.cases.filter(c=>c.name.startsWith('network-'));
   const failures=summary.cases.filter(c=>!c.passed);
   if(kind==='positive'&&(run.exitCode!==0||!summary.allPassed||summary.failedCount!==0))throw Error('Current production consumer regression failed');
   const rawMustDetect=[...['429','503','404'].map(s=>'network-raw-error-large-'+s),
    ...[false,true].flatMap(fast=>[false,true].flatMap(image=>[429,503].map(status=>`network-raw-provider-wait-${status}-${image?'image':'metadata'}-${fast?'auto':'manual'}`))),
    ...[0,2].map(size=>'network-raw-zero-read-eof-'+size),
    ...[false,true].flatMap(fast=>[false,true].map(image=>`network-raw-invalid-server-response-${image?'image':'metadata'}-${fast?'auto':'manual'}`))];
   const deadlineMustDetect=['send-exhaustion-stops-before-receive','receive-uses-remaining-budget',
    'request-preparation-stops-before-send','availability-exhaustion-stops-before-read',
    'availability-eof-still-obeys-budget','zero-read-eof-still-obeys-budget','error-headers-still-obey-budget',
    'effective-identity','effective-artwork','retry-shares-total-budget-metadata','retry-shares-total-budget-image',
    ...[1,2,3,4].map(n=>'timeout-setting-failure-'+n)].map(n=>'network-deadline-'+n);
   const semanticMustDetect=[...['manual','auto'].flatMap(mode=>['metadata','image'].flatMap(kind=>[
    ...[[429,120000],[503,120000],[503,1000]].map(([status,wait])=>`network-semantic-provider-wait-${status}-${wait}-${kind}-${mode}`),
    `network-semantic-existing-provider-wait-${kind}-${mode}`,
    ...[12002,12029,12175].map(code=>`network-semantic-transport-${code}-${kind}-${mode}`)])),
    ...['identity','artwork'].map(kind=>`network-semantic-expired-budget-${kind}`)];
   const integrityMustDetect=[...['metadata','image'].flatMap(kind=>[
    `network-integrity-raw-partial-${kind}`,
    ...['manual','auto'].map(mode=>`network-integrity-partial-recovers-${kind}-${mode}`),
    `network-integrity-partial-exhaustion-${kind}`,
    ...['before-dns','after-dns'].map(phase=>`network-integrity-fallback-budget-${kind}-${phase}`)]),
    'network-integrity-json-partial-cannot-become-identity','network-integrity-post-partial-cannot-become-absence',
    'network-integrity-semantic-partial-rejected'];
   const storeBrowseMustDetect=[...['null-root','array-root','missing-envelope','null-envelope','missing-inventory','invalid-inventory','invalid-row','foreign-app','conflicting-id','missing-success','invalid-success','missing-assets','invalid-assets','unsafe-assets','duplicate-app','wrong-item-type','zero-result'].map(n=>'network-storebrowse-malformed-'+n),
    ...[2,3,10,16,20,38,84].map(n=>'network-storebrowse-transient-result-'+n),
    'network-storebrowse-rate-limit-persists-across-appids-and-worker-memory',
    ...['missing','expired'].map(n=>'network-storebrowse-no-usable-cache-'+n),
    'network-storebrowse-invalid-inventory-reaches-full-worker-retry'];
   const mustDetect=suite==='http-transport'?[...rawMustDetect,...deadlineMustDetect,...semanticMustDetect,...integrityMustDetect]:suite==='artwork'?[...storeBrowseMustDetect,'network-pipeline-discovery-outage-persists-retry-and-recovers-without-steamid','network-pipeline-malformed-baidu-response-persists-retry-job',
    'network-pipeline-invalid-image-empty-recovers','network-pipeline-invalid-image-html-recovers','network-pipeline-invalid-image-xml-error-recovers','network-pipeline-invalid-image-truncated-png-recovers','network-pipeline-invalid-image-truncated-jpeg-recovers','network-pipeline-invalid-image-preserves-manual-cover']:["weak-automatic-429-defers-once-and-shares-across-queries","weak-429-stale-fallback-keeps-next-query-throttled","weak-html-rate-limit-cannot-disable-next-compact-query",'network-json-steam-truncated','network-json-igdb-post-html','network-stale-steam-envelope-error',
    'network-igdb-search-envelope-error','network-stale-igdb-envelope-error','network-long-retry-after-503-auto','network-long-retry-after-429-manual','network-discovery-timeout-playnite-igdb','network-discovery-timeout-baidu-image',
    ...[false,true].flatMap(fast=>[[429,0],[429,1000],[429,120000],[503,1000],[503,120000]].map(([status,after])=>`network-image-cooldown-${status}-${after}-${fast?'auto':'manual'}`)),
    ...[false,true].flatMap(fast=>['steam','playnite-igdb'].flatMap(provider=>[[429,0],[429,1000],[429,120000],[503,1000],[503,120000]].map(([status,after])=>`network-metadata-provider-wait-${provider}-${status}-${after}-${fast?'auto':'manual'}`))),
    ...[false,true].flatMap(fast=>[false,true].flatMap(image=>[12002,12029,12007,12030,12019].map(code=>`network-terminal-session-${code}-${image?'image':'metadata'}-${fast?'auto':'manual'}`)))];
   if(kind==='fault'&&(run.exitCode!==1||failures.length<(suite==='http-transport'?77:suite==='artwork'?36:90)||mustDetect.some(n=>!failures.some(c=>c.name===n))))throw Error('Private fault did not fail the expected business assertions');
   const expectedLegacy=['weak-stale-search-keeps-invalid-json-visible','weak-igdb-api-error-is-never-cached-as-absence',"weak-automatic-429-defers-once-and-shares-across-queries","weak-429-stale-fallback-keeps-next-query-throttled","weak-html-rate-limit-cannot-disable-next-compact-query"];
   if(kind==='fault'&&failures.some(c=>!c.name.startsWith('network-')&&!c.name.startsWith('round6-')&&!expectedLegacy.includes(c.name)))throw Error('Private fault caused unrelated regressions');
   const names=summary.cases.map(c=>c.name).sort();
   if(kind==='positive')caseSets.set(suite,names);
   else if(JSON.stringify(caseSets.get(suite))!==JSON.stringify(names))throw Error('Fault/positive case sets differ');
   if(rows.length<(suite==='http-transport'?88:suite==='artwork'?43:95))throw Error('Required network cases are missing');
   cases.push({kind,suite,caseCount:summary.caseCount,networkCaseCount:rows.length,failedCount:summary.failedCount,exitCode:run.exitCode,executableSha256:sha(fs.readFileSync(executable)),workerSha256:sha(fs.readFileSync(path.join(snap,worker))),failures});
   }
  }
  const changed=inputs.filter(p=>sha(fs.readFileSync(path.join(repo,p)))!==hashes[p]);
  if(changed.length)throw Error('Source changed during run: '+changed.join(', '));
  const result={status:'PASS',scope:'actual offline production consumers; synthetic transport only',sourceHashes:hashes,cases,commands,hardwareIo:false,liveNetwork:false,steamWrites:false,productionDataWrites:false};
  save(path.join(root,'result.json'),result);return result;
 }catch(error){save(path.join(root,'result.json'),{status:'FAIL',error:String(error),sourceHashes:hashes,cases,commands});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const flag=process.argv.indexOf('--evidence-dir');
 if(flag<0||!process.argv[flag+1])throw Error('--evidence-dir <fresh Build directory> required');
 const r=await runNetworkRegression(process.argv[flag+1]);
 console.log(JSON.stringify({status:r.status,cases:r.cases.map(({failures,...c})=>c)},null,2));
}
