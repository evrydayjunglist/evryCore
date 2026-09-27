"""Read-only snapshots for the Reaper damage comparison; no account credentials saved."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import pymysql

RUNTIME = Path('F:/evry/WOWEmulation/Emulators/Builds/evryCore-job-modules/bin/RelWithDebInfo')


def capture():
    config = (RUNTIME/'worldserver.conf').read_text()
    result = {}
    queries = {
        'Character': {
            'reapers': 'SELECT guid,race,class,level,health,power1,online FROM characters WHERE class=17',
            'equipment': 'SELECT ci.guid,ci.slot,i.itemEntry FROM character_inventory ci JOIN item_instance i ON ci.item=i.guid JOIN characters c ON ci.guid=c.guid WHERE c.class=17 AND ci.bag=0 AND ci.slot<19',
            'saved_stats': 'SELECT cs.* FROM character_stats cs JOIN characters c ON cs.guid=c.guid WHERE c.class=17',
        },
        'World': {
            'class_stats': 'SELECT * FROM player_classlevelstats WHERE level IN (1,2,6,10,20,40,60,80,90)',
            'race_stats': 'SELECT * FROM player_racestats',
            'starting_spells': 'SELECT * FROM playercreateinfo_spell_custom',
            'starting_ghouls': "SELECT entry,name FROM creature_template WHERE name LIKE '%Risen%Dead%' OR name LIKE '%Mindless%Zombie%' OR name LIKE '%Wretched%Zombie%'",
            'reaper_scripts': 'SELECT * FROM spell_script_names WHERE spell_id BETWEEN 600000 AND 600012',
            'zombie_resistance': 'SELECT * FROM creature_template_resistance WHERE CreatureID IN (1501,1503)',
            'zombie_difficulty': 'SELECT * FROM creature_template_difficulty WHERE Entry IN (1501,1503)',
            'zombie_addon': 'SELECT * FROM creature_template_addon WHERE entry IN (1501,1503)',
            'zombie_template': 'SELECT * FROM creature_template WHERE entry IN (1501,1503)',
            'server_effects': 'SELECT * FROM serverside_spell_effect WHERE SpellID BETWEEN 600000 AND 600012',
        },
        'Hotfix': {
            'effects': 'SELECT * FROM spell_effect WHERE SpellID BETWEEN 600000 AND 600012',
            'levels': 'SELECT * FROM spell_levels WHERE SpellID BETWEEN 600000 AND 600012',
            'misc': 'SELECT * FROM spell_misc WHERE SpellID BETWEEN 600000 AND 600012',
            'scaling': 'SELECT * FROM spell_scaling WHERE SpellID BETWEEN 600000 AND 600012',
            'expected_stats': 'SELECT * FROM expected_stat WHERE Lvl IN (1,2,6,10,20,40,60,80,90)',
            'classes': 'SELECT * FROM chr_classes WHERE ID<=17',
            'specs': 'SELECT * FROM chr_specialization WHERE ClassID=17',
            'retail_effects': 'SELECT * FROM spell_effect WHERE SpellID IN (116,133,585,686,1464,1752,35395,56641,100780,188196,190984,316239)',
        },
    }
    for kind, commands in queries.items():
        match = re.search(r'^'+kind+r'DatabaseInfo\s*=\s*"([^"]+)"', config, re.M)
        host,port,user,password,schema = match[1].split(';')
        if host not in ('localhost','127.0.0.1'): raise ValueError('Expected local database')
        db = pymysql.connect(host=host,port=int(port),user=user,password=password,database=schema,
                             cursorclass=pymysql.cursors.DictCursor,autocommit=False)
        try:
            with db.cursor() as cursor:
                cursor.execute('SET TRANSACTION READ ONLY')
                cursor.execute('START TRANSACTION WITH CONSISTENT SNAPSHOT')
                data = result[kind] = {}
                for name,sql in commands.items():
                    cursor.execute(sql)
                    data[name] = cursor.fetchall()
            db.rollback()
        finally:
            db.close()
    result['runtime'] = str(RUNTIME)
    result['worldserverSha256'] = hashlib.sha256((RUNTIME/'worldserver.exe').read_bytes()).hexdigest()
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(capture(),indent=2,default=str)+'\n')
    print('Read-only database snapshot saved:',args.out)
