# Reaper damage package: owner installation and test

This is for the custom **12.1.0.69814** client at `F:\evry\WOWEmulation\Clients\World of Warcraft\_retail_`, with visual effects package-05 already installed. The separate retail 69933 client is a comparison source, not an installation target.

The package replaces the server executable and matching debug symbols, then publishes six new hotfix records for Murder and Soulrend. It does not start or stop any program. Install and Restore refuse to write while WoW, worldserver, or bnetserver is open.

## Install

1. Exit WoW normally. In the **worldserver** console type `server shutdown 0` and press Enter. Wait for it to exit. Close the **bnetserver** console normally. Leave MySQL running.
2. Open **Windows PowerShell** and paste:

   ```powershell
   $damagePackage = 'F:\evry\WOWEmulation\Emulators\Builds\reaper-class\damage-scaling-69814-20260926\package-01'
   & "$damagePackage\Install.ps1" -Mode Check
   & "$damagePackage\Install.ps1" -Mode Install
   & "$damagePackage\Install.ps1" -Mode Check
   ```

   The final check must show `installed: true`, `database: installed`, `records: 6`, and `files: 2`. If any command errors, stop and provide the error. Do not start the servers after a failed write. The same package can retry Install or Restore after the cause is corrected.
3. Start the servers from their normal runtime folder. If you need the commands, open two PowerShell windows. In the first:

   ```powershell
   Set-Location 'F:\evry\WOWEmulation\Emulators\Builds\evryCore-job-modules\bin\RelWithDebInfo'
   .\bnetserver.exe
   ```

   In the second:

   ```powershell
   Set-Location 'F:\evry\WOWEmulation\Emulators\Builds\evryCore-job-modules\bin\RelWithDebInfo'
   .\worldserver.exe
   ```

   Wait for the worldserver startup to finish. Launch `F:\evry\WOWEmulation\Clients\World of Warcraft\_retail_\Wow.exe` and log in to your undead Reaper.

## Short test

1. In WoW chat type `/combatlog` once to start recording.
2. Fight the same **Mindless Zombies**. Use several **Reaps**, several **Murders**, and at least two **Soulrends** when Soul Infusion lights up. Do not kill a target with Reap while Murder's crow is still travelling to it.
3. Confirm that Murder's crow impact now removes health and Soulrend produces a clear damage hit. Reap should retain its previous behavior. Exact numbers depend on level, gear, critical hits and target scaling; no single number is required.
4. Confirm Murder's cost, crow/gesture, soul generation, Soulrend's resource consumption, and the buff icons still behave as before. Stored souls and infusion must have no four-day timer; fragments still expire after 30 seconds.
5. Type `/combatlog` again to stop recording. Reply **recorded** and mention anything visibly wrong. The log is in the custom client's `Logs` folder; Codex can read it directly.

Optional extra comparison: a level-1 undead warrior on the emulator, wearing its starting gear, can record Slam against the same zombies. The owner's real retail build-69933 test delivered eight Slam hits of 43–45 and normal weapon swings of 6. It is a useful comparison, not an exact equality requirement across builds and character state.

## Restore

Close WoW and both servers normally as above. In PowerShell:

```powershell
$damagePackage = 'F:\evry\WOWEmulation\Emulators\Builds\reaper-class\damage-scaling-69814-20260926\package-01'
& "$damagePackage\Install.ps1" -Mode Restore
& "$damagePackage\Install.ps1" -Mode Check
```

Expect `baseline: true`, `database: baseline`, and `state: restored`. This restores both original server files and all six original spell records, and publishes a fresh restoration hotfix. Restore this damage package **before** using an older visual package's checker or rollback, because those older packages pin the old server hash. Earlier visual packages and their files remain preserved.

Automated tests prove the core calculation and package lifecycle. They do not establish native gameplay acceptance, complete level-90 balance, or general client-build-69933 compatibility.
