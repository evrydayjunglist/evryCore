// Capture the installed retail client without changing its data or live databases.
import {readFileSync, writeFileSync, mkdirSync, existsSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {loadDb2Table} from 'file:///F:/evry/WOWEmulation/Emulators/Tools/evryOps/src/server/db2.ts';
import {buildMeta} from 'file:///F:/evry/WOWEmulation/Emulators/Builds/reaper-class/research/retail-client/db2meta.mts';

const out=process.argv[2];
if(!out) throw Error('Supply a fresh audit output directory.');
const base='F:/evry/WOWEmulation', emu=base+'/Emulators';
const fx=emu+'/Builds/reaper-class/3d-effects-69814-20260926';
const client=base+'/Clients/World of Warcraft';
const sha=(b:Buffer)=>createHash('sha256').update(b).digest('hex');
if(sha(readFileSync(client+'/_retail_/Wow.exe'))!=='c7c4795b5de2f0f5b3da840ee06120714bf520a2ada8b9cb7130d2a3754e4195')throw Error('Client changed');
const names=['Spell','SpellName','SpellEffect','SpellScaling','SpellLevels','SpellMisc',
 'SpellCategories','SpellCooldowns','SpellPower','SpellClassOptions','SkillLineAbility',
 'ChrClasses','ChrSpecialization','ExpectedStat','ExpectedStatMod','ContentTuning',
 'Item','ItemSparse','ItemDamageOneHand','ItemDamageTwoHand','ItemDamageOneHandCaster',
 'ItemDamageTwoHandCaster','SpellCastTimes','SpellRange'];
const metas=Object.fromEntries(names.map(n=>[n,buildMeta(n)]));
const reader=fx+'/tools/casc-reader/casc-reader.exe';
const cached=process.argv.includes('--cached');
const result=cached?null:spawnSync(reader,['--root',client,'--product','wow','--locale','enUS'],{
 input:names.map(n=>'GET '+metas[n].raw.fileDataId+'\n').join('')+'QUIT\n',
 windowsHide:true,timeout:180000,maxBuffer:512*1024*1024});
if(result&&(result.error||result.status))throw Error(String(result.error??result.stderr.toString()));
let offset=0;
function line(){if(!result)throw Error('No CASC response');const e=result.stdout.indexOf(10,offset);if(e<0)throw Error('Truncated response');
 const v=result.stdout.subarray(offset,e).toString().trim();offset=e+1;return v;}
if(!cached&&line()!=='READY 69814')throw Error('CASC build changed');
mkdirSync(out+'/db2',{recursive:true});mkdirSync(out+'/decoded',{recursive:true});
const keys=new Set(readFileSync(fx+'/research/TACTKeys.txt','utf8').trim().split(/\r?\n/).map(l=>BigInt('0x'+l.split(/\s+/)[0])));
const manifest:any={build:69814,readerSha256:sha(readFileSync(reader)),tables:{}}, tables:any={};
for(const name of names){
 const file=out+'/db2/'+name+'.db2';
 const l=cached?'OK '+readFileSync(file).length:line();if(!l.startsWith('OK '))throw Error(name+': '+l);
 const size=Number(l.slice(3)),raw=cached?readFileSync(file):result!.stdout.subarray(offset,offset+size);offset+=size;
 if(raw.length!==size||raw.subarray(0,4).toString()!=='WDC5')throw Error('Invalid capture '+name);
 if(existsSync(file)&&sha(readFileSync(file))!==sha(raw))throw Error('Existing capture differs '+name);
 writeFileSync(file,raw);
 const decoded=Buffer.from(raw), sections=[];
 for(let i=0;i<raw.readUInt32LE(200);i++){
  const p=204+40*i,key=raw.readBigUInt64LE(p),available=key===0n||keys.has(key);
  sections.push({key:key.toString(16),records:raw.readUInt32LE(p+12),available});
  if(key!==0n&&available)decoded.writeBigUInt64LE(0x5452494e49545900n,p);
 }
 const path=out+'/decoded/'+name+'.db2';writeFileSync(path,decoded);
 const parsed=loadDb2Table(path,metas[name].meta);
 tables[name]=[...parsed.byId].map(([id,r])=>({id,...r}));
 manifest.tables[name]={...metas[name],sha256:sha(raw),bytes:size,sections,
  tableHash:raw.readUInt32LE(152),records:parsed.byId.size,skippedEncrypted:parsed.skippedEncrypted};
 console.log(name,parsed.byId.size,'rows, skipped',parsed.skippedEncrypted);
}
if(result&&offset!==result.stdout.length)throw Error('Trailing CASC bytes');
const levels=new Map(tables.SpellLevels.map((r:any)=>[r.SpellID,r.SpellLevel]));
const selected=new Set([6603,1752,196819,1757,133,116,585,686,190984,188196,193455,56641,
 100780,35395,85256,1464,12294,49998,206930,162243,361469,361500,185438,205523]);
for(const row of tables.SkillLineAbility){
 const mask=Number(row.ClassMask),level=Number(levels.get(row.Spell));
 if(mask>0&&mask<8192&&level>0&&level<=2)selected.add(Number(row.Spell));
}
// Follow native trigger records, including Frostbolt's separate impact damage.
const effectsBySpell=new Map<number,any[]>();
for(const row of tables.SpellEffect){const id=Number(row.SpellID);
 const rows=effectsBySpell.get(id)??[];rows.push(row);effectsBySpell.set(id,rows);}
for(const id of selected)for(const effect of effectsBySpell.get(id)??[]){
 const trigger=Number(effect.EffectTriggerSpell);if(trigger)selected.add(trigger);
}
const captured:any={...manifest,tables:{},spells:{},starterCandidates:[]};
for(const name of names){
 let rows=tables[name];
 if(name.startsWith('Spell')||name==='SkillLineAbility')rows=rows.filter((r:any)=>selected.has(Number(['Spell','SpellName'].includes(name)?r.id:r.SpellID??r.Spell)));
 if(['Item','ItemSparse'].includes(name))rows=rows.filter((r:any)=>[2092,35,2361,36,37,38,39,40,43,44,45].includes(r.id));
 captured.tables[name]={...manifest.tables[name],rows};
 for(const row of rows){
  const id=Number(['Spell','SpellName'].includes(name)?row.id:row.SpellID??row.Spell);
  if(selected.has(id))((captured.spells[id]??={})[name]??=[]).push(row);
 }
}
for(const [id,entry] of Object.entries(captured.spells) as any[]){
 const effects=entry.SpellEffect??[];
 if(effects.some((r:any)=>[2,17,31,58,121].includes(Number(r.Effect))))
  captured.starterCandidates.push({id:Number(id),name:entry.SpellName?.[0]?.Name,level:entry.SpellLevels?.[0]?.SpellLevel,
   classMasks:entry.SkillLineAbility?.map((r:any)=>r.ClassMask),effects});
}
writeFileSync(out+'/retail.json',JSON.stringify(captured,null,2)+'\n');
