"""Synthetic PE fixtures only. No DLL or product process is loaded."""
import importlib.util, struct, tempfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('audit',Path(__file__).with_name('hc_native_import_audit.py'))
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
def fixture(is64):
    b=bytearray(2048)
    def u16(o,v):struct.pack_into('<H',b,o,v)
    def u32(o,v):struct.pack_into('<I',b,o,v)
    b[:2]=b'MZ';u32(0x3c,0x80);b[0x80:0x84]=b'PE\0\0';u16(0x86,1)
    opt=0x98;size=240 if is64 else 224;u16(0x94,size);u16(opt,0x20b if is64 else 0x10b)
    if is64:struct.pack_into('<Q',b,opt+24,0x140000000)
    else:u32(opt+28,0x400000)
    u32(opt+60,0x200);u32(opt+(108 if is64 else 92),16)
    section=opt+size;u32(section+8,0x600);u32(section+12,0x1000);u32(section+16,0x600);u32(section+20,0x200)
    dirs=opt+(112 if is64 else 96)
    u32(dirs+8,0x1000);u32(dirs+12,40);u32(0x200+12,0x1080)
    b[0x280:0x28e]=b'ucrtbased.dll\0'
    u32(dirs+13*8,0x1100);u32(dirs+13*8+4,64);u32(0x300,1);u32(0x304,0x1180)
    b[0x380:0x389]=b'SDL3.dll\0'
    return bytes(b)
checks=0
for is64 in (False,True):
    b=fixture(is64);imps=audit.pe_imports(b)
    assert imps==[{'kind':'normal','module':'ucrtbased.dll'},{'kind':'delay','module':'SDL3.dll'}],imps;checks+=2
bad=bytearray(fixture(True));struct.pack_into('<I',bad,0x98+112+8,0xDEADBEEF)
try:audit.pe_imports(bad)
except ValueError:checks+=1
else:raise AssertionError('unmapped RVA incorrectly treated as no imports')
with tempfile.TemporaryDirectory(prefix='hc-slim-native-selftest-') as temp:
    root=Path(temp);(root/'malformed.exe').write_bytes(b'MZ');(root/'literal-only.bin').write_bytes(b'libVIIPER.dll')
    result=audit.scan(root)
    assert len(result['errors'])==1;checks+=1
    row=next(r for r in result['files'] if r['path']=='literal-only.bin')
    assert row['imports']==[] and 'libviiper.dll' in row['literalEvidenceNotCallProof'];checks+=1
    assert result['targetCodeExecuted'] is False;checks+=1
print(f'NATIVE_IMPORT_AUDIT_SELFTEST checks={checks} PASS_NOT_DEVICE_PASS')
