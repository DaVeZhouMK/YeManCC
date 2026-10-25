"""Offline negatives for deduplication and observational (not causal) claims."""
import importlib.util
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

path = Path(__file__).with_name('msi_self_disruptive_rearm_replay.py')
spec = importlib.util.spec_from_file_location('recovery_observation', path)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def row(seconds, event, **extra):
    return dict(time=f'2026-10-12T21:00:{seconds:02d}.000',event=event,pid=91,powerGeneration=1,**extra)


def base():
    return [row(1,'boot-single-instance-acquired'),row(2,'input-host-started',epoch=4),
            row(3,'msi-dinput-transition',phase='input-lost',lastDisruptiveTxKind='switch-dinput',lastDisruptiveTxAgeMs=20),
            row(5,'msi-claw-dinput-route',phase='mode-resent',trigger='first-window-timeout')]


def wrap(rows):
    return [dict(id=audit.fingerprint(r),row=r,sources=[]) for r in sorted(rows,key=lambda r:r['time'])]


class Observation(unittest.TestCase):
    def test_overlap_and_rotation(self):
        with tempfile.TemporaryDirectory() as temp:
            paths=[]
            for i in range(2):
                path=Path(temp)/f'{i}.zip'
                with zipfile.ZipFile(path,'w') as z:
                    data='\n'.join(json.dumps(r) for r in base())+'\n'
                    z.writestr('native-lifecycle.log',data)
                    z.writestr('native-lifecycle.log.1',data)
                paths.append(path)
            events,archives,issues=audit.collect(paths)
            self.assertEqual(len(events),4)
            self.assertEqual(sum(a['parsedRows'] for a in archives),16)
            self.assertEqual(len(events[0]['sources']),4)
            self.assertFalse(issues)
            self.assertEqual(audit.audit(events)['counts']['uniqueModeResents'],1)

    def test_recent_is_only_correlation(self):
        r=audit.audit(wrap(base()))
        self.assertEqual(r['counts']['recentOwnedLabelBeforeResend'],1)
        self.assertEqual(r['pairs'][0]['attribution'],'temporal-correlation-only')
        self.assertNotIn('would_suppress_mode_resend',r['counts'])

    def test_old_label_is_not_recent(self):
        rows=base();rows[2]['lastDisruptiveTxAgeMs']=3_600_000
        r=audit.audit(wrap(rows))
        self.assertEqual(r['counts']['staleOwnedLabelBeforeResend'],1)
        self.assertNotIn('recentOwnedLabelBeforeResend',r['counts'])

    def test_pid_reuse_boot_boundary(self):
        rows=base();rows.insert(3,row(4,'boot-single-instance-acquired'))
        self.assertNotIn('resendWithin10sOfSameContextLoss',audit.audit(wrap(rows))['counts'])

    def test_host_generation(self):
        rows=base();rows.insert(3,row(4,'input-host-started',epoch=5))
        self.assertNotIn('resendWithin10sOfSameContextLoss',audit.audit(wrap(rows))['counts'])

    def test_power_generation(self):
        rows=base();rows[3]['powerGeneration']=2
        self.assertNotIn('resendWithin10sOfSameContextLoss',audit.audit(wrap(rows))['counts'])

    def test_stop_cancels_previous_loss(self):
        rows=base();rows.insert(3,row(4,'input-host-stop-request',reason='virtual-target-disabled'))
        self.assertNotIn('resendWithin10sOfSameContextLoss',audit.audit(wrap(rows))['counts'])

    def test_new_route_cancels_previous_loss(self):
        rows=base();rows.insert(3,row(4,'msi-claw-dinput-route',phase='armed'))
        self.assertNotIn('resendWithin10sOfSameContextLoss',audit.audit(wrap(rows))['counts'])

    def test_unbound_host_is_not_inferred_from_pid(self):
        rows=base();del rows[1]
        self.assertNotIn('resendWithin10sOfSameContextLoss',audit.audit(wrap(rows))['counts'])

    def test_outside_recovery_window(self):
        rows=base();rows[-1]['time']='2026-10-12T21:00:30.000'
        self.assertNotIn('resendWithin10sOfSameContextLoss',audit.audit(wrap(rows))['counts'])

    def test_missing_age_not_fabricated_zero(self):
        rows=base();del rows[2]['lastDisruptiveTxAgeMs']
        r=audit.audit(wrap(rows))
        self.assertEqual(r['counts']['ownedLabelOlderThanWindowOrUnknown'],1)
        self.assertNotIn('recentOwnedLabelBeforeResend',r['counts'])

    def test_parse_errors_are_accounted(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'bad.zip'
            with zipfile.ZipFile(p,'w') as z:z.writestr('native-lifecycle.log','{truncated\n'+json.dumps(base()[0]))
            events,_,issues=audit.collect([p])
            self.assertEqual(len(events),1)
            self.assertEqual(issues[0]['reason'],'malformed-json')


if __name__=='__main__':unittest.main(verbosity=2)
