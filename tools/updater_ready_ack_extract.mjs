import fs from 'node:fs';import path from 'node:path';
const ev=process.argv[2];const source=fs.readFileSync('native/main.cpp','utf8'),start=source.indexOf('        bool helperReady = false;'),end=source.indexOf('        if (!helperReady) failInstall(',start);if(start<0||end<0)throw Error('ready ack boundaries');
let body=source.slice(start,end);if(body.split('ymcc::updaterecovery::read(exeStr, pcDir)').length!==2)throw Error('journal IO boundary changed');body=body.replace('ymcc::updaterecovery::read(exeStr, pcDir)','fixtureJournal').replaceAll('GetTickCount64()','fixtureNow()').replaceAll('Sleep(100)','fixturePause(100)');
fs.writeFileSync(path.join(ev,'production-ready-ack.h'),`#pragma once
static unsigned long long fixtureTicks=0;static int fixturePauses=0;
static unsigned long long fixtureNow(){return fixtureTicks;}
static void fixturePause(int n){fixtureTicks+=n;++fixturePauses;}
static bool actualReadyAck(const std::string& operationId){
`+body+'return helperReady;\n}\n');
fs.writeFileSync(path.join(ev,'ready-ack-extraction.json'),JSON.stringify({scope:'Actual native launch acknowledgement body; IO and monotonic clock replaced by explicit test ports',source:'native/main.cpp',startOffset:start,endOffset:end},null,2));
