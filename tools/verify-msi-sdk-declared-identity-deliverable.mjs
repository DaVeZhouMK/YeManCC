// Read-only binding verification. Does not compile/run Host, SDK or hardware.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const workspace = path.resolve(repo, '..', '..', '..');
const base = path.join(workspace, 'Build', 'Validation', 'MSI-Controller-R12-20261013');
const out = path.join(base, 'sdk-declared-identity-implementation-20261014');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const checks = [];
const check = (name, passed) => checks.push({name, passed: !!passed});
const binding = (label, file, hash) => check(label + ': ' + file, fs.existsSync(file) && sha(file) === hash.toLowerCase());
const hostDir = path.join(out, 'actual-host-reviewed');
const host = read(path.join(hostDir, 'validated-deliverable.json'));
const hostSources = read(path.join(hostDir, 'frozen-host-source-manifest.json'));
for(const s of hostSources) {binding('Host current source', s.source, s.sha256); binding('Host frozen source', s.frozen, s.sha256);}
binding('Host compiled executable', host.frozenHostBinary, host.hostSha256);
binding('Host compiled managed assembly', path.join(path.dirname(host.frozenHostBinary), 'YeManInputHost.dll'), host.hostDllSha256);
check('Host build successful / compile only', fs.readFileSync(path.join(hostDir,'host-build.log'),'utf8').includes('0 个错误') && !host.hostExecuted && !host.SDKExecuted && !host.realPnP);
const fakeLog = fs.readFileSync(path.join(hostDir, 'fake-tests.log'), 'utf8');
check('Actual extracted Host170 fake test groups', fakeLog.includes('PASS 170 offline test groups'));
check('Native Host852 paired wire checks', fs.readFileSync(path.join(hostDir,'paired-wire-tests.log'),'utf8').includes('PASS 852'));
const extraction = read(path.join(hostDir, 'extracted-source-receipt.json'));
check('All preexisting protected members preserved', extraction.preservation.every(p=>p.unchanged));
binding('Actual Host extracted current source', path.join(repo,'InputHost','Program.cs'), extraction.sourceSha256);
const pureDir = path.join(out,'pure-identity-test');
const pure = read(path.join(pureDir,'validation-summary.json'));
check('Identity44 cases792 checks', pure.passed && pure.caseCount===44 && pure.checkCount===792 && pure.failureCount===0 && pure.sourceStable && pure.reportSourcesMatched);
check('Identity test did not reference/load SDK or live device API', pure.sdkReferences===0 && pure.sdkAssembliesLoaded===0 && pure.realSdkObjectReads===0 && pure.liveDeviceApis===0);
for(const s of pure.sources) binding('Identity test source',s.path,s.sha256);
binding('Identity fixture binary',pure.fixtureAssembly.path,pure.fixtureAssembly.sha256);
const pureReport=read(pure.report);
check('Identity property getter trap calls zero',pureReport.propertyGetterCalls===0);
const releaseDir=path.join(out,'pure-release-regression');
const release=read(path.join(releaseDir,'result.json'));
check('Release helper125 cases1435 checks',release.passed && release.caseCount===125 && release.checkCount===1435 && release.failureCount===0);
const releaseBinding=read(path.join(releaseDir,'source-and-artifact-binding.json'));
for(const s of releaseBinding.inputs)binding('Release regression source',s.path,s.sha256);
binding('Release regression binary',releaseBinding.binary.path,releaseBinding.binary.sha256);
binding('Release regression result',releaseBinding.result.path,releaseBinding.result.sha256);
const review=read(path.join(out,'independent-review','review.json'));
check('Independent8 static checks / no confirmed defect',review.checks.length===8 && review.confirmedDefects.length===0 && review.sourceStableAtWrite);
for(const s of review.inputs)binding('Independent review input',s.path,s.sha256);
const metadata=read(path.join(base,'sdk-primary-identity-diagnostic-20261014','metadata-review.json'));
check('Fixed SDK10 metadata checks pass',metadata.passed && metadata.checks.length===10 && metadata.checks.every(c=>c.passed));
binding('Fixed SDK binary',metadata.binary.path,metadata.binary.sha256);
const modeDir=path.join(base,'mode-report-payload-20261014');
const mode=read(path.join(modeDir,'report.json'));
check('Actual mode constructor6 fake cases',mode.passed && mode.cases.length===6 && mode.cases.every(c=>c.passed) && mode.hardwareCalls===0 && !mode.productExecuted && !mode.actualIoOwnerExecuted);
const modeBinding=read(path.join(modeDir,'source-binding.json'));
for(const s of modeBinding.source)binding('Mode constructor source',s.file,s.sha256);
const previous=read(path.join(base,'pnp-census-gap-20261014','final-verification.json'));
const priorNative=previous.source.find(s=>s.path===path.join(repo,'native','main.cpp'));
check('Native source unchanged from prior compiled receipt',!!priorNative && sha(priorNative.path)===priorNative.sha256);
for(const key of ['native','probe']) {
 const b=previous.nativeObserverPriorCompileBinding[key];binding('Prior unchanged compiled '+key,b.Path,b.Hash);
}
const oldWarnings=fs.readFileSync(path.join(base,'pnp-census-gap-20261014','actual-host-final','host-build.log'),'utf8');
const newWarnings=fs.readFileSync(path.join(hostDir,'host-build.log'),'utf8');
const warningSet=s=>JSON.stringify([...new Set(s.match(/Program.cs\(\d+,\d+\): warning CS\d+/g)??[])].sort());
check('Host warning class and line locations preserved',warningSet(oldWarnings)===warningSet(newWarnings));
for(const s of read(path.join(out,'source-review-manifest.json')))binding('Local review source',s.path,s.sha256);
for(const a of previous.logComparison.archives)binding('Previously compared archive',a.zipPath,a.zipSha256);
const report=read(path.join(out,'report-path.json'));binding('Current written report',report.path,report.sha256);
const passed=checks.every(c=>c.passed);
const final={schemaVersion:1,dateLocal:'2026-10-14',changeVerificationPassed:passed,bindingCheckCount:checks.length,checks,
 scope:'MSI-only optional non-SD SDK constructor declaration diagnostic; no control decision or shared recovery change',
 source:hostSources.map(s=>({path:s.source,sha256:s.sha256})),
 actualHostFakeGroups:170,nativeHostWireChecks:852,pureIdentityCases:44,pureIdentityChecks:792,
 pureReleaseHelperCases:125,pureReleaseHelperChecks:1435,independentStaticReviewChecks:8,metadataChecks:10,modeConstructorCases:6,
 hostBinding:host,nativeSourceUnchanged:true,nativeObserverPriorCompileBinding:previous.nativeObserverPriorCompileBinding,
 knownSdRequestLossAlreadyFixedInMsiGate:true,sdPhysicalAcceptanceComplete:false,
 latestArchive:previous.logComparison.archives.find(a=>a.id==='023942'),report,
 classificationCompleteProven:false,sdkExactOwnedIdentityBundleProven:false,physicalReturnProven:false,
 SteamConsumptionProven:false,nameRootCauseProven:false,crossRebootExitClosureProven:false,
 productExecuted:false,realHostExecuted:false,realSdkObjectFieldsRead:false,liveDeviceOperations:false,
 deployed:false,testPackageCreated:false,goalComplete:false,
 verifier:{path:fileURLToPath(import.meta.url),sha256:sha(fileURLToPath(import.meta.url))}};
fs.writeFileSync(path.join(out,'final-verification.json'),JSON.stringify(final,null,2)+'\n','utf8');
console.log(JSON.stringify({passed,checks:checks.length,failed:checks.filter(c=>!c.passed)},null,2));
process.exitCode=passed?0:1;
