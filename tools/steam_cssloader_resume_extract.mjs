import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';
const out=path.resolve(process.argv[2]),source=path.resolve(process.argv[3]||'native/main.cpp');fs.mkdirSync(out,{recursive:true});
const text=fs.readFileSync(source,'utf8'),marker='static bool steamAppearanceResumeCommitted(unsigned long long generation) {';const at=text.indexOf(marker);assert(at>=0,'Production CSS Ready callback missing');let depth=0,end=-1,string=null,escape=false;
for(let i=at+marker.length-1;i<text.length;i++){const c=text[i];if(string){if(escape)escape=false;else if(c==='\\')escape=true;else if(c===string)string=null;continue;}if(c==='"'||c==="'"){string=c;continue;}if(c==='{')depth++;if(c==='}'&&!--depth){end=i+1;break;}}
assert(end>at);const code=text.slice(at,end);assert(code.includes('requestPublicResumeRecovery(session, power)')&&code.includes('captureSharedPowerLifecycle(power)')&&text.includes('cssGuard.powerReader = &readSharedSystemPower;'));assert(!/ShellExecute|SetUIMode|RestartJSContext|launchSteam/.test(code));
const publicMarker='static void steamAppearancePublicReady(unsigned long long generation) noexcept {';
const publicAt=text.indexOf(publicMarker),publicEnd=text.indexOf('\n}',publicAt);assert(publicAt>end&&publicEnd>publicAt);
const publicReady=text.slice(publicAt,publicEnd+2);assert(publicReady.includes('steamAppearanceResumeCommitted(generation)'));
const hook=text.slice(text.indexOf('case WM_POWER_RESUME_COMMIT:'),text.indexOf('case WM_POWER_INPUT_RECOVER:'));
assert(hook.includes('steamAppearanceResumeCommitted(generation)')||hook.includes('steamAppearanceResumeCommitted(currentPowerGeneration())'),'Committed Ready never reaches CSS recovery');assert(hook.indexOf('PowerLifecycle::Ready')<hook.indexOf('steamAppearanceResumeCommitted('),'Power admission precedes CSS callback');
const refreshAt=text.indexOf('    cssGuard.refreshMode =');assert(refreshAt>=0);const refreshStart=text.indexOf('[]',refreshAt),refreshEnd=text.indexOf('\n    };',refreshStart);assert(refreshStart>=0&&refreshEnd>refreshStart);const refresh=text.slice(refreshStart,refreshEnd+6).trim();
assert(refresh.includes('g_steamCssUse.invalidateForResume(session)')&&refresh.includes('g_steamModeObserver.requestRefresh(session)'));
fs.writeFileSync(path.join(out,'production-css-resume.h'),code+'\n'+publicReady+'\ninline const auto productionCssResumeRefresh = '+refresh+';\n');fs.writeFileSync(path.join(out,'source.json'),JSON.stringify({source,sha256:crypto.createHash('sha256').update(text).digest('hex'),callback:code,publicReady,refreshCallback:refresh,readyHook:hook,scope:'Exact production Ready entry points and committed Ready hook; external observer/runtime state remains fixture-owned'},null,2));
console.log('Extracted exact committed CSS resume callback.');

const stableAt=text.indexOf('    modeObserver.cssBigPicturePublished =');assert(stableAt>=0);const stableStart=text.indexOf('[]',stableAt),stableEnd=text.indexOf('\n    };',stableStart);assert(stableStart>=0&&stableEnd>stableStart);const stable=text.slice(stableStart,stableEnd+6).trim();
assert(stable.includes('g_steamCssGuard.observeMode(session, sample, stable)')&&stable.includes('appearanceModeReady(session, state)'));
fs.appendFileSync(path.join(out,'production-css-resume.h'),'\ninline const auto productionCssBigPicturePublished = '+stable+';\ninline void productionCssStableMode(const ymcc::steamsession::Session& session) { productionCssBigPicturePublished(session, g_steamModeObserver.snapshot(session), {}, true); }\n');
fs.writeFileSync(path.join(out,'stable-source.json'),JSON.stringify({source,sha256:crypto.createHash('sha256').update(text).digest('hex'),stableCallback:stable,scope:'Exact CSS publication callback, called with the fixture-owned verified mode result'},null,2));

for(const [field,name] of [['readMode','productionCssReadMode'],['refreshPublicResumeMode','productionCssPublicResumeRefresh']]){
 const at=text.indexOf(`    cssGuard.${field} = []`),start=text.indexOf('[]',at),end=text.indexOf('\n    };',start);
 assert(at>=0&&start>=at&&end>start);
 fs.appendFileSync(path.join(out,'production-css-resume.h'),`\ninline const auto ${name} = ${text.slice(start,end+6).trim()};\n`);
}
