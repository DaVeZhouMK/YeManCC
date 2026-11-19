import fs from 'node:fs';
// Original Host consumers share the shipping process-wide switch. Overrides are
// explicit offline variants; no product process or configuration is edited.
export function readInputHostRuntimePool(projectPath, requested='product') {
 if(!['product','windows','portable'].includes(requested))throw Error('Unknown InputHost runtime pool variant: '+requested);
 const source=fs.readFileSync(projectPath,'utf8');
 const literalSource=source.replace(/<!--[\s\S]*?-->/g,'');
 const values=[...literalSource.matchAll(/<UseWindowsThreadPool>([^<]*)<\/UseWindowsThreadPool>/g)];
 if((literalSource.match(/<UseWindowsThreadPool\b/g)??[]).length!==values.length)throw Error('Conditional/nonliteral runtime pool requires evaluated MSBuild evidence');
 if(values.length>1||values.some(v=>!['true','false'].includes(v[1].trim())))throw Error('InputHost UseWindowsThreadPool must be one literal true/false');
 const product=values.length===1&&values[0][1].trim()==='true';
 const expected=requested==='product'?product:requested==='windows';
 return {projectPath,requested,productWindowsThreadPool:product,expectedWindowsThreadPool:expected,
  projectProperty:'<UseWindowsThreadPool>'+expected+'</UseWindowsThreadPool>'};
}
export function runtimePoolProbeSource(pool) {
 return `using System;using System.IO;using System.Reflection;using System.Runtime.CompilerServices;using System.Text.Json;
internal static class HostFixtureRuntimePool {
 [ModuleInitializer]internal static void Verify(){
  var value=typeof(System.Threading.ThreadPool).GetProperty("UseWindowsThreadPool",BindingFlags.Static|BindingFlags.NonPublic)?.GetValue(null);
  if(value is not bool actual)throw new InvalidOperationException("Actual runtime pool unavailable");
  var expected=${pool.expectedWindowsThreadPool?'true':'false'};
  if(actual!=expected)throw new InvalidOperationException("Actual runtime pool differs from selected source configuration");
  var path=Path.Combine(Environment.CurrentDirectory,"runtime-pool."+Environment.ProcessId+"."+Guid.NewGuid().ToString("N")+".json");
  using var f=new FileStream(path,FileMode.CreateNew,FileAccess.Write,FileShare.Read);
  JsonSerializer.Serialize(f,new{expectedWindowsThreadPool=expected,actualWindowsThreadPool=actual,pid=Environment.ProcessId});f.Flush(true);
 }
}`;
}
export function readRuntimePoolReceipts(directory,pool) {
 const rows=fs.readdirSync(directory).filter(n=>/^runtime-pool\.[0-9]+\.[a-f0-9]{32}\.json$/.test(n)).sort().map(n=>{
  const path=directory+'/'+n;return {path,result:JSON.parse(fs.readFileSync(path,'utf8'))};
 });
 if(!rows.length||rows.some(r=>r.result.expectedWindowsThreadPool!==pool.expectedWindowsThreadPool||r.result.actualWindowsThreadPool!==pool.expectedWindowsThreadPool))throw Error('Missing or incorrect actual runtime pool receipts');
 return rows;
}
