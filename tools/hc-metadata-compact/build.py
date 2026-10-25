"""Losslessly compact the two HC JSON metadata files; never mutate an input runtime."""
from pathlib import Path
import argparse, hashlib, json

def sha(data):
    return hashlib.sha256(data).hexdigest()

def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate JSON key: " + key)
        result[key] = value
    return result

def parse(data):
    return json.loads(data.decode("utf-8-sig"), object_pairs_hook=unique_object)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--input", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
a = parser.parse_args()
source, output = a.input.resolve(), a.output.resolve()
if output == source or source in output.parents:
    raise ValueError("Output must be outside the input runtime")
output.mkdir(parents=True, exist_ok=False)
rows = []
for name in ("HandheldCompanion.deps.json", "HandheldCompanion.json"):
    before = (source / name).read_bytes()
    content = parse(before)
    after = json.dumps(content, ensure_ascii=True, separators=(",", ":"), allow_nan=False).encode("utf-8")
    if parse(after) != content:
        raise ValueError("JSON content changed: " + name)
    (output / name).write_bytes(after)
    rows.append(dict(path=name, beforeBytes=len(before), afterBytes=len(after),
                     beforeSha256=sha(before), afterSha256=sha(after),
                     action="replace-candidate", sourceCandidate=str(output / name),
                     completeParsedValueEqual=True))
report = dict(schemaVersion=1, status="LOSSLESS_JSON_COMPACTION_PASS", inputRuntime=str(source),
              rows=rows, savedBytes=sum(r["beforeBytes"]-r["afterBytes"] for r in rows),
              inputModified=False, productStarted=False, hardwareAccessed=False)
(output / "PROMOTION-LIST.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
print(json.dumps(report, ensure_ascii=False, indent=2))
