// Execute the exact final ZIP plugin in an inert browser realm. No Steam,
// network, installed files, business owner or cache is touched.
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import crypto from 'node:crypto';
import assert from 'node:assert/strict';import {execFileSync} from 'node:child_process';import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json')),{transformSync}=require('esbuild');
const args=process.argv.slice(2),zip=args[args.indexOf('--zip')+1],out=args[args.indexOf('--out')+1];
if(!args.includes('--zip')||!args.includes('--out'))throw Error('Use --zip absolute-release-zip --out evidence-json');
const escaped=path.resolve(zip).replaceAll("'","''");
const script="$ErrorActionPreference='Stop';Add-Type -AssemblyName System.IO.Compression.FileSystem;$z=[IO.Compression.ZipFile]::OpenRead('"+escaped+"');try{$e=@($z.Entries|Where-Object {$_.FullName.Replace('\\','/')-eq'PowerControl/decky/plugins/ymcc-sidebar/dist/index.js'});if($e.Count-ne1){throw 'Exactly one final plugin required'};$m=[IO.MemoryStream]::new();$s=$e[0].Open();try{$s.CopyTo($m);[Convert]::ToBase64String($m.ToArray())}finally{$s.Dispose();$m.Dispose()}}finally{$z.Dispose()}";
const bytes=Buffer.from(execFileSync('powershell.exe',['-NoProfile','-NonInteractive','-Command',script],{encoding:'utf8',maxBuffer:1024*1024}).trim(),'base64');
const events=new Map(),sdkCalls=[],module={exports:{}};
const React={createElement:(type,props,...children)=>({type,props:props??{},children}),Fragment:'Fragment'};
const window={addEventListener:(event,callback)=>events.set(event,callback),removeEventListener:event=>events.delete(event),__DECKY_SECRET_INTERNALS_DO_NOT_USE_OR_YOU_WILL_BE_FIRED_deckyLoaderAPIInit:{connect:(version,name)=>{sdkCalls.push({version,name});return {_version:2,useQuickAccessVisible:()=>false};}}};
const fail=()=>{throw Error('Exported icon evaluation must not launch network/timers');};
const code=transformSync(bytes.toString('utf8'),{loader:'js',format:'cjs'}).code;
vm.runInNewContext(code,{module,exports:module.exports,SP_REACT:React,DFL:{PanelSection:'PanelSection',PanelSectionRow:'PanelSectionRow',SliderField:'SliderField',DropdownItem:'DropdownItem',ToggleField:'ToggleField',Router:{}},window,crypto:{randomUUID:()=> 'export-inert'},structuredClone,setTimeout:fail,clearTimeout:()=>{},WebSocket:fail,console},{timeout:3000});
const plugin=module.exports.default(),icon=plugin.icon,tests=[];
assert.equal(icon.type,'svg');assert.equal(icon.props['aria-label'],'YMCC 控制台');assert.equal(icon.props.role,'img');tests.push('Actual exported factory returns native SVG, not a Y text span');
assert.equal(icon.props.viewBox,'0 0 24 24');assert.equal(icon.props.width,'1em');assert.equal(icon.props.height,'1em');assert.equal(icon.props.fill,'currentColor');tests.push('Exported icon retains native viewBox/dimensions/color');
assert.equal(icon.props.style,undefined);assert.equal(icon.props.transform,undefined);assert.equal(icon.props.y,undefined);tests.push('No fixed tab height or font-baseline/translation override exported');
assert.equal(icon.children.length,1);assert.equal(icon.children[0].type,'path');assert.equal(icon.children[0].props.d,'M4.5 4H8L12 10.5L16 4H19.5L13.5 13.25V20H10.5V13.25Z');tests.push('Exact fixed Y path has centered 4..20 vertical and 4.5..19.5 horizontal extents');
assert.equal(plugin.name,'YMCC 控制台');assert.deepEqual(sdkCalls,[{version:2,name:'ymcc-sidebar'}]);plugin.onDismount();assert.equal(events.size,0);tests.push('Final plugin loads and cleans up under inert SDK without any business/network call');
const report={status:'passed',cases:tests.length,results:tests,releaseZip:path.resolve(zip),pluginBytes:bytes.length,pluginSha256:crypto.createHash('sha256').update(bytes).digest('hex'),actualExportedPluginExecuted:true,inertBrowserOnly:true,actualRemoteSteamCssVerified:false,businessCommands:0,installedFilesModified:false,cacheModified:false};
fs.mkdirSync(path.dirname(path.resolve(out)),{recursive:true});fs.writeFileSync(out,JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
