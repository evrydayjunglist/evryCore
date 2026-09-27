"""Extend the immutable first-delivery graph with the repaired Infusion chain."""
import argparse
import copy
import json
from pathlib import Path

from generate_reaper_effects import ATTACHMENT, blank, digest, encode


def generate(root, infusion, out):
    graph = json.loads((root / 'records-v3.json').read_text())
    source = json.loads((root / 'recovered/manifest.json').read_text())
    native = json.loads((root / 'retail/tables.json').read_text())['tables']
    database = json.loads((root / 'database.json').read_text())
    repaired = json.loads((infusion / 'manifest.json').read_text())
    if repaired['model']['effectID'] != 100115 or len(repaired['repair']['edits']) != 2:
        raise ValueError('Expected the bounded Infusion reconstruction')
    result = copy.deepcopy(graph)
    occupied = {n: set(t['ids']) for n, t in native.items()}
    for record in graph['records']:
        occupied[record['table']].add(record['id'])
    for name, table in native.items():
        for row in database['pushes'] + database['blobs']:
            if row['TableHash'] == table['tableHash']:
                occupied[name].add(row['RecordId'])
    for table, details in database['tables'].items():
        for name in native:
            if ''.join('_' + c.lower() if c.isupper() else c for c in name).lstrip('_') == table:
                occupied[name].update(details['ids'])
    def add(table, source_id, fields, short=False):
        used = occupied[table]
        row_id = next(i for i in range(65000, 1, -1) if i not in used) if short else max(used) + 1
        used.add(row_id)
        row = blank(native[table]['meta'])
        if set(fields) - set(row):
            raise ValueError('Unexpected fields: ' + table)
        row.update(fields, ID=row_id)
        result['records'].append(dict(table=table, tableHash=native[table]['tableHash'],
            layoutHash=native[table]['meta']['layoutHash'], id=row_id, source=source_id,
            fields=row, blob=encode(native[table]['meta'], row).hex(),
            storage={'SpellVisual': 'spell_visual', 'SpellVisualKit': 'spell_visual_kit',
                     'SpellVisualEffectName': 'spell_visual_effect_name'}.get(table, 'hotfix_blob')))
        result['ownership'][table + ':' + str(source_id)] = row_id
        return row_id
    effect = source['effects']['100115']
    effect_id = add('SpellVisualEffectName', 100115, dict(ModelFileDataID=repaired['model']['fileDataID'],
        BaseMissileSpeed=0, Scale=effect['Scale'], MinAllowedScale=effect['MinAllowedScale'],
        MaxAllowedScale=effect['MaxAllowedScale'], Alpha=1, EffectRadius=effect['AreaEffectSize'], ModelPosition=-1), True)
    kit = source['kits']['1000298']
    if kit['BaseEffect'] != 100115 or kit['StartAnimID'] != -1 or kit['AnimID'] != -1:
        raise ValueError('Infusion state kit changed')
    kit_id = add('SpellVisualKit', 1000298, dict(Flags1=kit['Flags']))
    sound = next(r for r in graph['records'] if r['table'] == 'SoundKit' and r['source'] == kit['SoundID'])
    add('SpellVisualKitEffect', '1000298/sound', dict(EffectType=5, Effect=sound['id'], ParentSpellVisualKitID=kit_id))
    attach = add('SpellVisualKitModelAttach', '1000298/BaseEffect', dict(SpellVisualEffectNameID=effect_id,
        AttachmentID=19, Scale=1, StartAnimID=-1, AnimID=-1, EndAnimID=-1, ParentSpellVisualKitID=kit_id))
    add('SpellVisualKitEffect', '1000298/BaseEffect', dict(EffectType=2, Effect=attach, ParentSpellVisualKitID=kit_id))
    visual = source['visuals']['1000091']
    values = dict(Flags=visual['Flags'], StateKit=0, MissileAttachment=ATTACHMENT[visual['MissileAttachment']],
                  MissileDestinationAttachment=ATTACHMENT[visual['MissileDestinationAttachment']])
    for stem in ('MissileCastOffset', 'MissileImpactOffset'):
        values.update({stem + str(i + 1): v for i, v in enumerate(visual[stem])})
    visual_id = add('SpellVisual', 1000091, values)
    add('SpellVisualEvent', '1000091/StateKit/2', dict(StartEvent=7, EndEvent=8, TargetType=2,
        SpellVisualKitID=kit_id, SpellVisualID=visual_id))
    # A new aura binding uses the next unoccupied gap inside the native ID range.
    binding_id = next(i for i in range(7820, max(native['SpellXSpellVisual']['ids']))
                      if i not in occupied['SpellXSpellVisual'])
    binding = dict(ID=binding_id, DifficultyID=0, SpellVisualID=visual_id, Probability=1.0,
        Flags=0, Priority=0, SpellIconFileID=0, ActiveIconFileID=0, ViewerUnitConditionID=0,
        ViewerPlayerConditionID=0, CasterUnitConditionID=0, CasterPlayerConditionID=0,
        SpellID=600007, VerifiedBuild=69814)
    chain = next(c for c in result['chains'] if c['spellID'] == 600007)
    chain.update(visualID=visual_id, bindingBefore=None, bindingAfter=binding,
                 status='generated with two native-reference track repairs; game test pending')
    if result['records'][:len(graph['records'])] != graph['records'] or result['chains'][:4] != graph['chains'][:4]:
        raise ValueError('Infusion extension changed a previous chain')
    result['infusionReconstruction'] = dict(manifest=str(infusion / 'manifest.json'),
        sha256=digest(infusion / 'manifest.json'), **repaired['repair'])
    result['note'] = 'All five chains generated. Remaining-effects package excludes the installed Murder closure.'
    data = (json.dumps(result, indent=2) + '\n').encode()
    if out.exists() and out.read_bytes() != data:
        raise ValueError('Refusing to replace different generated records')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    print(json.dumps(dict(records=len(result['records']), infusionVisual=visual_id,
                          infusionBinding=binding_id, originalRecordsUnchanged=True), indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--infusion', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    generate(args.root, args.infusion, args.out)
