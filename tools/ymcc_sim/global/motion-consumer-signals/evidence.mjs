import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {CONFIGURATIONS,EXPECTED_MUTATIONS,SOURCE_FINISH_FLAGS,needsCapabilityGrid,validHash,MAX_RAW_REPORT_JSON_BYTES,MAX_COMPILER_INPUT_PATH_CHARS} from './contracts.mjs';
export const hashFile=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export function inside(root,target){const rel=path.relative(root,target);return rel===''||(!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel));}
export function readJson(file){if(fs.statSync(file).size>MAX_RAW_REPORT_JSON_BYTES)throw Error('MOTION_JSON_SIZE_LIMIT');return JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));}
function noReparseAncestors(target){let cursor=path.resolve(target);for(;;){if(fs.existsSync(cursor)&&fs.lstatSync(cursor).isSymbolicLink())throw Error('MOTION_OUTPUT_REPARSE_PATH');const parent=path.dirname(cursor);if(parent===cursor)break;cursor=parent;}}
export function longestCompilerInputPath(out){const longest=EXPECTED_MUTATIONS.reduce((a,b)=>a.length>=b.length?a:b);return path.join(out,'r','mutants',longest,'evidence','hc_legacy_motion_catalog.snapshot.h');}
export function validateOutputRoot(value,repoRoot){
 if(typeof value!=='string'||!path.isAbsolute(value)||/[\0\r\n]/.test(value))throw Error('MOTION_ABSOLUTE_FRESH_OUTPUT_REQUIRED');
 const out=path.resolve(value),repo=fs.realpathSync(repoRoot);
 if(out===path.parse(out).root||inside(repo,out)||inside(path.resolve('C:/SOFT/YeMan'),out)||out.startsWith('\\\\'))throw Error('MOTION_OUTPUT_MUST_BE_OUTSIDE_REPOSITORY');
 if(longestCompilerInputPath(out).length>MAX_COMPILER_INPUT_PATH_CHARS)throw Error('MOTION_COMPILER_OUTPUT_PATH_TOO_LONG');
 noReparseAncestors(out);if(fs.existsSync(out))throw Error('MOTION_FRESH_OUTPUT_REQUIRED');return out;
}
export function ownedFile(root,relative){
 if(typeof relative!=='string'||!relative||path.isAbsolute(relative)||relative.split(/[\\/]/).includes('..'))throw Error('MOTION_EVIDENCE_PATH_INVALID');
 const file=path.resolve(root,relative);if(!inside(path.resolve(root),file))throw Error('MOTION_EVIDENCE_PATH_ESCAPE');
 noReparseAncestors(file);const actual=fs.realpathSync(file);if(!inside(fs.realpathSync(root),actual)||!fs.statSync(actual).isFile())throw Error('MOTION_EVIDENCE_FILE_INVALID');return actual;
}
export function ownedTree(root){const result=[];const visit=directory=>{for(const entry of fs.readdirSync(directory,{withFileTypes:true})){const file=path.join(directory,entry.name);const stat=fs.lstatSync(file);if(stat.isSymbolicLink())throw Error('MOTION_EVIDENCE_REPARSE_PATH');if(stat.isDirectory())visit(file);else if(stat.isFile()){if(result.length>=12000)throw Error('MOTION_EVIDENCE_FILE_LIMIT');result.push(file);}else throw Error('MOTION_EVIDENCE_FILE_INVALID');}};visit(root);return result.sort();}
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
/** Bind reports to the actual original logs, compiled executables and raw inputs. */
export function verifyMotionEvidence(rawRoot,report,expected) {
 if(!validHash(report.oracleSha256)||!validHash(report.generatedCppSha256))throw Error('MOTION_REQUIRED_EVIDENCE_HASH_MISSING');
 const proofs=[];const requireFile=(relative,sha256)=>{const file=ownedFile(rawRoot,relative),actual=hashFile(file);if(sha256!==undefined&&(!validHash(sha256)||actual!==sha256.toLowerCase()))throw Error('MOTION_EVIDENCE_HASH_MISMATCH');proofs.push({path:file,sha256:actual});return file;};
 requireFile('source-bindings.json');const oracleFile=requireFile('expectations.json',report.oracleSha256);if(!same(readJson(oracleFile),expected))throw Error('MOTION_ORACLE_EVIDENCE_DIFFERS');requireFile('motion_consumer.cpp',report.generatedCppSha256);
 const checkConfig=(folder,config,expectedReport,failed=false)=>{
  if(!validHash(config.executableSha256)||!Array.isArray(config.compileCommand)||config.compileCommand[0]!=='cl.exe'||!config.compileCommand.includes(config.name==='Release'?'/O2':'/Od'))throw Error('MOTION_COMPILED_BINARY_IDENTITY_REQUIRED');
  requireFile(path.join(folder,'compile.log'));const exe=requireFile(path.join(folder,'motion_consumer.exe'),config.executableSha256);const runLog=requireFile(path.join(folder,'run.log'));
  let observed;try{observed=JSON.parse(fs.readFileSync(runLog,'utf8').trim());}catch{throw Error('MOTION_EXECUTABLE_LOG_INVALID');}
  if(!same(observed,expectedReport))throw Error('MOTION_EXECUTABLE_LOG_DIFFERS');
  if(failed&&(config.exitCode!==1||observed.status!=='fail'||!observed.failures?.length))throw Error('MOTION_COMPILE_OR_SETUP_IS_NOT_DETECTION');
  return {configuration:config.name,exe,exeSha256:hashFile(exe),log:runLog,logSha256:hashFile(runLog)};
 };
 const positive=report.configurations.map(config=>checkConfig(config.name,config,config.report));const faults=[];
 for(const name of EXPECTED_MUTATIONS){
  const outer=report.mutations.find(row=>row.name===name);if(!outer)throw Error('MOTION_FAULT_MISSING');
  const childRelative=path.join('mutants',name,'evidence'),childRoot=path.join(rawRoot,childRelative),childFile=requireFile(path.join(childRelative,'source-bindings.json'));const child=readJson(childFile);
  if(path.resolve(outer.evidence??'')!==path.resolve(childRoot)||path.resolve(child.out??'')!==path.resolve(childRoot)||path.resolve(child.source?.path??'')!==path.resolve(rawRoot,'mutants',name,'main.mutant.cpp')||!validHash(child.source?.sha256)||!validHash(child.oracleSha256)||!validHash(child.generatedCppSha256))throw Error('MOTION_FAULT_INPUT_OR_OUTPUT_IDENTITY_INVALID');
  if(child.schema!==report.schema||child.mode!=='candidate'||child.status!=='fail'||child.error!=='Semantic failures in candidate'||child.consumerOnlyControl!==!needsCapabilityGrid(name)||!Array.isArray(child.configurations)||child.configurations.length!==2||new Set(child.configurations.map(row=>row.name)).size!==2||!CONFIGURATIONS.every(config=>child.configurations.some(row=>row.name===config)))throw Error('MOTION_FAULT_SETUP_OR_WRONG_GRID');
  if(!child.sourceAtFinish||SOURCE_FINISH_FLAGS.some(flag=>child.sourceAtFinish[flag]!==true)||child.sourceAtFinish.sha256!==child.source?.sha256)throw Error('MOTION_FAULT_SOURCE_REJECTION_IS_NOT_DETECTION');
  requireFile(path.join(childRelative,'expectations.json'),child.oracleSha256);requireFile(path.join(childRelative,'motion_consumer.cpp'),child.generatedCppSha256);
  const configs=[];for(const config of child.configurations){
   const summary=outer.configurations.find(row=>row.name===config.name);if(!summary||config.accepted!==false||config.exitCode!==1||config.report?.status!=='fail'||!config.report.failures?.length||summary.checks!==config.report.checks||summary.failedAssertions!==config.report.failures.length)throw Error('MOTION_FAULT_SUMMARY_DIFFERS');
   if(needsCapabilityGrid(name)&&(config.report.capabilityCoverage?.available!==true||config.report.capabilityCoverage?.skipped===true||!config.report.capabilityCoverage?.executions))throw Error('MOTION_FAULT_GRID_SKIPPED');
   configs.push(checkConfig(path.join(childRelative,config.name),config,config.report,true));
  }
  const inputRelative=path.join('mutants',name,'main.mutant.cpp');requireFile(inputRelative,child.source.sha256);requireFile(path.join('mutants',name,'runner.log'));
  faults.push({name,configurations:configs,actualSemanticDetection:true});
 }
 // Every generated report, log, source, object, PDB and executable remains hash-bound.
 const closure=ownedTree(rawRoot).map(file=>({path:file,sha256:hashFile(file)}));return {positive,faults,closure,requiredFiles:proofs};
}
