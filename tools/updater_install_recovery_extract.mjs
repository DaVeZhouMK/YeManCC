import fs from 'node:fs';import path from 'node:path';
const source=fs.readFileSync('native/main.cpp','utf8');const start=source.indexOf('struct StrictUpdateVersion {'),end=source.indexOf('static std::string readPackagedUpdateVersion',start);if(start<0||end<0)throw Error('guard boundaries missing');
let body=source.slice(start,end);const matches=[...body.matchAll(/ymcc::updaterecovery::read\((liveExe|exe), POWER_CONTROL_DIR\)/g)];if(matches.length!==2)throw Error('journal boundary changed');body=body.replace(/ymcc::updaterecovery::read\((liveExe|exe), POWER_CONTROL_DIR\)/g,'fixtureJournal');
const root=process.argv[2];fs.writeFileSync(path.join(root,'production-admission.h'),`#pragma once
#define APP_VER_STR "0.0.36"
static const std::wstring POWER_CONTROL_DIR=L"D:\\\\fixture\\\\PowerControl";
static nlohmann::json fixtureJournal=nlohmann::json::object();
`+body);
