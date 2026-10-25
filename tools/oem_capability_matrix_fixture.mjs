import {captureOemInputHeaders, oemInputBindingFunctions} from './lib/hc_oem_input_fixture.mjs';
// Emit exact native capability/keyboard-routing evidence without opening devices.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
if(!process.argv[2])throw Error('Output directory required');
const out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});
const sourcePath=process.argv[3]?path.resolve(process.argv[3]):path.join(repo,'native/main.cpp');
const source=fs.readFileSync(sourcePath,'utf8');
const oemHeaders=captureOemInputHeaders({repo,out,source});
function block(marker){const start=source.indexOf(marker);if(start<0)throw Error('Missing '+marker);const end=source.indexOf('\n}',start);if(end<0)throw Error('No end '+marker);return source.slice(start,end+(source[end+2]===';'?3:2));}
function match(re){const m=re.exec(source);if(!m)throw Error('No definition '+re);return m[0];}
const definitions=[
 block('enum class YmccFamily : int {'),block('enum OemVkBit : uint32_t {'),
 ...source.matchAll(/static constexpr uint64_t kOemBit[^;]+;/g)].map(x=>typeof x==='string'?x:x[0]);
definitions.push(block('struct OemChordRule {'),block('struct OemChordProfile {'),
 ...[...source.matchAll(/static const OemChordRule \w+\[\] = \{[\s\S]*?\n\};/g)].map(m=>m[0]),
 block('enum class OemChordProfileId : int {'),block('static const OemChordProfile kOemProfiles[]'),
 block('static const OemChordProfile& oemChordProfileFor('),
 match(/enum class OemKeyForm : int \{[^\n]+\};/),block('struct OemKeyDef {'),
 'struct OemCatalogRef { const OemKeyDef* keys; int count; };',
 ...[...source.matchAll(/static const OemKeyDef \w+\[\] = \{[\s\S]*?\n\};/g)].map(m=>m[0]),
 'template<size_t N>\n'+block('static OemCatalogRef oemCatalogOf('),
 block('static OemCatalogRef oemCatalogForFamily('),block('static bool oemKeyCaptureImplemented('),block('static int oemChordProfileIdFor('),
 block('static const std::vector<std::string>& ymccDeviceClassLightingSet('),
 block('static const std::vector<std::string>& ymccDeviceClassSecondColorSet('),
 block('static bool ymccDeviceClassSupportsLighting('),block('static bool ymccDeviceClassSupportsSecondColor('),
 block('static std::vector<std::string> ymccDeviceClassLedEffects('),
 block('static bool nativeKeyboardShortcutMaskBit('),block('static void oemChordEvalRules('));
if(source.includes('oemUnifiedInputBindingFor(')){
 definitions.push(block('static const ymcc::capabilities::oem::InputBinding<OemCatalogRef>& oemUnifiedInputBindingFor('),block('static OemCatalogRef oemBoundCatalogForFamily('));
}
const runs=[...source.matchAll(/\bMI_RUN\("([^"]+)",\s*\w+,\s*YmccFamily::(\w+),\s*"([^"]+)"\)/g)].map(m=>({name:m[1],family:m[2],cls:m[3]}));
const identityCaseCount=runs.length;if(process.argv[4]){const extra=JSON.parse(fs.readFileSync(path.resolve(process.argv[4]),'utf8'));for(const item of extra){if(runs.some(r=>r.name===item.name))throw Error('Duplicate extra case '+item.name);runs.push({...item,caseKind:'reference-capability-query-only',physicalIdentitySelectionVerified:false});}}
if(runs.length<80)throw Error('Unexpected identity-case extraction '+runs.length);

const queryStart=source.indexOf('ipc_on("oem.keys.get"'),queryEnd=source.indexOf('\n    });',queryStart)+8;
if(queryStart<0||queryEnd<=queryStart)throw Error('No actual OEM query');
const query=source.slice(queryStart,queryEnd);
const queryFixture=String.raw`
struct {YmccFamily family;std::string deviceClass;}g_machineIdentity;
bool fixtureLogs=true;json fixtureLog;
bool domainLogEnabled(const char*){return fixtureLogs;}
const char* ymccFamilyName(YmccFamily){return "fake-family-label";}
void appendNativeLifecycleLog(const char*,const json& row){fixtureLog=row;}
`+'\nstatic json fixtureOemQuery(){auto query='+query.replace('ipc_on("oem.keys.get", ','').replace(/\);\s*$/,'')+';return query(json::object());}\n';
const declarations=runs.map(r=>`{${JSON.stringify(r.name)},${JSON.stringify(r.family)},YmccFamily::${r.family},${JSON.stringify(r.cls)}}`).join(',\n');
const cpp=oemHeaders.preamble+'#include '+JSON.stringify(path.join(repo,'native/oem_lighting_operations.h').replaceAll('\\','/'))+'\n#include <Windows.h>\n#include <Xinput.h>\n#include <algorithm>\n#include <cstdint>\n#include <cstddef>\n#include <string>\n#include <vector>\n#include <utility>\n#include <iostream>\n#include "json.hpp"\nusing json=nlohmann::json;\n'+definitions.join('\n\n')+'\n'+queryFixture+String.raw`
struct Model{const char* name;const char* familyName;YmccFamily family;const char* cls;};
static const Model models[]={
`+declarations+String.raw`
};
int main(){
 json all=json::array();unsigned unbound=0;
 for(const auto& model:models){

  g_machineIdentity.family=model.family;g_machineIdentity.deviceClass=model.cls;
  fixtureLogs=true;fixtureLog={};const auto observedQuery=fixtureOemQuery();
  if(fixtureLog.at("schema")!=2 || fixtureLog.at("keys")!=observedQuery.at("keys") ||
     fixtureLog.at("physicalDirectionVerified")!=false || fixtureLog.at("inputStateObserved")!=false ||
     fixtureLog.at("deviceClass")!=model.cls)throw std::runtime_error("OEM catalog receipt mismatch");
  fixtureLogs=false;fixtureLog={};const auto noLogQuery=fixtureOemQuery();
  if(noLogQuery!=observedQuery || !fixtureLog.empty())throw std::runtime_error("OEM query logging gate changed reply");

  const auto cat=oemCatalogForFamily(model.family,model.cls);
  const int profileId=oemChordProfileIdFor(model.family,model.cls);const auto& profile=oemChordProfileFor(profileId);
  json keys=json::array();
  for(int i=0;i<cat.count;++i){const auto& k=cat.keys[i];uint64_t bit=0;const bool bound=nativeKeyboardShortcutMaskBit(k.keyId,bit);if(!bound)++unbound;
   json keyboard=json::array();
   for(int j=0;j<profile.count;++j){const auto& r=profile.rules[j];if(r.bit!=bit)continue;
    uint64_t result=0;int back=0,physical=0;uint32_t base=0,prefix=0;
    oemChordEvalRules(profileId,r.keys,r.mouseChord,true,&result,&back,&physical,&base,&prefix);
    keyboard.push_back({{"keysMask",r.keys},{"mouseChord",r.mouseChord},{"sourceRuleBackIndex",r.backIndex},
      {"canonicalMaskDecimal",std::to_string(result)},{"outputBackBits",back},{"physicalDiagnosticBits",physical},
      {"routeIsCapabilityFixture",true},{"realDeviceRead",false}});
   }
   keys.push_back({{"keyId",k.keyId},{"label",k.label},{"captureImplementationReported",observedQuery.at("keys").at(i).contains("captureImplemented")},{"captureImplemented",observedQuery.at("keys").at(i).value("captureImplemented",json(nullptr))},{"captureRouteReported",observedQuery.at("keys").at(i).contains("captureRoute")},{"captureRoute",observedQuery.at("keys").at(i).value("captureRoute",json(nullptr))},{"backIndex",k.backIndex},{"form",int(k.form)},
     {"clickOnly",k.clickOnly},{"canonicalMaskDecimal",std::to_string(bit)},{"canonicalBound",bound},
     {"keyboardRules",keyboard},{"physicalDirectionVerified",false}});
  }
  all.push_back({{"identityCase",model.name},{"family",model.familyName},{"deviceClass",model.cls},
    {"profileId",profileId},{"keys",keys},{"lightingDeclared",ymccDeviceClassSupportsLighting(model.cls)},
    {"secondColorDeclared",ymccDeviceClassSupportsSecondColor(model.cls)},{"ledEffects",ymccDeviceClassLedEffects(model.cls)},
    {"lightingOperations", {{"brightnessImplemented",ymccDeviceClassSupportsLighting(model.cls) && ymcc::lighting::brightnessImplemented(model.cls)},
      {"presetImplemented",ymcc::lighting::presetImplemented(model.cls)},
      {"presetDeclaredButUnavailable",ymcc::lighting::presetDeclaredButUnavailable(model.cls)}}},
    {"actualCompiledRoute",true},{"hardwareVerified",false}});
 }
 std::cout<<json{{"schema",1},{"modelCases",all.size()},{"unboundKeys",unbound},{"models",all},{"hardwareIo",false},
 {"scope","actual compiled native catalog, profile route, lighting declarations, canonical resolver and keyboard evaluator; no hardware transport or physical-direction acceptance"}}.dump(2)<<'\n';
 return unbound?1:0;
}
`;
fs.writeFileSync(path.join(out,'actual-oem-capability-matrix.cpp'),cpp);
const sha=s=>crypto.createHash('sha256').update(s).digest('hex');
fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify({schema:1,sourcePath,mainSha256:sha(source),oemInputHeaders:oemHeaders.inputs,lightingOperationsHeaderSha256:sha(fs.readFileSync(path.join(repo,'native/oem_lighting_operations.h'))),definitionSha256:sha(definitions.join('\n\n')),fixtureSha256:sha(cpp),modelCases:runs.length,identityCaseCount,capabilityOnlyCaseCount:runs.length-identityCaseCount,cases:runs,hardwareIo:false},null,2));
console.log('Generated exact native capability matrix: '+identityCaseCount+' existing identity cases + '+(runs.length-identityCaseCount)+' reference capability queries; hardware APIs absent.');