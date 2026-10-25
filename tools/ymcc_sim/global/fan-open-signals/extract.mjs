import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const sha = s => crypto.createHash('sha256').update(s).digest('hex');
export function block(text, signature) {
 const at=text.indexOf(signature); if(at<0) throw Error('missing '+signature);
 const start=text.indexOf('{',at); let level=0,state='code';
 for(let i=start;i<text.length;i++) { const c=text[i],n=text[i+1];
  if(state==='line'){if(c==='\n')state='code';continue;}
  if(state==='comment'){if(c==='*'&&n==='/'){state='code';i++;}continue;}
  if(state==='string'||state==='char'){if(c==='\\'){i++;continue;}if(c===(state==='string'?'"':"'"))state='code';continue;}
  if(c==='/'&&n==='/'){state='line';i++;continue;} if(c==='/'&&n==='*'){state='comment';i++;continue;}
  if(c==='"'){state='string';continue;}if(c==="'"){state='char';continue;}
  if(c==='{')level++;if(c==='}'&&--level===0)return text.slice(at,i+1);
 } throw Error('unclosed '+signature);
}
export function generate(repo,out,template) {
 const sources={};const methods=[];
 function take(file,sig){const bytes=fs.readFileSync(path.join(repo,file));sources[file]=sha(bytes);const text=block(bytes.toString('utf8'),sig);methods.push({file,signature:sig,sha256:sha(text),source:text});return text;}
 const dev='deps/handheldcompanion-runtime/source/Devices/'; const msi=dev+'MSI/ClawA1M.cs';
 const base=take(dev+'IDevice.cs','public virtual bool Open()');
 const names=['public override bool Open()','protected void GetWMI()','protected void InitOverBoost(bool enabled)',
  'protected bool SwitchMode(GamepadMode gamepadMode)','protected bool SyncToROM()',
  'public override void set_long_limit(int limit)','public override void set_short_limit(int limit)',
  'protected void SetCPUPowerLimit(int iDataBlockIndex, int limit)','public int GetShiftValue()',
  'public void SetShiftValue(int newShiftValue)','public bool IsShiftSupported()',
  'protected virtual int GetShiftModeValue(ShiftType shiftType)',
  'public void SetShiftMode(ShiftModeCalcType calcType, ShiftType shiftType = ShiftType.None)',
  'private byte[] GetM12(bool useM1, bool xinput)',
  'protected enum GamepadMode','protected enum MKeysFunction','public enum CommandType',
  'public enum ShiftType','public enum ShiftModeCalcType'];
 let code=fs.readFileSync(template,'utf8').replace('/*BASE_OPEN*/',base).replace('/*MSI_METHODS*/',names.map(s=>take(msi,s)).join('\n'));
 for(const name of ['ClawA2VM','ClawCG3EM'])code=code.replace('/*'+name+'*/',take(dev+'MSI/'+name+'.cs','public override bool Open()'));
 code=code.replace('/*ClawBZ2EM*/',take(dev+'MSI/ClawBZ2EM.cs','public override void set_short_limit(int limit)'));
 fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'CompleteMsiFixture.g.cs'),code);
 fs.writeFileSync(path.join(out,'fixture-source-manifest.json'),JSON.stringify({kind:'source-derived-complete-parent-open-with-leaf-io',sourceHashes:Object.entries(sources).map(([path,sha256])=>({path,sha256})),methods,generatedSha256:sha(code),hardwareIo:false},null,2));
 return sources;
}

