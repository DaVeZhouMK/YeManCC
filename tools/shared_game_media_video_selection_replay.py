"""Bounded video choice replay through production Store/Queue/Worker/WinHTTP."""
import argparse
import copy
import hashlib
import json
import os
import pathlib
import subprocess
import time

from shared_game_media_popular_games_selftest import Fixture, INIT, pathkey, payload

TASK = (pathlib.Path(__file__).resolve().parents[4] /
        'Build/Tasks/YMCC-Decky-SharedWallpaper-20261017').resolve()
DEFAULT_PROFILE = TASK / 'implementation/popular-games-r45/replay-profile.json'
ORIGINAL_CASES = ('selected-second', 'expired-choice', 'new-choice-after-queue',
                  'static-only-expired-choice', 'encoded-official-choice')
EXTRA_CASES = ('selected-after-default-cap', 'selected-after-transient')
LEGACY_CASES = ('legacy-nobu-selection', 'legacy-nobu-wrong-appid',
                'legacy-nobu-cross-game-url')
CASE_TIMEOUT = 40  # Keep the existing per-process bound.
EXE_SENTINEL = b'MZ-YMCC-owned-Nobu-identity-sentinel-v1'
OLD_VIDEO = bytes.fromhex('00000018') + b'ftypisom'


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding='utf-8')


def prepare_profile(profile0, mode):
    profile = copy.deepcopy(profile0)
    profile['items'] = profile['items'][:1]
    item = profile['items'][0]
    if mode in LEGACY_CASES:
        if item['appId'] != 1336980 or 'legacyIdentity' not in item:
            raise ValueError('legacy Nobu cases require an explicit Nobu profile with legacyIdentity')
        return profile
    app = item['appId']
    alternate = f'https://video.steamstatic.com/store_trailers/{app}/selected/master.m3u8'
    profile['routes'][alternate] = copy.deepcopy(profile['routes'][item['master']])
    profile['routes'][alternate]['text'] = (
        '#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=6000000,RESOLUTION=1920x1080\n' +
        item['variant'] + '\n')
    data = item['sourceDocument'][str(app)]['data']
    data['movies'] = [{'name': 'Trailer A', 'hls_h264': item['master']},
                      {'name': 'Trailer B', 'hls_h264': alternate}]
    item['selectedVideo'] = {
        'appId': app,
        'url': alternate if mode == 'selected-second' else item['master']
        if mode == 'new-choice-after-queue' else alternate + '?expired'}
    if mode == 'encoded-official-choice':
        canonical = alternate + '?a=1&b=2'
        profile['routes'][canonical] = copy.deepcopy(profile['routes'][alternate])
        data['movies'][1]['hls_h264'] = canonical.replace('&', '&amp;')
        item['selectedVideo'] = {'appId': app, 'url': data['movies'][1]['hls_h264']}
    if mode == 'selected-after-default-cap':
        # Six ordinary URLs per movie used to exhaust the worker's 16-source
        # default fallback budget before reaching an authenticated later choice.
        defaults = []
        for index in range(4):
            base = f'https://video.steamstatic.com/store_trailers/{app}/default-{index}/'
            defaults.append({'hls_h264': base + 'master.m3u8',
                             'mp4': {quality: base + quality + '.mp4'
                                     for quality in ('720', '480', '1080', 'max')}})
        data['movies'] = defaults + [data['movies'][1]]
        item['selectedVideo'] = {'appId': app, 'url': alternate}
    if mode == 'selected-after-transient':
        item['selectedVideo'] = {'appId': app, 'url': alternate}
    if mode == 'new-choice-after-queue':
        item['selectionAfterQueue'] = {'appId': app, 'url': alternate}
    if mode == 'static-only-expired-choice':
        item['staticOnly'] = True
    return profile


def check_legacy(check, value, item, paths, target):
    app = item['appId']
    key = 'steam-' + str(app)
    entry = value['entries'][str(app)]
    trace = value['legacySelection']
    before = trace['boundBefore']
    after = trace['boundAfter']
    queued = trace['queueBeforeWorker']
    mode = value['case']
    check(not trace['identityBefore'].get('verified', False),
          'legacy EXE cache without match remains an identity miss')
    check(trace['legacyEnqueueJobs'] == {},
          'ordinary EXE Queue admission rejects legacy cache despite numeric AppID')
    check(trace['officialMatchControl'], 'offline negative uses an otherwise valid official match shape')
    check(not trace['offlineIdentity'].get('verified', False) and trace['offlineEnqueueJobs'] == {},
          'offline provenance cannot become official identity or enter the ordinary queue')
    check(before.get('identityVerified') and before.get('key') == key,
          'media entry is already bound to the official Nobu AppID')
    check(after.get('key') == key and after.get('appId') == app and
          after.get('executable') == before.get('executable'),
          'worker preserves AppID and EXE alias binding')
    check(not trace['identityAfter'].get('verified', False) and trace['identityCacheUnchanged'],
          'explicit media selection never promotes or rewrites the legacy EXE identity')
    cache = pathlib.Path(trace['identityCache']).resolve()
    fixture_exe = pathlib.Path(trace['executable']).resolve()
    owned_target = pathlib.Path('\\\\?\\' + str(target)) if str(cache).startswith('\\\\?\\') else target
    check(cache.is_relative_to(owned_target) and fixture_exe.is_relative_to(owned_target),
          'identity cache and inert EXE sentinel are owned by this fresh case root')
    check(sha256(cache) == sha256(target / 'legacy-identity-before.json'),
          'legacy identity cache bytes retain the exact before SHA256')
    check(sha256(fixture_exe) == hashlib.sha256(EXE_SENTINEL).hexdigest(),
          'isolated Nobu EXE sentinel retains its exact body SHA256')
    check(value['jobs'].get('steam-570') == trace['unrelatedBefore'],
          'scoped manual work leaves the unrelated queued game untouched')
    old = trace['oldAsset']
    check(sha256(pathlib.Path(trace['oldAssetPath'])) == hashlib.sha256(OLD_VIDEO).hexdigest(),
          'old published wallpaper remains intact while its binding is replaced or rejected')
    metadata = [p for p in paths if p.startswith('/api/appdetails?')]
    if mode == 'legacy-nobu-wrong-appid':
        check(not trace['accepted'] and bool(trace.get('selectionError')),
              'production helper rejects a request AppID conflicting with the bound entry')
        check(not entry.get('selectedVideo') and entry['assets']['dynamic']['id'] == old['id'] and
              entry['bindingRevision'] == before['bindingRevision'] + 1,
              'wrong AppID preserves old asset, preference and binding revision after old publication')
        check(key not in queued['jobs'] and not queued.get('manualTargets') and not paths,
              'wrong AppID creates no Nobu job, manual scope or network request')
        return
    selected = trace['selectedEntry']['selectedVideo']
    job = queued['jobs'][key]
    check(trace['accepted'] and selected.get('selectionToken') == trace['request']['searchToken'],
          'production helper reads the root catalog and persists its exact selection token')
    check(job.get('selectedVideo') == selected and not job.get('executable') and
          job.get('catalogSource') == 'manual-video-selection' and
          job.get('requestBindingRevision') == trace['selectedEntry']['bindingRevision'],
          'helper admits the exact durable preference as an AppID-only media target')
    check(queued.get('manualTargets') == [key] and queued.get('manualRequestId') and
          queued.get('manualTargetRevisions', {}).get(key) == job['requestBindingRevision'],
          'manual request is scoped to this Nobu binding revision')
    check(len(metadata) == 1 and ('appids=' + str(app)) in metadata[0],
          'Worker fetches fresh Nobu appdetails over real WinHTTP after catalog selection')
    check(not any('appids=570' in p or '/store_trailers/570/' in p for p in paths),
          'worker never requests unrelated game metadata or a cross-game video URL')
    if mode == 'legacy-nobu-cross-game-url':
        check(value['jobs'][key].get('lastError') == 'media-video-selection-expired',
              'fresh official metadata rejects the cross-game cached choice explicitly')
        check(entry['assets']['dynamic']['id'] == old['id'] and
              not any('/store_trailers/' in p for p in paths),
              'rejected cross-game choice keeps the old asset and downloads no fallback video')
    else:
        check(entry.get('dynamicReady') and entry['assets']['dynamic']['id'] != old['id'] and
              value['jobs'][key].get('state') == 'done',
              'real HLS download replaces the old dynamic asset and completes the scoped job')
        check(pathkey(item['master']) in paths and pathkey(item['variant']) in paths and
              all(pathkey(segment) in paths for segment in item['segments']),
              'selected captured Nobu HLS URL, variant, init and every media segment are consumed')
        asset = entry['assets']['dynamic']
        published = target / 'artwork/video' / (asset['id'] + '.' + asset['extension'])
        expected = INIT + b''.join(payload(app, n) for n in range(1, len(item['segments'])))
        check(sha256(published) == hashlib.sha256(expected).hexdigest(),
              'published wallpaper body has the exact per-Nobu synthetic HLS SHA256')
        check(entry.get('selectedVideo') == selected and after.get('selectedVideo') == selected,
              'published media and EXE alias retain the confirmed durable video preference')
    check(value['state'].get('activeCount', 0) == 0,
          'bounded manual completion or rejection releases all active download lanes')


def run_case(exe, root, mode, profile0):
    started = time.monotonic()
    target = root / mode
    checks = []
    row = {'case': mode, 'ok': False, 'checks': checks}
    fixture = None

    def check(value, label):
        checks.append({'ok': bool(value), 'label': label})

    try:
        profile = prepare_profile(profile0, mode)
        item = profile['items'][0]
        app = item['appId']
        source = root / (mode + '.json')
        write_json(source, profile)
        row['profileSha256'] = sha256(source)
        fixture = Fixture('transient20' if mode == 'selected-after-transient' else 'normal20', profile)
        fixture.start()
        env = os.environ.copy()
        env['YMCC_POPULAR_TEST_PROFILE'] = str(source)
        proc = subprocess.run(
            [str(exe), str(target), str(fixture.server.server_port),
             mode if mode in LEGACY_CASES else 'normal20'],
            env=env, capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW,
            timeout=CASE_TIMEOUT)
        target.mkdir(parents=True, exist_ok=True)
        (target / 'stdout.json').write_bytes(proc.stdout)
        (target / 'stderr.txt').write_bytes(proc.stderr)
        row['exitCode'] = proc.returncode
        if proc.returncode:
            raise RuntimeError(proc.stderr.decode('utf-8', errors='replace'))
        value = json.loads(proc.stdout)
        entry = value['entries'][str(app)]
        paths = [r['path'] for r in fixture.logs]
        if mode in LEGACY_CASES:
            check_legacy(check, value, item, paths, target)
        elif mode in ('selected-second', 'new-choice-after-queue', 'encoded-official-choice') + EXTRA_CASES:
            check(entry.get('dynamicReady'), 'chosen trailer reaches published cache')
            check(value['jobs']['steam-' + str(app)].get('state') == 'done',
                  'selected transfer has a real completed queue receipt')
            if mode == 'selected-after-transient':
                check(fixture.calls.get(app, 0) >= 2,
                      'transient official metadata failure is retried within the same worker')
            check(any('/selected/master.m3u8' in p for p in paths),
                  'download requests current persisted chosen trailer')
            check(not any(p == item['master'].split('.com', 1)[-1] for p in paths),
                  'stale queued/default trailer is never requested')
        elif mode == 'static-only-expired-choice':
            check(entry.get('staticReady'), 'expired video choice cannot block static-only completion')
            check(entry.get('dynamicReady'), 'existing dynamic asset remains available')
            check(not any('/store_trailers/' in p for p in paths),
                  'static-only work never downloads a video')
            check(entry.get('selectedVideo', {}).get('url') == item['selectedVideo']['url'],
                  'static-only work preserves the independent video preference')
        else:
            check(not entry.get('dynamicReady'), 'expired selection never downloads another trailer silently')
            check(value['jobs']['steam-' + str(app)].get('lastError') == 'media-video-selection-expired',
                  'expired candidate gets explicit retry reason')
        check(value['state'].get('workerPid', 0) == 0,
              'all completion/rejection paths release worker ownership')
        row['ok'] = all(c['ok'] for c in checks)
    except Exception as error:
        row['error'] = str(error)
    finally:
        if fixture is not None:
            fixture.close()
        target.mkdir(parents=True, exist_ok=True)
        write_json(target / 'http.json', fixture.logs if fixture else [])
        row['seconds'] = round(time.monotonic() - started, 3)
        row['requests'] = len(fixture.logs) if fixture else 0
        write_json(target / 'assertions.json', row)
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=pathlib.Path,
                    default=TASK / 'implementation/reverse-chain-r65/worker.exe')
    ap.add_argument('--owned-evidence-root', type=pathlib.Path, default=TASK,
                    help='Owned parent directory; output must be a fresh strict descendant')
    ap.add_argument('--output', type=pathlib.Path)
    ap.add_argument('--profile', type=pathlib.Path, default=DEFAULT_PROFILE,
                    help='Replay source profile; defaults to the existing popular-games profile')
    ap.add_argument('--case', action='append', choices=ORIGINAL_CASES + LEGACY_CASES + EXTRA_CASES)
    args = ap.parse_args()
    owned = args.owned_evidence_root.resolve()
    root = (args.output or (owned / 'implementation/reverse-chain-r65/selection-replay'
                           if owned == TASK else owned / 'selection-replay')).resolve()
    exe = args.exe.resolve()
    profile_path = args.profile.resolve()
    if (not root.is_relative_to(owned) or root == owned or root.exists() or
            not exe.is_file() or not profile_path.is_file()):
        raise ValueError('fresh output below owned-evidence-root, existing profile and compiled fixture required')
    cases = args.case or list(ORIGINAL_CASES)
    if len(cases) != len(set(cases)):
        raise ValueError('duplicate replay cases are not bounded distinct work')
    profile0 = json.loads(profile_path.read_text(encoding='utf-8'))
    if not profile0.get('items') or not isinstance(profile0.get('routes'), dict):
        raise ValueError('replay profile requires items and routes')
    # Validate all requested modes before starting a worker or creating evidence.
    for mode in cases:
        prepare_profile(profile0, mode)
    root.mkdir(parents=True)
    started = time.monotonic()
    repo = pathlib.Path(__file__).resolve().parents[1]
    inputs = [pathlib.Path(__file__).resolve(),
              repo / 'tools/shared_game_media_popular_games_selftest.py',
              repo / 'tools/shared_game_media_popular_games_selftest.cpp',
              repo / 'native/shared_game_media_video_request.h',
              repo / 'native/custom-steam-library/custom_steam_library_network_policy.h',
              repo / 'native/shared_game_media.h',
              repo / 'native/shared_game_media_download_queue.h',
              repo / 'native/shared_game_media_download_worker.h',
              repo / 'native/shared_game_media_download_supervisor.h',
              repo / 'native/shared_game_media_shortcut.h',
              repo / 'native/shared_game_media_capacity.h',
              repo / 'native/custom-steam-library/custom_steam_library_identity_cache.h',
              repo / 'native/custom-steam-library/custom_steam_library_video_catalog.h']
    identity = {str(p): sha256(p) for p in inputs}
    artifacts = {str(p): sha256(p) for p in (exe, profile_path)}
    reports = []
    for mode in cases:
        row = run_case(exe, root, mode, profile0)
        reports.append(row)
        print(json.dumps({'case': mode, 'ok': row['ok'],
                          'failed': [c['label'] for c in row['checks'] if not c['ok']],
                          'error': row.get('error', '')}), flush=True)
    drift = [str(p) for p in inputs if sha256(p) != identity[str(p)]]
    artifact_drift = [str(p) for p in (exe, profile_path) if sha256(p) != artifacts[str(p)]]
    report = {
        'ok': all(r['ok'] for r in reports) and not drift and not artifact_drift,
        'checks': sum(len(r['checks']) for r in reports), 'results': reports,
        'seconds': round(time.monotonic() - started, 3),
        'ownedEvidenceRoot': str(owned), 'profile': str(profile_path),
        'profileSha256': artifacts[str(profile_path)], 'exe': str(exe), 'exeSha256': artifacts[str(exe)],
        'python': os.sys.executable, 'pythonSha256': sha256(pathlib.Path(os.sys.executable)),
        'sourceInputs': identity, 'sourceDrift': drift, 'artifactDrift': artifact_drift,
        'legacySelectionExecuted': any(mode in LEGACY_CASES for mode in cases),
        'scope': 'Production Store/Queue/Worker and real WinHTTP, with original video-choice assertions. '
                 'Legacy cases additionally execute the production catalog and selection helper. '
                 'Network destinations/source documents and synthetic media use the existing loopback fixture; '
                 'its existing AC power precondition is retained.',
        'notCovered': ['Live Steam/Nobu video network downloads',
                       'Installed app, real game executable, real configuration or real UI actions',
                       'Host IPC integration, physical device acceptance and full fixture suite',
                       'Supervisor restart/backoff and helper native admission fault matrix'],
        'registeredNewFixtureIds': False, 'sharedCoreRunBudgetClaimed': False,
        'caseTimeoutSeconds': CASE_TIMEOUT,
    }
    write_json(root / 'results.json', report)
    print(json.dumps({k: report[k] for k in ('ok', 'checks', 'seconds')}))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
