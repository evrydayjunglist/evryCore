"""Compare pinned client spell records and summarize the owner's combat log."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re

STARTERS = [(1,1464,'Slam'),(2,35395,'Crusader Strike'),(3,56641,'Steady Shot'),
            (4,1752,'Sinister Strike'),(5,585,'Smite'),(7,188196,'Lightning Bolt'),
            (8,228597,'Frostbolt (116 impact)'),(9,686,'Shadow Bolt'),
            (10,100780,'Tiger Palm'),(11,190984,'Wrath')]


def measured_warrior(root, combat_log, sniff):
    """Retain only selected combat fields, not the sniff's authentication packets."""
    hits, swings = [], []
    for number, line in enumerate(combat_log.read_text(encoding='utf-8-sig').splitlines(), 1):
        if '  ' not in line: continue
        timestamp, payload = line.split('  ', 1)
        f = next(csv.reader([payload]))
        if len(f) < 10: continue
        if f[0] == 'SPELL_DAMAGE' and f[9] == '1464':
            if len(f) != 42: raise ValueError('Unexpected retail spell damage layout')
            hits.append(dict(line=number,time=timestamp,damage=int(f[31]),original=int(f[32]),
                critical=f[38]=='1',target=f[6],targetMaxHealth=int(f[15]),targetArmor=int(f[18]),
                resisted=int(f[35]),absorbed=int(f[37])))
        elif f[0] == 'SWING_DAMAGE' and f[1].startswith('Player-'):
            if len(f) != 38: raise ValueError('Unexpected retail swing layout')
            swings.append(dict(line=number,damage=int(f[28]),original=int(f[29]),critical=f[35]=='1'))
    text = sniff.read_text(encoding='utf-8-sig')
    if '# Detected build: V12_1_0_69933' not in text[:1000]:
        raise ValueError('Expected the owner supplied 69933 sniff')
    packets = []
    for block in re.split(r'(?=^(?:ServerToClient|ClientToServer): )', text, flags=re.M):
        if not block.startswith('ServerToClient: SMSG_SPELL_NON_MELEE_DAMAGE_LOG') or not re.search(r'^SpellID: 1464\b', block, re.M):
            continue
        fields = {}
        for name in ('SpellID','Damage','OriginalDamage','Resisted','Absorbed','SchoolMask',
                     '(ContentTuning) TargetLevel','(ContentTuning) PlayerPrimaryStatToExpectedRatio'):
            match = re.search('^'+re.escape(name)+r': (.+)$',block,re.M)
            fields[name] = match[1] if match else None
        packets.append(fields)
    result = dict(build=69933,playerLevel=1,characterClass='Warrior',race='Undead',
        sourceFiles={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (combat_log,sniff)},
        slamHits=hits,swings=swings,sniffSlamPackets=packets,
        limits='Owner-reported level 1. Combat log gives player-visible scaled damage. Raw packet amounts are not directly comparable. Parsed sniff header targets WrathOfTheLichKing, so uncorroborated parsed fields are not treated as an exact modern schema. This sample is build 69933, not the emulator client 69814.')
    (root/'retail-owner-warrior.json').write_text(json.dumps(result,indent=2)+'\n')
    return dict(retailWarriorHits=len(hits),damage=sorted({r['damage'] for r in hits}))


def compare(root, combat_log):
    retail = json.loads((root/'retail.json').read_text())
    database = json.loads((root/'database.json').read_text())
    result = {'build':69814,'scope':'Starter PvE damage formulas, not endgame or PvP balance.',
        'assumptions':'Race-neutral level 1 base stats, no equipment, buffs, talents, armor, resistance or critical hit. Native spell coefficients; locally installed class base stats. These are modeled direct hits, not measured retail gameplay or DPS.',
        'retail':[], 'observations':{}, 'reaper':[]}
    for cls,spell,name in STARTERS:
        stats = next(r for r in database['World']['class_stats'] if r['class']==cls and r['level']==1)
        cr = next(r for r in retail['tables']['ChrClasses']['rows'] if int(r['ID'])==cls)
        entry = retail['spells'][str(spell)]
        effect = next(r for r in entry['SpellEffect'] if int(r['Effect'])==2 and int(r['DifficultyID'])==0)
        ap = stats['str']*float(cr['AttackPowerPerStrength'])+stats['agi']*float(cr['AttackPowerPerAgility'])
        if cls==3: ap = (1+stats['agi'])*float(cr['RangedAttackPowerPerAgility'])
        sp = stats['inte'] if int(cr['PrimaryStatPriority'])<2 else 0
        ap_coeff,sp_coeff = float(effect['BonusCoefficientFromAP']),float(effect['EffectBonusCoefficient'])
        assert float(effect['EffectBasePoints'])==0
        result['retail'].append(dict(classID=cls,spellID=spell,name=name,attackPower=ap,spellPower=sp,
            apCoefficient=ap_coeff,spCoefficient=sp_coeff,unmitigated=int(ap*ap_coeff)+int(sp*sp_coeff),
            variance=float(effect['Variance']),sourceEffect=int(effect['ID'])))
    result['excluded']='Death Knight and Demon Hunter start at level 8 in ChrClasses. Living Flame uses script-selected child spells and has no captured level-1 learning row; no level-1 Evoker claim is made.'
    for level in (1,2,6,10,20,40,60,80,90):
        expected = next(r for r in retail['tables']['ExpectedStat']['rows'] if int(r['Lvl'])==level and int(r['ExpansionID'])==-2)
        result['reaper'].append(dict(level=level,
            oldMurderBase=round(float(expected['CreatureSpellDamage'])*0.13),
            oldSoulrendBase=round(float(expected['CreatureSpellDamage'])*0.17),
            proposedMurder='120% AP + 0..1',proposedSoulrend='180% AP + 29.7% Shadow SP + 0..1'))
    raw = combat_log.read_bytes()
    (root/'combat-baseline.txt').write_bytes(raw)
    events = {s:{'casts':0,'damage':[],'misses':[]} for s in (600000,600001,600003)}
    for line_number,line in enumerate(raw.decode('utf-8-sig').splitlines(),1):
        if '  ' not in line: continue
        timestamp,payload = line.split('  ',1)
        fields = next(csv.reader([payload]))
        if len(fields)<12 or fields[0] not in ('SPELL_CAST_SUCCESS','SPELL_DAMAGE','SPELL_MISSED'):continue
        if not fields[9].isdigit() or int(fields[9]) not in events:continue
        record = events[int(fields[9])]
        if fields[0]=='SPELL_CAST_SUCCESS':record['casts']+=1
        elif fields[0]=='SPELL_MISSED':record['misses'].append(dict(line=line_number,reason=fields[12]))
        else:
            # The captured 12.1 log includes the 19 advanced unit fields before damage.
            if len(fields)!=42:raise ValueError('Unexpected combat log damage layout')
            record['damage'].append(dict(line=line_number,time=timestamp,amount=int(fields[31]),
                original=int(fields[32]),critical=fields[38]=='1',target=fields[6]))
    result['observations']={'source':str(combat_log),'sha256':hashlib.sha256(raw).hexdigest(),
        'characterLevel':2,'target':'Mindless Zombie 1501','events':events,
        'limits':'The log establishes delivered results. Murder has cast/debuff events without damage events; the post-fix test must confirm its delayed damage separately.'}
    (root/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
    return {'retail':result['retail'],'observations':result['observations']}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--audit',required=True,type=Path)
    p.add_argument('--combat-log',required=True,type=Path)
    p.add_argument('--retail-combat-log',type=Path)
    p.add_argument('--retail-sniff',type=Path)
    args=p.parse_args()
    summary = compare(args.audit,args.combat_log)
    if args.retail_combat_log or args.retail_sniff:
        if not (args.retail_combat_log and args.retail_sniff):p.error('Provide both retail input paths')
        summary['measuredRetail'] = measured_warrior(args.audit,args.retail_combat_log,args.retail_sniff)
    print(json.dumps(summary,indent=2))
