# HISTORICAL FIXTURE ONLY: retired from production on 2026-10-19.
# Upgrade unmodified official releases with tools/decky-loader-upgrade.ps1.
"""Reproducible narrow patch of YMCC's pinned Windows Decky archive. Python 3.11 only.
Only two reviewed helpers functions and the pinned frontend router/async boot entries change.
Does not execute the loader, Steam, its backend or imports from the bundled helper module.
"""
import argparse, hashlib, importlib.util, json, marshal, pathlib, struct, sys, types, zlib
BASE_SHA = '1d8e06921ced35b0349e677207953d90e548ea254079150d99ced4d360381824'
COOKIE = struct.Struct('!8sIIII64s')
ENTRY = struct.Struct('!IIIIBc')
MAGIC = b'MEI\014\013\012\013\016'

def digest(data): return hashlib.sha256(data).hexdigest()
def read_archive(data):
    end = data.rfind(MAGIC)
    if end < 0 or end + COOKIE.size != len(data): raise ValueError('Unexpected archive cookie/trailing data')
    cookie = COOKIE.unpack_from(data, end)
    if cookie[4] != 311 or cookie[5].rstrip(b'\0') != b'python311.dll': raise ValueError('Expected pinned Python 3.11 loader')
    start = len(data) - cookie[1]
    offset = start + cookie[2]; limit = offset + cookie[3]; entries = []
    while offset < limit:
        size, pos, packed, unpacked, compressed, kind = ENTRY.unpack_from(data, offset)
        if size < ENTRY.size + 1 or offset + size > limit: raise ValueError('Invalid archive table')
        namebytes = data[offset + ENTRY.size:offset + size]
        name = namebytes.rstrip(b'\0').decode('utf-8')
        entries.append(dict(name=name, namebytes=namebytes, kind=kind, compressed=compressed,
            unpacked=unpacked, data=data[start+pos:start+pos+packed]))
        offset += size
    if offset != limit or len({e['name'] for e in entries}) != len(entries): raise ValueError('Ambiguous archive entries')
    return data[:start], cookie, entries

def read_pyz(data):
    if data[:4] != b'PYZ\0' or data[4:8] != importlib.util.MAGIC_NUMBER: raise ValueError('Unexpected PYZ/Python magic')
    toc = marshal.loads(data[struct.unpack('!I', data[8:12])[0]:])
    pairs = list(toc.items()) if isinstance(toc, dict) else toc
    return toc, [(name, kind, data[pos:pos+size]) for name, (kind, pos, size) in pairs]

def function(code):
    result = [c for c in code.co_consts if isinstance(c, types.CodeType) and c.co_name == 'create_inject_script']
    if len(result) != 1: raise ValueError('Expected exactly one create_inject_script function')
    return result[0]

def generated_script(fn):
    return types.FunctionType(fn, {'get_loader_version': lambda: 'v3.1.10'}, 'create_inject_script')('index.js')

def patch(base, policy, python_policy, router_policy):
    prefix, cookie, entries = read_archive(base)
    matches = [e for e in entries if e['kind'] == b'z']
    if len(matches) != 1: raise ValueError('Expected one PYZ')
    entry = matches[0]; oldpyz = zlib.decompress(entry['data']) if entry['compressed'] else entry['data']
    toc, modules = read_pyz(oldpyz)
    helper = [i for i, (name, _, _) in enumerate(modules) if name == 'decky_loader.helpers']
    if len(helper) != 1: raise ValueError('Expected one pinned helpers module')
    idx = helper[0]; name, kind, packed = modules[idx]
    code = marshal.loads(zlib.decompress(packed)); fn = function(code)
    before = generated_script(fn)
    oldfirst = "try{if (window.deckyHasLoaded){setTimeout(() => SteamClient.Browser.RestartJSContext(), 100)}else{window.deckyHasLoaded = true;(async()=>{try{await import('http://localhost:1337/frontend/"
    oldlast = "')}catch(e){console.error(e)};})();}}catch(e){console.error(e)}"
    if fn.co_consts.count(oldfirst) != 1 or fn.co_consts.count(oldlast) != 1: raise ValueError('Original function does not match reviewed bytecode')
    if policy.count('__ROUTER_POLICY__') != 1: raise ValueError('Expected one router policy slot')
    expanded_policy = policy.replace('__ROUTER_POLICY__', router_policy.strip())
    first, rest = expanded_policy.split('__SCRIPT__'); middle, last = rest.split('__VERSION__')
    if middle != '?v=' or '__SCRIPT__' in last or '__VERSION__' in last: raise ValueError('Invalid policy placeholders')
    newfn = fn.replace(co_consts=tuple(first if c == oldfirst else last if c == oldlast else c for c in fn.co_consts))
    probes = [c for c in code.co_consts if isinstance(c, types.CodeType) and c.co_name == 'get_system_pythonpaths']
    if len(probes) != 1: raise ValueError('Expected one optional Python path probe')
    probe = probes[0]
    if probe.co_consts[6] != ('capture_output', 'user', 'env') or 'creationflags' in probe.co_consts:
        raise ValueError('Original Python probe does not match reviewed bytecode')
    compiled = compile(python_policy, probe.co_filename, 'exec', dont_inherit=True)
    replacements = [c for c in compiled.co_consts if isinstance(c, types.CodeType)]
    if len(replacements) != 1 or replacements[0].co_name != probe.co_name:
        raise ValueError('Python policy must contain exactly the reviewed function')
    newprobe = replacements[0]
    newcode = code.replace(co_consts=tuple(newfn if c is fn else newprobe if c is probe else c for c in code.co_consts))
    # Roundtrip proves all module-level imports, callers and other functions are unchanged.
    if newcode.replace(co_consts=tuple(fn if c is newfn else probe if c is newprobe else c for c in newcode.co_consts)) != code:
        raise ValueError('Patch modified code outside the two reviewed functions')
    modules[idx] = name, kind, zlib.compress(marshal.dumps(newcode), 9)
    body = bytearray(oldpyz[:12]); newtoc = []
    for modname, modkind, payload in modules:
        newtoc.append((modname, (modkind, len(body), len(payload)))); body.extend(payload)
    body[8:12] = struct.pack('!I', len(body)); body.extend(marshal.dumps(dict(newtoc) if isinstance(toc, dict) else newtoc))
    entry['data'] = zlib.compress(body, 9) if entry['compressed'] else bytes(body); entry['unpacked'] = len(body)
    routers = [e for e in entries if e['name'] == r'decky_loader\static\chunk-BFRQAfGY.js']
    if len(routers) != 1: raise ValueError('Expected the pinned frontend router')
    router = routers[0]
    original_router = zlib.decompress(router['data']) if router['compressed'] else router['data']
    if digest(original_router) != '3134d43c6adc7720f262b290f18897dc1f4c40330374de81fd59ed3c6abe4261':
        raise ValueError('Pinned frontend router source mismatch')
    anchor = '\nexport { PyError, WSRouter };'
    adapter = '\nconst ymccAttachRecovery = ' + router_policy.strip() + ';\n' + \
        'const ymccOriginalConnect = WSRouter.prototype.connect;\n' + \
        'WSRouter.prototype.connect = function () { return ymccAttachRecovery(this, ymccOriginalConnect) ? this.connect() : ymccOriginalConnect.call(this); };\n' + \
        "Object.defineProperty(WSRouter.prototype.connect, '__YMCC_DECKY_ORIGINAL_CONNECT__', { value: ymccOriginalConnect });\n"
    source_router = original_router.decode('utf-8')
    if source_router.count(anchor) != 1: raise ValueError('Router export anchor mismatch')
    patched_router = source_router.replace(anchor, adapter + anchor).encode('utf-8')
    router['data'] = zlib.compress(patched_router, 9) if router['compressed'] else patched_router
    router['unpacked'] = len(patched_router)
    boot = next(e for e in entries if e['name'] == r'decky_loader\static\index.js')
    original_boot = zlib.decompress(boot['data']) if boot['compressed'] else boot['data']
    if digest(original_boot) != '9ca1a1b91346135099b871a97b066e8808706a5cafe14aef5ced8ff41b19c7dd':
        raise ValueError('Pinned async boot entry source mismatch')
    boot_source = original_boot.decode('utf-8')
    replacements = [
        ('(async () => {', 'window.__YMCC_DECKY_BOOT_PROMISE__ = (async () => {'),
        ('setTimeout(r, 0)', 'setTimeout(r, 250)'),
        ("await fetch('http://127.0.0.1:1337/auth/token').then((r) => r.text())", 'await window.__YMCC_DECKY_INJECTION_POLICY__.authenticate()'),
        ('window.DeckyBackend = new ', 'window.DeckyBackend ??= new '),
    ]
    for old, new in replacements:
        if boot_source.count(old) != 1: raise ValueError('Pinned async boot anchor mismatch: ' + old)
        boot_source = boot_source.replace(old, new, 1)
    patched_boot = boot_source.encode('utf-8')
    boot['data'] = zlib.compress(patched_boot, 9) if boot['compressed'] else patched_boot
    boot['unpacked'] = len(patched_boot)
    payload = bytearray(); table = bytearray()
    for e in entries:
        table.extend(ENTRY.pack(ENTRY.size + len(e['namebytes']), len(payload), len(e['data']), e['unpacked'], e['compressed'], e['kind']))
        table.extend(e['namebytes']); payload.extend(e['data'])
    result = prefix + payload + table + COOKIE.pack(cookie[0], len(payload)+len(table)+COOKIE.size, len(payload), len(table), cookie[4], cookie[5])
    _, _, fresh = read_archive(result)
    for a, b in zip(entries, fresh):
        if a['name'] != b['name'] or a['data'] != b['data']: raise ValueError('Archive readback mismatch')
    freshpyz = next(e for e in fresh if e['kind'] == b'z')['data']
    if entry['compressed']: freshpyz = zlib.decompress(freshpyz)
    _, freshmods = read_pyz(freshpyz)
    freshhelper = next(p for n, _, p in freshmods if n == 'decky_loader.helpers')
    after = generated_script(function(marshal.loads(zlib.decompress(freshhelper))))
    if 'RestartJSContext' in after or 'RestartJSContext' not in before: raise ValueError('Restart removal verification failed')
    return result, before, after, dict(python=311, archiveEntries=len(entries), modules=len(modules), changedModules=['decky_loader.helpers'], changedFunctions=['create_inject_script','get_system_pythonpaths'], changedConstants=2, changedStaticEntries=[router['name'],boot['name']], originalBootSha256=digest(original_boot), patchedBootSha256=digest(patched_boot), routerPolicySha256=digest(router_policy.encode()), originalRouterSha256=digest(original_router), patchedRouterSha256=digest(patched_router), pythonPolicySha256=digest(python_policy.encode()), policySha256=digest(policy.encode()), baseSha256=digest(base), patchedSha256=digest(result), originalScriptSha256=digest(before.encode()), patchedScriptSha256=digest(after.encode()))

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--base',required=True); ap.add_argument('--policy',required=True); ap.add_argument('--python-policy',required=True); ap.add_argument('--router-policy',required=True); ap.add_argument('--out',required=True); ap.add_argument('--report',required=True); args=ap.parse_args()
    if sys.version_info[:2] != (3,11): raise ValueError('Use isolated Python 3.11 to preserve marshal format')
    base=pathlib.Path(args.base).read_bytes()
    if digest(base) != BASE_SHA: raise ValueError('Pinned base hash mismatch')
    policy=pathlib.Path(args.policy).read_text(encoding='utf-8-sig')
    python_policy=pathlib.Path(args.python_policy).read_text(encoding='utf-8-sig')
    router_policy=pathlib.Path(args.router_policy).read_text(encoding='utf-8-sig')
    patched,before,after,report=patch(base,policy,python_policy,router_policy)
    report['normalizedPolicySha256']=report['policySha256']
    report['policySha256']=digest(pathlib.Path(args.policy).read_bytes())
    report['normalizedPythonPolicySha256']=report['pythonPolicySha256']
    report['pythonPolicySha256']=digest(pathlib.Path(args.python_policy).read_bytes())
    report['normalizedRouterPolicySha256']=report['routerPolicySha256']
    report['routerPolicySha256']=digest(pathlib.Path(args.router_policy).read_bytes())
    out=pathlib.Path(args.out); out.parent.mkdir(parents=True,exist_ok=True); out.write_bytes(patched)
    reportpath=pathlib.Path(args.report); reportpath.parent.mkdir(parents=True,exist_ok=True)
    _, _, readback_entries = read_archive(patched)
    readback_router = next(e for e in readback_entries if e['name'] == r'decky_loader\static\chunk-BFRQAfGY.js')
    readback_router_data = zlib.decompress(readback_router['data']) if readback_router['compressed'] else readback_router['data']
    reportpath.with_name('patched-router.js').write_bytes(readback_router_data)
    readback_boot = next(e for e in readback_entries if e['name'] == r'decky_loader\static\index.js')
    reportpath.with_name('patched-boot.js').write_bytes(zlib.decompress(readback_boot['data']) if readback_boot['compressed'] else readback_boot['data'])
    reportpath.with_name('original-injection.js').write_text(before,encoding='utf-8')
    reportpath.with_name('patched-injection.js').write_text(after,encoding='utf-8')
    reportpath.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8'); print(json.dumps(report))
if __name__=='__main__': raise SystemExit('RETIRED: Loader forks are no longer published. Use tools/decky-loader-upgrade.ps1; archive helpers remain fixture-only.')
