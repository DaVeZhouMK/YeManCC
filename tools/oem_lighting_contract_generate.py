"""Compatibility CLI for the unified HC lighting generator; no hardware IO."""
import argparse,hashlib,json,pathlib,shutil,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--hc-devices',required=True);p.add_argument('--output-header',required=True);p.add_argument('--receipt',required=True);a=p.parse_args()
repo=pathlib.Path(__file__).resolve().parent.parent
catalog=json.loads((repo/'capabilities/hc-device-catalog.json').read_text(encoding='utf-8-sig'))
root=pathlib.Path(a.hc_devices).resolve()
for row in catalog['sourceFiles']:
    source=root/row['file']
    if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest()!=row['sha256']:
        raise ValueError('Legacy generator must use the reviewed frozen model source: '+row['file'])
with tempfile.TemporaryDirectory(prefix='ymcc-lighting-generation-') as scratch:
    generated=pathlib.Path(scratch)/'generated'
    subprocess.run(['node',str(repo/'tools/hc_capability_catalog_generate.mjs'),'--generated-root',str(generated),'--out',str(pathlib.Path(scratch)/'build')],cwd=repo,check=True,capture_output=True)
    current=json.loads((generated/'capabilities/hc-device-catalog.json').read_text(encoding='utf-8'))['domains']['lighting']
    header=generated/'native/oem_lighting_operations.h';out=pathlib.Path(a.output_header);out.parent.mkdir(parents=True,exist_ok=True)
    if out.resolve()==(repo/'native/oem_lighting_operations.h').resolve():
        shutil.copyfile(header,out)
    else:
        # Legacy scratch callers expect a single self-contained header.
        shared=(generated/'capabilities/hc-device-catalog.generated.h').read_text(encoding='utf-8')
        text=header.read_text(encoding='utf-8').replace('#include "../capabilities/hc-device-catalog.generated.h"',shared)
        out.write_text(text,encoding='utf-8')
    receipt={'schemaVersion':2,'generator':'unified-hc-capability-catalog','presets':current['presets'],'implementedPresetClasses':[r['name'] for r in current['rows'] if r['presetAvailable']],'declaredPresetWithoutTransport':[r['name'] for r in current['rows'] if r['presetDeclared'] and not r['presetAvailable']],'baseNoopBrightnessClasses':[r['name'] for r in current['rows'] if not r['brightness']],'headerSha256':hashlib.sha256(out.read_bytes()).hexdigest(),'hardwareIo':False}
    result=pathlib.Path(a.receipt);result.parent.mkdir(parents=True,exist_ok=True);result.write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'presets':len(current['presets']),'implementedClasses':len(receipt['implementedPresetClasses']),'hardwareIo':False}))
