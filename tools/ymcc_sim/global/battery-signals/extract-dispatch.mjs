/** Emit exact current production C# dispatch/restore bodies into the simulator. */
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const repo=path.resolve(import.meta.dirname,'../../../..');
const [output,receipt]=process.argv.slice(2);if(!output||!receipt)throw Error('BATTERY_DISPATCH_OUTPUTS_REQUIRED');
const sourceFile=path.join(repo,'native/BatteryHost/Program.cs'),bytes=fs.readFileSync(sourceFile),source=bytes.toString('utf8');
const hash=value=>createHash('sha256').update(value).digest('hex');
function unique(needle){const at=source.indexOf(needle);if(at<0||source.indexOf(needle,at+needle.length)>=0)throw Error('BATTERY_DISPATCH_ANCHOR_CHANGED');return at;}
function endBrace(start){let depth=0;for(let i=start;i<source.length;i++){
 if(source.startsWith('//',i)){const n=source.indexOf('\n',i+2);i=n<0?source.length:n;continue;}
 if(source.startsWith('/*',i)){const n=source.indexOf('*/',i+2);if(n<0)throw Error('UNCLOSED_CSHARP_COMMENT');i=n+1;continue;}
 if(source.startsWith('@"',i))throw Error('VERBATIM_BATTERY_DISPATCH_NEEDS_REVIEW');
 if(source[i]==='"'||source[i]==="'"){const quote=source[i++];for(;i<source.length;i++){if(source[i]==='\\'){i++;continue;}if(source[i]===quote)break;}continue;}
 if(source[i]==='{')depth++;else if(source[i]==='}'&&!--depth)return i+1;
 }throw Error('UNCLOSED_BATTERY_DISPATCH_BODY');}
const at=unique('Snapshot result=op switch'),brace=source.indexOf('{',at),end=endBrace(brace);if(source[end]!==';')throw Error('BATTERY_DISPATCH_TERMINATOR_CHANGED');
const statement=source.slice(at,end+1),restoreAt=unique('static Snapshot Restore(Controller controller)'),restore=source.slice(restoreAt,endBrace(source.indexOf('{',restoreAt)));
const generated='using System.Text.Json;\nusing YeManBatteryHost;\ninternal static class ProductionBatteryDispatch\n{\n internal static Snapshot Dispatch(Controller controller,JsonElement root)\n {\n var op=root.GetProperty("op").GetString();\n'+statement+'\n return result;\n }\n '+restore+'\n}\n';
fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,generated);
const own='tools/ymcc_sim/global/battery-signals/extract-dispatch.mjs';
fs.writeFileSync(receipt,JSON.stringify({schemaVersion:1,status:'PASS',kind:'exact-current-battery-dispatch-source',sourceHashes:{'native/BatteryHost/Program.cs':hash(bytes),[own]:hash(fs.readFileSync(path.join(repo,own))),[path.resolve(output)]:hash(generated)},
 fragments:[{name:'dispatch-switch',line:source.slice(0,at).split('\n').length,sha256:hash(statement)},{name:'restore-helper',line:source.slice(0,restoreAt).split('\n').length,sha256:hash(restore)}],bodiesRewritten:false,scope:'source identity; generated bodies execute only when compiled and invoked by the controller fixture'},null,2));
console.log('BATTERY_CURRENT_DISPATCH_EXTRACTED');