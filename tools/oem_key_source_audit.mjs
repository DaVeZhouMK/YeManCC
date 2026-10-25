// Static reachability audit: canonical lookup does not prove a source publisher exists.
// This joins compiled native routing with reviewed producer-selection predicates;
// it is source analysis only, not runtime readiness or physical acceptance.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');if(!process.argv[2]||!process.argv[3])throw Error('Usage: <compiled-native-matrix.json> <out-dir>');
const out=path.resolve(process.argv[3]);fs.mkdirSync(out,{recursive:true});const source=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8'),matrixData=fs.readFileSync(process.argv[2],'utf8'),matrix=JSON.parse(matrixData);
const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
function block(marker){const start=source.indexOf(marker),end=source.indexOf('\n}',start);if(start<0||end<0)throw Error('Missing '+marker);return source.slice(start,end+(source[end+2]===';'?3:2));}

function functionBody(name){const m=new RegExp('^static [^\\n;]*\\b'+name+'\\([^;]*?\\)\\s*\\{','m').exec(source);if(!m)throw Error('No function '+name);return source.slice(m.index,source.indexOf('\n}',m.index)+2);}
const constants=Object.fromEntries([...source.matchAll(/\b(kOemBit\w+)\s*=\s*1ull\s*<<\s*(\d+)/g)].map(m=>[m[1],1n<<BigInt(m[2])]));
const producerEnum=/enum class OemProducerId[^\{]+\{([^}]+)\}/.exec(source)?.[1].split(',').map(s=>s.trim()).filter(s=>s!=='Count');
const producerBody=/static constexpr std::array<uint64_t, kOemProducerCount> kOemProducerMasks = \{([\s\S]*?)\n\};/.exec(source)?.[1];if(!producerEnum||!producerBody)throw Error('Missing producer scope declarations');
const producers=producerBody.split(',').map((expr,i)=>{const names=expr.match(/kOemBit\w+/g)||[];const value=names.reduce((v,k)=>{if(!(k in constants))throw Error('Unknown source mask '+k);return v|constants[k]},0n);return{name:producerEnum[i],mask:value,names};});if(producers.length!==producerEnum.length)throw Error('Producer mask order mismatch');
const desc=block('static OnexCapDescriptor onexCapDescriptorFor(');const onexClasses=[...desc.matchAll(/cls == "([^"]+)"/g)].map(m=>m[1]);
// Bind this analysis to actual selection and dispatch seams. Missing or changed
// structural anchors require review rather than silently counting every key.
const anchors=[
 'const bool gpdFam = g_machineIdentity.family == YmccFamily::Gpd &&',
 'g_machineIdentity.deviceClass == "HandheldCompanion.Devices.GPDWin5"',
 'g_machineIdentity.family == YmccFamily::OneXPlayer &&',
 'onexCapDescriptorFor(onexCls).valid',
 'if (gpdFam != g_gpdBack.enabled) gpdBackSetEnabled(gpdFam);',
 'if (onexFam != g_onexBack.enabled) onexBackSetEnabled(onexFam);',
 'const bool msiFam = g_machineIdentity.family == YmccFamily::MsiClaw;',
 'if (msiFam != g_msiWmiEnabled) msiWmiSetEnabled(msiFam);',
 'if (msiFam != g_msiDinput.enabled.load(std::memory_order_acquire)) msiDinputSetEnabled(msiFam);',
 'return id(OemChordProfileId::None);',
];for(const a of anchors)if(!source.includes(a))throw Error('Reader-dispatch contract changed: '+a);
const classes=[];const seen=new Set();let keys=0,gaps=[];
for(const m of matrix.models){if(seen.has(m.deviceClass))continue;seen.add(m.deviceClass);
 const family=m.family,cls=m.deviceClass;let selected=[];
 if(['AsusRogAllyX','AsusRogAlly','AsusRogAllyClassic','AsusRogAllyXClassic'].includes(family)&&cls.includes('ROGAlly'))selected.push('RogClick');
 if(family==='MsiClaw')selected.push('Msi','MsiWmiFront');
 if(family==='Gpd'&&cls==='HandheldCompanion.Devices.GPDWin5')selected.push('Gpd');
 if(family==='LenovoLegionGo')selected.push(/LegionGoSZ[12]$/.test(cls)?'LegionS':'Legion');
 if(family==='OneXPlayer'&&onexClasses.includes(cls))selected.push('OneX');
 const rows=[];
 for(const k of m.keys){++keys;const mask=BigInt(k.canonicalMaskDecimal);const sources=[];
  if(k.keyboardRules.length)sources.push({kind:'keyboard-or-mouse-profile',profileId:m.profileId,ruleRows:k.keyboardRules.length});
  for(const name of selected){const p=producers.find(p=>p.name===name);const scope=name==='MsiWmiFront'?(constants.kOemBitClaw|constants.kOemBitQs):p.mask;
   if((scope&mask)!==0n)sources.push({kind:name==='MsiWmiFront'?'WMI-notification':'vendor-HID-producer',source:name});
  }
  const row={keyId:k.keyId,label:k.label,canonicalBound:k.canonicalBound,sourceCandidates:sources,
    sourceCandidateDeclared:sources.length>0,runtimeReadyVerified:false,physicalResponseVerified:false,
    evidenceScope:'reviewed reader selection + compiled keyboard route; candidate is not current device readiness'};
  if(!sources.length){row.finding='catalog-key-has-no-selected-producer-or-keyboard-route';gaps.push({deviceClass:cls,...row});}
  rows.push(row);
 }
 classes.push({deviceClass:cls,family,selectedVendorSources:selected,keys:rows});
}
// Special2 has no keyboard or producer writer anywhere in this build. Special
// belongs to OneX's vendor scope, whose dispatch is not enabled for Steam Deck.
const globalKeyboardMask=matrix.models.flatMap(m=>m.keys).filter(k=>k.keyboardRules.length).reduce((v,k)=>v|BigInt(k.canonicalMaskDecimal),0n);
const allProducerMask=producers.reduce((v,p)=>v|p.mask,constants.kOemBitClaw|constants.kOemBitQs);
const invariants={steamDeckSpecial2HasNoGlobalProducer:((globalKeyboardMask|allProducerMask)&constants.kOemBitSpecial2)===0n,
  steamDeckDoesNotSelectOneXSource:classes.filter(c=>c.family==='ValveSteamDeck').every(c=>!c.selectedVendorSources.includes('OneX')),
  allRemainingKeysHaveStaticSourceCandidate:gaps.every(g=>g.deviceClass==='HandheldCompanion.Devices.SteamDeck')};
if(!invariants.steamDeckSpecial2HasNoGlobalProducer||!invariants.steamDeckDoesNotSelectOneXSource)throw Error('Steam Deck source gap hypothesis contradicted; inspect current new reader');
const report={schema:1,nativeMainSha256:sha(source),compiledMatrixSha256:sha(matrixData),modelClasses:classes.length,catalogKeys:keys,
  staticCandidateKeys:keys-gaps.length,sourceGaps:gaps,classes,invariants,
  sourceBindings:{producerMasksSha256:sha(producerBody),onexDescriptorSha256:sha(desc),consumerSha256:sha(functionBody('nativeOemKeyMask')),keyboardEvaluatorSha256:sha(functionBody('oemChordEvalRules'))},
  producerScopes:producers.map(p=>({name:p.name,canonicalNames:p.names,maskDecimal:p.mask.toString()})),
  proofLimit:'static reachability; no claim of hardware readiness, event delivery, action suppression, lifecycle or real per-key input',hardwareIo:false,goalComplete:false};
fs.writeFileSync(path.join(out,'oem-key-source-reachability.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({classes:classes.length,keys,staticCandidates:keys-gaps.length,sourceGaps:gaps.map(g=>g.deviceClass+':'+g.keyId),invariants},null,2));