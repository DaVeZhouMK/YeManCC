// Independent fixed-HC method oracle. Never calls the production plan or codec.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {sha} from '../msi_owned_return_retired_io/source-binding.mjs';
const pins=[
 ['U01','Devices/OneXPlayer/OneXPlayerX1Mini.cs','93feaddb6b0387e110e92265d000d08c9242968939a36f69d1653bab70630121'],
 ['U02','Devices/OneXPlayer/OneXPlayerApex.cs','f16cde1294b01aba99d51542ca9935af25dedf203eafc6638c42338407b74ff8'],
 ['U03','Devices/OneXPlayer/OneXPlayerX1.cs','3b80d932227c3beddbb6b3a4007c57fc19e0fb55df0db54d389ca8834eae3485'],
 ['U04','Devices/IDevice.cs','f608b6f5a165e8b74a48d09d81fa5316c72273c0efd721327b1d2deccf1d5a47'],
];
export function fixedOracle(root){
 const inputs=pins.map(([id,file,expected])=>{const original=path.join(root,file),bytes=fs.readFileSync(original);assert.equal(sha(bytes),expected,id+' fixed source SHA');return {id,file,original,sha256:expected,text:bytes.toString('utf8')};});
 const [mini,apex,x1]=inputs.map(x=>x.text);
 const array=(text,name)=>{const re=new RegExp('\\b'+name+'\\([^\\n]*\\)\\s*=>\\s*\\[([\\s\\S]*?)\\]\\s*;');const m=text.match(re);assert(m,'fixed array '+name);return m[1].replaceAll('preset','0x01').replaceAll('m1KeyCode','0x67').replaceAll('m2KeyCode','0x66').split(',').map(x=>x.trim()).filter(Boolean).map(x=>{assert(/^0x[0-9a-f]+$/i.test(x),'unknown fixed byte '+x);return Number(x);});};
 const configure=text=>{const m=text.match(/(?:virtual |override )async Task ConfigureController\(\)\s*\{([\s\S]*?)\n\s*\}/);assert(m,'fixed ConfigureController');return m[1];};
 const m=configure(mini),a=configure(apex),x=configure(x1);
 assert.match(m,/VibrationCommandId, \[0x01, 0x05, 0x05\]/);assert.equal((m.match(/WriteVendorHidCommand/g)||[]).length,1);assert(!m.includes('BuildRemap'));
 assert.deepEqual([...a.matchAll(/Task.Delay\((\d+)\)/g)].map(v=>+v[1]),[50,50,50]);
 assert.deepEqual([...x.matchAll(/Task.Delay\((\d+)\)/g)].map(v=>+v[1]),[50]);
 assert.match(x1,/Task.Delay\(4000\)/);assert.match(mini,/data\[3\]/);assert.match(x1,/data\[5\]/);
 assert.match(x1,/enabled \? \[0x03\] : \[0x00, 0x01, 0x02\]/);
 // Input ReportID belongs to HidReport independently of Report.Data. The
 // fixed ReadLoop tests the logical payload and its dynamic trailing marker.
 assert.match(x1,/HidReport report = await device\.ReadReportAsync\(\)/);
 assert.match(x1,/byte\[\] data = report\.Data/);
 assert.match(x1,/data\.Length < 14/);
 assert.match(x1,/data\[1\] != FrameMarker \|\| data\[\^2\] != FrameMarker/);
 const step=(cmd,payload,delay)=>({cmd,payload,delay});
 const plans={X1:[step(0xb4,array(x1,'BuildRemapPage1'),50),step(0xb4,array(x1,'BuildRemapPage2'),0)],X1Mini:[step(0xb3,[1,5,5],0)],Apex:[step(0xb4,array(apex,'BuildRemapPage1'),50),step(0xb4,array(apex,'BuildRemapPage2'),50),step(0xb2,array(apex,'BuildRemapPage3'),50),step(0xb2,[0,1,2],0)]};
 for(const plan of Object.values(plans))for(const s of plan)assert(s.payload.length<=59);
 return {schema:1,fixedCommit:'f2e58468d9917189d4be7ccfc242385ce2c3035b',inputs:inputs.map(({text,...pin})=>pin),plans,inputShape:{source:'U03 ReadLoopAsync consumes HidReport.Data',logicalMinimumBytes:14,rawInputPolicy:[15,512],commands:[0xb2,0xb8],reportId:'independent observed value; no output-derived zero restriction',legacyRaw64:true},initialDelayMs:4000,reinitDelayMs:200};
}
