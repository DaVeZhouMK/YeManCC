"""Exercise the real ZIP reader and the fixed Steam tail pair contract."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import warnings
import zipfile

from log_lane_audit import SteamTailMirrorAudit, audit_zip
from steam_tail_alias_reader import RECEIPT, read_steam_tail


class SteamTailMirrorTests(unittest.TestCase):
    tail, source = SteamTailMirrorAudit.PAIRS[0]
    canonical = 'steam/logs/' + source

    def manifest(self, **kwargs):
        limit = SteamTailMirrorAudit.LIMIT
        self.row = {'file': self.tail, 'sourceFile': self.source, 'available': True, 'saved': True,
                    'truncated': False, 'originalBytes': 4, 'copiedBytes': 4, 'limitBytes': limit, **kwargs}
        return {'schema': 1, 'scope': 'steam', 'maxBytesPerFile': limit, 'files': [self.row]}

    def check_archive(self, rows, manifest=None):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'input.zip'
            with warnings.catch_warnings(), zipfile.ZipFile(path, 'w') as archive:
                warnings.simplefilter('ignore', UserWarning)
                for name, data in rows:
                    archive.writestr(name, data)
                if manifest is not None:
                    archive.writestr(SteamTailMirrorAudit.MANIFEST,
                                     json.dumps(manifest) if isinstance(manifest, dict) else manifest)
            before = hashlib.sha256(path.read_bytes()).hexdigest()
            view = audit_zip(path, {'baseEvents': []})['commonEvidence']['steamTailMirrors']
            self.assertEqual(before, hashlib.sha256(path.read_bytes()).hexdigest())
        return view, view['pairs'][0]

    def test_exact_bytes_with_correct_provenance_once(self):
        data = b'\xef\xbb\xbfx'
        view, pair = self.check_archive([(self.canonical, data), (self.tail, data)], self.manifest())
        self.assertEqual(pair['state'], 'exact-whole-member-match')
        self.assertEqual(pair['sharedSha256'], hashlib.sha256(data).hexdigest())
        self.assertEqual(view['exactMatchRedundantBytes'], 4)

    def test_changed_and_truncated_are_not_redundant(self):
        cases = ((b'ABCD', b'abcd', self.manifest(), 'bytes-different'),
                 (b'abcd', b'abcde', self.manifest(), 'size-different'),
                 (b'abcd', b'abcd', self.manifest(truncated=True), 'truncated-tail-retained'))
        for left, right, manifest, state in cases:
            with self.subTest(state=state):
                view, pair = self.check_archive([(self.tail, left), (self.canonical, right)], manifest)
                self.assertEqual(pair['state'], state)
                self.assertEqual(view['exactMatchRedundantBytes'], 0)

    def test_missing_and_duplicate_members_are_distinct(self):
        rows = [(self.tail, b'abcd'), (self.canonical, b'abcd')]
        _, pair = self.check_archive(rows[:1], self.manifest())
        self.assertEqual(pair['state'], 'pair-member-missing')
        _, pair = self.check_archive(rows + rows[1:], self.manifest())
        self.assertEqual(pair['state'], 'ambiguous')

    def test_missing_wrong_and_repeated_provenance_do_not_supply_a_pair(self):
        rows = [(self.tail, b'abcd'), (self.canonical, b'abcd')]
        _, pair = self.check_archive(rows)
        self.assertEqual(pair['state'], 'tail-provenance-unavailable')
        _, pair = self.check_archive(rows, self.manifest(sourceFile='another.txt'))
        self.assertEqual(pair['state'], 'tail-provenance-unavailable')
        manifest = self.manifest()
        manifest['files'] *= 2
        _, pair = self.check_archive(rows, manifest)
        self.assertEqual(pair['state'], 'ambiguous')

    def test_invalid_manifest_and_counter_types_do_not_vote(self):
        rows = [(self.tail, b'abcd'), (self.canonical, b'abcd')]
        view, pair = self.check_archive(rows, '{"schema":1,"schema":1}')
        self.assertEqual(view['tailManifestState'], 'invalid')
        self.assertIsNone(pair['sharedSha256'])
        _, pair = self.check_archive(rows, self.manifest(copiedBytes=4.0))
        self.assertEqual(pair['state'], 'tail-metadata-inconsistent')

    def test_alias_verified_and_resolved_while_distinct_physical_tail_wins(self):
        data = b'ab\x00d'
        snapshot = self.alias_snapshot(data)
        view, pair = self.check_archive([(self.canonical, data), (RECEIPT, snapshot)], self.manifest())
        self.assertEqual(pair['state'], 'verified-alias')
        self.assertEqual(view['exactMatchRedundantBytes'], 0)
        self.assertEqual(view['verifiedAliasCount'], 1)
        self.assertEqual(self.resolve([(self.canonical, data), (RECEIPT, snapshot)]), data)
        self.assertEqual(self.resolve([(self.canonical, data), (RECEIPT, snapshot), (self.tail, b'saved')]), b'saved')

    def test_wrong_hash_missing_receipt_and_ambiguous_member_never_resolve(self):
        snapshot = self.alias_snapshot(b'abcd')
        view, pair = self.check_archive([(self.canonical, b'xxxx'), (RECEIPT, snapshot)], self.manifest())
        self.assertEqual(view['verifiedAliasCount'], 0)
        self.assertEqual(pair['state'], 'alias-not-verified')
        for rows in ([(self.canonical, b'xxxx'), (RECEIPT, snapshot)], [(self.canonical, b'abcd')],
                     [(self.canonical, b'abcd'), (self.canonical, b'abcd'), (RECEIPT, snapshot)],
                     [(self.canonical, b'abcd'), (RECEIPT, snapshot), (RECEIPT, snapshot)],
                     [(self.tail, b'abcd'), (self.tail, b'abcd'), (RECEIPT, snapshot)]):
            with self.assertRaises(ValueError):
                self.resolve(rows)

    def alias_snapshot(self, data):
        return json.dumps({'steamTailAliases': {'schema': 1, 'entries': [
            {'tail': self.tail, 'canonical': self.canonical, 'bytes': len(data),
             'sha256': hashlib.sha256(data).hexdigest()}]}})

    def resolve(self, rows):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'input.zip'
            with warnings.catch_warnings(), zipfile.ZipFile(path, 'w') as archive:
                warnings.simplefilter('ignore', UserWarning)
                for name, data in rows:
                    archive.writestr(name, data)
            with zipfile.ZipFile(path) as archive:
                return read_steam_tail(archive, self.tail)


if __name__ == '__main__':
    unittest.main()
