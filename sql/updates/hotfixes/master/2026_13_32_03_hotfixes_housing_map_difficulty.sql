-- Housing: server-only map difficulty rows for the three housing maps.
--
-- Neither the 12.1 client's MapDifficulty.db2 nor the hotfixes database has a
-- row for map 2735 (Founder's Point), 2736 (Razorwind Shores) or 2783 (Home
-- Interior). ObjectMgr accepts a creature or gameobject spawn only in a
-- difficulty its map has a MapDifficulty row for, so without these rows every
-- database spawn on those maps is skipped at startup as "not spawned in any
-- difficulty".
--
-- These rows are ours, not Blizzard's. Retail's world server info reports
-- difficulty 0 on 2736 and on 2783 (the 25 July 2026 retail capture hbcd3,
-- lines 365412 and 1354029), so each map gets one row, for difficulty 0. Every
-- other field is 0, the same shape as the client's own difficulty 0 rows for
-- open maps, for example row 4439 for Exile's Reach (map 2175).
--
-- VerifiedBuild 0 marks them as custom rows: DB2DatabaseLoader loads rows whose
-- VerifiedBuild is 0 or below after the others. No hotfix_data row names them,
-- so nothing is pushed to clients.
--
-- The ids 843 to 845 open the largest run of unused ids inside the client's
-- range (843 to 1579 in the 12.1 file, whose ids run from 2 to 6429). They are
-- not taken from just above 6429, because that is where the next client's new
-- rows will land, and a custom row is loaded over a client row with the same id,
-- which would quietly replace a real map's difficulty. On 24 September 2026
-- they were free in the 12.1 MapDifficulty.db2, in the live hotfixes
-- map_difficulty table and hotfix_data, and no MapDifficultyXCondition row in
-- the 12.1 file or the live hotfixes table points at them.

DELETE FROM `map_difficulty` WHERE `ID` IN (843, 844, 845) AND `VerifiedBuild` = 0;
INSERT INTO `map_difficulty` (`Message`, `ID`, `DifficultyID`, `LockID`, `ResetInterval`, `MaxPlayers`, `ItemContext`, `ItemContextPickerID`, `Flags`, `ContentTuningID`, `WorldStateExpressionID`, `MapID`, `VerifiedBuild`) VALUES
('', 843, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2735, 0), -- Founder's Point
('', 844, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2736, 0), -- Razorwind Shores
('', 845, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2783, 0); -- Home Interior
