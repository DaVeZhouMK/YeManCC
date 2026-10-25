import fs from 'node:fs';
import path from 'node:path';

// Select only observed compiler inputs. Snapshot copies remain archived separately.
// A missing/unknown source or generated include fails closed; nothing is inferred
// from a directory prefix alone when validating a concrete compiler observation.
export function nativeCompilerInputClosure({records, buildLog, snapshotRoot, generatedFiles, requiredSources}) {
 const key=p=>path.resolve(p).toLowerCase(),root=key(snapshotRoot)+path.sep;
 const bySnapshot=new Map();
 for(const row of records){const p=key(row.snapshot);if(bySnapshot.has(p))throw Error('DUPLICATE_SNAPSHOT_RECORD');bySnapshot.set(p,row);}
 const generated=new Set(generatedFiles.map(key));
 const includes=[...new Set(buildLog.split(/\r?\n/).filter(x=>/(?:including file:|包含文件:)/i.test(x)).map(x=>x.match(/[A-Z]:\\.*$/i)?.[0]?.trim()).filter(Boolean).map(key))];
 if(!includes.length)throw Error('COMPILER_INCLUDE_OBSERVATIONS_REQUIRED');
 const inert=key(path.join(snapshotRoot,'native/main.inert.cpp'));
 if(!generated.has(inert)||!includes.includes(inert))throw Error('COMPILED_NATIVE_BODY_NOT_OBSERVED');
 const required=new Set(requiredSources);
 for(const rel of required)if(records.filter(r=>r.path===rel).length!==1)throw Error('REQUIRED_NATIVE_SOURCE_MISSING:'+rel);
 for(const observed of includes){
  if(generated.has(observed))continue;
  if(observed.startsWith(root)){if(!bySnapshot.has(observed))throw Error('UNBOUND_SNAPSHOT_COMPILER_INPUT:'+observed);continue;}
  // System includes are separately pinned by the existing manifest finalizer.
  if(/^[c]:\\/i.test(observed))continue;
  throw Error('UNBOUND_EXTERNAL_COMPILER_INPUT:'+observed);
 }
 const used=new Set(includes),bound=records.filter(r=>required.has(r.path)||used.has(key(r.snapshot)));
 const uncompiled=records.filter(r=>!bound.includes(r));
 return {records:bound,copiedButUncompiled:uncompiled,observedIncludePaths:includes,meaning:'Exact primary/control source plus compiler-observed snapshot includes; copied uncompiled files are archived, not claimed as compiled product input.'};
}
