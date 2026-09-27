"""Generate build-pinned visual records; never connect to or change a database.

The first owner candidate contains Murder only. Other valid chains are generated
for review, and remain unbound until that delivery path is observed in the client.
No gameplay table or previously applied migration is regenerated here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

TYPED = {'SpellVisual':'spell_visual', 'SpellVisualKit':'spell_visual_kit',
    'SpellVisualEffectName':'spell_visual_effect_name', 'SpellVisualMissile':'spell_visual_missile',
    'SpellXSpellVisual':'spell_x_spell_visual', 'SoundKit':'sound_kit'}
CHAINS = [('Reap',600000,2279),('Murder',600001,20459),('Soul Strike',600002,2004),
          ('Soulrend',600003,2000),('Soul Infusion',600007,1000091)]
ATTACHMENT = {-1:-1,0:20,1:34,2:19,3:21,4:22,5:17,6:18}
KIT_ATTACHMENTS = {'HeadEffect':20,'ChestEffect':34,'BaseEffect':19,
    'LeftHandEffect':21,'RightHandEffect':22,'BreathEffect':17,
    'LeftWeaponEffect':0,'RightWeaponEffect':1}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(meta):
    result=[]
    for f in meta['fields']:
        for i in range(f['arraySize']):
            result.append((f['name']+(str(i+1) if f['arraySize']>1 else ''),f))
    if meta.get('parentFieldName'):
        result.append((meta['parentFieldName'],dict(type='int',signed=True)))
    return result


def encode(meta,row):
    """Match DB2StorageBase::WriteRecord, including its non-inline parent field."""
    data=bytearray()
    for name,f in fields(meta):
        v=row[name]
        if f['type'] in ('string','locstring'):
            if '\0' in v: raise ValueError('Embedded NUL: '+name)
            data.extend(v.encode('utf8')+b'\0')
        else:
            fmt={'float':'f','byte':'b','short':'h','int':'i','long':'q'}[f['type']]
            if f['type']!='float' and not f['signed']:fmt=fmt.upper()
            data.extend(struct.pack('<'+fmt,v))
    return bytes(data)


def blank(meta):
    return {name:('' if f['type'] in ('string','locstring') else 0) for name,f in fields(meta)}


def generate(root,out):
    source=json.loads((root/'recovered/manifest.json').read_text())
    converted=json.loads((root/'converted/manifest.json').read_text())
    native=json.loads((root/'retail/tables.json').read_text())['tables']
    db=json.loads((root/'database.json').read_text())
    evidence=json.loads((root/'translation-evidence.json').read_text())
    comparisons=json.loads((root/'native-comparison.json').read_text())
    occupied={n:set(t['ids']) for n,t in native.items()}
    for n,t in native.items():
        occupied[n].update(p['RecordId'] for p in db['pushes'] if p['TableHash']==t['tableHash'])
        occupied[n].update(p['RecordId'] for p in db['blobs'] if p['TableHash']==t['tableHash'])
        if n in TYPED:occupied[n].update(db['tables'][TYPED[n]]['ids'])
    counters={n:max(ids)+1 for n,ids in occupied.items()}
    records=[]; ownership={}; modelmap={m['effectID']:m for m in converted['models'] if 'fileDataID' in m}
    filemap={f['path']:f for f in converted['files']}

    def allocate(name,label,short=False):
        key=name+':'+str(label)
        if key in ownership:return ownership[key]
        val=next(i for i in range(65000,1,-1) if i not in occupied[name]) if short else counters[name]
        if val in occupied[name] or val>2147483647:raise ValueError('ID allocation failed')
        occupied[name].add(val);counters[name]=max(counters[name],val+1);ownership[key]=val
        return val

    def add(name,label,values,short=False):
        meta=native[name]['meta'];row=blank(meta);row['ID']=allocate(name,label,short)
        if any(k not in row for k in values):raise ValueError('Unknown '+name+' fields: '+str(values.keys()-row.keys()))
        row.update(values);wire=encode(meta,row)
        r=dict(table=name,tableHash=native[name]['tableHash'],layoutHash=meta['layoutHash'],id=row['ID'],
               source=label,fields=row,blob=wire.hex(),storage=TYPED.get(name,'hotfix_blob'))
        records.append(r);return row['ID']

    effect_ids={}
    for sid,m in sorted(modelmap.items()):
        s=source['effects'][str(sid)]
        effect_ids[sid]=add('SpellVisualEffectName',sid,dict(ModelFileDataID=m['fileDataID'],
            BaseMissileSpeed=0,Scale=s['Scale'],MinAllowedScale=s['MinAllowedScale'],
            MaxAllowedScale=s['MaxAllowedScale'],Alpha=1,EffectRadius=s['AreaEffectSize'],ModelPosition=-1),short=True)
    sound_ids={};advanced_ids={};sound_files={}
    for sid,entry in source['sounds'].items():
        s=entry['record'];sound_id=allocate('SoundKit',int(sid));advanced=0
        if s['SoundEntriesAdvancedID']:
            old=evidence['advanced'][str(s['SoundEntriesAdvancedID'])]
            originals=[v for v in native['SoundKitAdvanced']['rows'] if int(v['ID'])==old['ID']]
            if len(originals)!=1:raise ValueError('No native advanced-sound defaults')
            values={n:(float(originals[0][n]) if f['type']=='float' else int(originals[0][n]))
                    for n,f in fields(native['SoundKitAdvanced']['meta'])}
            for k,v in old.items():
                if k in values:values[k]=v
            values['SoundKitID']=sound_id
            advanced=add('SoundKitAdvanced','sound '+sid,values)
            advanced_ids[sid]=advanced
        values={k:s[k] for k in ('SoundType','VolumeFloat','Flags','MinDistance','DistanceCutoff','EAXDef')}
        values.update(SoundKitAdvancedID=advanced)
        sound_ids[int(sid)]=add('SoundKit',int(sid),values)
        sound_files[int(sid)]=[]
        for i,name in enumerate(s['File']):
            if not name:continue
            path=(s['DirectoryBase']+'/'+name).replace('\\','/').lower()
            file_id=filemap[path]['fileDataID'];sound_files[int(sid)].append(file_id)
            add('SoundKitEntry',f'{sid}/{i}',dict(SoundKitID=sound_id,FileDataID=file_id,
                Frequency=s['Freq'][i],Volume=1))
    motion_ids={}
    for sid,s in source['motions'].items():
        motion_ids[int(sid)]=add('SpellMissileMotion',int(sid),{k:s[k] for k in ('Name','ScriptBody','Flags','MissileCount')},short=True)
    kit_ids={}; animation_ids={}
    for sid,s in source['kits'].items():
        if any(s[k] and s[k] not in effect_ids for k in KIT_ATTACHMENTS):continue
        if any(s['SpecialEffect']) or s['WorldEffect'] or s['ShakeID'] or any(x!=-1 for x in s['CharProc']):
            raise ValueError('Untranslated source kit operation')
        kit_id=add('SpellVisualKit',int(sid),dict(Flags1=s['Flags']))
        kit_ids[int(sid)]=kit_id
        if s['AnimID']!=-1 or s['StartAnimID']!=-1:
            pair=(s['StartAnimID'],s['AnimID'])
            if pair not in animation_ids:
                # Identical native records are sufficient for character animation.
                matches=[v for v in comparisons['SpellVisualAnim'] if int(v['InitialAnimID'])==pair[0]
                    and int(v['LoopAnimID'])==pair[1] and int(v['AnimKitID'])==0 and int(v['Field_12_0_0_63967_003'])==0]
                if not matches:raise ValueError('No identical native character animation: '+str(pair))
                animation_ids[pair]=min(int(v['ID']) for v in matches)
            # Type 6 selects SpellVisualAnim; type 1 selects SpellProceduralEffect.
            add('SpellVisualKitEffect',str(sid)+'/animation',dict(EffectType=6,Effect=animation_ids[pair],ParentSpellVisualKitID=kit_id))
        if s['SoundID']:
            add('SpellVisualKitEffect',str(sid)+'/sound',dict(EffectType=5,Effect=sound_ids[s['SoundID']],ParentSpellVisualKitID=kit_id))
        for name,attach in KIT_ATTACHMENTS.items():
            if not s[name]:continue
            model=add('SpellVisualKitModelAttach',str(sid)+'/'+name,dict(SpellVisualEffectNameID=effect_ids[s[name]],
                AttachmentID=attach,Scale=1,StartAnimID=-1,AnimID=-1,EndAnimID=-1,ParentSpellVisualKitID=kit_id))
            add('SpellVisualKitEffect',str(sid)+'/'+name,dict(EffectType=2,Effect=model,ParentSpellVisualKitID=kit_id))
    set_ids={int(m['SpellVisualMissileSetID']) for m in native['SpellVisualMissile']['rows']}
    chains=[]
    for title,spell,sid in CHAINS:
        s=source['visuals'][str(sid)]
        if any(s[k] and s[k] not in kit_ids for k in ('PrecastKit','CastKit','ImpactKit','StateKit','TargetImpactKit')):
            chains.append(dict(name=title,spellID=spell,sourceVisualID=sid,status='blocked by source model track mismatch',gameplayAccepted=False));continue
        v=dict(Flags=s['Flags'],MissileAttachment=ATTACHMENT[s['MissileAttachment']],
            MissileDestinationAttachment=ATTACHMENT[s['MissileDestinationAttachment']],StateKit=0)
        # The contemporary event table owns state lifetime. Do not also attach
        # the same kit through the older StateKit field.
        for stem in ('MissileCastOffset','MissileImpactOffset'):
            v.update({stem+str(i+1):x for i,x in enumerate(s[stem])})
        if s['HasMissile']:
            set_id=next(i for i in range(65000,1,-1) if i not in set_ids);set_ids.add(set_id)
            missile=dict(SpellVisualEffectNameID=effect_ids[s['MissileModel']],SoundEntriesID=sound_ids[s['MissileSound']],
                Attachment=v['MissileAttachment'],DestinationAttachment=v['MissileDestinationAttachment'],
                FollowGroundHeight=s['MissileFollowGroundHeight'],FollowGroundDropSpeed=300,FollowGroundApproach=750,
                Flags=516,SpellMissileMotionID=motion_ids[s['MissileMotion']],SpellVisualMissileSetID=set_id)
            for old,new in (('MissileCastOffset','CastOffset'),('MissileImpactOffset','ImpactOffset')):
                missile.update({new+str(i+1):x for i,x in enumerate(s[old])})
            add('SpellVisualMissile',sid,missile);v['SpellVisualMissileSetID']=set_id
        visual=add('SpellVisual',sid,v)
        # These are the source-to-retail event tuples found in thousands of
        # unchanged legacy visual chains in the pinned local client.
        roles={'PrecastKit':[(1,2,1)],'CastKit':[(3,13,1)],'ImpactKit':[(6,13,1),(6,13,2)],
               'CasterImpactKit':[(6,13,1)],'TargetImpactKit':[(6,13,4)],'StateKit':[(7,8,2)],
               'StateDoneKit':[(8,13,2)],'ChannelKit':[(11,12,2)]}
        for role,events in roles.items():
            if not s[role]:continue
            for start,end,target in events:
                add('SpellVisualEvent',f'{sid}/{role}/{target}',dict(StartEvent=start,EndEvent=end,TargetType=target,
                    SpellVisualKitID=kit_ids[s[role]],SpellVisualID=visual))
        before=next(b for b in db['bindings'] if b['SpellID']==spell)
        after=dict(before,SpellVisualID=visual)
        chains.append(dict(name=title,spellID=spell,sourceVisualID=sid,visualID=visual,bindingBefore=before,bindingAfter=after,
            status='generated; native delivery untested',gameplayAccepted=False))
    result=dict(format=1,build=69814,records=records,chains=chains,ownership=ownership,
        metadata={n:dict(meta=t['meta'],tableHash=t['tableHash'],sha256=t['sha256']) for n,t in native.items()},
        databaseSchema=db['schema'],schemas={n:t['schema'] for n,t in db['tables'].items()},
        sourceInputs={str(root/p):digest(root/p) for p in ['recovered/manifest.json','converted/manifest.json',
            'retail/tables.json','database.json','translation-evidence.json','native-comparison.json']},
        nativeAnimations={str(k):v for k,v in animation_ids.items()},
        note='Generated candidates. Only the Murder closure is included in package-01. Native acceptance remains pending.')
    data=(json.dumps(result,indent=2)+'\n').encode()
    if out.exists() and out.read_bytes()!=data:raise ValueError('Refusing to replace different generated records')
    out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(data)
    print(json.dumps(dict(records=len(records),chains=[dict(name=c['name'],status=c['status']) for c in chains]),indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();generate(a.root,a.out)
