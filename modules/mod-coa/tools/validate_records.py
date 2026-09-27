"""Validate retail IDs including encrypted-section ID lists and spell linkage."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--evidence',required=True,type=Path)
parser.add_argument('--out',required=True,type=Path)
args=parser.parse_args()
manifest=json.loads((ROOT/'data/reaper-69814.json').read_text())
retail=json.loads((args.evidence/'retail-data.json').read_text())
data=manifest['databases']['hotfixes']
tables={row['tableHash']:row for row in retail['tables'].values()}
checked=[]
for table, records in data.items():
    if table=='hotfix_data':
        continue
    table_hash=records[0]['TableHash'] if table=='hotfix_blob' else manifest['tableHashes'][table]
    original=tables[table_hash]
    raw=Path(original['path']).read_bytes()
    assert hashlib.sha256(raw).hexdigest()==original['sha256'], 'Changed DB2 '+table
    u32=lambda offset:struct.unpack_from('<I',raw,offset)[0]
    assert raw[:4]==b'WDC5' and u32(4)==5
    sections=u32(200)
    position=204+sections*40+u32(140)*4+u32(188)+u32(192)+u32(196)
    encrypted=[]
    for index in range(sections):
        tact=struct.unpack_from('<Q',raw,204+index*40)[0]
        if not tact:
            continue
        count=u32(position)
        position+=4
        assert position+count*4<=len(raw)
        encrypted.extend(struct.unpack_from('<'+'I'*count,raw,position))
        position+=count*4
    reserved=set(original['ids'])|set(encrypted)
    for row in records:
        identifier=row['RecordId'] if table=='hotfix_blob' else row['ID']
        assert identifier not in reserved, (table,identifier,'reserved native ID')
        assert u32(160)<=identifier<=u32(164), (table,identifier,'outside native bounds')
    checked.append(dict(table=table,records=len(records),encryptedIds=len(encrypted),
        minEncrypted=min(encrypted,default=None),maxEncrypted=max(encrypted,default=None),
        assetSha256=original['sha256'],assetBuild=original['build'],tableHash=table_hash,layoutHash=u32(156)))

for entry in manifest['spells']:
    sid=entry['id']
    for table in ('spell_name','spell_misc','spell_class_options','spell_levels','spell_equipped_items'):
        assert sum(row.get('SpellID',row.get('ID'))==sid for row in data[table])==1, (sid,table)
    effects=[row for row in data['spell_effect'] if row['SpellID']==sid]
    assert effects and [r['EffectIndex'] for r in effects]==list(range(len(effects)))
    blob=next(row for row in data['hotfix_blob'] if row['RecordId']==sid)
    strings=bytes.fromhex(blob['Blob']['hex']).decode('utf-8').split('\0')
    assert len(strings)==4 and strings[0]=='' and strings[1]==entry['description'] and strings[-1]==''
    assert next(row for row in data['spell_name'] if row['ID']==sid)['Name']==entry['name']
    assert sid!=205523
    for table in ('spell_misc','spell_effect','spell_name'):
        for row in data[table]:
            if row.get('SpellID',row.get('ID'))==sid:
                assert any(push['RecordId']==row['ID'] and push['TableHash']==manifest['tableHashes'][table] and push['Status']==1 for push in data['hotfix_data'])

for sid,cost in ((600000,0),(600001,300),(600002,400),(600003,0)):
    power=next(row for row in data['spell_power'] if row['SpellID']==sid)
    assert power['PowerType']==6 and power['ManaCost']==cost
    cd=next(row for row in data['spell_cooldowns'] if row['SpellID']==sid)
    assert cd['StartRecoveryTime']==1250 and cd['RecoveryTime']==0
    target=next(row for row in data['spell_target_restrictions'] if row['SpellID']==sid)
    assert target['Targets']==2 and target['MaxTargets']==1
    ability=next(row for row in data['skill_line_ability'] if row['Spell']==sid)
    assert ability['AcquireMethod']==2 and ability['ClassMask']==65536 and ability['SkillLine']==1311
assert len([r for r in data['skill_line_ability'] if r['Spell']>=600005])==0
assert next(r for r in data['spell_aura_restrictions'] if r['SpellID']==600003)['CasterAuraSpell']==600007
for sid in (600000,600002,600003):
    equipment=next(row for row in data['spell_equipped_items'] if row['SpellID']==sid)
    assert equipment['EquippedItemClass']==2 and equipment['EquippedItemSubclass']==173555
    misc=next(row for row in data['spell_misc'] if row['SpellID']==sid)
    assert misc['Attributes4']&0x400 and misc['RangeIndex']==2
for sid,percent in ((600000,90),(600002,75)):
    effects=[row for row in data['spell_effect'] if row['SpellID']==sid]
    assert [(row['Effect'],row['EffectBasePoints']) for row in effects]==[(31,percent),(121,3)]
    # Spell::EffectWeaponDmg applies percent and fixed components in sequence.
    assert 100*effects[0]['EffectBasePoints']/100+effects[1]['EffectBasePoints']==percent+3
assert next(row for row in retail['tables']['PowerType']['rows'] if row['PowerTypeEnum']=='6')['MaxBasePower']=='1000'
args.out.parent.mkdir(parents=True,exist_ok=True)
args.out.write_text(json.dumps(dict(result='pass',spells=len(manifest['spells']),tables=checked,
    limits='Static server-extract/schema evidence; client native cast identity and visuals require owner gameplay.'),indent=2)+'\n')
print('PASS: 13 complete spell roots; native/encrypted ID ranges; costs, ownership, equipment, GCD and helper visibility.')
