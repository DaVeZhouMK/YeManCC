// Exact production bodies; no broad whole-main or unrelated caller hash gate.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';
import {extract,functions,structures} from '../legion_motion_consumer/extract_main.mjs';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function region(text,symbol,code,kind='constant'){
 const i=text.indexOf(code);assert(i>=0&&i===text.lastIndexOf(code),'Missing/ambiguous '+symbol);
 return {symbol,kind,code,sha256:sha(code),startUtf8Byte:Buffer.byteLength(text.slice(0,i)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,i+code.length)),startLine:text.slice(0,i).split('\n').length};
}
export function freeze(repo,out){
 const bytes=fs.readFileSync(path.join(repo,'native/main.cpp')),text=bytes.toString('utf8');
 const entries=[...functions.map(s=>extract(text,s)),...structures.map(s=>extract(text,s,'struct'))];
 const constants=[];
 for(const [name,re]of [
  ['kLegionBackPids',/^static const USHORT kLegionBackPids\[\] = \{[^}]+\};/gm],
  ['kLegionSPids',/^static const USHORT kLegionSPids\[\] = \{[^}]+\};/gm],
  ['GP_STATE_FALLBACK_TIMER_ID',/^#define GP_STATE_FALLBACK_TIMER_ID[^\r\n]*/gm],
  ['kLegionGyroSide',/^static constexpr int kLegionGyroSide = \d+;/gm],
  ['g_legionMotionEpochStartedAt',/^static std::atomic<(?:std::)?uint64_t> g_legionMotionEpochStartedAt\s*\{0\};/gm]]){
  const m=[...text.matchAll(re)];assert.equal(m.length,1,name);constants.push(region(text,name,m[0][0]));
 }
 const start=text.indexOf('static std::mutex g_controllerCalibrationMx;');assert(start>=0);
 const declaration=/^static void inputCaptureControllerCalibrationAbort\(\)(?: noexcept)?;/m.exec(text.slice(start));assert(declaration);
 const registry=region(text,'controllerCalibrationRegistry',text.slice(start,start+declaration.index));constants.push(registry);
 const advance=entries.find(e=>e.symbol==='inputCaptureControllerCalibrationAdvance').code;
 const owner=entries.find(e=>e.symbol==='inputCaptureLegionMotion').code;
 const refresh=extract(text,'inputCaptureRefreshRealStickSettings').code;
 const publication=/const bool automaticCalibration = readBool\(motion, "autoCalibrate", true\);\s*g_realStickTest\.autoCalibrate = automaticCalibration;\s*g_controllerAutoCalibrationRequested\.store\(automaticCalibration,std::memory_order_release\);/.exec(refresh);
 assert(publication,'Existing setting key/default must feed local, Windows field and release publication in order');
 const steadyLoad='const bool automatic=g_controllerAutoCalibrationRequested.load(std::memory_order_acquire);';
 const ownerLoad='{"automaticRequested",g_controllerAutoCalibrationRequested.load(std::memory_order_acquire)},';
 assert(advance.includes(steadyLoad)&&owner.includes(ownerLoad),'Controller steady and owner evidence must acquire the published preference');
 assert(!advance.includes('g_realStickTest.autoCalibrate')&&!owner.includes('g_realStickTest.autoCalibrate'),'No ordinary bool preference read in these Controller BUS functions');
 const invalidation=/if \(g_controllerCalibrationActiveRequest \|\| request\)\s*g_legionMotionSession->invalidateCalibrationMode\(\);/.exec(advance);
 const snapshotAt=advance.indexOf('request=g_controllerCalibrationRequest;');
 assert(invalidation&&snapshotAt>=0&&snapshotAt<invalidation.index,'Manual cache invalidation must follow the locked request snapshot');
 const invalidateAt=advance.indexOf('g_legionMotionSession->invalidateCalibrationMode();');
 assert(invalidateAt>=invalidation.index&&invalidateAt<invalidation.index+invalidation[0].length,'No unconditional invalidation before the request snapshot');
 const anchors=[region(text,'product-default-auto-true','    bool autoCalibrate = true;','source-only'),
  region(text,'controller-published-atomic-default-true','static std::atomic<bool> g_controllerAutoCalibrationRequested{true};','source-only'),
  region(text,'existing-setting-local-windows-field-release-publication',publication[0],'source-only'),
  region(text,'controller-steady-acquire-published-preference',steadyLoad,'source-only'),
  region(text,'controller-owner-evidence-acquire-published-preference',ownerLoad,'source-only'),
  region(text,'manual-invalidation-only-after-actual-request-snapshot',invalidation[0],'source-only')];
 const get=name=>constants.find(e=>e.symbol===name).code;
 const generated=registry.code+'\n'+declaration[0]+'\n'+constants.filter(e=>e!==registry).map(e=>e.code).join('\n')+'\n'+entries.filter(e=>e.kind==='function').map(e=>e.code).join('\n\n')+'\n';
 fs.writeFileSync(path.join(out,'main.source.snapshot.cpp'),bytes);
 fs.writeFileSync(path.join(out,'production_types.generated.h'),entries.filter(e=>e.kind==='struct'&&!e.symbol.endsWith('Scope')).map(e=>e.code).join('\n\n')+'\n');
 fs.writeFileSync(path.join(out,'production_scopes.generated.h'),entries.filter(e=>e.symbol.endsWith('Scope')).map(e=>e.code).join('\n\n')+'\n');
 fs.writeFileSync(path.join(out,'production_functions.generated.h'),generated);
 const binding={source:'native/main.cpp',sourceSha256:sha(bytes),sourceBytes:bytes.length,entries:[...entries,...constants].map(({code,...e})=>e),anchors,scope:'Verbatim functions/types/registry required by existing base harness compile; only new named automatic/manual scenarios execute. Whole main/Windows/Serial/UI/Host/reader not executed.'};
 fs.writeFileSync(path.join(out,'SOURCE-BINDINGS.json'),JSON.stringify(binding,null,2)+'\n');return {binding,entries:[...entries,...constants],anchors,generated};
}
export function verify(repo,frozen){
 const b=fs.readFileSync(path.join(repo,'native/main.cpp')),t=b.toString('utf8');
 return {methodsStable:frozen.entries.filter(e=>e.kind!=='constant').every(e=>extract(t,e.symbol,e.kind).sha256===e.sha256),constantsStable:frozen.entries.filter(e=>e.kind==='constant').every(e=>t.includes(e.code)),anchorsStable:frozen.anchors.every(e=>t.includes(e.code)),wholeMainDrifted:sha(b)!==frozen.binding.sourceSha256,currentMainSha256:sha(b)};
}
