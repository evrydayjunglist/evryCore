"""Generate the bounded 69814 Reaper records from pinned, read-only evidence.

Input: retail-data.json and database-evidence.json captured by the owner package.
No connection to a server, database, or installed client is made by this tool.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
BUILD = 69814
PUSH = 1609394700
PUSH_ID = 111761
SPELLS = [
    (600000, 500357, 'Reap', 1, 'Strike for 90% normalized weapon damage plus 3 Physical damage. A damaging hit generates 7 to 13 Runic Power and a Soul Fragment.'),
    (600001, 500376, 'Murder', 1, 'Deal 13 to 14 Shadow damage, plus 1 per level through level 6 and 18% of attack power. With Soul Collector, a hit grants a Reaped Soul. Reduces melee hit chance by 3% for 10 seconds. Costs 30 Runic Power.'),
    (600002, 500517, 'Soul Strike', 6, 'Strike for 75% normalized weapon damage plus 3 Shadow damage. A hit grants a Reaped Soul and heals you for 80% of damage dealt plus 10% of your missing health. Costs 40 Runic Power.'),
    (600003, 573316, 'Soulrend', 1, 'Requires Soul Infusion. Deal 17 to 18 Shadow damage plus 36% of attack power and 29.7% of Shadow spell power. Consumes all souls, fragments and infusion. A hit generates 15 Runic Power. Miss, dodge and parry preserve infusion.'),
    (600004, 706731, 'Soul Collector', 1, 'Reap generates a Soul Fragment when it deals damage. Three fragments become one Reaped Soul. Murder also grants a soul on hit. Three souls grant Soul Infusion, enabling Soulrend. Soul resources clear on death or logout.'),
    (600005, 805077, 'Soul Fragment', 0, 'Three Soul Fragments become one Reaped Soul. Expires after 30 seconds. Clears on death or logout.'),
    (600006, 500363, 'Reaped Soul', 0, 'Three Reaped Souls grant Soul Infusion. Soulrend consumes all souls and fragments. Clears on death or logout.'),
    (600007, 803031, 'Soul Infusion', 0, 'Soulrend is available. It consumes your souls, fragments and infusion. Clears on death or logout.'),
    (600008, 355461, 'Reap (Runic Power)', 0, ''),
    (600009, None, 'Soulrend (Runic Power)', 0, ''),
    (600010, 500522, 'Soul Strike (Healing)', 0, ''),
    (600011, 560421, 'Murder', 0, 'Melee hit chance reduced by 3%.'),
    (600012, None, 'Reaper equipment receipt', 0, ''),
]
START = dict(spell_name=600000, spell_misc=2216, spell_effect=2888,
    spell_power=3016, spell_aura_options=5800, spell_aura_restrictions=1928,
    spell_categories=15528, spell_cooldowns=5160, spell_class_options=4840,
    spell_levels=13576, spell_equipped_items=2216, spell_target_restrictions=1192,
    spell_x_spell_visual=7816, skill_line_ability=1000, skill_race_class_info=1250,
    spell_duration=100)
NAMES = {''.join('_'+c.lower() if c.isupper() else c for c in n).lstrip('_'): n for n in
    ['SpellName','SpellMisc','SpellEffect','SpellPower','SpellAuraOptions','SpellAuraRestrictions',
     'SpellCategories','SpellCooldowns','SpellClassOptions','SpellLevels','SpellEquippedItems',
     'SpellTargetRestrictions','SpellXSpellVisual','SkillLineAbility','SkillRaceClassInfo','SpellDuration']}


def sql(value):
    if value is None:
        return 'NULL'
    if isinstance(value, bytes):
        return '0x'+value.hex()
    if isinstance(value, str):
        return "'"+value.replace("'", "''")+"'"
    return str(value)


def predicate(row):
    return ' AND '.join(f'`{k}` <=> {sql(v)}' for k, v in row.items())


def generate(evidence):
    retail = json.loads((evidence/'retail-data.json').read_text())
    db = json.loads((evidence/'database-evidence.json').read_text())
    additional = json.loads((evidence/'hotfix-additional.json').read_text())
    assert additional['maximumPushId'] < PUSH_ID and not additional['family36']
    schema = db['Hotfix']['tables']
    rows = {n: [] for n in [*START, 'hotfix_blob', 'hotfix_data']}
    counters = START.copy()

    def add(table, **values):
        fields = schema[table]['fields']
        row = {}
        for f in fields:
            name, kind, default = f['Field'], f['Type'], f['Default']
            value = values.pop(name, default)
            if name == 'ID' and value in ('0', 0, None):
                value = counters[table]
                counters[table] += 1
            if name == 'VerifiedBuild':
                value = BUILD
            if value is not None and not isinstance(value, bytes):
                if 'int' in kind:
                    value = int(value)
                elif 'float' in kind:
                    value = struct.unpack('f', struct.pack('f', float(value)))[0]
            row[name] = value
        assert not values, (table, values)
        if 'ID' in row:
            name = NAMES[table]
            assert row['ID'] not in retail['tables'][name]['ids'], (name, row['ID'], 'DB2 collision')
            assert row['ID'] not in schema[table]['ids'], (name, row['ID'], 'hotfix collision')
        rows[table].append(row)
        return row

    def effect(spell, index=0, kind=6, target=1, points=0, **other):
        return add('spell_effect', SpellID=spell, EffectIndex=index, Effect=kind,
                   ImplicitTarget1=target, EffectBasePoints=points, EffectChainAmplitude=1,
                   PvpMultiplier=1, GroupSizeBasePointsCoefficient=1, **other)

    add('spell_duration', ID=100, Duration=300000010, MaxDuration=300000010)
    for sid, source, name, level, desc in SPELLS:
        assert sid not in retail['tables']['Spell']['ids']
        assert sid not in db['Hotfix']['blobRecordIds']
        active = sid < 600004
        melee = sid in (600000,600002,600003)
        resource = sid in (600005,600006,600007)
        # Resources retain visible native auras, but are absent from spellbook.
        hidden = sid >= 600008
        attrs = (0x10 | 0x40000 if melee else 0) | (0x40 if sid == 600004 else 0)
        if resource:
            attrs |= 0x80000000  # native NO_AURA_CANCEL
        if hidden and sid != 600011:
            attrs |= 0x80
        if sid == 600011:
            attrs |= 0x04000000
        if attrs >= 2**31:
            attrs -= 2**32
        icon_source = '49998' if melee else '686'
        icon = int(retail['spells'][icon_source]['SpellMisc'][0]['SpellIconFileDataID'])
        add('spell_name', ID=sid, Name=name)
        add('spell_misc', SpellID=sid, Attributes1=attrs,
            Attributes2=0x400 if hidden else 0,
            Attributes3=0x20000000 if sid == 600010 else 0,
            Attributes4=(0x400 if melee else 0) | (0x20000000 if sid == 600010 else 0),
            Attributes5=0x8000 if sid >= 600005 else 0,
            CastingTimeIndex=1, DurationIndex=9 if sid == 600005 else 100 if resource else 1 if sid == 600011 else 21 if sid == 600004 else 0,
            RangeIndex=2 if melee else 11 if sid in (600001,600011) else 1,
            SchoolMask=1 if sid in (600000,600012) else 32,
            Speed=16 if sid == 600001 else 0, SpellIconFileDataID=icon)
        add('spell_class_options', SpellID=sid, SpellClassSet=36)
        add('spell_levels', SpellID=sid, BaseLevel=level, SpellLevel=level,
            MaxLevel=6 if sid == 600001 else 10 if sid == 600003 else 0)
        add('spell_equipped_items', SpellID=sid, EquippedItemClass=2 if melee else -1,
            EquippedItemSubclass=173555 if melee else 0)
        if active:
            add('spell_categories', SpellID=sid, DefenseType=2 if melee else 1,
                PreventionType=1 if sid == 600001 else 2, StartRecoveryCategory=133)
            add('spell_cooldowns', SpellID=sid, StartRecoveryTime=1250)
            add('spell_target_restrictions', SpellID=sid, Targets=2, MaxTargets=1)
            add('spell_power', SpellID=sid, PowerType=6, ManaCost={600001:300,600002:400}.get(sid,0))
            visual = int(retail['spells'][icon_source]['SpellXSpellVisual'][0]['SpellVisualID'])
            add('spell_x_spell_visual', SpellID=sid, SpellVisualID=visual, Probability=1)
        if sid in (600000,600002):
            # Native weapon damage applies these effects in order: multiply
            # the weapon, then add the flat amount, matching the CoA records.
            effect(sid, kind=31, target=6, points=90 if sid == 600000 else 75)
            effect(sid, index=1, kind=121, target=6, points=3)
        elif sid == 600001:
            effect(sid, kind=2, target=6, points=13,
                EffectRealPointsPerLevel=1, BonusCoefficientFromAP=0.18)
        elif sid == 600003:
            effect(sid, kind=2, target=6, points=17,
                BonusCoefficientFromAP=0.36, EffectBonusCoefficient=0.297)
            add('spell_aura_restrictions', SpellID=sid, CasterAuraSpell=600007)
        elif sid in (600004,600005,600006,600007):
            effect(sid, EffectAura=4)
            add('spell_aura_options', SpellID=sid, CumulativeAura=2 if sid == 600005 else 3 if sid == 600006 else 1)
        elif sid in (600008,600009):
            effect(sid, kind=30, points=100 if sid == 600008 else 150, EffectMiscValue1=6)
        elif sid == 600010:
            effect(sid, kind=10)
        elif sid == 600011:
            effect(sid, target=6, EffectAura=54, points=-3)
        else:
            effect(sid, kind=3)
        blob = ('\0'+desc+'\0'+(desc if sid in (600005,600006,600007,600011) else '')+'\0').encode()
        add('hotfix_blob', TableHash=retail['tables']['Spell']['tableHash'], RecordId=sid, locale='enUS', Blob=blob)
        if level:
            add('skill_line_ability', Spell=sid, SkillLine=1311, ClassMask=65536,
                AcquireMethod=2, MinSkillLineRank=1, NumSkillUps=1)

    for skill in (173,413,414):
        add('skill_race_class_info', SkillID=skill, ClassMask=65536, Flags=128,
            Availability=1, MinLevel=1, RaceMask1=-1, RaceMask2=-1)
    for spell in (1180,8737,9077,9078):
        add('skill_line_ability', Spell=spell, SkillLine=1311, ClassMask=65536,
            AcquireMethod=2, MinSkillLineRank=1, NumSkillUps=1)

    for table, table_rows in list(rows.items()):
        if table == 'hotfix_data':
            continue
        for row in table_rows:
            table_hash = row['TableHash'] if table == 'hotfix_blob' else retail['tables'][NAMES[table]]['tableHash']
            record = row.get('ID', row.get('RecordId'))
            add('hotfix_data', Id=PUSH_ID, UniqueId=PUSH, TableHash=table_hash, RecordId=record, Status=1)
    assert not any(p['UniqueId'] == PUSH for p in db['Hotfix']['pushes']), 'push collision'

    world = {
        'spell_script_names': [dict(spell_id=s, ScriptName='spell_coa_reaper_combat') for s in range(600000,600004)] +
            [dict(spell_id=s, ScriptName='spell_coa_reaper_souls') for s in (600006,600007)],
        'spell_custom_attr': [dict(entry=s, attributes=0x01000000) for s in (600005,600006,600007)],
    }
    keys = {n: ['ID'] for n in START}
    keys.update(hotfix_blob=['TableHash','RecordId'], hotfix_data=['Id'],
                spell_script_names=['spell_id'], spell_custom_attr=['entry'])

    def migration(data, kind):
        proc = 'coa_reaper_20260925_'+kind
        lines = ['-- Generated by modules/mod-coa/tools/generate_reaper.py; WoW 12.1.0.69814.',
            '-- Only empty or byte-equivalent owned rows are accepted. Every data write is atomic.',
            f'DROP PROCEDURE IF EXISTS `{proc}`;', 'DELIMITER $$', f'CREATE PROCEDURE `{proc}`()', 'BEGIN',
            '  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;', '  START TRANSACTION;']
        for table, records in data.items():
            if not records:
                continue
            scope = ' OR '.join('('+predicate({k:r[k] for k in keys[table]})+')' for r in records)
            matches = ' OR '.join('('+predicate(r)+')' for r in records)
            # hotfix_data is keyed independently of its push id. Protect both.
            if table == 'hotfix_data':
                scope += f' OR `UniqueId` = {PUSH}'
            lines += [f'  IF EXISTS (SELECT 1 FROM `{table}` WHERE ({scope}) AND NOT ({matches})) THEN',
                f"    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Reaper conflict in {table}; nothing changed';", '  END IF;']
        for table, records in data.items():
            for row in records:
                columns = ','.join('`'+k+'`' for k in row)
                values = ','.join(sql(v) for v in row.values())
                lines.append(f'  INSERT INTO `{table}` ({columns}) SELECT {values} WHERE NOT EXISTS (SELECT 1 FROM `{table}` WHERE {predicate(row)});')
        lines += ['  COMMIT;', 'END$$', 'DELIMITER ;', f'CALL `{proc}`();', f'DROP PROCEDURE `{proc}`;', '']
        target = ROOT/'data'/'sql'/('db-'+kind)/'2026_09_25_00_mod_coa_reaper_combat.sql'
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text('\n'.join(lines),encoding='utf-8',newline='\n')

    migration(rows,'hotfixes')
    migration(world,'world')
    def encode(value):
        if isinstance(value,bytes):
            return {'hex':value.hex()}
        raise TypeError(type(value))
    manifest = dict(build=BUILD, push=PUSH, pushId=PUSH_ID, spells=[dict(id=s,source=src,name=n,level=l,description=d) for s,src,n,l,d in SPELLS],
        keys=keys, databases={'hotfixes':rows,'world':world},
        tableHashes={table:retail['tables'][name]['tableHash'] for table,name in NAMES.items()},
        evidence={n:hashlib.sha256((evidence/n).read_bytes()).hexdigest() for n in ('retail-data.json','database-evidence.json','hotfix-additional.json')},
        sourceCommits={'coa':'49e0dca1b1751118a99efc07e7738c360a136dcb','datamine':'5078f9ac22541c45c17a866c23aac6c12073de23'},
        adaptations=['First rank only; complete kit at level 6.',
            'Soulrend gain of 15 RP is provisional retail tuning, not established official CoA tuning.',
            'Soul resources clear on death/logout; native RP remains native.',
            'Atomic thresholds replace the reconstruction periodic synchronization.',
            'Native retail visuals/icons reused; no client executable patch.'])
    (ROOT/'data'/'reaper-69814.json').write_text(json.dumps(manifest,default=encode,indent=2)+'\n',encoding='utf-8',newline='\n')
    print('Generated',sum(map(len,rows.values())),'hotfix rows and',sum(map(len,world.values())),'world rows.')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence',required=True,type=Path)
    generate(parser.parse_args().evidence)
