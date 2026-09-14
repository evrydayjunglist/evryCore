from pathlib import Path
import re
import argparse
import pymysql

parser = argparse.ArgumentParser(description='Check boost SQL using connection-local temporary tables only.')
parser.add_argument('--config', required=True, type=Path, help='Path to worldserver.conf; requires pymysql')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
config = args.config.read_text()
value = re.search(r'^CharacterDatabaseInfo\s*=\s*"([^"]+)"', config, re.M)[1].split(';')
db = pymysql.connect(host=value[0], port=int(value[1]), user=value[2], password=value[3], database=value[4], autocommit=False)
tables = ['characters', 'character_inventory', 'item_instance', 'character_select_screen_equipment_cache', 'battlepay_account_distribution', 'mail', 'mail_items']
sources = (root / 'src/server/database/Database/Implementation/CharacterDatabase.cpp').read_text()
mgr = (root / 'src/server/game/BattlePay/BattlePayMgr.cpp').read_text()

def sql_literals(text):
    return ''.join(re.findall(r'"([^"\n]*)"', text))
def statement(name):
    block = re.search(r'PrepareStatement\(' + name + r', (.*?), CONNECTION_\w+\);', sources, re.S)[1]
    if 'CharacterSelectEquipment' in block:
        macro = sources[sources.index('#define CharacterSelectEquipment(table_alias)'):sources.index('    PrepareStatement', sources.index('#define CharacterSelectEquipment(table_alias)'))]
        slots = re.findall(r'CharacterSelectEquipmentSlot\(table_alias, "(\w+)"\)', macro)
        suffixes = ['EquippedItemID', 'VisibleItemID', 'Subclass', 'InvType', 'DisplayID', 'DisplayEnchantID', 'SecondaryItemModifiedAppearanceID', 'SheatheCategory']
        cols = ','.join(slot + suffix for slot in slots for suffix in suffixes)
        block = block.replace('CharacterSelectEquipment("")', '"' + cols + '"')
    return sql_literals(block).replace('?', '%s')

with db:
    with db.cursor() as cur:
        for table in tables:
            cur.execute(f'CREATE TEMPORARY TABLE bpverify_{table} LIKE {table}')
        def execute(sql, args=None):
            # Every DML target is replaced with a connection-local temporary table.
            for table in sorted(tables, key=len, reverse=True):
                sql = re.sub(r'\b' + table + r'\b', 'bpverify_' + table, sql, flags=re.I)
            assert not any(re.search(r'\b' + table + r'\b', sql, re.I) for table in tables)
            assert not re.search(r'\b(?:DROP|ALTER|TRUNCATE|CREATE)\b', sql, re.I)
            cur.execute(sql, args)
        def snapshot():
            result = []
            for table in tables:
                execute(f'SELECT * FROM {table} ORDER BY 1')
                result.append(cur.fetchall())
            return result

        execute('SHOW COLUMNS FROM characters')
        character = {col[0]: ('' if any(t in col[1] for t in ['text', 'char']) else 0)
            for col in cur.fetchall() if col[2] == 'NO' and col[4] is None}
        character.update(guid=100, account=2, name='BoostTest', level=1, inventorySlots=16, money=250000)
        character['class'] = 8
        character['race'] = 2
        execute('INSERT INTO characters (' + ','.join('`' + col + '`' for col in character) + ') VALUES (' + ','.join(['%s'] * len(character)) + ')', tuple(character.values()))
        execute("INSERT INTO battlepay_account_distribution (distributionId, accountId, productId) VALUES (9876, 2, 1161)")
        execute("INSERT INTO item_instance (guid, itemEntry, owner_guid, count, charges, enchantments) VALUES (1000, 111, 100, 1, '', ''), (1001, 112, 100, 1, '', ''), (1002, 6948, 100, 1, '', '')")
        execute("INSERT INTO character_inventory (guid, bag, slot, item) VALUES (100, 0, 4, 1000), (100, 0, 30, 1001), (100, 0, 35, 1002)")
        execute("INSERT INTO character_select_screen_equipment_cache (guid, chestEquippedItemID, chestVisibleItemID, chestDisplayID) VALUES (100, 111, 111, 1111)")
        db.commit()
        baseline = snapshot()
        update = re.search(r'trans->PAppend\(("UPDATE characters SET level = .*?)\);', mgr, re.S)[1]
        update = sql_literals(update).replace('{}', '%s')

        for inject_failure in [True, False]:
            db.begin()
            try:
                execute(statement('CHAR_INS_MAIL'), (123, 0, 41, 0, 0, 100, 'Recovered', 'Test', 1, 2000000000, 1900000000, 0, 0, 4))
                for item in [1000, 1001]:
                    execute(statement('CHAR_DEL_CHAR_INVENTORY_BY_ITEM'), (item,))
                    execute(statement('CHAR_INS_MAIL_ITEM'), (123, item, 100))
                execute(statement('CHAR_REP_ITEM_INSTANCE'), (222, 100, 0, 0, 1, 0, '', 1, '', 0, 100, 0, 1900000000, '', 0, 0, 0, 0, 62, '', 2000))
                execute('INSERT INTO character_inventory (guid, bag, slot, item) VALUES (100, 0, 4, 2000)')
                execute(statement('CHAR_DEL_CHARACTER_SELECT_EQUIPMENT_CACHE_CUSTOMIZATIONS'), (100,))
                equipment = [100] + [0] * (19 * 8)
                equipment[1 + 4 * 8 : 1 + 5 * 8] = [222, 222, 1, 20, 2222, 0, 0, 0]
                execute(statement('CHAR_INS_CHARACTER_SELECT_EQUIPMENT_CACHE_CUSTOMIZATIONS'), equipment)
                execute(update, (80, 100000, 2552, 14771, 2633.37, -2591.66, 219.659, 0, 62, 0, 1900000000, 100, 2))
                execute(statement('CHAR_UPD_BATTLEPAY_DISTRIBUTION_ASSIGN'), (4, 100, 62, 9876, 2))
                if inject_failure:
                    execute('INSERT INTO character_inventory (guid, bag, slot, item) VALUES (100, 0, 35, 9999)')
                db.commit()
            except pymysql.IntegrityError:
                assert inject_failure
                db.rollback()
                assert snapshot() == baseline, 'Rollback did not restore all seven temporary tables'
                print('PASS: a conflicting inventory insert rolls back gear, cache, mail, level and boost consumption')
                continue
            assert not inject_failure
            execute(statement('CHAR_SEL_BATTLEPAY_DISTRIBUTION'), (9876, 2))
            assert cur.fetchone() == (1161, 1, 1, 100, 62)
            execute('SELECT level, primarySpecialization, money FROM characters WHERE guid = 100')
            assert cur.fetchone() == (80, 62, 250000)
            execute('SELECT chestEquippedItemID, chestVisibleItemID, chestDisplayID FROM character_select_screen_equipment_cache WHERE guid = 100')
            assert cur.fetchone() == (222, 222, 2222)
            execute('SELECT item_guid FROM mail_items ORDER BY item_guid')
            assert cur.fetchall() == ((1000,), (1001,))
            execute('SELECT item FROM character_inventory ORDER BY item')
            assert cur.fetchall() == ((1002,), (2000,))
            execute('SELECT COUNT(*) FROM item_instance')
            assert cur.fetchone()[0] == 4
            print('PASS: saved gear and appearance agree, old items are mailed, backpack and money are preserved, one boost is completed')
print('PASS: validation used connection-local temporary tables only; no gameplay rows were modified')
