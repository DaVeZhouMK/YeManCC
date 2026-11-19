"""Read one legacy Steam tail from a YeManCC ZIP; write nothing to disk."""
from __future__ import annotations

import argparse
import hashlib
import re
import sys
import zipfile

from ai_log_reader import ZIP_READ_ERRORS, evidence_json_loads

TAIL_LIMIT = 2 * 1024 * 1024
RECEIPT = 'log-export-snapshot.json'
RECEIPT_LIMIT = 8 * 1024 * 1024
PAIRS = (
    ('steam-gameprocess.log', 'gameprocess_log.txt'),
    ('steam-controller.log', 'controller.txt'),
    ('steam-controller-ui.log', 'controller_ui.txt'),
    ('steam-gameoverlay-renderer.log', 'gameoverlay_renderer.txt'),
    ('steam-gameoverlay-ui.log', 'gameoverlay_ui.txt'),
    ('steam-gameoverlay-renderer.previous.log', 'gameoverlay_renderer.previous.txt'),
    ('steam-gameoverlay-ui.previous.log', 'gameoverlay_ui.previous.txt'),
)
TARGETS = {tail: 'steam/logs/' + source for tail, source in PAIRS}


def alias_claims(value):
    """Validate the entire fixed, bounded mapping; exact member bytes are checked separately."""
    if (not isinstance(value, dict) or type(value.get('schema')) is not int or value['schema'] != 1 or
            not isinstance(value.get('entries'), list) or not 1 <= len(value['entries']) <= len(PAIRS)):
        raise ValueError('invalid Steam tail alias receipt')
    claims = {}
    for row in value['entries']:
        if not isinstance(row, dict):
            raise ValueError('invalid Steam tail alias entry')
        tail, size, sha = row.get('tail'), row.get('bytes'), row.get('sha256')
        if (not isinstance(tail, str) or tail not in TARGETS or tail in claims or
                row.get('canonical') != TARGETS[tail] or type(size) is not int or not 0 <= size <= TAIL_LIMIT or
                not isinstance(sha, str) or re.fullmatch('[0-9a-f]{64}', sha) is None):
            raise ValueError('invalid Steam tail alias entry')
        claims[tail] = {'canonical': TARGETS[tail], 'bytes': size, 'sha256': sha}
    return claims


def verify_alias_bytes(claim, data):
    return len(data) == claim['bytes'] and hashlib.sha256(data).hexdigest() == claim['sha256']


def read_steam_tail(archive, tail):
    """Return the original legacy member, or the canonical member after exact receipt verification."""
    if tail not in TARGETS:
        raise ValueError('unknown Steam tail name')

    def read_unique(name, cap):
        items = [item for item in archive.infolist() if item.filename == name]
        if len(items) != 1 or items[0].is_dir() or items[0].file_size > cap:
            raise ValueError('missing, ambiguous, or oversized ZIP member: ' + name)
        return archive.read(items[0])

    if tail in archive.namelist():
        return read_unique(tail, TAIL_LIMIT)
    receipt = evidence_json_loads(read_unique(RECEIPT, RECEIPT_LIMIT))
    if not isinstance(receipt, dict):
        raise ValueError('invalid snapshot receipt')
    claim = alias_claims(receipt.get('steamTailAliases')).get(tail)
    if claim is None:
        raise ValueError('Steam tail has no verified alias: ' + tail)
    data = read_unique(claim['canonical'], claim['bytes'])
    if not verify_alias_bytes(claim, data):
        raise ValueError('Steam tail alias does not match the retained bytes: ' + tail)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('zip')
    parser.add_argument('tail', choices=TARGETS)
    args = parser.parse_args()
    try:
        with zipfile.ZipFile(args.zip) as archive:
            data = read_steam_tail(archive, args.tail)
        sys.stdout.buffer.write(data)
    except ZIP_READ_ERRORS + (ValueError, UnicodeError) as exc:
        parser.exit(2, str(exc) + '\n')


if __name__ == '__main__':
    main()
