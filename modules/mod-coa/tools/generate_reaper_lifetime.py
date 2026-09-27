"""Generate the owner-selected soul lifetime correction over the frozen combat data.

The original combat manifest/migration remain reproducible installation history.
Apply this overlay after them through the separate guarded lifetime package.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def generate():
    baseline = ROOT / 'data/reaper-69814.json'
    manifest = json.loads(baseline.read_text())
    data = manifest['databases']['hotfixes']
    before = next(row for row in data['spell_duration'] if row['ID'] == 100)
    expected = dict(ID=100, Duration=300000010, MaxDuration=300000010,
                    DurationPerResource=0, VerifiedBuild=69814)
    if before != expected:
        raise ValueError('The historical Reaper duration baseline changed')
    references = [row for row in data['spell_misc'] if row['DurationIndex'] == 100]
    if sorted(row['SpellID'] for row in references) != [600006, 600007]:
        raise ValueError('Duration 100 must belong only to Reaped Soul and Soul Infusion')
    after = dict(before, Duration=-1, MaxDuration=-1)
    return dict(
        format=1, build=69814, kind='reaper-resource-lifetime',
        table='spell_duration', tableHash=manifest['tableHashes']['spell_duration'],
        before=before, after=after, references=references,
        fragment=next(row for row in data['spell_misc'] if row['SpellID'] == 600005),
        originalPush=[row for row in data['hotfix_data']
                      if row['TableHash'] == manifest['tableHashes']['spell_duration']
                      and row['RecordId'] == 100],
        baseline=dict(path=str(baseline), sha256=hashlib.sha256(baseline.read_bytes()).hexdigest()),
        behavior={
            'soulsAndInfusion': 'No timed expiry; consumed by Soulrend or cleared on death/reconnect.',
            'fragments': 'Unchanged: 30 seconds, refreshed only by fragment gain.',
            'stackLimits': {'fragments': 2, 'souls': 3, 'infusion': 1},
            'reload': 'Retains server resource state.',
            'selection': 'Owner explicitly selected this lifetime on 26 September 2026.',
            'sourceDifference': 'CoA duration 300000010 ms is replaced, not reinterpreted as 30 seconds.',
        })


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'data/reaper-lifetime-69814.json')
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(generate(), indent=2) + '\n', encoding='utf8', newline='\n')
    print('Generated one duration correction for exactly two resource auras.')
