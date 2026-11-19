// Only the original startup-return family uses this fixed environment recipe.
// Legacy descriptors keep their old preamble; neither branch runs a test.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
export function startupToolchainPreamble(toolchain){
 if(toolchain.kind===undefined)return calibrationToolchainPreamble(toolchain.vcvars64);
 return ['@echo off',...Object.entries(startupToolchainEnvironment(toolchain)).map(([name,value])=>'set "'+name+'='+value+'"')].join('\r\n')+'\r\n';
}
export function startupToolchainEnvironment(toolchain,inherited={}){
 if(toolchain.kind===undefined)return {...inherited};
 assert.equal(toolchain.kind,'fixed-startup-return-x64-v1','STARTUP_UNKNOWN_TOOLCHAIN_RECIPE');
 const values=['msvcRoot','sdkRoot','sdkVersion','windowsRoot'];for(const name of values)assert(typeof toolchain[name]==='string'&&toolchain[name].length>0&&!/["\0\r\n%]/.test(toolchain[name]),'STARTUP_FIXED_TOOLCHAIN_VALUE_REQUIRED:'+name);
 assert(/^[0-9]+(?:\.[0-9]+){3}$/.test(toolchain.sdkVersion),'STARTUP_FIXED_SDK_VERSION_REQUIRED');
 for(const name of ['msvcRoot','sdkRoot','windowsRoot'])assert(path.isAbsolute(toolchain[name]),'STARTUP_ABSOLUTE_TOOLCHAIN_ROOT_REQUIRED');
 const msvc=path.resolve(toolchain.msvcRoot),sdk=path.resolve(toolchain.sdkRoot),windows=path.resolve(toolchain.windowsRoot),version=toolchain.sdkVersion;
 const directories=[path.join(msvc,'bin/Hostx64/x64'),path.join(windows,'System32'),windows];
 const include=[...['ucrt','shared','um','winrt','cppwinrt'].map(name=>path.join(sdk,'Include',version,name)),path.join(msvc,'include')];
 const libraries=[path.join(sdk,'Lib',version,'ucrt/x64'),path.join(sdk,'Lib',version,'um/x64'),path.join(msvc,'lib/x64')];
 const fixed={PATH:directories.join(';'),VSLANG:'1033',VCToolsInstallDir:msvc+path.sep,WindowsSdkDir:sdk+path.sep,WindowsSDKVersion:version+path.sep,INCLUDE:include.join(';'),LIB:libraries.join(';'),SystemRoot:windows,WINDIR:windows,COMSPEC:path.join(windows,'System32/cmd.exe')};
 const names=new Set(Object.keys(fixed).map(name=>name.toLowerCase())),result={...inherited};for(const name of Object.keys(result))if(names.has(name.toLowerCase()))delete result[name];return Object.assign(result,fixed);
}
export function verifyStartupToolchain(toolchain){
 assert(Array.isArray(toolchain.files)&&toolchain.files.length>0&&toolchain.files.length<=640,'STARTUP_TOOL_MEMBERSHIP');
 const keys=new Set();for(const row of toolchain.files){assert(row&&path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256),'STARTUP_EXACT_TOOL_PIN_REQUIRED');const key=path.resolve(row.path).toLowerCase();assert(!keys.has(key),'STARTUP_DUPLICATE_TOOL_PIN');keys.add(key);const stat=fs.lstatSync(row.path);assert(stat.isFile()&&!stat.isSymbolicLink(),'STARTUP_REGULAR_TOOL_FILE_REQUIRED');assert.equal(sha(fs.readFileSync(row.path)),row.sha256,'STARTUP_TOOL_PIN_CHANGED:'+row.path);}
 if(toolchain.kind===undefined)return {legacy:true,files:toolchain.files.length,directories:0};
 startupToolchainPreamble(toolchain);
 assert(Array.isArray(toolchain.directories)&&toolchain.directories.length>0,'STARTUP_COMPILER_DIRECTORY_INVENTORY_REQUIRED');
 for(const row of toolchain.directories){
  assert(path.isAbsolute(row.path)&&Array.isArray(row.extensions)&&Array.isArray(row.files),'STARTUP_DIRECTORY_DESCRIPTOR_REQUIRED');
  assert.deepEqual(row.extensions,['.dll','.exe'],'STARTUP_COMPILER_INVENTORY_EXTENSIONS_REQUIRED');
  const members=fs.readdirSync(row.path,{withFileTypes:true}).filter(entry=>row.extensions.includes(path.extname(entry.name).toLowerCase()));for(const member of members)assert(member.isFile()&&!member.isSymbolicLink(),'STARTUP_COMPILER_REPARSE_MEMBER_REFUSED');const actual=members.map(entry=>entry.name).sort();
  assert.deepEqual(actual,row.files,'STARTUP_COMPILER_DIRECTORY_CHANGED:'+row.path);
  for(const name of row.files)assert(keys.has(path.resolve(row.path,name).toLowerCase()),'STARTUP_UNPINNED_COMPILER_DIRECTORY_MEMBER:'+name);
 }
 const compiler=path.join(path.resolve(toolchain.msvcRoot),'bin/Hostx64/x64');for(const name of ['cl.exe','link.exe'])assert(keys.has(path.join(compiler,name).toLowerCase()),'STARTUP_FIXED_COMPILER_NOT_PINNED:'+name);
 return {legacy:false,files:toolchain.files.length,directories:toolchain.directories.length,fixedRecipe:true};
}
