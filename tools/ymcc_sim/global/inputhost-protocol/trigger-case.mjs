import fs from 'node:fs';
import path from 'node:path';
import {MODULE} from './adapter.mjs';
import {need,readJson,writeJson,hash} from './provenance.mjs';
import {prepareTrigger} from './extract-parser.mjs';
export async function executeTrigger({root,source,build,command,project,spec}) {
 const dotnet=path.join(spec.dotnetRoot,'dotnet.exe');let profiles=[],originalReports=[],counterexample;
 const dir=path.join(root,'trigger'),binding=prepareTrigger(source,dir);const files=['Fixture.cs','GeneratedParsers.cs','ActualMetadata.cs','ProtocolEntry.cs','CurrentTriggerFixture.cs'];
 for(const name of ['ProtocolEntry.cs','CurrentTriggerFixture.cs'])fs.writeFileSync(path.join(dir,name),source[MODULE+'/'+name]);
 fs.writeFileSync(path.join(dir,'TriggerFixture.csproj'),project('TriggerFixture',files,'CurrentTriggerProtocolEntry',spec));
 const legacyFailures=[];
 for(const profile of ['Release','Debug']){
  const label='trigger-'+profile.toLowerCase(),dll=build(dir,'TriggerFixture',profile,label),reports=path.join(dir,'reports',profile);fs.mkdirSync(reports,{recursive:true});
  const legacyDir=path.join(dir,'legacy-whole-fixture',profile);fs.mkdirSync(legacyDir,{recursive:true});const legacyReport=path.join(legacyDir,'original-result.json');
  const legacy=command(dotnet,[dll,'--legacy-fixture',legacyReport],dir,label+'-original-legacy-whole',{negativeMarker:'matrix accepted'});
  const legacyBoundary=readJson(path.join(legacyDir,'boundary-report.json'));need(legacyBoundary.nativeAttempts===0&&legacyBoundary.prohibitedAssemblies.length===0,'INPUTHOST_LEGACY_UNSAFE_BOUNDARY');
  legacyFailures.push({profile,actualExitCode:legacy.exitCode,originalWholeFixtureInvoked:true,originalFixtureSha256:binding.fixtureTemplateSha256,reason:'Original matrix expects 6 personas and the obsolete elite profile; actual current wire sender and selector admit exactly three current pairs',rawStdout:path.join(root,label+'-original-legacy-whole.stdout.log'),rawStderr:path.join(root,label+'-original-legacy-whole.stderr.log'),oldFailureReSigned:false});
  const report=path.join(reports,'current-contract-result.json');command(dotnet,[dll,report],dir,label+'-run');const raw=readJson(report),boundary=readJson(path.join(reports,'boundary-report.json'));
  need(raw.pass===true&&raw.groups===7&&raw.productionParserAndHelpers===true&&raw.originalApplicableMethods.length===3&&raw.senderStaticCrosscheck===true&&Number.isSafeInteger(raw.checks)&&raw.checks>0,'INPUTHOST_CURRENT_PROTOCOL_ASSERTIONS_INCOMPLETE');need(boundary.nativeAttempts===0&&boundary.prohibitedAssemblies.length===0,'INPUTHOST_TRIGGER_UNSAFE_BOUNDARY');
  profiles.push({profile,groups:raw.groups,checks:raw.checks,originalApplicableChecks:raw.originalApplicableChecks,currentContractChecks:raw.currentContractChecks});originalReports.push({profile,path:report,sha256:hash(report),meaning:'Current sender/receiver contract plus three original still-applicable semantic methods, not an old full-four-group PASS'});
 }
 writeJson(path.join(root,'legacy-whole-fixture-failures.json'),{schemaVersion:1,status:'ORIGINAL_FULL_FIXTURE_FAIL_PROTOCOL_VERSION_DIFFERENCE_RETAINED',failures:legacyFailures,productionSelectorChanged:false,originalFixtureBytesChanged:false});
 const bad=path.join(root,'trigger-missing-fallback-negative');prepareTrigger(source,bad,{breakCurrent:true});for(const name of ['ProtocolEntry.cs','CurrentTriggerFixture.cs'])fs.writeFileSync(path.join(bad,name),source[MODULE+'/'+name]);
 fs.writeFileSync(path.join(bad,'TriggerNegative.csproj'),project('TriggerNegative',files,'CurrentTriggerProtocolEntry',spec));const dll=build(bad,'TriggerNegative','Release','trigger-counterexample'),report=path.join(bad,'original-result.json');const failure=command(dotnet,[dll,report],bad,'trigger-counterexample-run',{negativeMarker:'candidate restores both canonical triggers'});
 const boundary=readJson(path.join(bad,'boundary-report.json'));need(boundary.nativeAttempts===0&&boundary.prohibitedAssemblies.length===0,'INPUTHOST_NEGATIVE_UNSAFE_BOUNDARY');
 counterexample={kind:'actual-current-parser-fallback-contract-fault',actualParserSha256:binding.actualParserSha256,faultParserSha256:binding.faultParserSha256,originalFixtureBytesUnchanged:true,rejected:true,exitCode:failure.exitCode,rawStdout:path.join(root,'trigger-counterexample-run.stdout.log'),rawStderr:path.join(root,'trigger-counterexample-run.stderr.log'),baselineMeaning:'Current-source copied fault control, never a historical before source',legacyWholeFixtureFailures:path.join(root,'legacy-whole-fixture-failures.json')};writeJson(path.join(bad,'actual-failure-evidence.json'),counterexample);
 return {profiles,originalReports,counterexample};
}
