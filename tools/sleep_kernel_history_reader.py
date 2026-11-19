"""Read the explicit Windows power history in a YeManCC log ZIP; never repair or pair requests."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET
from zipfile import ZipFile

from ai_log_reader import evidence_json_loads

MEMBER = 'log-export-availability.json'
PROVIDER = 'Microsoft-Windows-Kernel-Power'
NS = {'e': 'http://schemas.microsoft.com/win/2004/08/events/event'}
EVENT_IDS = {'42', '107', '187', '506', '507'}
MAX_MEMBER = 8 * 1024 * 1024
MAX_EVENTS = 64
MAX_XML = 32 * 1024
MAX_ALL_XML = 256 * 1024
WIN_U64_SENTINEL = (1 << 64) - 1


def unsigned(raw):
    if isinstance(raw, str) and re.fullmatch(r'[0-9]{1,20}', raw.strip()):
        number = int(raw.strip())
        if number < WIN_U64_SENTINEL:
            return number
    return None


def boolean(raw):
    if isinstance(raw, str):
        return {'true': True, '1': True, 'false': False, '0': False}.get(raw.strip())
    return None


def decode_event(xml):
    original = xml.encode('utf-8')
    event = {'xmlSha256': hashlib.sha256(original).hexdigest(), 'xml': xml}
    try:
        if len(original) > MAX_XML or '<!DOCTYPE' in xml or '<!ENTITY' in xml:
            raise ValueError('xml-size-or-declaration')
        root = ET.fromstring(xml)
        system = root.find('e:System', NS)
        if root.tag != f'{{{NS["e"]}}}Event' or system is None:
            raise ValueError('event-envelope')
        provider = system.find('e:Provider', NS)
        event_id = system.findtext('e:EventID', namespaces=NS)
        record_id = system.findtext('e:EventRecordID', namespaces=NS)
        time = system.find('e:TimeCreated', NS)
        if (provider is None or provider.get('Name') != PROVIDER or
                system.findtext('e:Channel', namespaces=NS) != 'System' or event_id not in EVENT_IDS or
                not unsigned(record_id) or time is None or not time.get('SystemTime')):
            raise ValueError('event-scope')
    except (ET.ParseError, ValueError) as error:
        return {**event, 'status': 'invalid-event-xml', 'error': str(error)}

    fields = [{'name': data.get('Name'), 'value': data.text} for data in root.findall('e:EventData/e:Data', NS)]
    counts = Counter(field['name'] for field in fields)
    unique = {field['name']: field['value'] for field in fields if counts[field['name']] == 1}
    correlation = system.find('e:Correlation', NS)
    event.update(status='decoded', eventId=int(event_id), recordId=record_id, timeUtc=time.get('SystemTime'),
                 version=system.findtext('e:Version', namespaces=NS),
                 computer=system.findtext('e:Computer', namespaces=NS),
                 correlation=dict(correlation.attrib) if correlation is not None else {}, fields=fields,
                 duplicateFields=sorted(name for name, count in counts.items() if name is not None and count > 1),
                 reason=unsigned(unique.get('Reason')), bootId=unique.get('BootId'),
                 scenarioInstanceId=unique.get('ScenarioInstanceId'), scenarioInstanceIdV2=unique.get('ScenarioInstanceIdV2'))
    if event_id == '507':
        entered = boolean(unique.get('SleepEntered'))
        open_on_exit = boolean(unique.get('IsCsSessionInProgressOnExit'))
        sleep_us = unsigned(unique.get('SleepDurationInUs'))
        stage = 'unknown'
        if open_on_exit is False:
            if entered is True and sleep_us is not None and sleep_us > 0:
                stage = 'entered'
            elif entered is False and sleep_us == 0:
                stage = 'not-entered'
        event.update(windowsSleepStage=stage, sleepEnteredField=entered, sleepDurationUs=sleep_us,
                     sessionInProgressOnExit=open_on_exit)
    return event


def read_archive(path):
    result = {'schemaVersion': 1, 'archive': str(path.resolve()), 'member': MEMBER,
              'completeWindowsCoverageProven': False, 'requestAttributionProven': False,
              'automaticRepairAuthorized': False, 'events': []}
    with ZipFile(path) as archive:
        entries = [entry for entry in archive.infolist() if entry.filename == MEMBER]
        if not entries:
            return {**result, 'status': 'availability-member-missing'}
        if len(entries) > 1 or entries[0].file_size > MAX_MEMBER:
            return {**result, 'status': 'availability-member-ambiguous-or-too-large'}
        raw = archive.read(entries[0])
    result['memberSha256'] = hashlib.sha256(raw).hexdigest()
    try:
        availability = evidence_json_loads(raw)
        if not isinstance(availability, dict):
            raise ValueError('availability-object-required')
    except ValueError as error:
        return {**result, 'status': 'invalid-availability', 'error': str(error)}
    history = availability.get('windowsKernelPowerHistory')
    if history is None:
        return {**result, 'status': 'raw-history-absent'}
    if not isinstance(history, dict) or not isinstance(history.get('status'), str):
        return {**result, 'status': 'invalid-history'}
    result['captureStatus'] = history['status']
    result.update(queryExhausted=history.get('queryExhausted') is True, lookbackMs=history.get('lookbackMs'))
    if history['status'] in ('export-power-observation-changed', 'capture-unavailable',
                             'offline-consumer-system-event-read-suppressed'):
        return {**result, 'status': 'capture-unusable'}
    rows = history.get('events')
    if (history.get('schemaVersion') != 1 or history.get('channel') != 'System' or
            history.get('provider') != PROVIDER or not isinstance(rows, list) or len(rows) > MAX_EVENTS or
            any(not isinstance(row, dict) or not isinstance(row.get('xml'), str) for row in rows)):
        return {**result, 'status': 'invalid-history'}
    try:
        if sum(len(row['xml'].encode('utf-8')) for row in rows) > MAX_ALL_XML:
            return {**result, 'status': 'xml-total-too-large'}
    except UnicodeEncodeError:
        return {**result, 'status': 'invalid-history-xml-encoding'}
    result['events'] = [decode_event(row['xml']) for row in rows]
    # These are event observations. Missing rows, event limits or a long gap never imply a failed sleep.
    result['windowsExitStages'] = dict(Counter(row['windowsSleepStage'] for row in result['events']
                                               if row.get('eventId') == 507))
    result['invalidEvents'] = sum(row['status'] != 'decoded' for row in result['events'])
    status = 'no-raw-events' if not rows else 'event-decode-errors' if result['invalidEvents'] else 'decoded'
    return {**result, 'status': status}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--output', type=Path, help='Create a new JSON report; defaults to stdout.')
    args = parser.parse_args()
    report = json.dumps(read_archive(args.archive), ensure_ascii=True, indent=2) + '\n'
    if args.output:
        with args.output.open('x', encoding='utf-8') as output:
            output.write(report)
    else:
        print(report, end='')


if __name__ == '__main__':
    main()
