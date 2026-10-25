"""Adversarial static checks for PE repacking. No Windows/native DLL execution."""
import argparse
import json
from pathlib import Path
import struct
from pe_small_trim import PE, compact, verify, checksum, sha, write_json


def fix_checksum(raw):
    data=bytearray(raw)
    pe=PE(data)
    struct.pack_into('<I',data,pe.optional+64,checksum(data,pe.optional+64))
    return bytes(data)


def move_debug_and_coff_fixture(raw):
    """Insert a zero text block, shifting actual debug raw pointers; add COFF references.

    Fixture remains data for static parsing only. It is never loaded into Windows.
    The source .text VirtualSize is preserved, including its original zero tail.
    """
    pe=PE(raw)
    text=next(s for s in pe.sections if s['name']=='.text')
    cut=text['rawPointer']+text['rawSize']
    amount=pe.file_align
    data=bytearray(raw[:cut]+bytes(amount)+raw[cut:])
    struct.pack_into('<I',data,text['header']+16,text['rawSize']+amount)
    struct.pack_into('<I',data,pe.optional+4,pe.u32(pe.optional+4)+amount)
    for s in pe.sections:
        if s['rawSize'] and s['rawPointer']>=cut:
            struct.pack_into('<I',data,s['header']+20,s['rawPointer']+amount)
    for d in pe.debug():
        field=d['entryOffset']+(amount if d['entryOffset']>=cut else 0)+24
        pointer=d['pointer']+(amount if d['pointer']>=cut else 0)
        struct.pack_into('<I',data,field,pointer)
    symbol_pointer=len(data)
    data.extend(b'TESTSYM\0'+struct.pack('<IhHBB',0,1,0,2,0)+struct.pack('<I',4))
    require_size=18+4
    assert len(data)-symbol_pointer==require_size
    struct.pack_into('<II',data,pe.pe+12,symbol_pointer,1)
    relocation_pointer=len(data)
    data.extend(struct.pack('<IIH',0x10,0,1))
    line_pointer=len(data)
    data.extend(struct.pack('<IH',0x10,1))
    struct.pack_into('<IIHH',data,text['header']+24,relocation_pointer,line_pointer,1,1)
    # These zeros are unreferenced overlay, unlike zeros at the end of COFF records.
    data.extend(bytes(512))
    return bytes(data)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--evidence',type=Path,required=True)
    args=parser.parse_args()
    out=args.evidence.resolve()
    original=(out/'input/Xinput1_4.dll').read_bytes()
    candidate=(out/'candidate/Xinput1_4.dll').read_bytes()
    gm=(out/'input/GamepadMotion.dll').read_bytes()
    tests=[]
    def accepted(name,func):
        func()
        tests.append(dict(name=name,passed=True,expected='accept'))
    def rejected(name,func):
        try:
            func()
        except (ValueError,struct.error,IndexError) as exc:
            tests.append(dict(name=name,passed=True,expected='reject',reason=str(exc)))
        else:
            raise AssertionError('Verifier accepted '+name)
    accepted('real-512-byte-candidate-full-static-equivalence',lambda:verify(original,candidate))
    for name in ('GamepadMotion.dll','hidapi.dll'):
        data=(out/'input'/name).read_bytes()
        result,attempt=compact(data)
        assert result==data, name+' unexpected transformation'
        accepted(name+'-no-gain-output-identical',lambda d=data,r=result:verify(d,r))
    pe=PE(candidate)
    def mutant(name,off,payload):
        changed=bytearray(candidate)
        changed[off:off+len(payload)]=payload
        changed=fix_checksum(changed)
        rejected(name,lambda:verify(original,changed))
    text=next(s for s in pe.sections if s['name']=='.text')
    mutant('executable-byte-corruption',text['rawPointer'],bytes([candidate[text['rawPointer']]^1]))
    mutant('text-virtual-size-gap-regression',text['header']+8,struct.pack('<I',text['virtualSize']-1))
    xrva,_=pe.directory(0)
    functions=pe.u32(pe.offset(xrva)+28)
    function_off=pe.offset(functions,4)
    mutant('export-rva-drift',function_off,struct.pack('<I',pe.u32(function_off)+1))
    rva,_=pe.directory(1)
    import_name_rva=pe.u32(pe.offset(rva)+12)
    import_name_off=pe.offset(import_name_rva)
    mutant('import-module-drift',import_name_off,b'Z')
    reloc=next(s for s in pe.sections if s['name']=='.reloc')
    mutant('base-relocation-payload-corruption',reloc['rawPointer']+8,bytes([candidate[reloc['rawPointer']+8]^1]))
    debug=pe.debug()[0]
    mutant('debug-raw-pointer-not-repaired',debug['entryOffset']+24,struct.pack('<I',debug['pointer']+4))
    mutant('unapproved-header-timestamp',pe.pe+8,struct.pack('<I',pe.u32(pe.pe+8)^1))
    mutant('wrong-initialized-data-summary',pe.optional+8,struct.pack('<I',pe.u32(pe.optional+8)+512))
    bad=bytearray(candidate)
    struct.pack_into('<I',bad,pe.optional+64,pe.u32(pe.optional+64)^1)
    rejected('bad-output-checksum',lambda:verify(original,bytes(bad)))
    bad=bytearray(gm)
    gp=PE(gm)
    struct.pack_into('<II',bad,gp.dd+4*8,len(bad),8)
    rejected('certificate-bearing-input-is-refused',lambda:compact(bytes(bad)))
    # A real debug directory is shifted, and all three COFF file pointers move.
    fixture=move_debug_and_coff_fixture(gm)
    f=PE(fixture)
    compacted,attempt=compact(fixture)
    proof=verify(fixture,compacted)
    assert proof['savedBytes']==1024 # inserted text block + unreferenced overlay zeros
    assert proof['debugFilePointerChanges']
    assert f.u32(f.pe+12)!=PE(compacted).u32(PE(compacted).pe+12)
    (out/'verification/offset-fixture.input.bin').write_bytes(fixture)
    (out/'verification/offset-fixture.candidate.bin').write_bytes(compacted)
    write_json(out/'verification/offset-fixture.comparison.json',proof)
    accepted('shifted-debug-coff-symbol-relocation-line-pointers-repaired',lambda:verify(fixture,compacted))
    # Nonzero unexplained gap data cannot silently disappear.
    boundary=gp.sections[0]['rawPointer']
    gap=bytearray(gm[:boundary]+bytes(512)+gm[boundary:])
    gap[boundary]=0x5a
    for s in gp.sections:
        if s['rawSize']:
            struct.pack_into('<I',gap,s['header']+20,s['rawPointer']+512)
    for d in gp.debug():
        struct.pack_into('<I',gap,d['entryOffset']+512+24,d['pointer']+512)
    rejected('nonzero-unexplained-gap-is-refused',lambda:compact(bytes(gap)))
    for name in ('GamepadMotion.dll','hidapi.dll','Xinput1_4.dll'):
        data=(out/'input'/name).read_bytes()
        c,_=compact(data)
        c2,_=compact(c)
        assert c==c2
    accepted('all-three-compactions-are-idempotent',lambda:None)
    result=dict(status='passed',tests=len(tests),cases=tests,loadedAnyDll=False,hardwareExecuted=False,
                fixtureExecutionForbidden=True,scope='PE parser/repacker/verifier adversarial checks only')
    write_json(out/'STATIC-SELFTEST.json',result)
    print(json.dumps(dict(status='passed',tests=len(tests),loadedAnyDll=False)))

if __name__=='__main__':
    main()
