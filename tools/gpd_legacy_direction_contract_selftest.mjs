// Actual native evaluator vs pinned HC left/right declarations; no device APIs run.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
if(!process.argv[2]||!process.argv[3]||!process.argv[4])throw Error('Usage: <out> <HC Devices directory> <before-main.cpp> [source override for negative replay]');
const out=path.resolve(process.argv[2]),hc=path.resolve(process.argv[3]);fs.mkdirSync(out,{recursive:true});
const sourcePath=process.argv[5]?path.resolve(process.argv[5]):path.join(repo,'native/main.cpp');const source=fs.readFileSync(sourcePath,'utf8'),before=fs.readFileSync(path.resolve(process.argv[4]),'utf8');
const sha=s=>crypto.createHash('sha256').update(s).digest('hex');
function fn(s,n){const m=new RegExp('^static [^\\n;]*\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);if(!m)throw Error('Missing '+n);return s.slice(m.index,s.indexOf('\n}',m.index)+2);}
const names=['GPDWin3','GPDWin4','GPDWinMax2','GPDWinMax2Intel','GPDWinMini'],evidence=[];
for(const name of names){
 const file=path.join(hc,'GPD',name+'.cs'),text=fs.readFileSync(file,'utf8');
 const parent=new RegExp('public class '+name+'\\s*:\\s*(\\w+)').exec(text)?.[1];
 const effective=parent==='GPDWinMax2'?fs.readFileSync(path.join(hc,'GPD/GPDWinMax2.cs'),'utf8'):text;
 const chords=[...effective.matchAll(/new KeyboardChord\("([^"]+)",\s*\[([^\]]+)\],\s*\[([^\]]+)\],\s*(true|false),\s*ButtonFlags\.(\w+)/g)].filter(m=>/left|right|^L4$|^R4$/i.test(m[1])).map(m=>{
  const left=/left|^L4$/i.test(m[1]);const keys=m[2].split(',').map(k=>k.trim().replace(/^KeyCode\./,''));
  if(keys.join(',')!==(left?'F11,L':'F12,R'))throw Error('HC source changed: '+name+' '+m[1]);
  return{label:m[1],keys,canonicalKey:left?'l4':'r4',expectedBack:left?1:2,sourceFlag:m[5]};
 });if(chords.length!==2)throw Error('No independent HC left/right pair '+name);
 evidence.push({deviceClass:'HandheldCompanion.Devices.'+name,sourceSha256:sha(text),parent,chords});
}
fs.writeFileSync(path.join(out,'independent-HC-side-contract.json'),JSON.stringify(evidence,null,2));
const r=spawnSync(process.execPath,[path.join(repo,'tools/oem_capability_matrix_fixture.mjs'),path.join(out,'definitions'),sourcePath],{encoding:'utf8'});if(r.status!==0)throw Error(r.stderr||r.stdout);
const matrix=fs.readFileSync(path.join(out,'definitions/actual-oem-capability-matrix.cpp'),'utf8');const prefix=matrix.slice(0,matrix.indexOf('int main(){'));
const fixtures=evidence.flatMap(e=>e.chords.map(c=>`{${JSON.stringify(e.deviceClass)},${c.keys.map(k=>'kVk'+k).join('|')},${JSON.stringify(c.canonicalKey)},${c.expectedBack}}`));
const cpp='#include <stdexcept>\n#include <set>\n'+prefix+'\n'+fn(before,'oemChordEvalRules').replace('oemChordEvalRules(','beforeOemChordEvalRules(')+String.raw`
struct SideCase{const char* cls;uint32_t keys;const char* key;int side;};
static const SideCase sideCases[]={
`+fixtures.join(',\n')+String.raw`
};
int main(){unsigned checks=0,modelMatches=0;std::set<std::string> reachedClasses;const auto check=[&](bool ok,const char* n){++checks;if(!ok)throw std::runtime_error(n);};
try{
 for(const auto& c:sideCases)for(const auto& model:models)if(std::string(model.cls)==c.cls){++modelMatches;reachedClasses.insert(model.cls);
  const int profile=oemChordProfileIdFor(model.family,model.cls);check(profile==int(OemChordProfileId::GpdLegacy),"HC legacy model selects GpdLegacy");
  const auto cat=oemCatalogForFamily(model.family,model.cls);bool found=false;for(int i=0;i<cat.count;i++)if(std::string(cat.keys[i].keyId)==c.key){found=true;check(cat.keys[i].backIndex==c.side,"catalog position agrees with independent HC physical-side declaration");}check(found,"canonical key exists");
  for(unsigned cycle=0;cycle<20;++cycle)for(unsigned p=0;p<8;++p)for(bool ready:{false,true}){
   const uint32_t extra=(p&1?kVkWin:0)|(p&2?kVkCtrl:0)|(p&4?kVkShift:0);uint64_t canonical=0,expectedCanonical=0;int state=0,physical=0;uint32_t base=0,prefix=0;
   oemChordEvalRules(profile,c.keys|extra,false,ready,&canonical,&state,&physical,&base,&prefix);nativeKeyboardShortcutMaskBit(c.key,expectedCanonical);
   check(state==c.side,"GPD legacy actual output side must agree with independently named HC left/right chord");check(canonical==expectedCanonical,"canonical identity retained");check(physical==c.side&&base==c.keys&&prefix==extra,"diagnostic source side and prefix retained");
   oemChordEvalRules(profile,extra,false,ready,&canonical,&state,&physical,&base,&prefix);check(state==0&&canonical==0,"release cannot stick");
  }
 }
 check(modelMatches>=std::size(sideCases)&&reachedClasses.size()==5,"all five HC legacy classes reached native identity matrix");
 for(int id=0;id<int(std::size(kOemProfiles));++id){const auto& profile=oemChordProfileFor(id);
  for(int i=0;i<profile.count;++i)for(bool ready:{false,true})for(uint32_t extra:{0u,uint32_t(kVkWin),uint32_t(kVkCtrl|kVkShift)}){
   const auto& rule=profile.rules[i];uint64_t a=0,b=0;int sa=0,sb=0,pa=0,pb=0;uint32_t ba=0,bb=0,xa=0,xb=0;
   beforeOemChordEvalRules(id,rule.keys|extra,rule.mouseChord,ready,&a,&sa,&pa,&ba,&xa);oemChordEvalRules(id,rule.keys|extra,rule.mouseChord,ready,&b,&sb,&pb,&bb,&xb);
   check(a==b&&pa==pb&&ba==bb&&xa==xb,"every profile retains canonical source diagnostics");if(id!=int(OemChordProfileId::GpdLegacy))check(sa==sb,"all non-legacy output directions unchanged from round2");
  }
 }
 for(uint32_t partial:{uint32_t(kVkF11),uint32_t(kVkL),uint32_t(kVkF12),uint32_t(kVkR)}){uint64_t mask=0;int back=0;oemChordEvalRules(int(OemChordProfileId::GpdLegacy),partial,false,true,&mask,&back,nullptr,nullptr,nullptr);check(mask==0&&back==0,"partial chord cannot create a rear press");}
 uint64_t mask=0;int back=0;oemChordEvalRules(int(OemChordProfileId::GpdLegacy),kVkF11|kVkL|kVkF12|kVkR,false,false,&mask,&back,nullptr,nullptr,nullptr);check(mask==(kOemBitL4|kOemBitR4)&&back==3,"simultaneous legacy sides remain independent without Win5 interface gate");
 std::cout<<"GPD_LEGACY_SIDE_CONTRACT_OK checks="<<checks<<" independentHCClasses=5 hardware_io=none actual_native_evaluator=true\n";return 0;
}catch(const std::exception& e){std::cerr<<"GPD_LEGACY_SIDE_CONTRACT_FAIL "<<e.what()<<'\n';return 1;}}
`;
fs.writeFileSync(path.join(out,'actual-gpd-legacy-side-contract.cpp'),cpp);fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify({schema:1,sourcePath,mainSha256:sha(source),beforeMainSha256:sha(before),independentContractSha256:sha(JSON.stringify(evidence)),fixtureSha256:sha(cpp),hardwareIo:false},null,2));console.log('Generated actual GPD legacy evaluator vs five independently extracted HC side contracts.');
