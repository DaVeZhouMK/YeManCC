import fs from 'node:fs';import path from 'node:path';
const root=path.resolve(import.meta.dirname,'..');
const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8'),start=fs.readFileSync(path.join(root,'native/virtual_controller_start_runtime.h'),'utf8');
function slice(text,a,b){const begin=text.indexOf(a),end=text.indexOf(b,begin+a.length);if(begin<0||end<=begin)throw Error('Production startup boundary missing: '+a);return text.slice(begin,end);}
let fixture=fs.readFileSync(path.join(root,'tools/updater_module_startup_fixture.cpp'),'utf8');
fixture=fixture.replace('//__PRODUCTION_START_PREFIX__',slice(start,'static bool inputHostStart() {','    msiControllerVisibilityObserverStart();')+'    ++physicalBoundary;return true;\n}\n');
fixture=fixture.replace('//__PRODUCTION_STARTUP_RESULT__',slice(main,'static void recordStartupInputAttempt(bool succeeded) {','static void observeStartupInputGate() {'));
fixture=fixture.replace('//__PRODUCTION_REPLAY_REQUEST__',slice(main,'static void gamepadDriverSetupRequestRead() {','#include "external_controller_priority_runtime.h"'));
fs.writeFileSync(process.argv[2],fixture);
