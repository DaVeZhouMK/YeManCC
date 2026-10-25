// One exact historical expectation JSON, only for an explicitly frozen frame.
// The origin is provenance only and is never read. No source relocation or fallback.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const need=(ok,code)=>{if(!ok)throw Error('R3_FROZEN_EXPECTATION_'+code);};
const same=(a,b)=>typeof a==='string'&&typeof b==='string'&&path.resolve(a).toLowerCase()===path.resolve(b).toLowerCase();
const fileHash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const relative='tools/ymcc_sim/global/controller-coordinator-pipe/r3-current105-expectations.json';
const frame='FROZEN_R63_PRODUCT_WITH_EXPLICIT_OWNED_CORE75_TOOL_OVERLAY';
const liveAliases=new WeakSet();
function exactShape(pin){need(pin&&Object.keys(pin).sort().join('|')==='path|sha256'&&typeof pin.path==='string'&&path.isAbsolute(pin.path)&&!/[\0\r\n]/.test(pin.path)&&/^[a-f0-9]{64}$/.test(pin.sha256),'EXACT_PIN_SHAPE');}
export function resolveFrozenExpectationToolPins({pins,profile,repo,authorityTools}){
 if(!Object.hasOwn(profile,'frozenExpectationToolOrigin'))return {pins:structuredClone(pins),aliases:[],originFileRead:false};
 need(profile.currentPrimaryAccepted===false&&profile.executionFrame===frame,'EXPLICIT_FROZEN_FRAME');
 need(typeof repo==='string'&&path.isAbsolute(repo),'ABSOLUTE_REPOSITORY');
 need(typeof profile.frozenExpectationOriginRepository==='string'&&path.isAbsolute(profile.frozenExpectationOriginRepository)&&!same(profile.frozenExpectationOriginRepository,repo),'ORIGIN_REPOSITORY');
 const original=profile.frozenExpectationToolOrigin,archive=profile.expectations;exactShape(original);exactShape(archive);
 need(same(original.path,path.join(profile.frozenExpectationOriginRepository,relative)),'ONLY_EXPECTATION_ORIGIN');
 need(same(archive.path,path.join(repo,relative))&&!same(original.path,archive.path)&&original.sha256===archive.sha256,'SAME_BYTES_EXACT_ARCHIVE');
 need(Array.isArray(authorityTools)&&Array.isArray(pins),'INPUT_ARRAYS');
 const origins=authorityTools.filter(pin=>same(pin.path,original.path));
 need(origins.length===1&&origins[0].sha256===original.sha256,'HISTORICAL_AUTHORITY_TOOL');
 const hits=pins.filter(pin=>same(pin.path,original.path));
 need(hits.length===1&&hits[0].sha256===original.sha256,'EXACT_ORIGINAL_INPUT');
 for(let at=path.resolve(archive.path);;){need(!fs.lstatSync(at).isSymbolicLink(),'ARCHIVE_NO_REDIRECTION');const next=path.dirname(at);if(next===at)break;at=next;}
 need(fs.statSync(archive.path).isFile()&&same(fs.realpathSync(archive.path),archive.path)&&fileHash(archive.path)===archive.sha256,'ARCHIVE_BYTES');
 const mapped=pins.map(pin=>same(pin.path,original.path)?{...pin,path:archive.path}:structuredClone(pin)),unique=new Map();
 for(const pin of mapped){const key=path.resolve(pin.path).toLowerCase(),prior=unique.get(key);need(!prior||prior.sha256===pin.sha256,'ARCHIVE_PIN_CONFLICT');unique.set(key,pin);}
 // Only validated live aliases can map artifacts. Diagnostic clones are provenance, not authority.
 const alias=Object.freeze({original:Object.freeze(structuredClone(original)),archive:Object.freeze(structuredClone(archive)),scope:'Historical metadata tool only; immutable source/authority records retained.'});
 liveAliases.add(alias);
 return {pins:[...unique.values()],aliases:Object.freeze([alias]),originFileRead:false};
}
export function frozenExpectationArtifactPath(row,aliases){
 const hits=aliases.filter(alias=>same(row.path,alias.original.path));
 if(!hits.length)return row.path;
 need(hits.length===1&&row.sha256===hits[0].original.sha256,'ARTIFACT_IDENTITY');
 need(liveAliases.has(hits[0]),'LIVE_ALIAS_REQUIRED');
 return hits[0].archive.path;
}