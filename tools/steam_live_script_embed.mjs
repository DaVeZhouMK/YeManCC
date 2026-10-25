import fs from 'node:fs';
const js = fs.readFileSync('native/steam_live.js', 'utf8').trimEnd();
// MSVC limits an individual literal to ~16 KiB. Preserve exact script bytes
// with adjacent raw literals; never split a UTF-8 code point or add JS whitespace.
if (js.includes(')YMSTEAM"')) throw Error('Steam script collides with raw-literal delimiter');
function literals(source) {
  const chunks=[];let chunk='',bytes=0;
  for(const character of source){const size=Buffer.byteLength(character);if(bytes+size>8192){chunks.push(chunk);chunk='';bytes=0;}chunk+=character;bytes+=size;}
  if(chunk)chunks.push(chunk);
  return chunks.map(c=>'R"YMSTEAM('+c+')YMSTEAM"').join('\n');
}
// Generate the small mode expression from the SAME source. Frequent read-only
// mode requests must not send/parse the sleep, controller and settings bodies.
const prefixEnd=js.indexOf('    const checkAccount =');
const modeStart=js.indexOf('    // UI mode is process-wide');
const modeEnd=js.indexOf('    // Steam sleep captures',modeStart);
const footerStart=js.lastIndexOf('  } catch (error) {');
if(!(prefixEnd>0&&modeStart>prefixEnd&&modeEnd>modeStart&&footerStart>modeEnd))throw Error('Steam mode source boundaries changed');
const mode=js.slice(0,prefixEnd)+js.slice(modeStart,modeEnd)+js.slice(footerStart);
const output='// Generated from steam_live.js; do not edit. Adjacent literals avoid MSVC C2026.\n#pragma once\nnamespace ymcc::steamlive {\ninline constexpr const char kScript[] =\n'+literals(js)+';\ninline constexpr const char kUiModeScript[] =\n'+literals(mode)+';\n}\n';
const file='native/steam_live_script.h';
if(process.argv.includes('--check')){
  if(!fs.existsSync(file)||fs.readFileSync(file,'utf8')!==output)throw Error('Steam live script embed is stale');
  console.log('Steam live script embed OK (full='+Buffer.byteLength(js)+' mode='+Buffer.byteLength(mode)+' bytes)');
}else fs.writeFileSync(file,output);
