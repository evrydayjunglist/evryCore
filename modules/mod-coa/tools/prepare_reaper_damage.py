"""Capture installed Reaper state and prepare a separate reversible damage package."""
import argparse
import copy
import json
from pathlib import Path
import shutil
import sys


def prepare(previous, candidate, output, effects_tools):
    sys.path.insert(0, str(effects_tools))
    import deploy
    import fidelity
    import records
    import reaper_damage_deploy as damage
    if output.exists():
        raise ValueError('Choose a new package folder; prepared packages are immutable')
    old = json.loads((previous/'package.json').read_text())
    fidelity.inspect_files(previous, old)
    build = json.loads((candidate/'build.json').read_text())
    if build['installedSha256'] != deploy.digest(Path(old['runtime'])/'worldserver.exe'):
        raise ValueError('The installed server changed since candidate compilation')
    m = dict(format=1, build=69814, kind='reaper-damage', client=old['client'], runtime=old['runtime'],
        databaseSchema=old['databaseSchema'], schemas=copy.deepcopy(old['schemas']),
        protectedRows=copy.deepcopy(old['protectedRows']), rangeGuards=[], changes=[],
        prerequisite=dict(path=str(previous), sha256=deploy.digest(previous/'package.json')),
        scope='Remove obsolete magic resistance for all classes. Convert only Murder and Soulrend to attack-power scaling. Preserve prior visuals, timings, costs and soul lifecycle.')
    for change in old['changes']:
        m['protectedRows'].setdefault(change['table'], []).append(copy.deepcopy(change['after']))

    def encoded(row):
        return {k:({'hex':v.hex()} if isinstance(v, bytes) else v) for k,v in row.items()}

    with deploy.database(old, True) as db:
        state = fidelity.inspect(db, old, fidelity.load_journal(previous))
        if state['rows'] != 'installed':
            raise ValueError('Visual fidelity package-05 must be installed')
        with db.cursor() as cursor:
            tables = ('spell_name', 'spell_effect', 'spell_levels', 'spell_scaling', 'spell_misc',
                'spell_power', 'spell_aura_options', 'spell_aura_restrictions', 'spell_categories',
                'spell_cooldowns', 'spell_class_options', 'spell_equipped_items',
                'spell_target_restrictions', 'spell_x_spell_visual', 'hotfix_blob')
            for table in tables:
                cursor.execute('SHOW CREATE TABLE `' + table + '`')
                m['schemas'][table] = cursor.fetchone()['Create Table']
                where, args = ('ID BETWEEN %s AND %s', [600000, 600012]) if table == 'spell_name' else (
                    ('TableHash=%s AND RecordId BETWEEN %s AND %s', [damage.SPELL_HASH,600000,600012])
                    if table == 'hotfix_blob' else ('SpellID BETWEEN %s AND %s', [600000,600012]))
                cursor.execute('SELECT * FROM `' + table + '` WHERE ' + where, args)
                rows = m['protectedRows'].setdefault(table, [])
                for raw in cursor.fetchall():
                    row = encoded(raw)
                    if not any(records.equal(row, r) for r in rows):
                        rows.append(row)
                m['rangeGuards'].append(dict(table=table, where=where, args=args))
            specs = [('spell_effect',2890,damage.EFFECT_HASH), ('spell_effect',2893,damage.EFFECT_HASH),
                     ('spell_levels',13577,damage.LEVEL_HASH), ('spell_levels',13579,damage.LEVEL_HASH),
                     ('hotfix_blob',600001,damage.SPELL_HASH), ('hotfix_blob',600003,damage.SPELL_HASH)]
            for table, record, table_hash in specs:
                rows = m['protectedRows'][table]
                before = next(r for r in rows if (r.get('ID') == record if table != 'hotfix_blob'
                    else r['RecordId'] == record and r['TableHash'] == table_hash))
                rows.remove(before)
                m['changes'].append(dict(table=table, tableHash=table_hash, before=before,
                    after=dict(before, **damage.expected_changes(before, table))))
            pairs = {(r['TableHash'],r['RecordId']) for r in old['protectedPushes']+state['history']}
            pairs.update((h,i) for _,i,h in specs)
            # Preserve every existing push for the Reaper combat records as well.
            cursor.execute('SELECT * FROM hotfix_data WHERE RecordId BETWEEN 600000 AND 600012')
            pairs.update((r['TableHash'],r['RecordId']) for r in cursor.fetchall())
            cursor.execute('SELECT * FROM hotfix_data WHERE '+ ' OR '.join('(TableHash=%s AND RecordId=%s)' for _ in pairs), [v for p in sorted(pairs) for v in p])
            m['protectedPushes'] = list(cursor.fetchall())
        damage.inspect(db, m)
    protected = {p['path']:p['sha256'] for p in old['protected']}
    for name, sha in old['packageFiles'].items():
        protected[str(previous/name)] = sha
    protected[str(previous/'package.json')] = deploy.digest(previous/'package.json')
    for f in old['files']:
        path = deploy.inside(old['client'], f['target'])
        if deploy.digest(path) != f['after']:
            raise ValueError('Previous effects are not fully installed')
        protected[str(path)] = f['after']
    owned_paths = {(Path(old['runtime'])/n).resolve() for n in damage.OWNED_FILES}
    m['protected'] = [dict(path=p, sha256=sha) for p,sha in sorted(protected.items()) if Path(p).resolve() not in owned_paths]
    m['otherMappings'] = damage.mapping_snapshot(Path(old['client']))
    output.mkdir(parents=True)
    m['files'] = []
    for name in damage.OWNED_FILES:
        target = Path(old['runtime'])/name
        if deploy.digest(candidate/name) != build['files'][name]:
            raise ValueError('Candidate build payload changed')
        payload, backup = 'payload/'+name, 'backup/'+name
        for source, relative in ((candidate/name,payload),(target,backup)):
            dest = output/relative
            dest.parent.mkdir(exist_ok=True)
            shutil.copyfile(source,dest)
        m['files'].append(dict(target=name,payload=payload,backup=backup,
            before=deploy.digest(output/backup),after=deploy.digest(output/payload)))
    for install,name in ((True,'forward.sql'),(False,'restore.sql')):
        (output/name).write_text(damage.sql(m,install))
    for name in ('deploy.py','records.py'):
        shutil.copyfile(effects_tools/name,output/name)
    shutil.copyfile(Path(__file__).with_name('reaper_damage_deploy.py'),output/'reaper_damage_deploy.py')
    shutil.copyfile(Path(__file__).with_name('REAPER_DAMAGE_OWNER_TEST.md'),output/'OWNER_TEST.md')
    (output/'Install.ps1').write_text('''param([ValidateSet('Check','Install','Restore')][string]$Mode = 'Check')
$ErrorActionPreference = 'Stop'
python -B "$PSScriptRoot\\reaper_damage_deploy.py" --package $PSScriptRoot --mode $Mode.ToLowerInvariant()
if ($LASTEXITCODE -ne 0) { throw 'Damage operation failed. After a failed write, keep the runtime closed and use this package for Check, retry Install or Restore.' }
''')
    m['sourceHashes'] = {str(p):deploy.digest(p) for p in [Path(__file__), Path(damage.__file__), candidate/'build.json']}
    m['packageFiles'] = {str(p.relative_to(output)).replace('\\','/'):deploy.digest(p) for p in output.rglob('*') if p.is_file()}
    deploy.save(output/'package.json',m)
    damage.inspect_files(output,m)
    with deploy.database(m,True) as db:
        damage.inspect(db,m)
    print(json.dumps(dict(package=str(output),sha256=deploy.digest(output/'package.json'),records=len(m['changes']),files=len(m['files'])),indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('previous','candidate','out','effects-tools'):
        parser.add_argument('--'+option,required=True,type=Path)
    args=parser.parse_args()
    prepare(args.previous.resolve(),args.candidate.resolve(),args.out.resolve(),args.effects_tools.resolve())
