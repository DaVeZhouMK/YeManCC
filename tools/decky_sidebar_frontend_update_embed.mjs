import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const source=path.join(root,'native/decky_sidebar_frontend_update.js'),header=path.join(root,'native/decky_sidebar_frontend_update_script.h');
export function frontendUpdateHeader(){const js=fs.readFileSync(source,'utf8').replace(/\r\n/g,'\n');if(js.includes(')YMCCUPDATE"'))throw Error('frontend update delimiter collision');return '#pragma once\n// Generated from decky_sidebar_frontend_update.js; run its embed tool.\nnamespace ymcc::deckymirror {\ninline const char* frontendUpdateScript(){return R"YMCCUPDATE('+js+')YMCCUPDATE";}\n}\n';}
export function verifyFrontendUpdateHeader(){if(!fs.existsSync(header)||fs.readFileSync(header,'utf8').replace(/\r\n/g,'\n')!==frontendUpdateHeader())throw Error('Decky frontend update script/header differ; regenerate before building');}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){if(process.argv.includes('--check'))verifyFrontendUpdateHeader();else fs.writeFileSync(header,frontendUpdateHeader());}
