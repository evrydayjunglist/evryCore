-- Demon Hunter Mardum (map 1481, GhostZone 7705): graveyard links and Area Spirit Healer 65183.
-- Horde sniff: SMSG_DEATH_RELEASE_LOC / teleport at world_safe_locs 5082, healer 65183 at
-- (1180.7778, 3309.2378, 75.193016), then CMSG_AREA_SPIRIT_HEALER_QUERY/QUEUE and aura 2584.
-- Live world.graveyard_zone had no 7705 rows, so GetClosestGraveyard fell back off-map.
-- Safe locs 5082, 5083, 5119, 5140, 5188, 5284 already exist. Corpse-catcher locs have no healer.
-- Template 65183 already has UNIT_NPC_FLAG_AREA_SPIRIT_HEALER. Guid 11800191 is unused
-- (creature guid max on this world was 11800156).

SET @CGUID := 11800191;

DELETE FROM `graveyard_zone` WHERE `GhostZone`=7705 AND `ID` IN (5082,5083,5119,5140,5188,5284);
INSERT INTO `graveyard_zone` (`ID`, `GhostZone`, `Comment`) VALUES
(5082, 7705, 'DH-Mardum - (01) Start'),
(5083, 7705, 'DH-Mardum - (03) Seat of Command'),
(5119, 7705, 'DH-Mardum - (04) Illidari Foothold'),
(5140, 7705, 'DH-Mardum - (05) Volcano'),
(5188, 7705, 'DH-Mardum - (06) The Fel Hammer'),
(5284, 7705, 'DH-Mardum - (02) Molten Shore');

-- Start GY position is from the sniff. Other stands sit about 5.4 yards north of the matching
-- world_safe_locs row, the same offset as 5082 versus its healer.
DELETE FROM `creature` WHERE `guid` BETWEEN @CGUID+0 AND @CGUID+5;
INSERT INTO `creature` (`guid`, `id`, `map`, `zoneId`, `areaId`, `spawnDifficulties`, `phaseUseFlags`, `PhaseId`, `PhaseGroup`, `terrainSwapMap`, `modelid`, `equipment_id`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecs`, `wander_distance`, `currentwaypoint`, `curHealthPct`, `MovementType`, `npcflag`, `unit_flags`, `unit_flags2`, `unit_flags3`, `ScriptName`, `StringId`, `VerifiedBuild`) VALUES
(@CGUID+0, 65183, 1481, 7705, 7705, '0', 0, 0, 0, -1, 0, 0, 1180.7778, 3309.2378, 75.193016, 4.760153, 120, 0, 0, NULL, 0, NULL, NULL, NULL, NULL, '', NULL, 68887), -- Start (sniff)
(@CGUID+1, 65183, 1481, 7705, 7705, '0', 0, 0, 0, -1, 0, 0,  848.3980, 2400.0800, -52.028900, 4.760153, 120, 0, 0, NULL, 0, NULL, NULL, NULL, NULL, '', NULL, 68887), -- Molten Shore
(@CGUID+2, 65183, 1481, 7705, 7705, '0', 0, 0, 0, -1, 0, 0, 1062.6200, 2587.9400, -37.329500, 4.760153, 120, 0, 0, NULL, 0, NULL, NULL, NULL, NULL, '', NULL, 68887), -- Seat of Command
(@CGUID+3, 65183, 1481, 7705, 7705, '0', 0, 0, 0, -1, 0, 0, 1414.3200, 1779.7900,  56.412800, 4.760153, 120, 0, 0, NULL, 0, NULL, NULL, NULL, NULL, '', NULL, 68887), -- Illidari Foothold
(@CGUID+4, 65183, 1481, 7705, 7705, '0', 0, 0, 0, -1, 0, 0, 1807.1400, 1338.5200,  97.748100, 4.760153, 120, 0, 0, NULL, 0, NULL, NULL, NULL, NULL, '', NULL, 68887), -- Volcano
(@CGUID+5, 65183, 1481, 7705, 7705, '0', 0, 0, 0, -1, 0, 0, 1644.0200, 1419.4400, 243.324000, 4.760153, 120, 0, 0, NULL, 0, NULL, NULL, NULL, NULL, '', NULL, 68887); -- Fel Hammer
