-- Chromie Time Phase 1: Org Chromie 167032 spawn + gossip menu 25426 options (CT-A 12.0.7.68887)
-- Flow 2R dual-check PASS: NN=10 free on master+evry (evry world uses 00-09)

-- Orgrimmar Chromie (retail CT-A position). Existing Stormwind spawn guid 8000063 left as-is.
DELETE FROM `creature` WHERE `guid` = 11800156;
INSERT INTO `creature` (`guid`, `id`, `map`, `zoneId`, `areaId`, `spawnDifficulties`, `phaseUseFlags`, `PhaseId`, `PhaseGroup`, `terrainSwapMap`, `modelid`, `equipment_id`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecs`, `wander_distance`, `currentwaypoint`, `curHealthPct`, `MovementType`, `npcflag`, `unit_flags`, `unit_flags2`, `unit_flags3`, `ScriptName`, `StringId`, `VerifiedBuild`) VALUES
(11800156, 167032, 1, 1637, 5170, '0', 0, 0, 0, -1, 0, 0, 1557.1771, -4216.5415, 56.071663, 1.017489, 120, 0, 0, NULL, 0, NULL, NULL, NULL, NULL, '', NULL, 68887);

-- Menu 25426 already linked via creature_template_gossip; options were missing.
DELETE FROM `gossip_menu_option` WHERE `MenuID` = 25426 AND `GossipOptionID` IN (51901, 51902, 51903);
INSERT INTO `gossip_menu_option` (`MenuID`, `GossipOptionID`, `OptionID`, `OptionNpc`, `OptionText`, `OptionBroadcastTextID`, `Language`, `Flags`, `ActionMenuID`, `ActionPoiID`, `GossipNpcOptionID`, `BoxCoded`, `BoxMoney`, `BoxText`, `BoxBroadcastTextID`, `SpellID`, `OverrideIconID`, `VerifiedBuild`) VALUES
(25426, 51901, 0, 40, '|cFF0000FF(Recommended)|r Select a timeline.', 0, 0, 0, 0, 0, 32282, 0, 0, NULL, 0, NULL, NULL, 68887),
(25426, 51902, 1, 40, 'Select a different timeline.', 0, 0, 0, 0, 0, 32282, 0, 0, NULL, 0, NULL, NULL, 68887),
(25426, 51903, 2, 0, 'I''d like to return to the present timeline, Chromie.', 0, 0, 0, 0, 0, NULL, 0, 0, NULL, 0, 335807, NULL, 68887);
