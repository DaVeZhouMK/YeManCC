import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {closeBrace} from '../../../fixtures/hc_remaining_model_capabilities/expectations.mjs';
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');

export function generateFixture(repoRoot,evidenceRoot){
  const root=path.resolve(evidenceRoot);
  if(fs.existsSync(root))throw Error('FRESH_EVIDENCE_DIRECTORY_REQUIRED');
  fs.mkdirSync(root,{recursive:true});
  const inputs=['native/main.cpp','capabilities/hc-device-catalog.json','capabilities/hc-device-catalog.generated.h','capabilities/HcOemInputCapabilityBinding.h'];
  const records=inputs.map(relative=>{const bytes=fs.readFileSync(path.join(repoRoot,relative)),copy=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.writeFileSync(copy,bytes);return{path:relative,sha256:hash(bytes),copy};});
  const source=fs.readFileSync(records[0].copy,'utf8'),catalog=JSON.parse(fs.readFileSync(records[1].copy,'utf8'));
  const fragments=[];
  function block(marker,prefix=''){
    let start=-1,open,semi;
    do {start=source.indexOf(marker,start+1);if(start<0)throw Error('ACTUAL_DEFINITION_MISSING:'+marker);open=source.indexOf('{',start);semi=source.indexOf(';',start);} while(semi>=0&&semi<open);
    const end=closeBrace(source,open)+1;
    const code=source.slice(start,end+(source[end]===';'?1:0));
    fragments.push({marker,start,sha256:hash(code)});return prefix+code;
  }
  const definitions=[block('enum class YmccFamily : int {'),block('enum OemVkBit : uint32_t {')];
  for(const match of source.matchAll(/^static constexpr uint64_t kOemBit[^;]+;/gm))definitions.push(match[0]);
  definitions.push(block('struct OemChordRule {'),block('struct OemChordProfile {'));
  for(const match of source.matchAll(/^static const OemChordRule \w+\[\] = \{/gm))definitions.push(block(match[0]));
  definitions.push(block('enum class OemChordProfileId : int {'),block('static const OemChordProfile kOemProfiles[] = {'),block('static const OemChordProfile& oemChordProfileFor('));
  definitions.push(block('enum class OemKeyForm : int {'),block('struct OemKeyDef {'),block('struct OemCatalogRef {'));
  for(const match of source.matchAll(/^static const OemKeyDef \w+\[\] = \{/gm))definitions.push(block(match[0]));
  definitions.push(block('static OemCatalogRef oemCatalogOf(','template<size_t N>\n'),block('static OemCatalogRef oemCatalogForFamily('),block('static int oemChordProfileIdFor('),block('static void oemChordEvalRules('));
  const helperMarker='static const ymcc::capabilities::oem::InputBinding<OemCatalogRef>& oemUnifiedInputBindingFor(';
  const mainBindingPresent=source.includes(helperMarker);
  let gateSelectionFragment=null;
  if(mainBindingPresent){
    definitions.push(block(helperMarker),block('static int oemBoundChordProfileIdFor('),block('static OemCatalogRef oemBoundCatalogForFamily('));
    const begin=source.indexOf('    const int profileId = oemBoundChordProfileIdFor(g_machineIdentity.family, g_machineIdentity.deviceClass);');
    const end=source.indexOf('    const bool chordCapable = oemChordProfileFor(profileId).count > 0;',begin);
    if(begin<0||end<begin||end-begin>700)throw Error('ACTUAL_BOUND_GATE_FRAGMENT_MISSING');
    gateSelectionFragment=source.slice(begin,end+'    const bool chordCapable = oemChordProfileFor(profileId).count > 0;'.length);
    fragments.push({marker:'actual gate selection + exchange/epoch/chordCapable',sha256:hash(gateSelectionFragment)});
    const states=['g_oemBackProfileId','g_oemShortcutEdgeEpoch'].map(name=>{const row=source.match(new RegExp('^static std::atomic<[^>]+> '+name+'[^;]*;','m'));if(!row)throw Error('ACTUAL_GATE_STATE_MISSING:'+name);return row[0];});
    definitions.push('struct {YmccFamily family;std::string deviceClass;} g_machineIdentity;');
    definitions.push(...states,'static bool actualGateSelection(){\n'+gateSelectionFragment+'\nreturn chordCapable;\n}');
  }
  const enumText=definitions[0].slice(definitions[0].indexOf('{')+1,definitions[0].lastIndexOf('}'));
  const familyNames=enumText.replace(/\/\/[^\n]*/g,'').split(',').map(s=>s.trim().split('=')[0].trim()).filter(Boolean);
  const cases=catalog.devices.flatMap(device=>device.aliases.map(alias=>({alias,canonical:device.consumerClass,id:device.deviceId})));
  const header=path.join(root,'input-sources/capabilities/HcOemInputCapabilityBinding.h').replaceAll('\\','/');
  let main=String.raw`
using Cache=ymcc::capabilities::oem::InputBindingCache<YmccFamily,OemCatalogRef>;
static unsigned checks=0;
static void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
int main(){try{
  Cache cache;unsigned selected=0;
  for(const char* cls:{"HandheldCompanion.Devices.GPDWin4","HandheldCompanion.Devices.GPDWin5"}){
    const auto& value=cache.bind(YmccFamily::Gpd,std::string(cls),oemChordProfileIdFor,oemCatalogForFamily);
    check(value.profileId==oemChordProfileIdFor(YmccFamily::Gpd,std::string(cls)),"class change invalidates original profile consumer");
  }
  for(const auto family:families)for(const auto& row:cases){
    const auto& bound=cache.bind(family,std::string(row.alias),oemChordProfileIdFor,oemCatalogForFamily);
    const auto expected=oemChordProfileIdFor(family,std::string(row.canonical));
    const auto expectedCatalog=oemCatalogForFamily(family,std::string(row.canonical));
    check(bound.model&&bound.model->deviceId==row.id,"registered model binding");
    check(bound.profileId==expected,"actual original profile selection");
    check(bound.catalog.keys==expectedCatalog.keys&&bound.catalog.count==expectedCatalog.count,"actual original catalog selection");
    {
      uint64_t mask=~uint64_t(0);int state=7,physical=7;uint32_t base=~uint32_t(0),prefix=~uint32_t(0);
      oemChordEvalRules(bound.profileId,0,false,true,&mask,&state,&physical,&base,&prefix);
      check(mask==0&&state==0&&physical==0&&base==0&&prefix==0,"actual key-up releases all published state");
    }
    ++selected;
  }
  for(const auto family:families)for(const char* cls:{"unknown","","HandheldCompanion.Devices.AYANEOFlip1SFuture","HandheldCompanion.Devices.ROGAllyExtension"}){
    const auto& bound=cache.bind(family,std::string(cls),oemChordProfileIdFor,oemCatalogForFamily);
    check(bound.model==nullptr,"unregistered extensions stay explicit");
    check(bound.profileId==oemChordProfileIdFor(family,std::string(cls)),"original family extension fallback preserved");
    const auto expected=oemCatalogForFamily(family,std::string(cls));
    check(bound.catalog.keys==expected.keys&&bound.catalog.count==expected.count,"original extension catalog preserved");
  }
  const std::string same="HandheldCompanion.Devices.GPDWin5";
  for(const auto family:{YmccFamily::Gpd,YmccFamily::Unknown,YmccFamily::MsiClaw,YmccFamily::Gpd}){
    const auto& value=cache.bind(family,same,oemChordProfileIdFor,oemCatalogForFamily);
    check(value.profileId==oemChordProfileIdFor(family,same),"family change invalidates same-class cache");
  }
  unsigned profileCalls=0,catalogCalls=0;
  auto profileSelect=[&](YmccFamily family,const std::string& cls){++profileCalls;return oemChordProfileIdFor(family,cls);};
  auto catalogSelect=[&](YmccFamily family,const std::string& cls){++catalogCalls;return oemCatalogForFamily(family,cls);};
  cache.resetIdentity();
  for(unsigned i=0;i<10000;++i)cache.bind(YmccFamily::Gpd,same,profileSelect,catalogSelect);
  check(profileCalls==1&&catalogCalls==1,"stable identity resolves actual consumers once");
  cache.resetIdentity();cache.bind(YmccFamily::Gpd,same,profileSelect,catalogSelect);
  check(profileCalls==2&&catalogCalls==2,"reset forces fresh consumer binding");
  const int win5=cache.bind(YmccFamily::Gpd,same,profileSelect,catalogSelect).profileId;
  for(const bool ready:{true,false,true,false}){
    uint64_t mask=0;int state=0,physical=0;
    oemChordEvalRules(win5,kVkF14,false,ready,&mask,&state,&physical,nullptr,nullptr);
    check(physical==1,"physical observation remains separate from interface gate");
    check(state==(ready?1:0)&&mask==(ready?kOemBitL4:0),"live interface admission is never cached with identity");
    oemChordEvalRules(win5,0,false,ready,&mask,&state,&physical,nullptr,nullptr);
    check(mask==0&&state==0&&physical==0,"keyup clears active and refused consumers");
  }
  std::cout<<"OEM_BINDING_SUMMARY checks="<<checks<<" selected="<<selected<<" hardwareAccess=false productionSelectors=true productionEvaluator=true\n";
  return 0;
}catch(const std::exception& error){std::cerr<<"OEM_BINDING_FAIL "<<error.what()<<"\n";return 1;}}
`;
  if(mainBindingPresent)main=main.replace('    ++selected;',String.raw`
    const int beforeProfile=g_oemBackProfileId.load();
    const auto beforeEpoch=g_oemShortcutEdgeEpoch.load();
    g_machineIdentity.family=family;g_machineIdentity.deviceClass=row.alias;
    const bool capable=actualGateSelection();
    check(g_oemBackProfileId.load()==expected,"actual main gate consumes unified original profile");
    check(g_oemShortcutEdgeEpoch.load()==beforeEpoch+(beforeProfile==expected?0u:1u),"actual main exchange/epoch is retained");
    check(capable==(oemChordProfileFor(expected).count>0),"actual main chord capability remains original table");
    const auto actualCatalog=oemBoundCatalogForFamily(family,std::string(row.alias));
    check(actualCatalog.keys==expectedCatalog.keys&&actualCatalog.count==expectedCatalog.count,"actual main wrapper consumes original catalog");
    ++selected;`);
  const cpp='#include <atomic>\n#include <cstdint>\n#include <string>\n#include <vector>\n#include <stdexcept>\n#include <iostream>\n#include '+JSON.stringify(header)+'\n'+definitions.join('\n')+'\nstruct Case{const char* alias;const char* canonical;const char* id;};\nstatic const Case cases[]={\n'+cases.map(row=>'{'+[row.alias,row.canonical,row.id].map(JSON.stringify).join(',')+'}').join(',\n')+'\n};\nstatic const YmccFamily families[]={'+familyNames.map(name=>'YmccFamily::'+name).join(',')+'};\n'+main;
  fs.writeFileSync(root+'/actual-oem-binding.cpp',cpp);
  const binding={generatedAt:new Date().toISOString(),scope:'New binding header consumes unchanged extracted production selectors, actual catalog tables and actual key-up/interface-gate evaluator. Product main wiring remains separately pending.',inputs:records,fragments,cases:cases.length,families:familyNames.length,mainBindingPresent,gateFragmentSha256:gateSelectionFragment?hash(gateSelectionFragment):null,physicalHardwareValidated:false};
  fs.writeFileSync(root+'/source-binding.json',JSON.stringify(binding,null,2)+'\n');
  return binding;
}
