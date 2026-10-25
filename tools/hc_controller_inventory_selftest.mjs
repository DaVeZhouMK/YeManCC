import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {controllerInventoryErrors} from './lib/hc_controller_inventory.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2);assert(args.length===2&&args[0]==='--out');
const output=path.resolve(args[1]);assert(!fs.existsSync(output));fs.mkdirSync(output,{recursive:true});
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const contractPath=path.join(repo,'capabilities/CONTROLLER-CONSUMPTION.json');
const contract=read(contractPath),contractSha256=hash(contractPath);
const generation=spawnSync(process.execPath,[path.join(repo,'tools/hc_capability_catalog_generate.mjs'),'--check','--out',path.join(output,'syntax')],{cwd:repo,encoding:'utf8',timeout:120000,maxBuffer:4194304});
fs.writeFileSync(path.join(output,'generation.log'),(generation.stdout??'')+(generation.stderr??''));assert.equal(generation.status,0,generation.stdout+generation.stderr);
const syntax=read(path.join(output,'syntax/controllers-inventory.json'));
assert.deepEqual(controllerInventoryErrors(contract,syntax),[]);
const definitions=[
 ['other-enum-name',value=>value.sourceInventory.files.find(row=>row.file==='Controllers/XInputController.cs').sourceButtonsMentioned.push('A'),'CONTROLLER_SYNTAX_sourceButtonsMentioned:Controllers/XInputController.cs'],
 ['commented-button',value=>value.sourceInventory.files.find(row=>row.file==='Controllers/Steam/NeptuneController.cs').sourceButtonsMentioned.push('LeftStickOuterRing'),'CONTROLLER_SYNTAX_sourceButtonsMentioned:Controllers/Steam/NeptuneController.cs'],
 ['invented-motion',value=>value.sourceInventory.files.find(row=>row.file==='Controllers/MSI/XClawController.cs').literalCapabilities.push('MotionSensor'),'CONTROLLER_SYNTAX_literalCapabilities:Controllers/MSI/XClawController.cs'],
 ['wrong-parent',value=>value.sourceInventory.files.find(row=>row.file==='Controllers/MSI/DClawController.cs').classes[0].bases='XInputController','CONTROLLER_SYNTAX_CLASSES:Controllers/MSI/DClawController.cs'],
 ['missing-source',value=>value.sourceInventory.files.pop(),'CONTROLLER_SYNTAX_FILE_COUNT'],
 ['duplicate-source',value=>value.sourceInventory.files.push(value.sourceInventory.files[0]),'CONTROLLER_SYNTAX_DUPLICATE_FILE'],
 ['wrong-source-hash',value=>value.sourceInventory.files[0].sha256='0'.repeat(64),'CONTROLLER_SYNTAX_SOURCE_HASH:Controllers/Asus/XboxAdaptiveController.cs'],
 ['wrong-class-count',value=>value.sourceInventory.classDeclarationCount++,'CONTROLLER_SYNTAX_CLASS_COUNT'],
 ['dummy-count-as-hardware',value=>value.sourceInventory.dummyFiles=0,'CONTROLLER_SYNTAX_DUMMY_COUNT'],
];
const cases=[{id:'actual-frozen-source',passed:true,errors:[]}];
for(const[id,change,required]of definitions){const candidate=structuredClone(contract);change(candidate);const errors=controllerInventoryErrors(candidate,syntax);assert(errors.includes(required),id+' must be rejected');cases.push({id,passed:true,expectedRejection:true,errors});}
assert.equal(hash(contractPath),contractSha256);
for(const row of syntax.files)assert.equal(hash(path.join(repo,'deps/handheldcompanion-runtime/source',row.file)),row.sha256);
const result={status:'PASS',passed:true,fileCount:syntax.fileCount,classDeclarationCount:syntax.classDeclarationCount,dummyFiles:contract.sourceInventory.dummyFiles,parser:'Roslyn CSharp syntax, comments excluded, exact enum qualifier',cases,contractSha256,sourceInventory:syntax.files.map(({file,sha256})=>({file,sha256})),hardwareIo:false,assemblyLoaded:false,physicalAcceptance:false};
fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify({status:result.status,files:result.fileCount,classes:result.classDeclarationCount,cases:cases.length,negativeCases:definitions.length,hardwareIo:false}));
