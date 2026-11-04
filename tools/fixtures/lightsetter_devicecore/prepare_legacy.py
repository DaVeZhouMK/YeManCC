"""Prepare the original legacy tests with private owner names and ABI-v2 data.

Source generation only. Never executes a fixture, compiler, product or OS owner.
All original test assertions and negative cases remain verbatim in the clone.
"""
import argparse
import difflib
import hashlib
import json
import pathlib
import sys
import uuid
sys.dont_write_bytecode = True
from x2_source import generate_private_owner

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def prepare(scratch):
    directory = scratch / 'legacy-isolation'
    directory.mkdir(parents=True, exist_ok=False)
    owner = generate_private_owner(directory, 'YMCC-LegacyReplay-' + uuid.uuid4().hex)
    source = ROOT / 'tools/LightSetterSelfTest/Program.cs'
    original = source.read_text(encoding='utf-8-sig')
    anchor = '        view.Write(Oxp3OwnerLease.StateOffset, state);'
    if original.count(anchor) != 1:
        raise RuntimeError('legacy fake publication boundary drift')
    generated = original.replace(anchor, anchor + '\n        view.Write(Oxp3OwnerLease.ModelTagOffset, Oxp3OwnerLease.OneXPlayer3ModelTag);')
    destination = directory / 'LegacyTests.cs'
    destination.write_text(generated, encoding='utf-8', newline='\n')
    patch = ''.join(difflib.unified_diff(original.splitlines(True), generated.splitlines(True), fromfile='actual/LegacyTests.cs', tofile='scratch/LegacyTests.cs'))
    (directory / 'legacy-fake-v2-data.patch').write_text(patch, encoding='utf-8')
    provenance = {'source': str(source), 'sourceSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                  'generatedSha256': hashlib.sha256(destination.read_bytes()).hexdigest(),
                  'changes': 'only fake PublishHandoff writes actual OneXPlayer3ModelTag at actual ModelTagOffset',
                  'testAssertionsAndNegativeBodiesUnchanged': generated.replace('\n        view.Write(Oxp3OwnerLease.ModelTagOffset, Oxp3OwnerLease.OneXPlayer3ModelTag);', '') == original,
                  'ownerSource': str(owner), 'tests': 'NOT_RUN', 'build': 'NOT_RUN', 'namedObjectsOpened': False}
    (directory / 'legacy-source.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--scratch', required=True)
    args = parser.parse_args()
    prepare(pathlib.Path(args.scratch).resolve())
