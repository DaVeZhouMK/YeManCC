// Independent expected aliases/matrices come from the pinned HC inputs, never C++.
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { reviewedResourceOracle } from './reviewed_resources.mjs';
import { generateExpectations, closeBrace } from '../hc_remaining_model_capabilities/expectations.mjs';
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const commit = 'a250b19a4b7e29dac3b383f2d1ac517d1af43e4e';
// Naming is an application contract; vectors and source digests are independently derived.
const slugs = {
  AYANEO3:'ayaneo3', AYANEOAIR2:'ayaneo-air2', AYANEOAIRPro1S:'ayaneo-airpro1s',
  AYANEOAIRPlusNxx:'ayaneo-airplus-nxx', AYANEOFlip11:'ayaneo-flip11',
  OneXPlayerX2:'onexplayer-x2', OneXPlayerX2MiniPro:'onexplayer-x2minipro',
  OneXPlayer3:'onexplayer-3', AYANEONEXT2:'ayanero-next2',
};
export function oracle(workspace, repo) {
  const input = generateExpectations(workspace, path.join(repo,'tools/fixtures/hc_remaining_model_capabilities'));
  if(input.hcCommit!==commit || input.identities.length!==63) throw Error('Fixed HC commit/63 alias contract changed');
  const upstream=path.join(workspace,'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream');
  const manifest=JSON.parse(fs.readFileSync(path.join(upstream,'source-manifest.json')));
  const models=input.models.map(m=>{
    if(m.name==='SteamMachine') {
      if(m.matrices.length!==0) throw Error('SteamMachine unexpectedly has an IMU');
      return {...m, admitted:false};
    }
    if(!slugs[m.name] || m.matrices.length!==2) throw Error(`Unreviewed IMU model ${m.name}`);
    const matrices=m.matrices.map(x=>{
      const bytes=fs.readFileSync(path.join(upstream,'sources/hc',x.source));
      const row=manifest.find(r=>r.project==='hc'&&r.path===x.source), digest=sha(bytes);
      const blob=createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex');
      if(!row || row.commit!==commit || row.sha256.toLowerCase()!==digest || row.gitBlob!==blob) throw Error(`Bad resource pin ${x.source}`);
      const config=JSON.parse(bytes), matrix=config[x.sensor==='gyro'?'GyroMatrix':'AcceleroMatrix'];
      if(JSON.stringify(matrix)!==JSON.stringify(x.matrix)) throw Error('Resource/oracle matrix disagreement');
      const axes=['X','Y','Z'];
      // Deliberately use different gyro/accel inputs, including basis and signed fractions.
      const samples=(x.sensor==='gyro'
        ? [[2,-3,5],[1,0,0],[0,1,0],[0,0,1],[-7.25,2.5,-0.125],[1e-9,-1e-8,1e-7]]
        : [[11,13,-17],[-1,0,0],[0,-1,0],[0,0,-1],[19.5,-23.25,0.0625],[-1e-6,2e-7,3e-8]])
        .map(raw=>{
          const output=[NaN,NaN,NaN];
          for(let i=0;i<3;i++) output[axes.indexOf(matrix.AxisSwap[axes[i]])]=raw[i];
          return {input:raw,output:output.map((v,i)=>v*matrix.Axis[axes[i]])};
        });
      return {...x,id:`hc-a250b19a:${slugs[m.name]}:${x.sensor}:${x.recipe}`,sourceSha256:digest,samples};
    });
    return {...m,admitted:true,selectorId:`hc-a250b19a:model:${m.name}`,matrices};
  });
  if(models.filter(m=>m.admitted).length!==9) throw Error('Expected nine IMU classes including NEXT2');
  const legacy=legacyOracle(workspace,repo),reviewed=reviewedResourceOracle(workspace,repo,legacy);
  for(const selected of reviewed){const r=legacy.recipes.find(r=>r.deviceClass===selected.deviceClass&&r.gyroMatrixId===selected.identityGyroMatrixId&&r.accelMatrixId===selected.identityAccelMatrixId);if(!r)throw Error('Missing reviewed legacy tuple');r.selectedRecipe=selected;}
  return {...input,schema:'hc-model-motion-consumer-oracle-v3',models,legacy,reviewed,motionCapabilityPolicy:capabilityPolicy(),genericFallbackPolicy:genericFallbackPolicy(workspace),
    expectationBoundary:'Pinned HC a250 aliases, inherited JSON, matrices and resource SHA256; application selector spelling is explicit contract. No candidate C++ supplies expected values.'};
}
// Legacy expectations may consume provenance metadata, but re-derive all axes
// and bundle hashes from pinned C# bytes, independently of the generated header.
export function legacyOracle(workspace, repo) {
  const provenancePath=path.join(repo,'tools/fixtures/hc_legacy_motion_catalog/catalog.json');
  const bytes=fs.readFileSync(provenancePath), metadata=JSON.parse(bytes);
  const baselinePath=path.join(repo,'deps/handheldcompanion-runtime/HC-BASELINE.json');
  const indexPath=path.join(repo,'deps/handheldcompanion-runtime/source-files.sha256');
  const baselineSha256='140a0bc4aa580053b8ee074499ce0d4d49d00b1b0244e3e1cc627afbf54c9914';
  const indexSha256='9105b05655c280f99ffffdb214c165955ced5ef3463719d681e7abecf7e904d1';
  if(sha(fs.readFileSync(baselinePath))!==baselineSha256||sha(fs.readFileSync(indexPath))!==indexSha256)throw Error('Pinned legacy HC baseline/index changed');
  const index=new Map(fs.readFileSync(indexPath,'utf8').split(/\r?\n/).filter(Boolean).map(line=>{const m=/^([0-9a-f]{64})\s+(.+)$/i.exec(line);if(!m)throw Error('Bad legacy source index');return [m[2].replaceAll('\\','/'),m[1].toLowerCase()];}));
  const sourceRoot='deps/handheldcompanion-runtime/source/';
  const sourceChecks=new Map();
  function source(relative) {
    if(!relative.startsWith(sourceRoot)||relative.includes('..'))throw Error('Unreviewed bundle source path');
    const contents=fs.readFileSync(path.join(repo,relative)),digest=sha(contents);
    if(index.get(relative.slice(sourceRoot.length))!==digest)throw Error(`Legacy pinned C# mismatch ${relative}`);
    sourceChecks.set(relative,{path:relative,sha256:digest});return contents.toString('utf8');
  }
  const uncomment=t=>t.replace(/@"(?:""|[^"])*"|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\/\/[^\n]*|\/\*[\s\S]*?\*\//g,m=>m.startsWith('/')?m.replace(/[^\r\n]/g,' '):m);
  const braceEnd=(text,open)=>closeBrace(text.replace(/@"(?:""|[^"])*"|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,m=>m.replace(/[^\r\n]/g,' ')),open);
  const axes=['X','Y','Z'];
  function matrixExpression(expression) {
    if(!/^new(?:\s+IMUMatrix)?\s*\(\s*\)/.test(expression.trim()))throw Error('Unsupported C# matrix allocation');
    const result={axis:[1,1,1],swap:[0,1,2]};
    const axis=expression.match(/\bAxis\s*=\s*new\s+Vector3\s*\(([^)]+)\)/);
    if(axis){const parts=axis[1].split(',').map(s=>Number(s.trim().replace(/[fF]$/,'')));if(parts.length!==3||parts.some(v=>v!==1&&v!==-1))throw Error('Unsupported C# axis');result.axis=parts;}
    if(/\bAxisSwap\s*=/.test(expression)) {
      const pairs=[...expression.matchAll(/\{\s*'([XYZ])'\s*,\s*'([XYZ])'\s*\}/g)];
      if(pairs.length!==3||new Set(pairs.map(m=>m[1])).size!==3||new Set(pairs.map(m=>m[2])).size!==3)throw Error('Unsupported C# permutation');
      result.swap=axes.map(key=>axes.indexOf(pairs.find(m=>m[1]===key)[2]));
    }
    return result;
  }
  function classState(item) {
    const code=uncomment(source(item.path)), match=new RegExp(`\\bclass\\s+${item.class}\\b(?:\\s*:\\s*([\\w.]+))?`).exec(code);
    if(!match)throw Error(`Missing pinned class ${item.class}`);
    const open=code.indexOf('{',match.index), body=code.slice(open+1,braceEnd(code,open));
    const ctor=new RegExp(`\\b(?:public|protected)\\s+${item.class}\\s*\\([^)]*\\)[^{;]*\\{`).exec(body);
    const assignments=[];
    if(ctor) {
      const start=body.indexOf('{',ctor.index),text=body.slice(start+1,braceEnd(body,start));
      for(const m of text.matchAll(/\b(GyroMatrix|AcceleroMatrix)\s*=\s*/g)) {
        const at=m.index+m[0].length,head=/^new(?:\s+IMUMatrix)?\s*\(\s*\)/.exec(text.slice(at));
        if(!head)throw Error(`Non-allocation C# matrix assignment ${item.class}`);
        let end=at+head[0].length;while(/\s/.test(text[end]||'')&&end<text.length)end++;
        if(text[end]==='{')end=braceEnd(text,end)+1;
        if(text.slice(end).trimStart()[0]!==';')throw Error('Unsupported matrix initializer tail');
        assignments.push({kind:m[1]==='GyroMatrix'?'gyro':'accel',...matrixExpression(text.slice(at,end))});
      }
    }
    let parent=match[1]?.split('.').at(-1)||null;
    const alias=parent&&new RegExp(`\\busing\\s+${parent}\\s*=\\s*([\\w.]+)\\s*;`).exec(code);
    if(alias)parent=alias[1].split('.').at(-1);
    return {parent,assignments};
  }
  const controllerClasses=new Set(['LegionGoSZ1','LegionGoSZ2','LegionGoTablet','LegionGoTablet2'].map(n=>'HandheldCompanion.Devices.'+n));
  const excluded=['ClawA1M','ClawA2VM','ClawBZ2EM','ClawCG3EM','XboxROGAlly','XboxROGAllyX','SteamMachine',...Object.keys(slugs)].map(n=>'HandheldCompanion.Devices.'+n);
  const recipes=metadata.recipes.map(r=>{
    if(excluded.includes(r.deviceClass))throw Error('Legacy table includes excluded class');
    if(r.controllerPreferred!==controllerClasses.has(r.deviceClass))throw Error('Controller policy metadata disagreement');
    const bundle=metadata.bundles[r.sourceSha256];if(!bundle)throw Error('Missing C# source bundle');
    const paths=bundle.sources.map(s=>s.path);
    if(JSON.stringify(paths)!==JSON.stringify([...new Set(paths)].sort()))throw Error('Noncanonical bundle source list');
    const manifest=paths.map(p=>`${sha(Buffer.from(source(p)))}  ${p}\n`).join('');
    if(manifest!==bundle.manifest||sha(Buffer.from(manifest))!==r.sourceSha256)throw Error(`Independent source bundle SHA mismatch ${r.deviceClass}`);
    const required=['Devices/IDevice.cs','Managers/SensorsManager.cs','Sensors/IMUGyrometer.cs','Sensors/IMUAccelerometer.cs'].map(p=>sourceRoot+p);
    for(const p of required)if(!paths.includes(p))throw Error('Missing bundle semantic support source');
    if(r.sourceScope!=='HC-BINARY-BATCH12-20260831/0.32.3.2/source-declarations'||r.sourcePath!==`tools/fixtures/hc_legacy_motion_catalog/catalog.json#bundles/${r.sourceSha256}`)throw Error('Unexpected legacy provenance scope/path');
    let expectedMatrices;
    for(const alias of r.aliases) {
      const state={gyro:null,accel:null};let previous=null;
      for(const item of alias.inheritance) {
        if(!paths.includes(item.path)||item.sha256!==sha(Buffer.from(source(item.path))))throw Error('Inheritance source missing from bundle');
        const parsed=classState(item);
        if(previous&&parsed.parent!==previous)throw Error(`C# inheritance order differs: ${item.class} parent ${parsed.parent}, previous ${previous}`);
        for(const assignment of parsed.assignments)state[assignment.kind]={axis:assignment.axis,swap:assignment.swap};
        previous=item.class;
      }
      if(previous!==alias.sourceClass||!state.gyro||!state.accel)throw Error('Incomplete independent C# matrix inheritance');
      for(const sensor of ['gyro','accel'])if(JSON.stringify(state[sensor].axis)!==JSON.stringify(alias[sensor].axis)||JSON.stringify(state[sensor].swap)!==JSON.stringify(alias[sensor].swap))throw Error(`Independent C# declaration mismatch ${alias.sourceClass}/${sensor}`);
      if(expectedMatrices&&JSON.stringify(state)!==JSON.stringify(expectedMatrices))throw Error('Collapsed native tuple has inconsistent source axes');
      expectedMatrices=state;
    }
    const samples={gyro:[[2,-3,5],[1,0,0],[0,1,0],[0,0,1],[-7.25,2.5,-.125],[1e-9,-1e-8,1e-7]],accel:[[11,13,-17],[-1,0,0],[0,-1,0],[0,0,-1],[19.5,-23.25,.0625],[-1e-6,2e-7,3e-8]]};
    const matrices=['gyro','accel'].map(sensor=>({sensor,id:r[sensor==='gyro'?'gyroMatrixId':'accelMatrixId'],...expectedMatrices[sensor],samples:samples[sensor].map(input=>{const output=[0,0,0];for(let i=0;i<3;i++)output[expectedMatrices[sensor].swap[i]]=input[i];return {input,output:output.map((v,i)=>v*expectedMatrices[sensor].axis[i])};})}));
    const tuple=JSON.stringify([r.deviceClass,r.gyroMatrixId,r.accelMatrixId]);
    const selector=`hc-legacy-0.32:model:${r.deviceClass.split('.').at(-1)}:${sha(Buffer.from(tuple)).slice(0,16)}`;
    if(selector!==r.selectorId)throw Error('Legacy selector tuple digest mismatch');
    return {...r,matrices,independentlyVerifiedCSharp:true};
  });
  const inputsPath=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/pc80-identity/inputs.json');
  const inputsBytes=fs.readFileSync(inputsPath),inputs=JSON.parse(inputsBytes.toString('utf8').replace(/^\uFEFF/,''));
  const cases=inputs.map(row=>{
    const matching=recipes.filter(r=>r.identityInputs.includes(row.id));if(matching.length>1)throw Error('Identity assigned multiple tuple recipes');
    return {...row,systemVersion:row.version||'',expectedRecipe:matching[0]?.selectorId||null};
  });
  for(const r of recipes)if(!cases.some(c=>c.expectedRecipe===r.selectorId))throw Error('Recipe has no raw identity test case');
  return {schema:'independent-legacy-csharp-consumer-oracle-v1',provenance:{path:provenancePath,sha256:sha(bytes)},baseline:{path:baselinePath,sha256:baselineSha256},sourceIndex:{path:indexPath,sha256:indexSha256},identityInputs:{path:inputsPath,sha256:sha(inputsBytes)},sourceChecks:[...sourceChecks.values()],recipes,cases,controllerClasses:[...controllerClasses],excluded,
    boundary:'C# inheritance, allocation reset, axis/permutation and bundle SHA are independently rebuilt from pinned source. Metadata supplies tuple/identity labels; no generated header supplies expected axes, hashes or admission. Declared C# recipes do not prove HC callback runtime/physical axes.'};
}

// Explicit software/diagnostic contract supplied for this continuation. Presence,
// observation and catalog declarations do not imply physical or output proof.
export function capabilityPolicy() {
  return {
    schema:1,
    controllers:{
      'HandheldCompanion.Devices.LegionGoTablet':true,
      'HandheldCompanion.Devices.LegionGoTablet2':true,
      'HandheldCompanion.Devices.LegionGoSZ1':true,
      'HandheldCompanion.Devices.LegionGoSZ2':true,
      'HandheldCompanion.Devices.SteamDeck':false,
    },
    defaultWindows:[['GPDWin2','GPD','WIN2','Gpd'],['GPDWin3','GPD','G1618-03','Gpd'],['SteamMachine','VALVE','FREMONT','ValveSteamMachine']].map(([name,manufacturer,product,family])=>({deviceClass:'HandheldCompanion.Devices.'+name,manufacturer,product,family})),
    existingWindowsClasses:['ClawA1M','ClawA2VM','ClawBZ2EM','ClawCG3EM','XboxROGAlly','XboxROGAllyX'].map(n=>'HandheldCompanion.Devices.'+n),
    routes:{
      windows:{sourceRoute:'windows-sensors',modelSelectorImplemented:true,inputConsumerImplemented:true,calibrationOwnerImplemented:true,calibrationOwner:'windows-gyro-device-id'},
      controller:{sourceRoute:'controller-specific',modelSelectorImplemented:false,inputConsumerImplemented:false,calibrationOwnerImplemented:false,calibrationOwner:'controller-owner-unimplemented'},
      legionController:{sourceRoute:'controller-specific',modelSelectorImplemented:false,inputConsumerImplemented:true,calibrationOwnerImplemented:true,calibrationOwner:'controller-physical-identity-and-sensor-index'},
      unknown:{sourceRoute:'unclassified',modelSelectorImplemented:false,inputConsumerImplemented:false,calibrationOwnerImplemented:false,calibrationOwner:'unclassified'},
    },
    reasons:{legionController:'controller-route-implemented;fresh-selected-source-and-runtime-owner-still-required',controller:'controller-consumer-and-calibration-owner-not-integrated',unknown:'no-proven-model-motion-route',bound:'model-bound;runtime-pair-still-required',unbound:'awaiting-valid-model-binding'},
    verificationScope:'software-route-and-current-model-binding;not-physical-or-output-acceptance',
    boundary:'Fixed user software capability/receipt contract; no production helper or header supplies these expected flags. Full controller consumer/calibration owner remains unimplemented even when a memory lease is observed.',
  };
}

// Independently frozen continuation contract, created before this simulator repair.
// It distinguishes a generic motion owner from exact machine identification.
export function genericFallbackPolicy(workspace) {
 const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/GenericWindowsMotion-20261018');
 const pins={"ADAPTER-CONTRACT.json": "4127e0fd6544adb6b6b688dcc4a1915b39cb30414dd863c0fad9c146f8ea5365", "SOURCE-SCOPE.json": "7e1e15ae14e2f59a4a1569be7b009fc56393b5bd125f6f20be08af83aa921d18"};
 const inputs=Object.entries(pins).map(([name,digest])=>{const file=path.join(root,name),bytes=fs.readFileSync(file);if(sha(bytes)!==digest)throw Error('Frozen generic motion contract changed: '+name);return {path:file,sha256:digest};});
 const contract=JSON.parse(fs.readFileSync(inputs[0].path)),scope=JSON.parse(fs.readFileSync(inputs[1].path));
 const archive=path.join(root,'NativeBuild-R1/YeManCC-source/YeManCC/native/main.cpp'),archiveBytes=fs.readFileSync(archive);if(sha(archiveBytes)!==scope.mainAfterSha256)throw Error('Frozen generic continuation source changed');const continuationSource={path:archive,sha256:sha(archiveBytes)};inputs.push(continuationSource);
 if(contract.machineIdentityMutable!==false||contract.genericClassUsedOnlyForMotionOwnership!==true||contract.physicalAcceptance!==false||contract.recipes.length!==2)throw Error('Generic source contract scope differs');
 const recipes=contract.recipes.map(row=>{const asus=row.sourceClass==='HandheldCompanion.Devices.ASUS.ASUS';if(!asus&&row.sourceClass!=='HandheldCompanion.Devices.DefaultDevice')throw Error('Unknown generic source contract');const axes=asus?'x-y-z':'x-z-y';if(row.gyro!==axes||row.accel!==axes)throw Error('Generic source axes differ');return {...row,manufacturer:asus?'ASUSTEK COMPUTER INC.':'*',selectorId:'hc-a250b19a:generic:'+(asus?'ASUS':'DefaultDevice'),gyroMatrixId:'hc-a250b19a:generic-'+(asus?'ASUS':'DefaultDevice')+':gyro:'+axes,accelMatrixId:'hc-a250b19a:generic-'+(asus?'ASUS':'DefaultDevice')+':accel:'+axes,expectedAxes:asus?[2,-3,5,11,13,-17]:[2,5,-3,11,-17,13]};});
 return {schema:1,scope:'frozen-generic-source-ownership-contract-not-physical',recipes,inputs,continuationSource,changedMethods:scope.changedMethods,machineIdentityMutable:false,physicalAcceptance:false};
}
