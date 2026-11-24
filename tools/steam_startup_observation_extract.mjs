import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';
const out=path.resolve(process.argv[2]),source=path.resolve(process.argv[3]||'native/main.cpp'),text=fs.readFileSync(source,'utf8').replaceAll('\r\n','\n');fs.mkdirSync(out,{recursive:true});
function blockEnd(opening,marker,open='{',close='}'){
 assert.equal(text[opening],open,marker);let depth=1,at=opening+1;
 for(;depth&&at<text.length;++at){
  if(text.startsWith('//',at)){const end=text.indexOf('\n',at);assert(end>=0,marker+' line comment');at=end;continue;}
  if(text.startsWith('/*',at)){const end=text.indexOf('*/',at+2);assert(end>=0,marker+' block comment');at=end+1;continue;}
  if(text[at]==='R'&&text[at+1]==='"')throw Error('Review raw literal in '+marker);
  if(text[at]==='"'||text[at]==="'"){const quote=text[at];for(++at;at<text.length&&text[at]!==quote;++at)if(text[at]==='\\')++at;assert(at<text.length,marker+' string');continue;}
  if(text[at]===open)depth++;if(text[at]===close)depth--;
 }
 assert.equal(depth,0,marker);return at;
}
function lambda(marker){const start=text.indexOf(marker);assert(start>=0&&text.indexOf(marker,start+marker.length)<0,marker);const expressionStart=text.indexOf('[',start);assert(expressionStart>=start,marker+' capture');const captureEnd=blockEnd(expressionStart,marker,'[',']'),opening=text.indexOf('{',captureEnd),end=blockEnd(opening,marker);return text.slice(expressionStart,end);}
const ensure=lambda('cssGuard.ensure ='),confirm=lambda('cssGuard.confirmIdle ='),sidebar=lambda('[]() -> ymcc::deckysidebar::LaunchAdmission'),changed=lambda('modeObserver.changed ='),cssBigPicture=lambda('modeObserver.cssBigPicturePublished =');
for (const callback of [ensure,confirm,sidebar,changed,cssBigPicture]) assert(!/\b(?:ShellExecute(?:Ex)?W|CreateProcess(?:AsUser|WithToken)?W|TerminateProcess|WinExec|SendInput|SetForegroundWindow|SetUIMode|SwitchToGamepadUI)\s*\(/.test(callback),'Unsafe direct external action in startup regression callback');
const health=lambda('config.connectionHealth=');
const stable=lambda('modeObserver.stableBigPicture =');
function definition(marker){const start=text.indexOf(marker);assert(start>=0&&text.indexOf(marker,start+marker.length)<0,marker);const opening=text.indexOf('{',blockEnd(text.indexOf('(',start),marker,'(',')'));return text.slice(start,blockEnd(opening,marker));}
const wallpaperPublish=definition('static void steamWallpaperForegroundPublish() {');
const postJson=definition(text.includes('static bool ipc_post_json(')?'static bool ipc_post_json(':'static void ipc_post_json(');
// For the baseline, observe the main renderer of the old fire-and-forget broadcast.
const legacyEmit=wallpaperPublish.includes('ipc_emit(')?`static void ipc_emit(const std::string& event,const json& proof){ipc_post_json(0,json{{"event",event},{"data",proof}});}`:'';
const output=`// Exact production lambdas extracted from ${source.replaceAll('\\','/')}\ninline void productionCssCallbacks(ymcc::steamcss::Config& cssGuard){cssGuard.ensure=${ensure};cssGuard.confirmIdle=${confirm};}\ninline auto productionConnectionHealth=${health};
inline const auto productionSidebarAdmission=${sidebar};\ninline const auto productionModeChanged=${changed};\ninline const auto productionCssBigPicture=${cssBigPicture};\ninline const auto productionStableBigPicture=${stable};\n`;
fs.writeFileSync(path.join(out,'production-startup-callbacks.h'),[output,postJson,legacyEmit,wallpaperPublish,''].join('\n'));
fs.writeFileSync(path.join(out,'production-callbacks-source.json'),JSON.stringify({source,sha256:createHash('sha256').update(fs.readFileSync(source)).digest('hex'),callbacks:{ensure,confirm,sidebar,changed,health,cssBigPicture,stable,wallpaperPublish,postJson},externalPorts:'Process probes/helper launch, mode/foreground inputs, and WebView receiver/log delivery replaced by in-memory fixture'},null,2));console.log('Extracted production startup callbacks, global connection health and wallpaper notification delivery.');
