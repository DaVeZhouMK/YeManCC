import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';
const out=path.resolve(process.argv[2]),source=path.resolve(process.argv[3]||'native/main.cpp'),text=fs.readFileSync(source,'utf8').replaceAll('\r\n','\n');fs.mkdirSync(out,{recursive:true});
function blockEnd(opening,marker){
 assert.equal(text[opening],'{',marker);let depth=1,at=opening+1;
 for(;depth&&at<text.length;++at){
  if(text.startsWith('//',at)){const end=text.indexOf('\n',at);assert(end>=0,marker+' line comment');at=end;continue;}
  if(text.startsWith('/*',at)){const end=text.indexOf('*/',at+2);assert(end>=0,marker+' block comment');at=end+1;continue;}
  if(text[at]==='R'&&text[at+1]==='"')throw Error('Review raw literal in '+marker);
  if(text[at]==='"'||text[at]==="'"){const quote=text[at];for(++at;at<text.length&&text[at]!==quote;++at)if(text[at]==='\\')++at;assert(at<text.length,marker+' string');continue;}
  if(text[at]==='{')depth++;if(text[at]==='}')depth--;
 }
 assert.equal(depth,0,marker);return at;
}
function lambda(marker){const start=text.indexOf(marker);assert(start>=0&&text.indexOf(marker,start+marker.length)<0,marker);const opening=text.indexOf('{',start),end=blockEnd(opening,marker),expressionStart=text.indexOf('[',start);assert(expressionStart>=start&&expressionStart<opening,marker+' capture');return text.slice(expressionStart,end);}
const ensure=lambda('cssGuard.ensure ='),confirm=lambda('cssGuard.confirmIdle ='),sidebar=lambda('[]() -> ymcc::deckysidebar::LaunchAdmission'),changed=lambda('modeObserver.changed =');
for (const callback of [ensure,confirm,sidebar,changed]) assert(!/\b(?:ShellExecute(?:Ex)?W|CreateProcess(?:AsUser|WithToken)?W|TerminateProcess|WinExec|SendInput|SetForegroundWindow|SetUIMode|SwitchToGamepadUI)\s*\(/.test(callback),'Unsafe direct external action in startup regression callback');
const gamingStart=text.indexOf('static bool deckySidebarGamingFullscreenConsent(');assert(gamingStart>=0,'Gaming full-screen consumer missing');const gamingEnd=blockEnd(text.indexOf('{',gamingStart),'Gaming full-screen consent'),gamingConsent=text.slice(gamingStart,gamingEnd);assert(!gamingConsent.includes('deckySidebarOwnedBindingPlan')&&!gamingConsent.includes('deckySidebarOwnedBindingDiagnostic'),'Adjacent binding functions must not enter the consent fixture');
const output=`// Exact production lambdas extracted from ${source.replaceAll('\\','/')}\ninline void productionCssCallbacks(ymcc::steamcss::Config& cssGuard){cssGuard.ensure=${ensure};cssGuard.confirmIdle=${confirm};}\n${gamingConsent}
inline const auto productionSidebarAdmission=${sidebar};\ninline const auto productionModeChanged=${changed};\n`;
fs.writeFileSync(path.join(out,'production-startup-callbacks.h'),output);fs.writeFileSync(path.join(out,'production-callbacks-source.json'),JSON.stringify({source,sha256:createHash('sha256').update(fs.readFileSync(source)).digest('hex'),callbacks:{ensure,confirm,sidebar,changed},externalPorts:'Process probes/helper launch/log delivery and shared snapshot replaced by in-memory fixture'},null,2));console.log('Extracted four production startup callbacks.');