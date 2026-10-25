"""Write only the adapter's own bounded synthetic ZIP members."""
import json
import pathlib
import sys
import zipfile

request = json.loads(sys.stdin.buffer.read(1048577))
root = pathlib.Path(request['root']).resolve(strict=True)
archive = root / request['name']
if archive.parent != root or archive.suffix != '.zip' or archive.exists():
    raise ValueError('FRESH_OWN_ZIP_REQUIRED')
entries = request['entries']
if not 1 <= len(entries) <= 3:
    raise ValueError('SYNTHETIC_MEMBER_LIMIT')
allowed = {'logs/fan-api.log', 'logs/fan-api.log.1'}
with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as output:
    seen = set()
    for entry in entries:
        name, data = entry['name'], entry['data'].encode('utf-8')
        if name not in allowed or name in seen or len(data) > 65536:
            raise ValueError('SYNTHETIC_MEMBER_INVALID')
        seen.add(name)
        member = zipfile.ZipInfo(name, date_time=(2026, 10, 19, 0, 0, 0))
        member.compress_type = zipfile.ZIP_DEFLATED
        member.external_attr = 0o100600 << 16
        output.writestr(member, data)
print(json.dumps({'status': 'SYNTHETIC_ZIP_WRITTEN', 'path': str(archive), 'members': len(entries)}))
