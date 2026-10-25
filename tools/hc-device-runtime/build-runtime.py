"""Build a hash-pinned YMCC derivative without mutating frozen upstream inputs."""
from pathlib import Path
import argparse, hashlib, json

ap = argparse.ArgumentParser()
ap.add_argument('--output', required=True)
ap.add_argument('--check', action='store_true')
a = ap.parse_args()
repo = Path(__file__).resolve().parents[2]
profile_path = repo / 'deps/ymcc-device-core-runtime/profile.json'
profile = json.loads(profile_path.read_text(encoding='utf-8'))
def digest(b): return hashlib.sha256(b).hexdigest()
# Declared replacements are governed by the profile's source, size and SHA-256 pins.
# Unlisted files retain the frozen upstream identity; DLL names do not restrict derivatives.
source = repo / profile['sourceDirectory']
source_manifest_bytes = (source / 'HandheldCompanion.runtime.json').read_bytes()
if digest(source_manifest_bytes) != profile['sourceManifestSha256']: raise RuntimeError('Unreviewed upstream manifest')
original = json.loads(source_manifest_bytes.decode('utf-8-sig'))
if original['runtimeId'] != profile['sourceRuntimeId']: raise RuntimeError('Upstream identity mismatch')
replacements = {x['path']: x for x in profile['replacements']}
excluded = {x['path'] for x in profile['exclusions']}
contents = {}
original_total = 0
for item in original['files']:
    rel = item['path'].replace('\\', '/')
    p = Path(rel)
    if p.is_absolute() or '..' in p.parts: raise RuntimeError('Unsafe manifest entry')
    b = (source / rel).read_bytes()
    if digest(b).lower() != item['sha256'].lower(): raise RuntimeError('Upstream input changed: ' + rel)
    original_total += len(b)
    if rel in excluded: continue
    if rel in replacements:
        entry = replacements[rel]
        b = (repo / entry['generatedSource']).read_bytes()
        if digest(b) != entry['sha256'] or len(b) != entry['bytes']: raise RuntimeError('Generated core changed: ' + rel)
    contents[rel] = b
if not replacements.keys() <= contents.keys(): raise RuntimeError('Replacement does not name a real runtime entry')
# External shared copies are a declared dependency, never a missing-file exception.
shared = profile.get('sharedDependencies')
if shared:
    if shared.get('directory') != 'PowerControl/ymcc-shared-runtime': raise RuntimeError('Unexpected shared device directory')
    shared_root = repo / 'deps/ymcc-shared-runtime'
    shared_bytes = (shared_root / 'manifest.json').read_bytes()
    if digest(shared_bytes) != shared['manifestSha256']: raise RuntimeError('Shared device source manifest changed')
    shared_source = json.loads(shared_bytes.decode('utf-8-sig'))
    records = {x['packagedPath']: x for x in shared_source['files'] if x.get('packagedDirectory', shared_source['packagedDirectory']) == shared_source['packagedDirectory']}
    upstream = {x['path']: x['sha256'] for x in original['files']}
    seen = set()
    for item in shared['files']:
        rel = item['path']
        if rel != Path(rel).name or rel in seen or rel not in excluded or rel in contents: raise RuntimeError('Invalid shared device contract: ' + rel)
        seen.add(rel)
        record = records.get(rel)
        if record is None or record['path'] != 'bin/' + rel or item['sha256'] != record['sha256'] or item['bytes'] != record['bytes'] or upstream.get(rel) != item['sha256']: raise RuntimeError('Shared device identity differs from frozen source: ' + rel)
        payload = (shared_root / record['path']).read_bytes()
        if digest(payload) != item['sha256'] or len(payload) != item['bytes']: raise RuntimeError('Shared device library changed: ' + rel)
    if seen != records.keys(): raise RuntimeError('Incomplete shared device contract')
manifest = dict(original)
manifest.update(runtimeId=profile['runtimeId'], role='YMCC derived device-only runtime; modified output, not upstream HC package',
    upstreamRuntimeId=profile['sourceRuntimeId'], upstreamManifestSha256=profile['sourceManifestSha256'],
    deviceCoreProfileSha256=digest(profile_path.read_bytes()), modifiedByYmcc=True,
    forbiddenEntryPoints=profile['forbiddenEntryPoints'], physicalDeviceVerified=False)
if shared: manifest['sharedDependencies'] = shared
manifest['files'] = [{'path': k, 'sha256': digest(v)} for k,v in sorted(contents.items())]
contents['HandheldCompanion.runtime.json'] = (json.dumps(manifest,ensure_ascii=False,indent=2)+'\n').encode('utf-8')
output = Path(a.output).resolve()
if not a.check:
    if output.exists() and any(output.iterdir()): raise RuntimeError('Refuse to overwrite a nonempty runtime output')
    output.mkdir(parents=True,exist_ok=True)
    for rel,b in contents.items():
        target=output/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(b)
actual={p.relative_to(output).as_posix():digest(p.read_bytes()) for p in output.rglob('*') if p.is_file()}
expected={rel:digest(b) for rel,b in contents.items()}
if actual != expected: raise RuntimeError('Runtime output differs from exact generated closure')
print(json.dumps({'status':'passed','mode':'check' if a.check else 'build','runtimeId':profile['runtimeId'],
    'files':len(contents),'originalFileBytes':original_total,'generatedFileBytes':sum(len(v) for k,v in contents.items() if k!='HandheldCompanion.runtime.json'),
    'replacements':list(replacements),'excluded':sorted(excluded),'physicalDeviceVerified':False}))
