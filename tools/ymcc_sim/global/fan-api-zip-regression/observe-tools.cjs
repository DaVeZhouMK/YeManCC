/** Passive CJS input observer; never changes CLI or product exports. */
const fs=require('node:fs'),crypto=require('node:crypto'),path=require('node:path');
const destination=process.env.YMCC_FAN_ZIP_TOOL_RECEIPT;
if(!destination||!path.isAbsolute(destination)||fs.existsSync(destination))throw Error('FRESH_TOOL_RECEIPT_REQUIRED');
process.once('exit',code=>{
 const files=Object.keys(require.cache).filter(file=>/[\\/]node_modules[\\/]/i.test(file)).sort().map(file=>({path:fs.realpathSync(file),bytes:fs.statSync(file).size,sha256:crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')}));
 fs.writeFileSync(destination,JSON.stringify({schemaVersion:1,kind:'passive-commonjs-tool-observer',nodeExecutable:fs.realpathSync(process.execPath),nodeVersion:process.version,exitCode:code,files},null,2)+'\n',{flag:'wx'});
});
