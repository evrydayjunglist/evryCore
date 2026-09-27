"""Exercise the damage package against its captured schema on disposable MySQL."""
from contextlib import redirect_stdout
import copy
import io
import json
from pathlib import Path
import sys
import unittest
import uuid
from unittest.mock import patch

# The existing fixture verifies its private datadir and port before every connection.
EMULATORS = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(EMULATORS/'Source/evryLoader/diagnostics/reaper/effects'))
import deploy
import records
import test_remaining as fixture
import reaper_damage_deploy as damage
import pymysql


class DeploymentTests(unittest.TestCase):
    setUpClass = classmethod(fixture.DeploymentTests.setUpClass.__func__)
    tearDownClass = classmethod(fixture.DeploymentTests.tearDownClass.__func__)
    guard = classmethod(fixture.DeploymentTests.guard.__func__)
    database = fixture.DeploymentTests.database
    change = fixture.DeploymentTests.change

    def setUp(self):
        self.schema = 'damage_'+uuid.uuid4().hex
        self.package = self.root/self.schema
        self.package.mkdir()
        self.m = copy.deepcopy(fixture.BASELINE)
        self.client, self.runtime = self.package/'client', self.package/'runtime'
        self.runtime.mkdir()
        marker = self.client/'mappings/prior.txt'
        marker.parent.mkdir(parents=True)
        marker.write_bytes(b'123;previous-effect\n')
        self.m.update(client=str(self.client),runtime=str(self.runtime),files=[],packageFiles={},
            protected=[dict(path=str(marker),sha256=deploy.digest(marker))],
            otherMappings=damage.mapping_snapshot(self.client))
        for name in damage.OWNED_FILES:
            payload,backup='payload/'+name,'backup/'+name
            for relative,raw in ((payload,b'candidate '+name.encode()),(backup,b'baseline '+name.encode())):
                path=self.package/relative
                path.parent.mkdir(exist_ok=True)
                path.write_bytes(raw)
                self.m['packageFiles'][relative]=deploy.digest(path)
            (self.runtime/name).write_bytes((self.package/backup).read_bytes())
            self.m['files'].append(dict(target=name,payload=payload,backup=backup,
                before=deploy.digest(self.package/backup),after=deploy.digest(self.package/payload)))
        self.unrelated=dict(Id=111780,UniqueId=777777,TableHash=123,RecordId=555,Status=1,VerifiedBuild=69814)
        with self.admin.cursor() as c:
            c.execute('CREATE DATABASE `'+self.schema+'` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci')
        with self.database(self.m) as db:
            with db.cursor() as c:
                for ddl in self.m['schemas'].values():c.execute(ddl)
                for table,rows in self.m['protectedRows'].items():
                    for row in rows:c.execute(records.insert(table,row).rstrip(';'))
                for change in self.m['changes']:
                    c.execute(records.insert(change['table'],change['before']).rstrip(';'))
                for row in [*self.m['protectedPushes'],self.unrelated]:
                    c.execute(records.insert('hotfix_data',row).rstrip(';'))
            db.commit()
        for install,name in ((True,'forward.sql'),(False,'restore.sql')):
            (self.package/name).write_text(damage.sql(self.m,install))
            self.m['packageFiles'][name]=deploy.digest(self.package/name)
        deploy.save(self.package/'package.json',self.m)
        for name,value in (('database',self.database),('running',lambda: [])):
            mock=patch.object(deploy,name,value)
            mock.start()
            self.addCleanup(mock.stop)

    def run_mode(self,mode):
        with redirect_stdout(io.StringIO()) as out:
            damage.run(self.package,mode)
        return out.getvalue()

    def check(self):
        with self.database(self.m,True) as db:
            result=damage.inspect(db,self.m,damage.load_journal(self.package))
            with db.cursor() as c:
                c.execute('SELECT * FROM hotfix_data WHERE TableHash=123')
                self.assertEqual(list(c.fetchall()),[self.unrelated])
            return result

    def test_check_does_not_write(self):
        before={str(p):deploy.digest(p) for p in self.package.rglob('*') if p.is_file()}
        self.assertIn('"baseline": true',self.run_mode('check'))
        self.assertEqual(before,{str(p):deploy.digest(p) for p in self.package.rglob('*') if p.is_file()})

    def test_install_restore_reinstall_and_idempotence(self):
        for mode in ('install','install','restore','restore','install'):
            self.run_mode(mode)
            self.assertEqual(self.check()['rows'],'installed' if mode=='install' else 'baseline')
            self.assertIn('"installed": true' if mode=='install' else '"baseline": true',self.run_mode('check'))
        self.assertEqual(len(damage.load_journal(self.package)['operations']),3)

    def test_restore_publishes_six_exact_original_records(self):
        self.run_mode('install')
        self.run_mode('restore')
        self.assertEqual(len(self.check()['history']),6)
        self.assertTrue(all(r['Status']==1 for r in self.check()['history']))

    def test_partial_file_copy_can_restore(self):
        real=deploy.atomic_copy
        def fail(source,target):
            if target.name=='worldserver.pdb':raise OSError('interruption')
            real(source,target)
        with patch.object(deploy,'atomic_copy',fail),self.assertRaises(OSError):self.run_mode('install')
        self.run_mode('restore')
        self.assertIn('"baseline": true',self.run_mode('check'))

    def test_commit_failure_retry_and_restore(self):
        for _ in range(2):
            with patch.object(pymysql.connections.Connection,'commit',side_effect=OSError('interruption')),self.assertRaises(OSError):
                self.run_mode('install')
            self.assertEqual(self.check()['rows'],'baseline')
        self.run_mode('install')
        self.run_mode('restore')
        self.assertIn('"baseline": true',self.run_mode('check'))

    def test_lost_post_commit_journal_can_restore(self):
        real=deploy.save
        def fail(path,value):
            if value.get('state')=='installed':raise OSError('interruption')
            real(path,value)
        with patch.object(deploy,'save',fail),self.assertRaises(OSError):self.run_mode('install')
        self.assertEqual(self.check()['rows'],'installed')
        self.run_mode('restore')
        self.assertIn('"baseline": true',self.run_mode('check'))

    def test_restore_commit_failure_can_retry(self):
        self.run_mode('install')
        with patch.object(pymysql.connections.Connection,'commit',side_effect=OSError('interruption')),self.assertRaises(OSError):
            self.run_mode('restore')
        self.assertEqual(self.check()['rows'],'installed')
        self.run_mode('restore')
        self.assertIn('"baseline": true',self.run_mode('check'))

    def test_unrelated_damage_fields_are_protected(self):
        self.m['changes'][0]['after']['ImplicitTarget1']=1
        deploy.save(self.package/'package.json',self.m)
        with self.assertRaisesRegex(ValueError,'outside the approved scope'):self.run_mode('install')

    def test_foreign_damage_row_refused(self):
        self.change('UPDATE spell_effect SET EffectBasePoints=999 WHERE ID=2890')
        with self.assertRaisesRegex(ValueError,'Foreign damage record'):self.run_mode('install')

    def test_extra_effect_and_locale_refused(self):
        row=dict(self.m['changes'][0]['before'],ID=888888,EffectIndex=5)
        self.change(records.insert('spell_effect',row).rstrip(';'))
        with self.assertRaisesRegex(ValueError,'Foreign Reaper record'):self.run_mode('install')
        self.change('DELETE FROM spell_effect WHERE ID=888888')
        row=dict(self.m['changes'][4]['before'],locale='frFR')
        self.change(records.insert('hotfix_blob',row).rstrip(';'))
        with self.assertRaisesRegex(ValueError,'Missing or duplicate'):self.run_mode('install')

    def test_old_lifetime_and_visual_are_preserved(self):
        self.change('UPDATE spell_duration SET Duration=30000 WHERE ID=100')
        with self.assertRaisesRegex(ValueError,'Protected database record'):self.run_mode('install')

    def test_foreign_or_missing_push_refused(self):
        self.run_mode('install')
        op=damage.load_journal(self.package)['operations'][-1]
        self.change('DELETE FROM hotfix_data WHERE Id='+str(op['id']))
        with self.assertRaisesRegex(ValueError,'push missing'):self.run_mode('restore')

    def test_foreign_push_refused(self):
        row=dict(self.m['protectedPushes'][0],Id=111782,UniqueId=888)
        self.change(records.insert('hotfix_data',row).rstrip(';'))
        with self.assertRaisesRegex(ValueError,'Foreign damage push'):self.run_mode('install')

    def test_running_server_prevents_any_write(self):
        with patch.object(deploy,'running',return_value=[{'ProcessName':'worldserver'}]),self.assertRaisesRegex(ValueError,'Close WoW'):
            self.run_mode('install')
        self.assertFalse((self.package/'deployment.json').exists())

    def test_runtime_start_before_commit_can_restore(self):
        with patch.object(deploy,'running',side_effect=[[],[],[{'ProcessName':'Wow'}]]),self.assertRaisesRegex(ValueError,'before commit'):
            self.run_mode('install')
        self.run_mode('restore')
        self.assertIn('"baseline": true',self.run_mode('check'))

    def test_foreign_executable_refused(self):
        (self.runtime/'worldserver.exe').write_bytes(b'owner replacement')
        with self.assertRaisesRegex(ValueError,'Foreign runtime target'):self.run_mode('install')

    def test_extra_mapping_refused(self):
        (self.client/'mappings/other.txt').write_bytes(b'999;other\n')
        with self.assertRaisesRegex(ValueError,'Other numeric mappings'):self.run_mode('install')

    def test_rehashed_broad_sql_refused(self):
        (self.package/'forward.sql').write_text('DELETE FROM spell_effect;\n')
        self.m['packageFiles']['forward.sql']=deploy.digest(self.package/'forward.sql')
        deploy.save(self.package/'package.json',self.m)
        with self.assertRaisesRegex(ValueError,'Unexpected damage SQL'):self.run_mode('install')


if __name__=='__main__':
    unittest.main(argv=[sys.argv[0],*fixture.REST])
