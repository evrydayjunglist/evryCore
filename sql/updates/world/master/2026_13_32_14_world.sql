-- Chromie Time: Dorn refuse gossip presentation (ct-start 12.0.7.68887)
-- Menu 25426 + npc_text 40348 → BroadcastText 269086 (Radiant Song / Isle of Dorn), 0 options.
-- Script npc_chromie_time shows this when present and CanSelectChromieTimeExpansion() is false.
-- Flow 2R dual-check PASS: NN=14 free on master+evry (tip world 10-13; master/evry max 09).

DELETE FROM `npc_text` WHERE `ID` = 40348;
INSERT INTO `npc_text` (`ID`, `Probability0`, `Probability1`, `Probability2`, `Probability3`, `Probability4`, `Probability5`, `Probability6`, `Probability7`, `BroadcastTextID0`, `BroadcastTextID1`, `BroadcastTextID2`, `BroadcastTextID3`, `BroadcastTextID4`, `BroadcastTextID5`, `BroadcastTextID6`, `BroadcastTextID7`, `VerifiedBuild`) VALUES
(40348, 1, 0, 0, 0, 0, 0, 0, 0, 269086, 0, 0, 0, 0, 0, 0, 0, 68887);

UPDATE `creature_template` SET `ScriptName` = 'npc_chromie_time', `VerifiedBuild` = 68887 WHERE `entry` = 167032;
