import crypto from 'node:crypto';
export const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
// C++ lexical brace matching: comments, escaped strings and raw strings cannot
// terminate a production function early. No function body rewriting is allowed.
function closeAt(text, start, open, close) {
  let depth=0;
  for(let i=start;i<text.length;++i){
    if(text.startsWith('//',i)){const next=text.indexOf('\n',i+2);i=next<0?text.length:next;continue;}
    if(text.startsWith('/*',i)){const next=text.indexOf('*/',i+2);if(next<0)throw Error('Unclosed C++ comment');i=next+1;continue;}
    if(text.startsWith('R"',i)){const begin=text.indexOf('(',i+2);if(begin<0)throw Error('Invalid C++ raw string');const endToken=')'+text.slice(i+2,begin)+'"';const end=text.indexOf(endToken,begin+1);if(end<0)throw Error('Unclosed C++ raw string');i=end+endToken.length-1;continue;}
    if(text[i]==='"'||text[i]==="'"){const quote=text[i];while(++i<text.length){if(text[i]==='\\'){++i;continue;}if(text[i]===quote)break;}continue;}
    if(text[i]===open)++depth;else if(text[i]===close&&!--depth)return i;
  }
  throw Error(`Unclosed ${open} at ${start}`);
}
export function func(text,name){
  const re=new RegExp(`^static[^\\r\\n;{}]*\\b${name}\\s*\\(`,'gm');let m;
  while((m=re.exec(text))){const paren=text.indexOf('(',m.index),endParen=closeAt(text,paren,'(',')');const tail=text.slice(endParen+1).match(/^\s*(?:noexcept\s*)?([;{])/);if(!tail||tail[1]===';')continue;
    const open=text.indexOf('{',endParen+1),end=closeAt(text,open,'{','}')+1;return {name,start:m.index,end,text:text.slice(m.index,end)};}
  throw Error(`Production definition not found: ${name}`);
}
function block(text,anchor){const start=text.indexOf(anchor);if(start<0||text.indexOf(anchor,start+1)>=0)throw Error(`Nonunique block ${anchor}`);const open=text.indexOf('{',start),end=closeAt(text,open,'{','}')+2;return {name:anchor,start,end,text:text.slice(start,end)};}
export function extract(main,before){
  const ranges=[],take=(name)=>{const r=func(main,name);ranges.push(r);return r.text;};
  const takeBlock=anchor=>{const r=block(main,anchor);ranges.push(r);return r.text;};
  const slice=(name,startAnchor,end)=>{const start=main.indexOf(startAnchor);if(start<0)throw Error(name);const r={name,start,end,text:main.slice(start,end)};ranges.push(r);return r.text;};
  const guidStart=main.indexOf('static const GUID kYeManGuidDeviceInterfaceHid ='),guidEnd=main.indexOf(';',guidStart)+1;if(guidStart<0)throw Error('HID notification GUID missing');
  const guid=slice('HID notification GUID','static const GUID kYeManGuidDeviceInterfaceHid =',guidEnd);
  const env=[guid,takeBlock('enum class PowerLifecycle :'),takeBlock('enum class SerialJobKind '),takeBlock('enum class YmccFamily :'),takeBlock('struct MachineIdentity {')].join('\n');
  const vk=takeBlock('enum OemVkBit :');
  const bitStart=main.indexOf('static constexpr uint64_t kOemBitM1'),bitEnd=main.indexOf(';',main.indexOf('static constexpr uint64_t kOemBitLegionFrontL'))+1;
  const keys=[vk,slice('OEM canonical bits','static constexpr uint64_t kOemBitM1',bitEnd)];
  const profileEnd=func(main,'oemChordProfileFor').end;
  const profiles=slice('OEM profiles','struct OemChordRule {',profileEnd)+'\n'+take('oemChordEvalRules')+'\n'+take('oemBackHookRecompute');
  const ledger=slice('OEM ledger','enum class OemProducerId :',func(main,'oemProducerTryPop').end);
  const owner=slice('OneX application owner','static ymcc::onex_x2::WindowsTransport g_onexX2Transport;',func(main,'onexX2OwnerStop').end);
  const tickRange=func(main,'onexX2OwnerTick'),stopRange=func(main,'onexX2OwnerStop');
  const ownerPrefix=main.slice(main.indexOf('static ymcc::onex_x2::WindowsTransport g_onexX2Transport;'),tickRange.start);
  const ownerTick=tickRange.text,ownerStop=stopRange.text;
  const masks=['oemBackMergedKeyMask','nativeOemKeyboardActionMask','nativeOemKeyMask'].map(take).join('\n');
  const rule=takeBlock('struct NativeKeyboardShortcutRule {');
  const submit=take('nativeYmccShortcutSubmit'),dispatch=take('nativeKeyboardShortcutDispatch');
  const home=take('nativeOnexX2HomeDispatch'),drain=take('nativeOemProducerDrain');
  const evaluator=take('nativeKeyboardShortcutsEvaluate'),recordingMask=take('gamepadShortcutRecordingMask');
  const maskEntries=text=>{const start=text.indexOf('kOemProducerMasks = {');if(start<0)throw Error('Mask catalog missing');const open=text.indexOf('{',start);return text.slice(open+1,closeAt(text,open,'{','}')).split(',').map(s=>s.replace(/\s+/g,'')).filter(Boolean);};
  const oldMasks=maskEntries(before),newMasks=maskEntries(main);
  if(oldMasks.length!==6||newMasks.length!==8||JSON.stringify(oldMasks)!==JSON.stringify(newMasks.slice(0,6)))throw Error('Old producer masks changed');
  const oldProfiles=before.slice(before.indexOf('struct OemChordRule {'),func(before,'oemChordProfileFor').end);
  const newProfiles=main.slice(main.indexOf('struct OemChordRule {'),func(main,'oemChordProfileFor').end);
  if(oldProfiles!==newProfiles)throw Error('Model keyboard profile data changed since before-application; review required');
  const rog=func(main,'nativeRogClickDispatch'),oldRog=func(before,'nativeRogClickDispatch');
  if(rog.text!==oldRog.text)throw Error('ROG dispatch changed against main.before-application.cpp; stub cannot stand for unreviewed changes');
  const staticWiring={};
  for(const name of ['inputCaptureEmergencyCleanup','inputCaptureShutdown','inputCaptureThreadProc']){const f=func(main,name);ranges.push({...f,staticOnly:true});if(!f.text.includes(name==='inputCaptureThreadProc'?'onexX2OwnerTick();':'onexX2OwnerStop();'))throw Error(`${name}: new owner wiring absent`);staticWiring[name]={sha256:sha(f.text),startLine:main.slice(0,f.start).split('\n').length,bytes:Buffer.byteLength(f.text)};}
  const shutdown=func(main,'inputCaptureShutdown').text;if(shutdown.indexOf('onexX2OwnerStop();')>shutdown.indexOf('if (!g_inputCaptureReady)'))throw Error('OwnerStop after early return');
  const powerFunction=take('currentPowerGeneration');
  return {env,keys:keys.join('\n'),profiles,ledger,owner,ownerPrefix,ownerTick,ownerStop,masks,rule,submit,dispatch,home,drain,evaluator,recordingMask,powerFunction,
    evidence:{ranges:ranges.map(({name,start,end,text,staticOnly})=>({name,start,end,startLine:main.slice(0,start).split('\n').length,endLine:main.slice(0,end).split('\n').length,sha256:sha(text),staticOnly:!!staticOnly})),
    staticWiring,oldProducerMasks:{entries:oldMasks,unchanged:true},oldModelKeyboardProfiles:{sha256:sha(newProfiles),beforeSha256:sha(oldProfiles),unchanged:true},rogDispatch:{currentSha256:sha(rog.text),beforeSha256:sha(oldRog.text),unchanged:true},bitRange:{start:bitStart,end:bitEnd}}};
}
export function generated(parts,slots){
  return ['// Production bodies below are byte-for-byte source slices. See source-bindings.json.',parts.env,slots.environment,parts.powerFunction,parts.keys,slots.forward,
    parts.profiles,parts.ledger,parts.ownerPrefix,slots.ownerObservation,'#define onexX2RevokeSources appObserveRealRevoke',parts.ownerTick,'#undef onexX2RevokeSources',parts.ownerStop,parts.masks,parts.rule,'static std::vector<NativeKeyboardShortcutRule> g_nativeKeyboardShortcuts;',
    parts.submit,slots.action,parts.dispatch,slots.ui,parts.recordingMask,parts.evaluator,slots.observation,parts.home,'#define nativeKeyboardShortcutsEvaluate appObserveRealEvaluation',parts.drain,'#undef nativeKeyboardShortcutsEvaluate'].join('\n\n')+'\n';
}
