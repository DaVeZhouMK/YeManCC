"""Bounded, lossless PE raw-padding experiment for three pinned native DLLs.

This tool never loads a DLL. All candidates and evidence go to --output.
Only raw file pointers/sizes, code/initialized-data byte counts and PE checksum
may change. Section RVAs/VirtualSize, mapped bytes and native ABI stay fixed.
Embedded certificate tables cause refusal, not signature removal.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct

PINS = {
    "GamepadMotion.dll": (25600, "e11d59f84b4a4a12e99b15ea998e79de06c6dfb8f1e9c637e973abd4d3d088a9"),
    "hidapi.dll": (162304, "ebeb835e2b4530ed68843f19d6a2604c51772e3c26e7f542fde194075f82d9b4"),
    "Xinput1_4.dll": (176640, "c89a11166acd59aa27dada45b9c64494221c04acb0b0886ea393c5d36f9071ec"),
}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def align(value, unit):
    return (value + unit - 1) // unit * unit

def require(value, message):
    if not value:
        raise ValueError(message)

def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

class PE:
    def __init__(self, data):
        self.data = bytes(data)
        require(len(data) >= 64 and data[:2] == b"MZ", "not an MZ file")
        self.pe = self.u32(60)
        require(data[self.pe:self.pe+4] == b"PE\0\0", "bad PE signature")
        self.machine = self.u16(self.pe+4)
        self.count = self.u16(self.pe+6)
        self.optional = self.pe + 24
        self.optional_size = self.u16(self.pe+20)
        require(self.machine == 0x8664 and self.u16(self.optional) == 0x20b, "only AMD64 PE32+ supported")
        self.dd = self.optional + 112
        self.dd_count = self.u32(self.optional+108)
        require(self.dd_count <= 16 and 112 + 8*self.dd_count <= self.optional_size, "invalid directory table")
        self.file_align = self.u32(self.optional+36)
        self.section_align = self.u32(self.optional+32)
        self.header_size = self.u32(self.optional+60)
        self.image_size = self.u32(self.optional+56)
        require(self.file_align >= 512 and self.file_align <= 65536 and self.file_align & (self.file_align-1) == 0, "unsupported FileAlignment")
        require(self.section_align >= self.file_align and self.section_align & (self.section_align-1) == 0, "bad SectionAlignment")
        require(self.header_size <= len(data) and self.header_size % self.file_align == 0, "invalid SizeOfHeaders")
        require(self.image_size < 256*1024*1024, "unreasonably large image")
        self.sections = []
        table = self.optional + self.optional_size
        require(table + 40*self.count <= self.header_size, "section table exceeds header")
        for i in range(self.count):
            off = table+40*i
            vs, va, rs, rp, reloc, lines, nr, nl, flags = struct.unpack_from("<IIIIIIHHI", data, off+8)
            s = dict(index=i, header=off, name=data[off:off+8].rstrip(b"\0").decode("ascii"), virtualSize=vs,
                     virtualAddress=va, rawSize=rs, rawPointer=rp, relocationPointer=reloc,
                     linePointer=lines, relocations=nr, lines=nl, characteristics=flags)
            require(va % self.section_align == 0, "unaligned section RVA")
            require(va + align(max(vs, rs), self.section_align) <= self.image_size, "section exceeds image")
            if rs:
                require(rp >= self.header_size and rp % self.file_align == 0 and rs % self.file_align == 0, "unaligned section raw extent")
                require(rp+rs <= len(data), "section raw extent exceeds file")
            else:
                require(rp == 0, "zero raw section with nonzero pointer is unsupported")
            self.sections.append(s)
        ordered = sorted((s for s in self.sections if s["rawSize"]), key=lambda s:s["rawPointer"])
        previous = self.header_size
        for s in ordered:
            require(s["rawPointer"] >= previous, "overlapping raw sections")
            previous = s["rawPointer"]+s["rawSize"]
        self.raw_end = previous
        previous = self.header_size
        for s in sorted(self.sections, key=lambda s:s["virtualAddress"]):
            require(s["virtualAddress"] >= previous, "overlapping virtual section extents")
            previous = s["virtualAddress"] + align(max(s["virtualSize"],s["rawSize"]),self.section_align)

    def u16(self, off):
        return struct.unpack_from("<H", self.data, off)[0]

    def u32(self, off):
        return struct.unpack_from("<I", self.data, off)[0]

    def directory(self, index):
        return struct.unpack_from("<II", self.data, self.dd+8*index) if index < self.dd_count else (0, 0)

    def offset(self, rva, size=1):
        if 0 <= rva and rva+size <= self.header_size:
            return rva
        for s in self.sections:
            delta = rva-s["virtualAddress"]
            if 0 <= delta and delta+size <= s["rawSize"]:
                return s["rawPointer"]+delta
        raise ValueError(f"RVA 0x{rva:x} size {size} has no file-backed extent")

    def string(self, rva):
        start = self.offset(rva)
        end = self.data.index(b"\0", start)
        return self.data[start:end].decode("ascii")

    def debug(self):
        rva, size = self.directory(6)
        if not size:
            require(rva == 0, "debug RVA without size")
            return []
        require(size % 28 == 0, "bad debug directory size")
        off = self.offset(rva, size)
        result = []
        for i in range(size//28):
            row = off+28*i
            _, _, _, _, typ, length, address, pointer = struct.unpack_from("<IIHHIIII", self.data, row)
            require(pointer+length <= len(self.data), "debug file span exceeds file")
            if length and address:
                require(self.offset(address,length) == pointer, "debug RVA/raw pointer mismatch")
            result.append(dict(index=i, directoryRva=rva+28*i, entryOffset=row, type=typ, size=length,
                               address=address, pointer=pointer, sha256=sha(self.data[pointer:pointer+length])))
        return result

    def file_references(self):
        result = []
        for row in self.debug():
            if row["size"]:
                result.append(dict(kind="debugPayload", field=row["entryOffset"]+24, pointer=row["pointer"], size=row["size"]))
        ptr, number = struct.unpack_from("<II", self.data, self.pe+12)
        require(bool(ptr) == bool(number), "inconsistent COFF symbol pointer/count")
        if ptr:
            string_table = ptr + 18*number
            length = self.u32(string_table)
            require(length >= 4, "invalid COFF string table")
            result.append(dict(kind="coffSymbolsAndStrings", field=self.pe+12, pointer=ptr, size=18*number+length))
        for s in self.sections:
            require(not(s["characteristics"] & 0x1000000) and s["relocations"] != 65535, "COFF overflow relocation format unsupported; refusing")
            for key, count, width, field in (("relocationPointer", "relocations",10,24),("linePointer","lines",6,28)):
                pointer = s[key]
                require(bool(pointer) == bool(s[count]), "inconsistent COFF section pointer/count")
                if pointer:
                    result.append(dict(kind=key, field=s["header"]+field, pointer=pointer, size=s[count]*width))
        for ref in result:
            require(ref["pointer"] and ref["pointer"]+ref["size"] <= len(self.data), "COFF/debug file reference out of bounds")
        return result

    def exports(self):
        rva, length = self.directory(0)
        if not rva:
            return None
        off = self.offset(rva,40)
        _, _, _, _, name, base, nf, nn, functions, names, ordinals = struct.unpack_from("<IIHHIIIIIII", self.data,off)
        values = list(struct.unpack_from("<"+"I"*nf,self.data,self.offset(functions,4*nf)))
        named = []
        for i in range(nn):
            nrva = self.u32(self.offset(names+4*i,4))
            ordinal = self.u16(self.offset(ordinals+2*i,2))
            require(ordinal < nf, "export ordinal exceeds function table")
            named.append(dict(name=self.string(nrva), ordinal=base+ordinal, rva=values[ordinal]))
        forwarded = [dict(ordinal=base+i, target=self.string(v)) for i,v in enumerate(values) if rva<=v<rva+length]
        return dict(name=self.string(name),base=base,functions=values,names=named,forwarded=forwarded)

    def imports(self, delayed=False):
        rva, size = self.directory(13 if delayed else 1)
        if not rva:
            return []
        off = self.offset(rva)
        result = []
        for i in range(10000):
            if delayed:
                row = struct.unpack_from("<IIIIIIII", self.data, off+32*i)
                if not any(row):
                    return result
                attrs, name, _, _, thunk, _, _, _ = row
                require(attrs & 1, "old VA-based delay imports unsupported")
            else:
                row = struct.unpack_from("<IIIII", self.data, off+20*i)
                if not any(row):
                    return result
                original, _, _, name, first = row
                thunk = original or first
            symbols = []
            for j in range(100000):
                value = struct.unpack_from("<Q",self.data,self.offset(thunk+8*j,8))[0]
                if not value:
                    break
                symbols.append({"ordinal":value & 65535} if value >> 63 else {"name":self.string(value+2), "hint":self.u16(self.offset(value,2))})
            else:
                raise ValueError("unterminated import thunk")
            result.append(dict(module=self.string(name), symbols=symbols))
        raise ValueError("unterminated import table")

    def image(self):
        out = bytearray(self.image_size)
        out[:self.header_size] = self.data[:self.header_size]
        for s in self.sections:
            va, rs, rp = (s[k] for k in ("virtualAddress","rawSize","rawPointer"))
            out[va:va+rs] = self.data[rp:rp+rs]
        return out

    def report(self):
        sections = []
        debug = self.debug()
        shadow = bytearray(self.data)
        drva, dsize = self.directory(6)
        if dsize:
            dp = self.offset(drva,dsize)
            shadow[dp:dp+dsize] = b"\0"*dsize
        for r in debug:
            if r["size"]:
                shadow[r["pointer"]:r["pointer"]+r["size"]] = b"\0"*r["size"]
        for s in self.sections:
            rs,rp = s["rawSize"],s["rawPointer"]
            raw=self.data[rp:rp+rs] if rs else b""
            last=len(raw.rstrip(b"\0"))
            shadow_last=len(shadow[rp:rp+rs].rstrip(b"\0")) if rs else 0
            sections.append({**s,"lastNonzero":last,"trailingZeroBytes":rs-last,
                             "minimumAlignedRawBytes":align(last,self.file_align),
                             "minimumIfDebugBytesWereZeroed":align(shadow_last,self.file_align)})
        return dict(bytes=len(self.data),sha256=sha(self.data),machine=hex(self.machine),entryPointRva=self.u32(self.optional+16),
                    fileAlignment=self.file_align,sectionAlignment=self.section_align,sizeOfHeaders=self.header_size,
                    sizeOfImage=self.image_size,directories=[list(self.directory(i)) for i in range(self.dd_count)],
                    certificateTable=list(self.directory(4)),coffSymbolPointer=self.u32(self.pe+12),coffSymbolCount=self.u32(self.pe+16),
                    overlayBytes=len(self.data)-self.raw_end,overlayNonzeroBytes=sum(x!=0 for x in self.data[self.raw_end:]),
                    sections=sections,debug=debug,exports=self.exports(),imports=self.imports(),delayImports=self.imports(True),
                    debugEraseCounterfactualSavings=sum(s["minimumAlignedRawBytes"]-s["minimumIfDebugBytesWereZeroed"] for s in sections))

def checksum(data, field):
    raw=bytearray(data)
    struct.pack_into("<I",raw,field,0)
    if len(raw)&1:
        raw.append(0)
    value=0
    for off in range(0,len(raw),2):
        value += struct.unpack_from("<H",raw,off)[0]
        value=(value&65535)+(value>>16)
    value=(value&65535)+(value>>16)
    return value+len(data)

def compact(original):
    pe=PE(original)
    require(pe.directory(4)==(0,0), "certificate table present: refusing to invalidate signature")
    refs=pe.file_references()
    ordered=sorted((s for s in pe.sections if s["rawSize"]), key=lambda s:s["rawPointer"])
    out=bytearray(original[:pe.header_size])
    translations=[(0,pe.header_size,0)]
    changes=[]
    previous=pe.header_size
    initialized_delta=0
    code_delta=0
    for s in ordered:
        rp,rs=s["rawPointer"],s["rawSize"]
        require(not any(original[previous:rp]), "nonzero inter-section data: refusing unexplained removal")
        for ref in refs:
            require(not(previous <= ref["pointer"] < rp), "file reference into inter-section gap")
        raw=original[rp:rp+rs]
        used=len(raw.rstrip(b"\0"))
        for ref in refs:
            if rp <= ref["pointer"] < rp+rs:
                require(ref["pointer"]+ref["size"] <= rp+rs, "file reference spans multiple sections")
                used=max(used,ref["pointer"]+ref["size"]-rp)
        new_size=align(used,pe.file_align)
        require(new_size<=rs and not any(raw[new_size:]), "refusing nonzero section removal")
        new_pointer=align(len(out),pe.file_align) if new_size else 0
        if new_size:
            out.extend(b"\0"*(new_pointer-len(out)))
            out.extend(raw[:new_size])
            translations.append((rp,rp+new_size,new_pointer))
        struct.pack_into("<II",out,s["header"]+16,new_size,new_pointer)
        if s["characteristics"] & 0x20:
            code_delta+=rs-new_size
        if s["characteristics"] & 0x40:
            initialized_delta+=rs-new_size
        changes.append(dict(section=s["name"],oldPointer=rp,newPointer=new_pointer,oldRawSize=rs,newRawSize=new_size,
                            virtualAddressPreserved=s["virtualAddress"],virtualSizePreserved=s["virtualSize"],removedBytes=rs-new_size))
        previous=rp+rs
    old_overlay=pe.raw_end
    overlay_end=max(old_overlay, len(original.rstrip(b"\0")))
    for ref in refs:
        if ref["pointer"]>=old_overlay:
            overlay_end=max(overlay_end,ref["pointer"]+ref["size"])
    if overlay_end>old_overlay:
        new_overlay=len(out)
        out.extend(original[old_overlay:overlay_end])
        translations.append((old_overlay,overlay_end,new_overlay))
    def translate(pointer,size=1):
        for start,end,new in translations:
            if start<=pointer and pointer+size<=end:
                return new+pointer-start
        raise ValueError(f"raw pointer 0x{pointer:x} would lose backing bytes")
    for ref in refs:
        new_pointer=translate(ref["pointer"],ref["size"])
        field=translate(ref["field"],4)
        struct.pack_into("<I",out,field,new_pointer)
    if code_delta:
        code=pe.u32(pe.optional+4)
        require(code>=code_delta,"code summary would underflow")
        struct.pack_into("<I",out,pe.optional+4,code-code_delta)
    if initialized_delta:
        initialized=pe.u32(pe.optional+8)
        require(initialized>=initialized_delta,"initialized-data summary would underflow")
        struct.pack_into("<I",out,pe.optional+8,initialized-initialized_delta)
    if bytes(out)!=original:
        struct.pack_into("<I",out,pe.optional+64,checksum(out,pe.optional+64))
    return bytes(out),dict(sections=changes,removedOverlayBytes=len(original)-overlay_end,
                           initializedDataSummaryReduction=initialized_delta,codeSummaryReduction=code_delta,debugPayloadsRetained=True)

def verify(original,candidate):
    a,b=PE(original),PE(candidate)
    require(a.directory(4)==b.directory(4)==(0,0),"signed image not eligible")
    require(a.count==b.count,"section count changed")
    require(a.header_size==b.header_size and a.image_size==b.image_size,"mapped extent changed")
    require(a.directory(0)==b.directory(0),"export directory moved")
    require([a.directory(i) for i in range(a.dd_count)]==[b.directory(i) for i in range(b.dd_count)],"data directory moved")
    require(a.exports()==b.exports(),"export names/ordinals/RVAs/forwarders changed")
    require(a.imports()==b.imports() and a.imports(True)==b.imports(True),"imports changed")
    require(a.u32(a.optional+16)==b.u32(b.optional+16),"entry point changed")
    normalized=bytearray(candidate[:b.header_size])
    header_changes=[]
    def allow(off,size,reason):
        if original[off:off+size]!=candidate[off:off+size]:
            header_changes.append(dict(offset=off,bytes=size,reason=reason,before=original[off:off+size].hex(),after=candidate[off:off+size].hex()))
        normalized[off:off+size]=original[off:off+size]
    allow(a.optional+4,4,"physical code summary")
    allow(a.optional+8,4,"physical initialized-data summary")
    allow(a.optional+64,4,"PE checksum")
    allow(a.pe+12,4,"COFF symbol file pointer")
    init_delta=0
    code_delta=0
    section_comparisons=[]
    for sa,sb in zip(a.sections,b.sections):
        for key in ("index","header","name","virtualSize","virtualAddress","relocations","lines","characteristics"):
            require(sa[key]==sb[key],f"section {sa['name']} changed {key}")
        require(sb["rawSize"]<=sa["rawSize"],"raw section grew")
        allow(sa["header"]+16,16,"section raw extent/COFF file pointers")
        if sa["characteristics"]&0x20:
            code_delta+=sa["rawSize"]-sb["rawSize"]
        if sa["characteristics"]&0x40:
            init_delta+=sa["rawSize"]-sb["rawSize"]
        size=align(max(sa["virtualSize"],sa["rawSize"]),a.section_align)
        section_comparisons.append(dict(name=sa["name"],rva=sa["virtualAddress"],virtualSize=sa["virtualSize"],mappedComparisonBytes=size))
    require(a.u32(a.optional+4)-b.u32(b.optional+4)==code_delta,"incorrect code summary")
    require(a.u32(a.optional+8)-b.u32(b.optional+8)==init_delta,"incorrect initialized-data summary")
    require(bytes(normalized)==original[:a.header_size],"unapproved PE header changes")
    if original!=candidate:
        require(b.u32(b.optional+64)==checksum(candidate,b.optional+64),"bad candidate checksum")
    ai,bi=a.image(),b.image()
    ar,br=a.file_references(),b.file_references()
    require(len(ar)==len(br),"file references changed")
    debug_changes=[]
    for ra,rb in zip(ar,br):
        require(ra["kind"]==rb["kind"] and ra["size"]==rb["size"],"file-reference identity changed")
        require(original[ra["pointer"]:ra["pointer"]+ra["size"]]==candidate[rb["pointer"]:rb["pointer"]+rb["size"]],"file-reference payload changed")
    for da,db in zip(a.debug(),b.debug()):
        require({k:v for k,v in da.items() if k not in {"pointer","entryOffset"}}=={k:v for k,v in db.items() if k not in {"pointer","entryOffset"}},"debug identity/RVA/payload changed")
        ptr_rva=da["directoryRva"]+24
        require(bytes(ai[ptr_rva:ptr_rva+4])==struct.pack("<I",da["pointer"]),"unexpected debug directory placement")
        if da["pointer"]!=db["pointer"]:
            debug_changes.append(dict(rva=ptr_rva,oldPointer=da["pointer"],newPointer=db["pointer"]))
        bi[ptr_rva:ptr_rva+4]=ai[ptr_rva:ptr_rva+4]
    require(ai[a.header_size:]==bi[b.header_size:],"mapped code/data/RVA bytes changed")
    require(original[a.raw_end:].rstrip(b"\0")==candidate[b.raw_end:].rstrip(b"\0"),"nonzero overlay bytes changed")
    require(len(candidate)<=len(original),"file grew")
    return dict(passed=True,originalBytes=len(original),candidateBytes=len(candidate),savedBytes=len(original)-len(candidate),
                originalSha256=sha(original),candidateSha256=sha(candidate),allNativeExportsPreserved=True,
                importsAndDelayImportsPreserved=True,allSectionVirtualAddressesAndSizesPreserved=True,
                completeMappedBodyByteIdenticalExceptDebugFilePointers=True,mappedBodyBytesCompared=len(ai)-a.header_size,
                mappedBodySha256=sha(ai[a.header_size:]),sections=section_comparisons,
                allowedHeaderChanges=header_changes,debugFilePointerChanges=debug_changes,
                coffAndDebugReferencedPayloadsByteIdentical=True,nonzeroOverlayPreserved=True,windowsLoadTested=False,hardwareTested=False)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    runtime=args.runtime.resolve()
    out=args.output.resolve()
    require(runtime not in [out,*out.parents] and out not in [runtime,*runtime.parents],"output must be separate from runtime")
    out.mkdir(parents=True,exist_ok=True)
    manifest_path=runtime/"HandheldCompanion.runtime.json"
    manifest_bytes=manifest_path.read_bytes()
    manifest=json.loads(manifest_bytes.decode("utf-8-sig"))
    entries={e["path"]:e for e in manifest["files"]}
    result=dict(schemaVersion=1,runtime=str(runtime),manifestSha256=sha(manifest_bytes),loadedAnyDll=False,
                hardwareExecuted=False,productionModified=False,files=[])
    for directory in ("input","candidate","verification"):
        (out/directory).mkdir(exist_ok=True)
    for name,(size,pin) in PINS.items():
        data=(runtime/name).read_bytes()
        require(len(data)==size and sha(data)==pin,f"input pin mismatch: {name}")
        require(entries[name]["sha256"]==pin,f"runtime manifest mismatch: {name}")
        (out/"input"/name).write_bytes(data)
        pe=PE(data)
        inventory=pe.report()
        candidate,attempt=compact(data)
        check=verify(data,candidate)
        dest=out/"candidate"/name
        dest.write_bytes(candidate)
        check_again=verify(data,dest.read_bytes())
        require(check==check_again,"on-disk candidate verification changed")
        write_json(out/"verification"/(name+".inventory.json"),inventory)
        write_json(out/"verification"/(name+".comparison.json"),check)
        ledger=dict(name=name,originalBytes=len(data),originalSha256=pin,candidateBytes=len(candidate),candidateSha256=sha(candidate),
                    savedBytes=len(data)-len(candidate),candidatePath=str(dest),status="static-only-candidate" if data!=candidate else "no-lossless-gain",
                    attempt=attempt,debugEraseCounterfactualSavings=inventory["debugEraseCounterfactualSavings"],
                    overlayBytes=inventory["overlayBytes"],certificateTable=inventory["certificateTable"],
                    comparison=str(out/"verification"/(name+".comparison.json")),
                    limitation="Not loaded or executed; static equivalence is not hardware acceptance.")
        result["files"].append(ledger)
        require((runtime/name).read_bytes()==data,f"production input changed during audit: {name}")
    require(manifest_path.read_bytes()==manifest_bytes,"runtime manifest changed during audit")
    result["totalOriginalBytes"]=sum(f["originalBytes"] for f in result["files"])
    result["totalCandidateBytes"]=sum(f["candidateBytes"] for f in result["files"])
    result["totalCandidateSavedBytes"]=sum(f["savedBytes"] for f in result["files"])
    result["productionSavedBytes"]=0
    result["status"]="completed-bounded-attempt"
    write_json(out/"RESULT.json",result)
    print(json.dumps({k:result[k] for k in ("status","totalOriginalBytes","totalCandidateBytes","totalCandidateSavedBytes","productionSavedBytes")}))

if __name__=="__main__":
    main()
