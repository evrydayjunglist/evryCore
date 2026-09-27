"""Guarded retail resistance and Reaper damage correction over installed visual package-05."""
import argparse
import json
from pathlib import Path

import deploy
import records

OWNED_FILES = ('worldserver.exe', 'worldserver.pdb')
SPELL_HASH = 3776013982
EFFECT_HASH = 4030871717
LEVEL_HASH = 501138918
DESCRIPTIONS = {
    600001: 'Deal Shadow damage equal to 120% of attack power, with up to 1 additional damage. With Soul Collector, a hit grants a Reaped Soul. Reduces melee hit chance by 3% for 10 seconds. Costs 30 Runic Power.',
    600003: 'Requires Soul Infusion. Deal Shadow damage equal to 180% of attack power and 29.7% of Shadow spell power, with up to 1 additional damage. Consumes all souls, fragments and infusion. A hit generates 15 Runic Power. Miss, dodge and parry preserve infusion.'}


def identity(change):
    return change['before']['RecordId' if change['table'] == 'hotfix_blob' else 'ID']


def amended_blob(row):
    fields = records.decode(row['Blob']).split(b'\0')
    if len(fields) != 4 or fields[-1] != b'':
        raise ValueError('Unexpected Spell string layout')
    fields[1] = DESCRIPTIONS[row['RecordId']].encode('utf8')
    return {'hex': b'\0'.join(fields).hex()}


def expected_changes(row, table):
    if table == 'spell_effect':
        sid = row['SpellID']
        return dict(EffectBasePoints=0.0, EffectRealPointsPerLevel=0.0,
                    BonusCoefficientFromAP=1.2 if sid == 600001 else 1.8)
    if table == 'spell_levels':
        return dict(MaxLevel=0)
    return dict(Blob=amended_blob(row))


def validate(m):
    if (m.get('format'), m.get('build'), m.get('kind')) != (1, 69814, 'reaper-damage'):
        raise ValueError('Unexpected damage package')
    expected = [('spell_effect', 2890, EFFECT_HASH, 600001),
                ('spell_effect', 2893, EFFECT_HASH, 600003),
                ('spell_levels', 13577, LEVEL_HASH, 600001),
                ('spell_levels', 13579, LEVEL_HASH, 600003),
                ('hotfix_blob', 600001, SPELL_HASH, 600001),
                ('hotfix_blob', 600003, SPELL_HASH, 600003)]
    if len(m['changes']) != len(expected):
        raise ValueError('Unexpected damage scope')
    for change, (table, row_id, table_hash, sid) in zip(m['changes'], expected):
        before, after = change['before'], change['after']
        if (change['table'], identity(change), change['tableHash'], before['VerifiedBuild']) != (table, row_id, table_hash, 69814):
            raise ValueError('Unexpected damage record')
        if table != 'hotfix_blob' and before['SpellID'] != sid:
            raise ValueError('Unexpected spell owner')
        if table == 'spell_effect':
            if (before['Effect'], before['EffectBasePoints'], before['EffectRealPointsPerLevel']) != (2, 13 if sid == 600001 else 17, 1 if sid == 600001 else 0):
                raise ValueError('Unexpected legacy damage baseline')
        if table == 'hotfix_blob' and (before['TableHash'] != SPELL_HASH or before['locale'] != 'enUS'):
            raise ValueError('Unexpected Spell blob')
        if not records.equal(dict(before, **expected_changes(before, table)), after):
            raise ValueError('Damage changes fields outside the approved scope')


def mapping_snapshot(client):
    return {str(p.relative_to(client)).replace('\\', '/'): deploy.digest(p)
            for p in sorted((Path(client) / 'mappings').rglob('*')) if p.is_file()}


def inspect_files(package, m):
    validate(m)
    for name, sha in m['packageFiles'].items():
        if deploy.digest(deploy.inside(package, name)) != sha:
            raise ValueError('Package file changed: ' + name)
    for p in m['protected']:
        if deploy.digest(p['path']) != p['sha256']:
            raise ValueError('Protected baseline changed: ' + p['path'])
    if mapping_snapshot(Path(m['client'])) != m['otherMappings']:
        raise ValueError('Other numeric mappings changed')
    if sorted(f['target'] for f in m['files']) != sorted(OWNED_FILES):
        raise ValueError('Only the server executable and its symbols are owned')
    for f in m['files']:
        if not f['before'] or not f['backup']:
            raise ValueError('A captured runtime backup is required')
        for key, sha in (('payload', f['after']), ('backup', f['before'])):
            if deploy.digest(deploy.inside(package, f[key])) != sha:
                raise ValueError('Damaged runtime payload or backup')
        if deploy.digest(deploy.inside(m['runtime'], f['target'])) not in (f['before'], f['after']):
            raise ValueError('Foreign runtime target')
    for install, name in ((True, 'forward.sql'), (False, 'restore.sql')):
        if (package / name).read_text() != sql(m, install):
            raise ValueError('Unexpected damage SQL')


def pushes(m, op):
    return [dict(Id=op['id'], UniqueId=op['unique'], TableHash=c['tableHash'],
                 RecordId=identity(c), Status=1, VerifiedBuild=69814) for c in m['changes']]


def inspect(db, m, journal=None, *, lock_rows=False):
    validate(m)
    locking = ' FOR UPDATE' if lock_rows else ''
    states = []
    with db.cursor() as cursor:
        for table, schema in m['schemas'].items():
            cursor.execute('SHOW CREATE TABLE `' + table + '`')
            if cursor.fetchone()['Create Table'] != schema:
                raise ValueError('Database schema changed: ' + table)
        def select(table, row):
            where, values = records.where(records.keys(table, row))
            if table in ('spell_misc', 'spell_x_spell_visual'):
                where, values = 'ID=%s OR SpellID=%s', (row['ID'], row['SpellID'])
            cursor.execute('SELECT * FROM `' + table + '` WHERE ' + where + locking, values)
            return list(cursor.fetchall())
        for table, rows in m['protectedRows'].items():
            for row in rows:
                found = select(table, row)
                if len(found) != 1 or not records.equal(found[0], row):
                    raise ValueError('Protected database record changed: ' + table)
        for change in m['changes']:
            found = select(change['table'], change['before'])
            if len(found) != 1:
                raise ValueError('Missing or duplicate damage record')
            if records.equal(found[0], change['before']):
                states.append('baseline')
            elif records.equal(found[0], change['after']):
                states.append('installed')
            else:
                raise ValueError('Foreign damage record')
        for guard in m['rangeGuards']:
            cursor.execute('SELECT * FROM `' + guard['table'] + '` WHERE ' + guard['where'] + locking, guard['args'])
            allowed = m['protectedRows'].get(guard['table'], []) + [r
                for c in m['changes'] if c['table'] == guard['table'] for r in (c['before'], c['after'])]
            if any(not any(records.equal(row, expected) for expected in allowed) for row in cursor.fetchall()):
                raise ValueError('Foreign Reaper record in ' + guard['table'])
        protected = m['protectedPushes']
        pairs = {(r['TableHash'], r['RecordId']) for r in protected}
        pairs.update((c['tableHash'], identity(c)) for c in m['changes'])
        conditions = ['(TableHash=%s AND RecordId=%s)' for _ in pairs]
        values = [v for pair in pairs for v in pair]
        operations = (journal or {}).get('operations', [])
        for op in operations:
            if any(op['id'] == row['Id'] or op['unique'] == row['UniqueId'] for row in protected):
                raise ValueError('Damage journal overlaps protected history')
            conditions.append('(Id=%s OR UniqueId=%s)')
            values.extend((op['id'], op['unique']))
        cursor.execute('SELECT * FROM hotfix_data WHERE ' + ' OR '.join(conditions) + locking, values)
        history = list(cursor.fetchall())
        if any(row not in history for row in protected):
            raise ValueError('Protected hotfix history changed')
        own = [r for r in history if r not in protected]
        allowed = [r for op in operations for r in pushes(m, op)]
        if any(row not in allowed for row in own):
            raise ValueError('Foreign damage push')
        if len({(r['Id'], r['UniqueId']) for r in own}) > 1 or (own and len(own) != len(m['changes'])):
            raise ValueError('Incomplete damage push')
        state = states[0] if len(set(states)) == 1 else 'partial'
        if own:
            op = next(op for op in operations if (op['id'], op['unique']) == (own[0]['Id'], own[0]['UniqueId']))
            if state != ('installed' if op['install'] else 'baseline'):
                raise ValueError('Damage push disagrees with row state')
        elif state != 'baseline' or (journal or {}).get('published'):
            raise ValueError('Damage push missing')
    return dict(rows=state, history=own)


def sql(m, install):
    lines = ['-- Use Install.ps1; the guarded installer owns the transaction and hotfix generation.']
    for c in m['changes']:
        row = c['after'] if install else c['before']
        key = records.keys(c['table'], row)
        lines.append('DELETE FROM `' + c['table'] + '` WHERE ' + ' AND '.join('`'+k+'`='+records.literal(v) for k,v in key.items()) + ';')
        lines.append(records.insert(c['table'], row))
    return '\n'.join(lines) + '\n'


def apply_files(package, m, install):
    inspect_files(package, m)
    for f in m['files']:
        target = deploy.inside(m['runtime'], f['target'])
        desired = f['after' if install else 'before']
        if deploy.digest(target) == desired:
            continue
        deploy.atomic_copy(deploy.inside(package, f['payload' if install else 'backup']), target)
        if deploy.digest(target) != desired:
            raise ValueError('Runtime file readback failed')


def apply(db, package, m, journal, op):
    inspect(db, m, journal, lock_rows=True)
    with db.cursor() as c:
        for line in sql(m, op['install']).splitlines():
            if line and not line.startswith('--'):
                c.execute(line.rstrip(';'))
        for previous in journal['operations']:
            c.execute('DELETE FROM hotfix_data WHERE Id=%s AND UniqueId=%s', (previous['id'], previous['unique']))
        for row in pushes(m, op):
            c.execute(records.insert('hotfix_data', row).rstrip(';'))


def load_journal(package):
    path = package / 'deployment.json'
    if not path.exists():
        return None
    journal = json.loads(path.read_text())
    if journal['packageSha256'] != deploy.digest(package / 'package.json'):
        raise ValueError('Damage journal belongs to a different package')
    return journal


def run(package, mode):
    package = package.resolve()
    m = json.loads((package / 'package.json').read_text())
    inspect_files(package, m)
    journal = load_journal(package)
    with deploy.database(m, True) as db:
        state = inspect(db, m, journal)
    processes = deploy.running()
    def complete(install, state):
        return (state['rows'] == ('installed' if install else 'baseline') and
                all(deploy.digest(deploy.inside(m['runtime'], f['target'])) == f['after' if install else 'before'] for f in m['files']))
    if mode == 'check':
        result = dict(installed=complete(True, state), baseline=complete(False, state), records=len(m['changes']),
            files=len(m['files']), database=state['rows'], running=processes,
            state=journal['state'] if journal else 'prepared', previousEffectsPreserved=True,
            soulLifetimePreserved=True, reapScaleChanged=False, gameplayAccepted=False)
        print(json.dumps(result, indent=2))
        return result
    if mode not in ('install', 'restore'):
        raise ValueError('Unknown damage operation')
    if processes:
        raise ValueError('Close WoW, worldserver and bnetserver normally before ' + mode)
    install = mode == 'install'
    if journal is None and (not install or not complete(False, state)):
        raise ValueError('First install requires the complete captured baseline')
    with deploy.lock(package), deploy.database(m) as db:
        with db.cursor() as c:
            c.execute("SELECT GET_LOCK('evry-reaper-effects-69814',0) AS acquired")
            if c.fetchone()['acquired'] != 1:
                raise ValueError('Another effects operation is active')
        journal = load_journal(package)
        if deploy.running():
            raise ValueError('Runtime started during damage preflight')
        inspect_files(package, m)
        state = inspect(db, m, journal, lock_rows=True)
        journal = journal or dict(packageSha256=deploy.digest(package / 'package.json'), operations=[])
        if complete(install, state):
            journal.update(state='installed' if install else 'restored', published=bool(state['history']))
            deploy.save(package / 'deployment.json', journal)
            print(journal['state'] + ' (already verified)')
            return
        op = deploy.new_operation(db, install)
        journal['operations'].append(op)
        journal['state'] = 'installing' if install else 'restoring'
        deploy.save(package / 'deployment.json', journal)
        apply_files(package, m, install)
        if deploy.running():
            raise ValueError('Runtime started before commit; close it normally and use this package for recovery')
        apply(db, package, m, journal, op)
        db.commit()
        with deploy.database(m, True) as verify:
            if not complete(install, inspect(verify, m, journal)):
                raise ValueError('Damage database readback failed')
        inspect_files(package, m)
        journal.update(state='installed' if install else 'restored', published=True)
        deploy.save(package / 'deployment.json', journal)
        print(journal['state'] + ': Retail resistance correction and Reaper damage verified.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--mode', choices=['check', 'install', 'restore'], default='check')
    args = parser.parse_args()
    run(args.package, args.mode)
