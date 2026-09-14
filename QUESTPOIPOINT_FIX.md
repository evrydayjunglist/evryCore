# QuestPOIPoint startup correction

The `game` archaeology implementation declared `QuestPOIPoint.QuestPOIBlobID` as signed. TrinityCore's DB2 loader requires parent index fields to be unsigned and rejects that declaration during startup. The final enUS locale error is the result of that rejection.

This correction is prepared on `job/questpoi-parent-ids`, based on `game` commit `5b4324546aefbc69de98201934137db8a870c985`. It carries only the QuestPOIPoint corrections from `evry` commit `b7ef9145315b2ea683ecd0194bb5f6714559e51e`, plus regression coverage and documentation. The CatalogShop and BattlePay changes in that commit are outside this fix.

## What changes

- `DB2LoadInfo.h` declares the point's parent field unsigned, and `DB2Structure.h` stores it as `uint32`.
- Archaeology groups points by an unsigned parent ID. The separate signed `ResearchSite.QuestPOIBlobID` still rejects zero and negative references before conversion to an unsigned lookup key.
- The original archaeology table-creation update declares the hotfix column unsigned. The existing `2026_09_11_01_hotfixes.sql` correction updates tables created with the previous signed declaration.
- The DB2 loader's validation and generated DB2 metadata remain unchanged.
- The archaeology README entry describes the correction. Its feature estimate remains approximately 80 percent because the listed gameplay work still remains.

## Merge behavior

The five runtime and SQL files match the correction already on `evry`. The SQL update retains the exact filename and contents from `evry`; there is no second migration with a different name. These matching changes allow Git to retain one copy when the corrected `game` is merged into `evry`.

The merge check uses `git merge-tree` with the current `game` as the base and a preview tree containing the complete working changes. It does not commit, stage, move branches, or change either working folder. The checked `evry` revision is `6d9b57773622cc41b69d86eb8bfda07ca06f86b8`. Later changes to either branch can require another check; no workflow can promise that all future unrelated edits will be conflict-free.

## Validation

The dedicated build is `D:\WOWEmulation\Emulators\Builds\evryCore-job-modules\questpoi-parent-ids`. Its source is the separate working folder `D:\WOWEmulation\Emulators\Source\evryCore-job-questpoi-parent-ids`. This keeps the original Timerunning files and the normal runtime executable intact.

The deterministic regression loads a small WDC5 fixture through `DB2FileLoader` and produces the actual `QuestPOIPointEntry`. It verifies parent ID 4294967295 and signed coordinates, then verifies that the former signed field declaration produces the original parent-index error. A separate optional test opens an installed DB2 file read-only and exercises the same loader and record production without starting worldserver or connecting to databases. No client data is included in the repository.

The SQL check uses a newly initialized MySQL 8.4 process with TCP and MySQL X networking disabled and a unique named pipe. It checks fresh creation, upgrade of an existing signed table, repeated application to an already-corrected table, preservation of records and signed coordinates, and refusal of invalid negative parent data under strict SQL mode. Its script and logs are retained in the dedicated build folder.

The RelWithDebInfo `worldserver` and `tests` targets built with warnings treated as errors. All three QuestPOIPoint cases passed, with 14 assertions. The installed-file check loaded 170,336 records, all with parent IDs. All 41 automatically registered CTest cases passed; the installed-file test is run separately by the explicit tag selector. The SQL checks passed. Full-suite and merge-preview logs are stored alongside the build.

Open PowerShell and run the following to repeat the build and DB2 checks:

```powershell
Set-Location 'D:\WOWEmulation\Emulators\Source\evryCore-job-questpoi-parent-ids'
$env:PATH = 'C:\Program Files\MySQL\MySQL Server 8.4\lib;C:\Program Files\OpenSSL-Win64\bin;' + $env:PATH
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake -S D:\WOWEmulation\Emulators\Source\evryCore-job-questpoi-parent-ids -B D:\WOWEmulation\Emulators\Builds\evryCore-job-modules\questpoi-parent-ids -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON -DSCRIPTS=static -DTOOLS=OFF -DWITH_WARNINGS_AS_ERRORS=ON -DCMAKE_NINJA_FORCE_RESPONSE_FILE=ON -DBoost_DIR=C:/local/boost_1_85_0/lib64-msvc-14.3/cmake/Boost-1.85.0 -DOPENSSL_ROOT_DIR="C:/Program Files/OpenSSL-Win64" && cmake --build D:\WOWEmulation\Emulators\Builds\evryCore-job-modules\questpoi-parent-ids --target worldserver tests -j 4'
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$env:TC_QUEST_POI_POINT_DB2 = 'D:\WOWEmulation\Emulators\Builds\evryCore-job-modules\bin\RelWithDebInfo\data\dbc\enUS\QuestPOIPoint.db2'
& 'D:\WOWEmulation\Emulators\Builds\evryCore-job-modules\questpoi-parent-ids\bin\RelWithDebInfo\tests.exe' '[QuestPOIPoint]'
if ($LASTEXITCODE -ne 0) { throw 'QuestPOIPoint checks failed.' }
ctest --test-dir D:\WOWEmulation\Emulators\Builds\evryCore-job-modules\questpoi-parent-ids -C RelWithDebInfo --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
python D:\WOWEmulation\Emulators\Builds\evryCore-job-modules\questpoi-parent-ids\check_questpoi_sql.py --source D:\WOWEmulation\Emulators\Source\evryCore-job-questpoi-parent-ids
if ($LASTEXITCODE -ne 0) { throw 'Private SQL checks failed.' }
git diff --check
```

The response-file setting keeps the Windows compiler command within the operating system's length limit. CMake can report that this internal generator setting is unused by the project even though Ninja uses it. The PATH addition applies only to this PowerShell process and lets the isolated test executable find the installed MySQL and OpenSSL libraries without copying runtime files.

These checks validate this startup failure and the SQL correction. They do not establish a complete worldserver startup, archaeology gameplay, or retail-client acceptance. The original failed Timerunning startup already ran database updates before reaching DB2 loading; a failed startup is not evidence that the databases were untouched.

## Commit and integration

The agent leaves the fix uncommitted. `AGENTS.md` assigns commits, merges, and pushes to the owner. The original repository remains on `job/timerunning` with its existing uncommitted work. The separate working folder contains only this correction and its documentation/tests.

First commit the fix from its separate folder. Open PowerShell and run:

```powershell
Set-Location 'D:\WOWEmulation\Emulators\Source\evryCore-job-questpoi-parent-ids'
if ((git branch --show-current) -ne 'job/questpoi-parent-ids') { throw 'Unexpected branch.' }
git status --short --branch
git diff --check
git add -- README.md QUESTPOIPOINT_FIX.md `
  sql/updates/hotfixes/master/2026_09_11_00_hotfixes.sql `
  sql/updates/hotfixes/master/2026_09_11_01_hotfixes.sql `
  src/server/game/Archaeology/ArchaeologyMgr.cpp `
  src/server/game/DataStores/DB2LoadInfo.h `
  src/server/game/DataStores/DB2Structure.h `
  tests/game/QuestPOIPoint.cpp
if ($LASTEXITCODE -ne 0) { throw 'Staging failed.' }
git diff --cached --check
if ($LASTEXITCODE -ne 0) { throw 'Staged whitespace check failed.' }
git diff --cached --stat
git commit -m 'fix(archaeology): correct QuestPOIPoint parent IDs on game' -m 'Bring the existing evry correction into game without importing CatalogShop or BattlePay work. Align DB2 loading, point storage, archaeology lookups and the hotfix column with unsigned parent IDs. Preserve the existing SQL update filename and contents so game-to-evry merges retain one correction. Add production-loader regression coverage for unsigned parent IDs and signed coordinates, plus an optional read-only installed-data check.'
if ($LASTEXITCODE -ne 0) { throw 'Commit failed.' }
git status --short --branch
git log -1 --oneline
```

The final status should have no modified or untracked files. Then advance `game` and bring its correction into the original Timerunning job. These commands do not commit or discard the Timerunning changes. The fast-forward merge advances that branch to the corrected base and carries its unrelated uncommitted files along.

```powershell
Set-Location 'D:\WOWEmulation\Emulators\Source\evryCore-job-questpoi-parent-ids'
if (git status --porcelain) { throw 'The fix folder must be clean before integration.' }
git switch game
if ($LASTEXITCODE -ne 0) { throw 'Could not select game.' }
git merge --ff-only job/questpoi-parent-ids
if ($LASTEXITCODE -ne 0) { throw 'game has changed; stop for a fresh merge check.' }
git switch job/questpoi-parent-ids
if ($LASTEXITCODE -ne 0) { throw 'Could not return to the fix branch.' }

Set-Location 'D:\WOWEmulation\Emulators\Source\evryCore'
if ((git branch --show-current) -ne 'job/timerunning') { throw 'Unexpected branch in the original folder.' }
git merge --ff-only game
if ($LASTEXITCODE -ne 0) { throw 'Stop and report the merge message; do not discard or stash the Timerunning work.' }
git status --short --branch
git log -1 --oneline
```

The original folder should still list the Timerunning changes, and its latest commit should be the QuestPOIPoint correction. Rebuild that folder before testing its executable:

```powershell
Set-Location 'D:\WOWEmulation\Emulators\Source\evryCore'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake -S D:\WOWEmulation\Emulators\Source\evryCore -B D:\WOWEmulation\Emulators\Builds\evryCore-job-modules && cmake --build D:\WOWEmulation\Emulators\Builds\evryCore-job-modules --target worldserver tests --config RelWithDebInfo -j 4'
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
```

This last build is still the Timerunning job based on `game`; it does not contain the evry modules. The normal gameplay build continues to belong to `evry`.

The existing `evry` already has the runtime correction. When ready to carry the corrected `game` forward, use the separate folder so the unfinished Timerunning work stays in place:

```powershell
Set-Location 'D:\WOWEmulation\Emulators\Source\evryCore-job-questpoi-parent-ids'
if (git status --porcelain) { throw 'The fix folder must be clean.' }
git switch evry
if ($LASTEXITCODE -ne 0) { throw 'Could not select evry.' }
git merge --no-edit game
if ($LASTEXITCODE -ne 0) { throw 'Stop and report the merge message.' }
git diff game -- src/server/game/Archaeology/ArchaeologyMgr.cpp src/server/game/DataStores/DB2LoadInfo.h src/server/game/DataStores/DB2Structure.h sql/updates/hotfixes/master/2026_09_11_00_hotfixes.sql sql/updates/hotfixes/master/2026_09_11_01_hotfixes.sql
git status --short --branch
git switch job/questpoi-parent-ids
```

For the verified revisions, the diff of those five files should be empty, and `evry` should be clean after its merge. Its extra README sections remain intact. Rebuild from the intended `evry` source before normal gameplay. Do not remove the temporary branch or working folder until the owner has finished integration and no longer needs its validation artifacts.
