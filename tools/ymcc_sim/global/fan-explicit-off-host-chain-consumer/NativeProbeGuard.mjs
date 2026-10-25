import fs from 'node:fs';import cp from 'node:child_process';import net from 'node:net';import tls from 'node:tls';import dgram from 'node:dgram';import {syncBuiltinESMExports} from 'node:module';
const denied=[];let beforeExitObserved=false;const deny=kind=>{denied.push(kind);throw Error('NATIVE_POWER_PROBE_FORBIDDEN:'+kind);};
if(typeof globalThis.window!=='undefined'||typeof globalThis.chrome!=='undefined')deny('WEBVIEW');
process.dlopen=()=>deny('NATIVE_ADDON');process.exit=()=>deny('EXPLICIT_EXIT');
for(const key of ['spawn','spawnSync','exec','execSync','execFile','execFileSync','fork'])cp[key]=()=>deny('CHILD_PROCESS');
for(const key of ['writeFile','writeFileSync','appendFile','appendFileSync','truncate','truncateSync','rename','renameSync','unlink','unlinkSync','rm','rmSync','rmdir','rmdirSync','mkdir','mkdirSync','copyFile','copyFileSync','cp','cpSync','symlink','symlinkSync','link','linkSync','createWriteStream'])if(typeof fs[key]==='function')fs[key]=()=>deny('FS_MUTATION');
net.Socket.prototype.connect=()=>deny('NETWORK');net.Server.prototype.listen=()=>deny('LISTEN');tls.connect=()=>deny('TLS');dgram.createSocket=()=>deny('UDP');globalThis.fetch=()=>deny('FETCH');syncBuiltinESMExports();

process.on('beforeExit',()=>{beforeExitObserved=true;if(denied.length)process.exitCode=9;});process.on('exit',exitCode=>{process.stderr.write(JSON.stringify({kind:'native-power-probe-boundary',pid:process.pid,beforeExitObserved,exitCode,denied,windowAbsent:typeof globalThis.window==='undefined',chromeAbsent:typeof globalThis.chrome==='undefined'})+'\n');});
