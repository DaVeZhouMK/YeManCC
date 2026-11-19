import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {isDeepStrictEqual} from 'node:util';

const PROFILES=[['standalone-release','/O2 /DNDEBUG'],['standalone-checked','/Od /RTC1']];
const SUMMARY=/^EXTERNALPAD_LEDGER_SUMMARY passed=(\d+) total=(\d+) assertions=(\d+) hardwareAccess=false productRun=false filesystem=private-temp-only$/;
const NOTE=/^NOTE symlink fixture unavailable Win32=\d+$/;
const TERMINAL='EXTERNALPAD_LEDGER_PASSED ';
const HEADER='standaloneHeaderCompile=passed firstInclude=true fixtureInterception=false codeRun=false';
const need=(ok,error)=>{if(!ok)throw Error(error);};
const key=value=>path.resolve(value).toLowerCase();
const lines=text=>text.replace(/^\uFEFF/,'').split(/\r?\n/).filter(Boolean);
const event=line=>/^(?:PASS |FAIL |EXTERNALPAD_LEDGER_|NOTE symlink fixture unavailable)/.test(line);
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
function regular(file){
 const info=fs.lstatSync(file);
 need(info.isFile()&&!info.isSymbolicLink()&&info.size>0&&key(fs.realpathSync(file))===key(file),'LEDGER_OUTPUT_NOT_OWNED:'+file);
 return file;
}
function readText(file){return fs.readFileSync(regular(file),'utf8');}
function originalCases(sourceRoot){
 const source=readText(path.join(sourceRoot,'tools/external_controller_priority_ledger_selftest.cpp'));
 const entry=source.slice(source.lastIndexOf('\nint wmain('));
 const names=[...entry.matchAll(/^    run\("([^"\\]+)", \w+\);\r?$/gm)].map(row=>row[1]);
 need(names.length===13&&new Set(names).size===names.length,'LEDGER_SCENARIO_DECLARATION_CHANGED');
 return names;
}
function profileReceipt(directory,profile,names,expectedFailure){
 const file=path.join(directory,profile+'-run.log');
 const raw=lines(readText(file)),terminal=raw.at(-1),match=SUMMARY.exec(terminal);
 need(match&&raw.filter(line=>line.startsWith('EXTERNALPAD_LEDGER_SUMMARY')).length===1,'LEDGER_PROFILE_SUMMARY_MISSING_OR_DUPLICATE:'+profile);
 const seen=new Map();let notes=0,failure=null;
 for(const line of raw.slice(0,-1)){
  if(NOTE.test(line)){notes++;continue;}
  const pass=line.startsWith('PASS '),failed=line.startsWith('FAIL ');
  const name=pass?line.slice(5):failed?names.find(n=>line.startsWith('FAIL '+n+': ')):null;
  need(name&&names.includes(name)&&!seen.has(name),'LEDGER_PROFILE_SCENARIO_MISMATCH:'+profile);
  seen.set(name,pass);
  if(failed){
   const assertion=/^line [1-9]\d*: (.+)$/.exec(line.slice(('FAIL '+name+': ').length));
   need(name===expectedFailure&&failure===null&&assertion?.[1]==='!ep::saveDebt(path, debt, error)','LEDGER_WRONG_SEMANTIC_FAILURE:'+profile);
   failure=line;
  }
 }
 const [,pass,total,assertions]=match.map(Number);
 need(seen.size===names.length&&notes<=1&&pass===[...seen.values()].filter(Boolean).length&&total===seen.size&&Number.isSafeInteger(assertions)&&assertions>=total,'LEDGER_PROFILE_COUNTS_MISMATCH:'+profile);
 need(expectedFailure?seen.get(expectedFailure)===false:failure===null,'LEDGER_EXPECTED_FAILURE_NOT_OBSERVED:'+profile);
 return {raw,terminal,row:{profile,passed:pass,total,assertions,caseNames:names,failedCases:expectedFailure?[expectedFailure]:[],runLog:file,symlinkCaseUnavailable:notes===1}};
}

export function consumeOriginalLedger(stdout,stderr,{exitCode,outputRoot,sourceRoot,expectedFailure=null}={}){
 const negative=expectedFailure!==null;
 const stdoutEvents=lines(stdout).filter(event),stderrEvents=lines(stderr).filter(event);
 const terminals=stdoutEvents.filter(line=>line.startsWith(TERMINAL));
 need(exitCode===(negative?1:0)&&stderrEvents.length===0&&terminals.length===(negative?0:1),'LEDGER_ORIGINAL_TERMINATION_INCOMPLETE');
 need(typeof outputRoot==='string'&&path.isAbsolute(outputRoot)&&typeof sourceRoot==='string'&&path.isAbsolute(sourceRoot),'LEDGER_ORIGINAL_ROOTS_REQUIRED');
 const names=originalCases(sourceRoot);
 need(!negative||names.includes(expectedFailure),'LEDGER_UNKNOWN_FAULT_SCENARIO');
 const directories=fs.readdirSync(outputRoot,{withFileTypes:true}).filter(row=>/^standalone-[a-f0-9]{32}$/i.test(row.name));
 need(directories.length===1&&directories[0].isDirectory()&&!directories[0].isSymbolicLink(),'LEDGER_ORIGINAL_ARTIFACT_ROOT_MISMATCH');
 const directory=path.join(outputRoot,directories[0].name);
 need(key(fs.realpathSync(directory))===key(directory),'LEDGER_REDIRECTED_ARTIFACT_ROOT');
 regular(path.join(directory,'standalone-header.obj'));
 const summaryPath=path.join(directory,'verification-summary.txt');
 const profiles=negative?PROFILES.slice(0,1):PROFILES;
 const receipts=profiles.map(([name])=>profileReceipt(directory,name,names,expectedFailure));
 const expectedEvents=receipts.flatMap(row=>row.raw);
 if(negative){
  need(!fs.existsSync(summaryPath)&&!fs.readdirSync(directory).some(name=>/^standalone-checked[.-]/i.test(name)),'LEDGER_NEGATIVE_HAS_UNSELECTED_PROFILE');
  regular(path.join(directory,'standalone-release.exe'));
 }else{
  const actualPath=terminals[0].slice(TERMINAL.length);
  need(path.isAbsolute(actualPath)&&key(actualPath)===key(summaryPath),'LEDGER_TERMINAL_SUMMARY_PATH_MISMATCH');
  const summary=lines(readText(summaryPath));
  const declarations=summary.filter(line=>line.startsWith('profile='));
  const binaries=summary.filter(line=>line.startsWith('binarySHA256='));
  need(summary.filter(line=>line===HEADER).length===1&&declarations.length===profiles.length&&binaries.length===profiles.length,'LEDGER_PROFILE_MANIFEST_INCOMPLETE');
  const seenBins=new Set();
  for(const [i,[name,flags]] of profiles.entries()){
   const receipt=receipts[i];
   need(declarations.filter(line=>line==='profile='+name+' flags='+flags+' exitCode=0 '+receipt.terminal).length===1,'LEDGER_PROFILE_MANIFEST_MISMATCH:'+name);
   const exe=path.join(directory,name+'.exe');
   const digest=sha(fs.readFileSync(regular(exe)));
   need(!seenBins.has(digest),'LEDGER_PROFILES_SHARE_BINARY');seenBins.add(digest);
   const refs=binaries.map(line=>/^binarySHA256=([0-9a-f]{64}) path=(.+)$/i.exec(line)).filter(row=>row&&path.isAbsolute(row[2])&&key(row[2])===key(exe));
   need(refs.length===1&&refs[0][1].toLowerCase()===digest,'LEDGER_PROFILE_BINARY_MISMATCH:'+name);
   receipt.row.executable={path:exe,sha256:digest};
  }
  expectedEvents.push(terminals[0]);
 }
 need(isDeepStrictEqual(stdoutEvents,expectedEvents),'LEDGER_PROCESS_LOG_DISAGREES_WITH_PROFILES');
 return {rows:receipts.map(value=>value.row),summaryPath:negative?null:summaryPath};
}
