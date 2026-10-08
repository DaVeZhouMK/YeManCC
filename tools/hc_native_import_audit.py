"""Read-only PE normal/delay imports plus literal DLL-name evidence. No native code runs."""
import argparse, hashlib, json, struct
from pathlib import Path

def pe_imports(data):
    def u16(off): return struct.unpack_from('<H', data, off)[0]
    def u32(off): return struct.unpack_from('<I', data, off)[0]
    pe=u32(0x3c)
    if data[pe:pe+4] != b'PE\0\0': raise ValueError('invalid PE signature')
    count=u16(pe+6); opt=pe+24; magic=u16(opt)
    if magic not in (0x10b,0x20b): raise ValueError('unsupported optional header')
    directories=opt+(112 if magic==0x20b else 96)
    image_base=struct.unpack_from('<Q' if magic==0x20b else '<I',data,opt+(24 if magic==0x20b else 28))[0]
    sections=[]; start=opt+u16(pe+20)
    for n in range(count):
        off=start+40*n; sections.append((u32(off+12), max(u32(off+8),u32(off+16)),u32(off+20)))
    def raw(rva):
        if rva < u32(opt+60): return rva
        for va,size,pointer in sections:
            if va<=rva<va+size: return pointer+rva-va
        raise ValueError(f'unmapped RVA {rva:#x}')
    def cstr(rva):
        off=raw(rva);end=data.index(b'\0',off);return data[off:end].decode('ascii')
    imports=[]
    for kind,index,step in [('normal',1,20),('delay',13,32)]:
        number=u32(opt+(108 if magic==0x20b else 92))
        if index>=number: continue
        rva,size=struct.unpack_from('<II',data,directories+index*8)
        if not rva: continue
        off=raw(rva)
        for n in range(min(10000,size//step+1)):
            pos=off+n*step;descriptor=data[pos:pos+step]
            if not any(descriptor):break
            name=u32(pos+(12 if kind=='normal' else 4))
            if kind=='delay' and not u32(pos)&1:name-=image_base
            imports.append({'kind':kind,'module':cstr(name)})
        else: raise ValueError('unterminated import descriptors')
    return imports

def scan(root):
    files=[];errors=[]
    for p in sorted(root.rglob('*')):
        if not p.is_file():continue
        b=p.read_bytes();rel=str(p.relative_to(root)).replace('\\','/')
        literals=[name for name in ['ucrtbased.dll','ucrtbased','libviiper.dll','libviiper','sdl3.dll'] if name.encode() in b.lower() or name.encode('utf-16-le') in b.lower()]
        imports=[]
        if b.startswith(b'MZ'):
            try:imports=pe_imports(b)
            except (ValueError,struct.error,IndexError) as ex:errors.append({'path':rel,'error':str(ex)})
        files.append({'path':rel,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest().upper(),'pe':b.startswith(b'MZ'),'imports':imports,'literalEvidenceNotCallProof':literals})
    return {'schema':'HC_NATIVE_IMPORT_AUDIT_V1','root':str(root),'readOnly':True,'targetCodeExecuted':False,'files':files,'errors':errors,'notProven':['computed-name LoadLibrary paths','device equivalence']}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('root',type=Path);ap.add_argument('output',type=Path);args=ap.parse_args()
    result=scan(args.root.resolve());args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print('native import audit:',len(result['files']),'files,',len(result['errors']),'errors')
    for f in result['files']:
        for imp in f['imports']:
            if 'ucrtbased' in imp['module'].lower():print('UCRTBASED_IMPORT',f['path'],imp)
    return int(bool(result['errors']))
if __name__=='__main__':raise SystemExit(main())
