"""Index identical invalid line bytes in a multi-ZIP log_lane_audit report."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

from ai_log_reader import evidence_json_loads


SHA256 = re.compile(r'[0-9a-f]{64}')


def warning_overlap(report, *, max_groups=64, max_witnesses=8):
    archives = set()
    groups = defaultdict(list)
    counts = Counter()
    for archive in report['archives']:
        sha = archive['zipSha256']
        if sha in archives:
            counts['duplicateArchiveInputs'] += 1
            continue
        if not isinstance(sha, str) or not SHA256.fullmatch(sha):
            raise ValueError('invalid archive SHA-256')
        archives.add(sha)
        if archive.get('memberReadCoverageComplete') is not True:
            counts['archivesWithIncompleteMemberReads'] += 1
        for warning in archive['coverageWarnings']:
            counts['coverageWarnings'] += 1
            evidence = warning.get('invalidLineByteEvidence')
            if evidence is None:
                counts['warningsWithoutInvalidLineByteEvidence'] += 1
                continue
            rows = evidence['observations']
            total = evidence['invalidRowsObserved']
            if type(total) is not int or total < len(rows):
                raise ValueError('invalid line witness count')
            counts['invalidRowsAcrossArchives'] += total
            counts['rowsNotIndexedBySource'] += total - len(rows)
            for row in rows:
                digest, size, lf = (row[k] for k in ('rawLineSha256', 'byteLength', 'terminatedByLf'))
                if (not isinstance(digest, str) or not SHA256.fullmatch(digest) or type(size) is not int or
                        size < 0 or type(lf) is not bool):
                    raise ValueError('invalid line fingerprint')
                # The line hash excludes the LF delimiter. Keep terminated and EOF witnesses apart.
                groups[(digest, size, lf)].append(dict(
                    zipSha256=sha, file=warning['file'], archiveEntryOccurrence=warning.get('archiveEntryOccurrence'),
                    line=row['line'], byteOffset=row['byteOffset'], kind=row['kind']))

    def archive_count(rows):
        return len({row['zipSha256'] for row in rows})

    ranked = sorted(groups, key=lambda key: (-archive_count(groups[key]), -len(groups[key]), key))
    summary = []
    for digest, size, lf in ranked[:max_groups]:
        rows = groups[digest, size, lf]
        # Prefer one witness per ZIP before adding other occurrences from that ZIP.
        first, rest, seen = [], [], set()
        for row in rows:
            sha = row['zipSha256']
            (rest if sha in seen else first).append(row)
            seen.add(sha)
        summary.append(dict(rawLineSha256=digest, byteLength=size, terminatedByLf=lf,
                            observations=len(rows), archives=len(seen), witnesses=(first + rest)[:max_witnesses],
                            witnessesNotListed=max(0, len(rows) - max_witnesses)))
    total_indexed = sum(map(len, groups.values()))
    return dict(schemaVersion=1, scope='indexed malformed-row bytes in supplied audit; not writer or incident identity',
                uniqueArchives=len(archives), duplicateArchiveInputs=counts['duplicateArchiveInputs'],
                archivesWithIncompleteMemberReads=counts['archivesWithIncompleteMemberReads'],
                coverageWarnings=counts['coverageWarnings'],
                warningsWithoutInvalidLineByteEvidence=counts['warningsWithoutInvalidLineByteEvidence'],
                invalidRowsAcrossArchives=counts['invalidRowsAcrossArchives'], rowsIndexed=total_indexed,
                rowsNotIndexedBySource=counts['rowsNotIndexedBySource'], fingerprints=len(groups),
                fingerprintsInMultipleArchives=sum(archive_count(rows) > 1 for rows in groups.values()),
                maxListedFingerprints=max_groups, maxWitnessesPerFingerprint=max_witnesses,
                fingerprintsNotListed=max(0, len(groups) - max_groups), groups=summary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('audit', type=Path)
    args = parser.parse_args()
    try:
        report = evidence_json_loads(args.audit.read_bytes())
        result = warning_overlap(report)
    except (OSError, ValueError, TypeError, KeyError, RecursionError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2, ensure_ascii=True))


if __name__ == '__main__':
    main()
