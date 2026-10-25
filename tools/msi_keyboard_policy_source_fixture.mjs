// Generates an exact-production-source MSI policy fixture. No Windows input API
// is available in the fixture; platform/producer state is fake memory only.
// Usage: node tools/msi_keyboard_policy_source_fixture.mjs <output-directory>
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
if(!process.argv[2])throw Error('Output directory is required; this only generates an offline fixture.');
const out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});
const source=fs.readFileSync(path.join(root,'native','main.cpp'),'utf8');
function extract(marker){const start=source.indexOf(marker+' {');if(start<0)throw Error('Missing '+marker);const end=source.indexOf('\n}',start);if(end<0)throw Error('Unclosed '+marker);return source.slice(start,end+2);}
const functions=['static uint64_t oemKeyboardConsumeEligibleMask()','static void nativeOemKeyboardConsumePublish()','static uint64_t nativeOemKeyboardActionMask()','static bool nativeLifecycleEventIsBase(const char* event)'].map(extract);
const bitNames=['kOemBitM1','kOemBitClaw','kOemBitR4'];
const constants=bitNames.map(name=>{const line=source.split('\n').find(l=>l.startsWith('static constexpr uint64_t '+name+' ='));if(!line)throw Error('Missing bit constants '+name);return line;}).join('\n');
const prefix=`#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>
#include <iostream>
#include <cstdlib>
#include ${JSON.stringify(path.join(root,'native','msi_rear_protocol.h').replaceAll('\\','/'))}
${constants}
enum class OemChordProfileId:int {Rog=0,Msi=1,Other=3,GpdWin5=4};
std::atomic<int> g_oemBackProfileId{1},g_oemConsumePolicyProfile{1};
std::atomic<uint32_t> g_oemShortcutEdgeEpoch{7},g_oemConsumePolicyEpoch{7};
std::atomic<bool> g_oemHooksWanted{true},g_oemKeyHooksActive{true},g_exitRequested{false},g_shortcutRecordingActive{false},g_gpdBackIfaceReady{true},g_msiNativeRearDemand{false};
std::atomic<uint64_t> g_oemConsumePolicyMask{0},g_oemKeyMaskMsiDinput{0};
struct {bool replayFault=false;}g_oemKeyboardConsume;
struct Rule {bool needsNeutral=false;std::vector<int>desktopKeys;uint64_t requiredMask=0;};
std::vector<Rule>g_nativeKeyboardShortcuts;
bool knownMsi=true,wmiAuthority=false;uint64_t backAuthority=0,producerAuthority=0,producerPhysical=0,mergedKeys=0;
static bool msiClawKnownBoardAuthority(){return knownMsi;}
static bool msiWmiShortcutAuthoritative(){return wmiAuthority;}
static uint64_t msiDinputBackAuthoritativeMask(){return backAuthority;}
static uint64_t oemProducerAuthorityMask(){return producerAuthority;}
static uint64_t oemProducerPhysicalMask(){return producerPhysical;}
static uint64_t oemBackMergedKeyMask(){return mergedKeys;}
`;
const tests=`
static unsigned checks=0;static void check(bool ok,const char* label){++checks;if(!ok){std::cerr<<"FAIL "<<label<<"\\n";std::exit(1);}}
static uint64_t expanded(unsigned mask){const uint64_t bits[]={kOemBitM1,kOemBitM2,kOemBitClaw,kOemBitQs,kOemBitL4,kOemBitR4};uint64_t r=0;for(unsigned i=0;i<6;++i)if(mask&(1u<<i))r|=bits[i];return r;}
static void reset(){g_oemHooksWanted=true;g_oemKeyHooksActive=true;g_exitRequested=false;g_shortcutRecordingActive=false;g_gpdBackIfaceReady=true;g_oemKeyboardConsume.replayFault=false;g_oemShortcutEdgeEpoch=7;g_oemConsumePolicyEpoch=7;g_oemBackProfileId=g_oemConsumePolicyProfile=1;knownMsi=true;wmiAuthority=false;backAuthority=producerAuthority=producerPhysical=mergedKeys=0;g_oemKeyMaskMsiDinput=0;g_nativeKeyboardShortcuts.clear();}
int main(){
 for(const auto* event:{"msi-native-rear-prepare","msi-claw-m12-remap","msi-native-rear-keyboard-edge","msi-wmi-key"})check(nativeLifecycleEventIsBase(event),"production preparation/WMI evidence stays visible with detail logs off");check(!nativeLifecycleEventIsBase("msi-native-m12-prepare-forged")&&!nativeLifecycleEventIsBase(nullptr),"default event whitelist does not accept arbitrary names");
 for(int profile:{0,1,3,4})for(bool known:{false,true})for(bool wmi:{false,true})for(unsigned subset=0;subset<64;++subset)for(bool gpdReady:{false,true}){
  reset();g_oemBackProfileId=g_oemConsumePolicyProfile=profile;knownMsi=known;wmiAuthority=wmi;g_gpdBackIfaceReady=gpdReady;const auto mask=expanded(subset);g_oemConsumePolicyMask=mask;
  uint64_t expected=mask;if(wmi)expected&=~(kOemBitClaw|kOemBitQs);if(profile==0)expected=0;if(profile==4&&!gpdReady)expected&=~(kOemBitL4|kOemBitR4);
  check(oemKeyboardConsumeEligibleMask()==expected,"exact production consumption preserves non-MSI policy");
  for(unsigned producer=0;producer<4;++producer){backAuthority=(producer&1)?kOemBitM1:0;producerAuthority=(producer&2)?kOemBitQs:0;check(oemKeyboardConsumeEligibleMask()==(expected&~backAuthority&~producerAuthority),"independent producer gates stay authoritative");}backAuthority=producerAuthority=0;
  g_nativeKeyboardShortcuts={};for(unsigned i=0;i<6;++i)if(subset&(1u<<i))g_nativeKeyboardShortcuts.push_back({false,{},expanded(1u<<i)});
  g_nativeKeyboardShortcuts.push_back({true,{},kOemBitM1});g_nativeKeyboardShortcuts.push_back({false,{1},kOemBitM2});g_nativeKeyboardShortcuts.push_back({false,{},kOemBitClaw|kOemBitQs});
  nativeOemKeyboardConsumePublish();const auto published=wmi?mask&~(kOemBitClaw|kOemBitQs):mask;
  check(g_oemConsumePolicyMask==published&&g_oemConsumePolicyProfile==profile&&g_oemConsumePolicyEpoch==7&&g_msiNativeRearDemand==(profile==1&&known),"exact publisher scopes keyboard mirrors without pretending WMI interception");
  mergedKeys=mask;const auto actionExpected=wmi?mask&~(kOemBitClaw|kOemBitQs):mask;
  check(nativeOemKeyboardActionMask()==actionExpected,"production action dedup still removes keyboard mirror when WMI owns click");
 }
 for(unsigned gate=0;gate<8;++gate){reset();g_oemConsumePolicyMask=kOemBitClaw|kOemBitQs;switch(gate){case 0:g_oemHooksWanted=false;break;case 1:g_oemKeyHooksActive=false;break;case 2:g_exitRequested=true;break;case 3:g_shortcutRecordingActive=true;break;case 4:g_oemBackProfileId=0;break;case 5:g_oemConsumePolicyProfile=3;break;case 6:g_oemConsumePolicyEpoch=6;break;case 7:g_oemKeyboardConsume.replayFault=true;break;}check(oemKeyboardConsumeEligibleMask()==0,"existing cancel/recording/profile/epoch/fault gate retained");}
 reset();g_nativeKeyboardShortcuts={{false,{},kOemBitClaw},{false,{},kOemBitQs}};nativeOemKeyboardConsumePublish();check(!g_msiNativeRearDemand,"front-key-only rules do not ask for persistent rear remapping");g_nativeKeyboardShortcuts.push_back({true,{},kOemBitM1});nativeOemKeyboardConsumePublish();check(g_msiNativeRearDemand,"unarmed configured rear rule requests source preparation before neutral exists");
 reset();mergedKeys=kOemBitM1|kOemBitM2|kOemBitClaw|kOemBitQs;g_oemKeyMaskMsiDinput=kOemBitM1;producerPhysical=kOemBitM2;producerAuthority=kOemBitQs;check(nativeOemKeyboardActionMask()==kOemBitClaw,"physical and authority action dedup remains intact");
 std::cout<<"MSI_KEYBOARD_POLICY_ACTUAL_SOURCE_OK checks="<<checks<<" platform_state=fake hardware_io=none official_wmi_suppression=not_verified\\n";
}
`;
const sha=s=>crypto.createHash('sha256').update(s).digest('hex');
fs.writeFileSync(path.join(out,'actual-keyboard-policy-selftest.cpp'),prefix+'\n'+functions.join('\n\n')+tests);
fs.writeFileSync(path.join(out,'keyboard-policy-source-provenance.json'),JSON.stringify({schema:1,mainSha256:sha(source),functions:functions.map((body,i)=>({name:['oemKeyboardConsumeEligibleMask','nativeOemKeyboardConsumePublish','nativeOemKeyboardActionMask','nativeLifecycleEventIsBase'][i],sha256:sha(body)})),hardwareIo:false,officialWmiSuppressionVerified:false},null,2));
console.log('Exact production keyboard consume/publish/action-mask fixture generated; no executable run.');