import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';

function once(text,from,to){if(text.split(from).length!==2)throw Error('SDK Host fixture anchor: '+from.slice(0,90));return text.replace(from,()=>to);}
export function copySdkWireHeaders(repo,out,freeze){
 const pending=['canonical_frame_fingerprint_memo.h','input_frame_submission.h'],copied=new Set();
 while(pending.length){const name=pending.pop();if(copied.has(name))continue;copied.add(name);
  const data=freeze(path.join(repo,'native',name));fs.writeFileSync(path.join(out,name),data);
  for(const m of data.toString('utf8').matchAll(/#include\s+"([^"/]+\.h)"/g))if(fs.existsSync(path.join(repo,'native',m[1])))pending.push(m[1]);
 }
}
export function composeSdkWireCases(code,control){
 code=once(code,'  candidate::WindowsSensorView firstView;','  candidate::WindowsSensorView firstView;candidate::NativeBusEvidence firstWire;');
 code=once(code,'    check(!ymcc::windows_motion::current(firstView.submission),"sdk-owner-retirement",model+": old input epoch cannot reappear");}',
  '    check(!ymcc::windows_motion::current(firstView.submission),"sdk-owner-retirement",model+": old input epoch cannot reappear");\n    fixture_wire::rejectRetired(firstWire,model+"/sdk-retired-source");fixture_wire::record(firstWire,model+"/sdk-neutral-release",false,true);\n   }');
 code=once(code,'   const auto receipt=candidate::inputCaptureWindowsProcessingReceiptSnapshot();',
  '   fixture_wire::record(frame,model+(pass?"/sdk-restored-offset":"/sdk-initial"),true,false);if(!pass)firstWire=frame;\n   const auto receipt=candidate::inputCaptureWindowsProcessingReceiptSnapshot();');
 code=once(code,'{"model",candidate::g_machineIdentity.deviceClass}',
  '{"model",candidate::inputCaptureMotionDeviceClass()},{"machineClass",candidate::g_machineIdentity.deviceClass},{"originalGyroTimestamp",e.view.parts[0].timestamp100ns},{"originalAccelTimestamp",e.view.parts[1].timestamp100ns},{"originalGyroReceipt",e.view.parts[0].receipt},{"originalAccelReceipt",e.view.parts[1].receipt},{"originalGyroSequence",e.view.parts[0].sequence},{"originalAccelSequence",e.view.parts[1].sequence}');
 code=once(code,'std::ofstream(argv[3])<<report.dump(2);',
  'std::ofstream wireOut(std::filesystem::path(argv[3]).parent_path()/"owner-host-wire.jsonl");for(const auto& row:fixture_wire::rows)wireOut<<row.dump()<<\'\\n\';wireOut.close();\nstd::ofstream(argv[3])<<report.dump(2);');
 if(control==='--wire-calibrated-as-raw')code=once(code,'content.gyroX=e.raw[0];content.gyroY=e.raw[1];content.gyroZ=e.raw[2];','content.gyroX=e.calibrated[0];content.gyroY=e.calibrated[1];content.gyroZ=e.calibrated[2];');
 return code;
}
export function runSdkHostConsumer({repo,ws,out,control,freeze,asynchronousProcessing=false}){
 if(['--drop-readback','--omit-final-source-guard'].includes(control))return null;
 let script=freeze(path.join(repo,'tools/hc_motion_host_wire_selftest.mjs')).toString('utf8');
 script=script.replaceAll("from './fixtures/","from 'file:///"+repo.replaceAll('\\','/')+"/tools/fixtures/");
 script=once(script,"const runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..'),workspace=path.resolve(repo,'../../..');",`const runner=fileURLToPath(import.meta.url),repo=${JSON.stringify(repo)},workspace=${JSON.stringify(ws)};`);
 script=once(script,"const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/HostMotionWire')",`const root=${JSON.stringify(path.join(out,'HostConsumer'))}`);
 script=once(script,"const before=process.argv.includes('--expected-before-failure')",`const before=${control==='--wire-calibrated-as-raw'}`);
 const corpusStart=script.indexOf(' const exp=oracle(workspace,repo),rows=[];'),corpusEnd=script.indexOf(" const headers=['canonical_frame_fingerprint_memo.h'",corpusStart);
 if(corpusStart<0||corpusEnd<corpusStart)throw Error('Host corpus seam');
 script=script.slice(0,corpusStart)+` const realFixture=path.join(${JSON.stringify(out)},'Release/owner-host-wire.jsonl');const input=path.join(out,'owner-input.json');fs.writeFileSync(input,'[]');result.coverage={actualStandardSdkOwnerFrames:true,pipeTransportExecuted:false};\n`+script.slice(corpusEnd);
 const compileStart=script.indexOf("const exe=path.join(dir,'native-wire.exe')"),compileEnd=script.indexOf(" const profile=configuration==='Checked'",compileStart);
 if(compileStart<0||compileEnd<compileStart)throw Error('Host native wire seam');
 script=script.slice(0,compileStart)+`const wireFile=path.join(out,configuration+'-wire.jsonl');fs.copyFileSync(path.join(${JSON.stringify(out)},configuration,'owner-host-wire.jsonl'),wireFile);\n`+script.slice(compileEnd);
 if(script.split("const allowed=new Set(['native-json-host-quaternion'").length!==3)throw Error('Host negative allowlist seam');
 script=script.replaceAll("const allowed=new Set(['native-json-host-quaternion'","const allowed=new Set(['native-json-host-six-axis','native-json-host-quaternion'");
 script=once(script,"if(before&&['Program.cs','ScreenTouchpadReport.cs'].includes(relative))","if(false&&['Program.cs','ScreenTouchpadReport.cs'].includes(relative))");
 script=once(script,"if(result.configurations[0].wireSha256!==result.configurations[1].wireSha256)throw Error('Native release/checked wire mismatch');",`
 const originalReceiptFields=['originalGyroTimestamp','originalAccelTimestamp','originalGyroReceipt','originalAccelReceipt','originalGyroSequence','originalAccelSequence'];
 const asynchronousProcessing=${asynchronousProcessing};
 const perRunFields=asynchronousProcessing?[...originalReceiptFields,'processedCalls','expectedQuaternion','expectedQuaternionWords']:originalReceiptFields;
 const records=result.configurations.map(c=>fs.readFileSync(path.join(out,c.configuration+'-wire.jsonl'),'utf8').trim().split(/\\r?\\n/).filter(Boolean).map(line=>JSON.parse(line)));
 const normalized=records.map(rows=>rows.map(row=>{
  const stable=Object.fromEntries(Object.entries(row).filter(([key])=>!perRunFields.includes(key)));
  if(asynchronousProcessing){const wire=JSON.parse(stable.wire);if(wire.imu&&Object.hasOwn(wire.imu,'quaternion'))wire.imu.quaternion='individually-verified-from-same-native-frame';stable.wire=JSON.stringify(wire);}
  return JSON.stringify(stable);
 }).join('\\n'));
 if(normalized[0]!==normalized[1])throw Error('Native protocol or expected encoding differs between configurations');
 result.crossConfigurationEvidence={rawFileHashes:result.configurations.map(c=>c.wireSha256),normalizedHashes:normalized.map(text=>sha(Buffer.from(text))),excludedOnlyOriginalReceiptFields:originalReceiptFields,additionalPerRunFields:asynchronousProcessing?['processedCalls','expectedQuaternion','expectedQuaternionWords']:[],onlyQuaternionInsideProtocolVaries:asynchronousProcessing,allAxesAndOtherProtocolFieldsEqual:true,perConfigurationActualQuaternionChecksRequired:true,rowCounts:records.map(rows=>rows.length),protocolAndExpectedDataEqual:true};
 `);
 const scriptPath=path.join(out,'host-consumer.mjs');fs.writeFileSync(scriptPath,script);
 const run=spawnSync('node',[scriptPath],{cwd:repo,windowsHide:true,encoding:'utf8',timeout:120000,maxBuffer:8e6});
 fs.writeFileSync(path.join(out,'host-consumer.log'),(run.stdout||'')+(run.stderr||'')+(run.error?.stack||''));
 if(run.status!==0)throw Error('Actual Host consumer failed: '+run.status);
 const hostRoot=path.join(out,'HostConsumer'),hostDir=fs.readdirSync(hostRoot).sort().at(-1),proof=path.join(hostRoot,hostDir,'RESULT.json');
 const result=JSON.parse(fs.readFileSync(proof,'utf8'));if(result.configurations.length!==2||!result.configurations.every(x=>x.accepted))throw Error('Host results incomplete');
 return {proof,result};
}