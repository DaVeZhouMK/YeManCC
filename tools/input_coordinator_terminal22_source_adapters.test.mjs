import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {guardCurrentSteam} from './input_coordinator_current105_source_adapters.mjs';
import {terminal22SteamCompileSpec} from './input_coordinator_terminal22_source_adapters.mjs';
import {readVirtualControllerSource} from './virtual_controller_runtime_source.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const capture=process.env.YMCC_TERMINAL22_SOURCE_ROOT||root;
const original=readVirtualControllerSource(path.join(capture,'native/main.cpp')).source;
const options={sourceRoot:capture,managedInputContract:'current105-terminal22-v1'};
test('explicit terminal22 keeps both original executing entries blocked',()=>{
 const guarded=guardCurrentSteam(original,options);
 assert.equal(guarded.substitutions.length,2);
 assert(guarded.substitutions.every(row=>row.rejectOnly));
 assert(guarded.inert.includes('#define steamOriginalRecoveryStart fixtureNeverCallOriginalSteamRecoveryStart'));
 assert(guarded.inert.includes('static void steamOriginalRecoveryStart(){++forbiddenIo;throw std::logic_error("unexpected-offline-steam-recovery-start");}'));
 assert(guarded.inert.includes('throw std::logic_error("unexpected-offline-steam-recovery-engine")'));
});
test('default historical guard still refuses the newer runtime header',()=>assert.throws(()=>guardCurrentSteam(original)));
test('an explicit legacy label cannot admit the new header',()=>assert.throws(()=>guardCurrentSteam(original,{...options,managedInputContract:'current105-exact21-v1'})));
test('unknown versions cannot borrow the newer compile guard',()=>{
 for(const managedInputContract of ['terminal22','current105-terminal22-v2',null,22])assert.throws(()=>guardCurrentSteam(original,{...options,managedInputContract}),/UNKNOWN_COMPILE_SOURCE_CONTRACT/);
});
test('terminal22 requires the captured absolute source root',()=>{
 for(const sourceRoot of [undefined,'relative-source','',null])assert.throws(()=>terminal22SteamCompileSpec(sourceRoot),/ABSOLUTE_CAPTURE_SOURCE_REQUIRED/);
});
test('missing or repeated original Start include cannot pass the wrapper',()=>{
 const include='#include "steam_original_recovery_runtime.h"';
 assert.throws(()=>guardCurrentSteam(original.replace(include,''),options));
 assert.throws(()=>guardCurrentSteam(original+'\n'+include,options));
});
test('changing the engine bridge is rejected before its replacement',()=>{
 const marker='static json steamOriginalRecoveryEngineHandle(';
 const at=original.indexOf(marker);assert(at>=0);
 const brace=original.indexOf('{',at);assert(brace>at);
 assert.throws(()=>guardCurrentSteam(original.slice(0,brace+1)+' /* unreviewed bridge */ '+original.slice(brace+1),options));
});
test('changed header bytes are refused, even when the guarded entry names still exist',()=>{
 const evidence=path.resolve(root,'../../../Testing/YMCC/Evidence/VC-Current105Terminal22-23');
 const owned=fs.mkdtempSync(path.join(evidence,'guard-header-negative-'));fs.mkdirSync(path.join(owned,'native'));
 for(const name of ['steam_original_recovery_runtime.h','steam_original_recovery_dual_route.h'])fs.copyFileSync(path.join(capture,'native',name),path.join(owned,'native',name));
 fs.appendFileSync(path.join(owned,'native/steam_original_recovery_runtime.h'),'\n// unreviewed header change\n');
 assert.throws(()=>terminal22SteamCompileSpec(owned),/REVIEWED_NATIVE_HEADER_CHANGED/);
});