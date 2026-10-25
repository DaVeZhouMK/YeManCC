import argparse,hashlib,json,pathlib,struct
def scan(data,name):
    if data[:2]!=b'MZ':raise ValueError('not MZ: '+name)
    pe=struct.unpack_from('<I',data,0x3c)[0]
    assert data[pe:pe+4]==b'PE\0\0'
    machine,sections=struct.unpack_from('<HH',data,pe+4)
    optional_size=struct.unpack_from('<H',data,pe+20)[0]
    optional=pe+24;magic=struct.unpack_from('<H',data,optional)[0]
    is64=magic==0x20b
    if magic not in (0x10b,0x20b):raise ValueError('bad optional magic')
    image_base=struct.unpack_from('<Q' if is64 else '<I',data,optional+(24 if is64 else 28))[0]
    dd=optional+(112 if is64 else 96)
    dd_count=struct.unpack_from('<I',data,optional+(108 if is64 else 92))[0]
    table=optional+optional_size
    ranges=[]
    for i in range(sections):
        off=table+40*i
        virtual_size,virtual_address,raw_size,raw_pointer=struct.unpack_from('<IIII',data,off+8)
        ranges.append((virtual_address,max(virtual_size,raw_size),raw_pointer))
    def mapped(rva):
        for start,size,raw in ranges:
            if start<=rva<start+size:return raw+rva-start
        if rva<struct.unpack_from('<I',data,optional+60)[0]:return rva
        raise ValueError(f'unmapped RVA {rva:x} in {name}')
    def string(rva):
        off=mapped(rva);return data[off:data.index(b'\0',off)].decode('ascii',errors='replace')
    def directory(index):return struct.unpack_from('<II',data,dd+index*8) if index<dd_count else (0,0)
    def symbols(thunk):
        if not thunk:return []
        off=mapped(thunk);width=8 if is64 else 4;fmt='<Q' if is64 else '<I';result=[]
        for i in range(100000):
            value=struct.unpack_from(fmt,data,off+i*width)[0]
            if not value:break
            if value&(1<<(width*8-1)):result.append('#'+str(value&0xffff))
            else:result.append(string(value+2))
        return result
    def checked_symbols(thunk):
        try:return {'symbols':symbols(thunk)}
        except (ValueError,struct.error) as error:return {'symbols':None,'symbolDecodeError':str(error)}
    imports=[];rva,size=directory(1)
    if rva:
        off=mapped(rva)
        while True:
            original,time,chain,name_rva,first=struct.unpack_from('<IIIII',data,off)
            if not any((original,time,chain,name_rva,first)):break
            imports.append({'module':string(name_rva),**checked_symbols(original or first)})
            off+=20
    delays=[];rva,size=directory(13)
    if rva:
        off=mapped(rva)
        while True:
            row=struct.unpack_from('<IIIIIIII',data,off)
            if not any(row):break
            attrs,name_rva,handle,iat,int_table,bound,unload,time=row
            if not(attrs&1):name_rva-=image_base;int_table-=image_base
            delays.append({'module':string(name_rva),**checked_symbols(int_table)})
            off+=32
    return {'name':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
      'machine':hex(machine),'pe32Plus':is64,'imports':imports,'delayImports':delays,
      'targetModuleImports':[x['module'] for x in imports+delays if x['module'].lower() in {'libviiper.dll','sdl3.dll','ucrtbased.dll'}]}

if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('runtime');parser.add_argument('output')
    args=parser.parse_args()
    root=pathlib.Path(args.runtime).resolve()
    files=[];errors=[]
    for path in sorted(root.rglob('*')):
        if not path.is_file():continue
        raw=path.read_bytes()
        if raw[:2]!=b'MZ':continue
        try:files.append(scan(raw,path.relative_to(root).as_posix()))
        except Exception as error:errors.append({'path':path.relative_to(root).as_posix(),'error':str(error)})
    result={'status':'passed' if not errors else 'failed','runtime':str(root),'files':files,'errors':errors,'loadedInspectedAssembly':False,'hardwareExecuted':False}
    pathlib.Path(args.output).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'status':result['status'],'peFiles':len(files),'errors':len(errors)}))
    if errors:raise SystemExit(1)
