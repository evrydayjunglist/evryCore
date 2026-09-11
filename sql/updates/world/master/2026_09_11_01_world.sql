-- Arathi Catch Up (map 2927). Pad, keep gnolls, Go'shek Farm openers, scenes, and journal launch dest.
-- Pad and keep positions: retail sniffs D/H/J, build 12.0.7.68453.
-- Farm pumpkin, kobold, ogre, Runk, and catapult positions: Horde capture coords from the later 12.0.7 dumps.
-- Stuck Ogre 253460 is a real 90886 objective; no sniffed stand position yet, so it is not spawned.

SET @CGUID := 11801000;

-- ---------------------------------------------------------------------------
-- Templates
-- ---------------------------------------------------------------------------
UPDATE `creature_template` SET `npcflag`=`npcflag`|2, `faction`=35, `ScriptName`='npc_arathi_rpe_leader', `VerifiedBuild`=68453 WHERE `entry` IN (244642, 244643);
UPDATE `creature_template` SET `npcflag`=`npcflag`|2, `faction`=35, `VerifiedBuild`=68453 WHERE `entry` IN (244729, 244656, 244655);
UPDATE `creature_template` SET `faction`=16, `VerifiedBuild`=68453 WHERE `entry` IN (244669, 244670, 244671, 244672, 244674, 244675, 244676, 244677, 249255);
UPDATE `creature_template` SET `faction`=35, `npcflag`=`npcflag`|16777216, `ScriptName`='npc_arathi_rpe_prized_pumpkin', `VerifiedBuild`=69404 WHERE `entry`=244956;
UPDATE `creature_template` SET `faction`=35, `npcflag`=`npcflag`|16777216, `ScriptName`='npc_arathi_rpe_worn_catapult', `VerifiedBuild`=69404 WHERE `entry`=249269;
UPDATE `creature_template` SET `faction`=35, `npcflag`=129, `VerifiedBuild`=68453 WHERE `entry`=245026;
UPDATE `creature_template` SET `faction`=714, `VerifiedBuild`=68453 WHERE `entry`=245028 AND `faction`=0;
UPDATE `creature_template` SET `unit_flags`=33536, `VerifiedBuild`=68453 WHERE `entry`=245027;
UPDATE `creature_template` SET `unit_flags`=64, `faction`=7, `VerifiedBuild`=68453 WHERE `entry`=249245;

UPDATE `creature_template_difficulty`
SET `ContentTuningID`=4306, `VerifiedBuild`=68453
WHERE `Entry` IN (244642, 244643, 244655, 244656, 244669, 244670, 244671, 244672, 244674, 244675, 244676, 244677, 244729, 244956, 245026, 245027, 249245, 249255, 249269)
  AND `DifficultyID`=0
  AND `ContentTuningID`=0;

UPDATE `creature_template_difficulty`
SET `StaticFlags1`=536871168, `VerifiedBuild`=68453
WHERE `Entry`=249245 AND `DifficultyID`=0;

UPDATE `creature_template_difficulty`
SET `StaticFlags1`=0, `VerifiedBuild`=68453
WHERE `Entry`=245027 AND `DifficultyID`=0;

DELETE FROM `creature_template_addon` WHERE `entry` IN (244642, 244643, 245027, 249245);
INSERT INTO `creature_template_addon` (`entry`, `PathId`, `mount`, `MountCreatureID`, `StandState`, `AnimTier`, `VisFlags`, `SheathState`, `PvPFlags`, `emote`, `aiAnimKit`, `movementAnimKit`, `meleeAnimKit`, `visibilityDistanceType`, `auras`) VALUES
(244642, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, '1237057'), -- Thrall pad pose (sniff J)
(244643, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, '1237118'), -- Jaina pad pose (sniff J)
(245027, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, '29266'), -- outdoor Assailant Feign Death
(249245, 0, 0, 0, 0, 3, 4, 1, 0, 0, 0, 0, 0, 0, NULL); -- Training Dummy float

-- ---------------------------------------------------------------------------
-- Spawns
-- ---------------------------------------------------------------------------
DELETE FROM `creature` WHERE `guid` BETWEEN @CGUID+0 AND @CGUID+63;
DELETE FROM `creature_addon` WHERE `guid` IN (@CGUID+31, @CGUID+32, @CGUID+33);
INSERT INTO `creature` (`guid`, `id`, `map`, `zoneId`, `areaId`, `spawnDifficulties`, `PhaseId`, `PhaseGroup`, `modelid`, `equipment_id`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecs`, `wander_distance`, `currentwaypoint`, `MovementType`, `npcflag`, `unit_flags`, `unit_flags2`, `unit_flags3`, `VerifiedBuild`) VALUES
-- Hammerfall pad (sniff H)
(@CGUID+0,  244642, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1086.4791, -3554.7744, 50.192024, 0.09973325, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+1,  244643, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1084.2153, -3559.9722, 50.44527, 5.0222363, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
-- Keep gnolls (90882)
(@CGUID+2,  244672, 2927, 16432, 16432, '0', 0, 0, 0, 0, -981.45026, -3513.761,  56.992092, 3.3214676, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+3,  244672, 2927, 16432, 16432, '0', 0, 0, 0, 0, -988.5839,  -3518.1963, 56.992092, 4.0285850, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+4,  244672, 2927, 16432, 16432, '0', 0, 0, 0, 0, -994.482,   -3525.4333, 56.992092, 0,         120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+5,  244672, 2927, 16432, 16432, '0', 0, 0, 0, 0, -999.4901,  -3530.6072, 56.98025,  0,         120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+6,  244672, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1005.5139, -3536.0608, 56.57396,  0,         120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+7,  244672, 2927, 16432, 16432, '0', 0, 0, 0, 0, -996.47,    -3527.87,   56.99,     0,         120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+8,  244672, 2927, 16432, 16432, '0', 0, 0, 0, 0, -975.55646, -3512.6892, 56.992092, 0,         120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+9,  244672, 2927, 16432, 16432, '0', 0, 0, 0, 0, -972.067,   -3511.9495, 56.992092, 0,         120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+10, 244672, 2927, 16432, 16432, '0', 0, 0, 0, 0, -962.2465,  -3509.7275, 56.992096, 0,         120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+11, 244669, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1016.804,  -3519.98,   61.393707, 3.3338888, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+12, 244669, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1020.7656, -3517.7917, 61.757744, 0,         120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+13, 244669, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1015.9045, -3519.804,  61.477505, 3.5282719, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+14, 244670, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1013.5469, -3574.7432, 56.647884, 5.3338089, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+15, 244670, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1014.908,  -3516.7205, 61.730278, 3.7660933, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+16, 244671, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1033.3021, -3551.8108, 56.267727, 3.1737113, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+17, 244671, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1010.8646, -3563.9548, 56.647884, 1.6648406, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+18, 244671, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1025.5286, -3489.8262, 62.304573, 0.7926827, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
(@CGUID+19, 244671, 2927, 16432, 16432, '0', 0, 0, 0, 0, -985.0482,  -3541.8672, 56.992737, 0.9016410, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
-- Go'shek Farm pad (sniff H)
(@CGUID+20, 244729, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1522.33,   -3089.36,   26.34,     2.175,     120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+21, 244656, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1522.62,   -3085.87,   26.17,     1.533,     120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+22, 244655, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1525.88,   -3089.80,   26.12,     3.182,     120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+23, 244672, 2927, 16432, 16432, '0', 0, 0, 0, 0, -983.041,   -3514.0503, 56.992092, 0,         120, 5, 0, 1, NULL, NULL, NULL, NULL, 68453),
-- Pad ambience (sniff D/H)
(@CGUID+24, 245027, 2927, 16432, 16432, '0', 0, 0, 103286, 0, -1099.5348, -3538.7761, 51.677532, 5.7316, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+25, 245027, 2927, 16432, 16432, '0', 0, 0, 103695, 0, -1083.3837, -3541.8142, 52.47486,  4.9389, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+26, 245027, 2927, 16432, 16432, '0', 0, 0, 103694, 0, -1076.6423, -3550.4011, 51.509766, 3.1003, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+27, 245027, 2927, 16432, 16432, '0', 0, 0, 103695, 0, -1095.731,  -3562.3176, 49.279354, 0.6838, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+28, 245027, 2927, 16432, 16432, '0', 0, 0, 103694, 0, -1073.4567, -3557.6145, 51.731472, 2.6257, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+29, 245027, 2927, 16432, 16432, '0', 0, 0, 103286, 0, -1081.0017, -3560.2847, 51.0606,   2.3654, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+30, 245027, 2927, 16432, 16432, '0', 0, 0, 103287, 0, -1093.6285, -3548.1216, 49.634605, 5.0607, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+31, 245026, 2927, 16432, 16432, '0', 0, 0, 93765,  0, -1089.5834, -3545.2188, 50.21548,  1.1165, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+32, 245028, 2927, 16432, 16432, '0', 0, 0, 84548,  0, -1088.342,  -3542.4358, 50.374855, 3.9584, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+33, 245028, 2927, 16432, 16432, '0', 0, 0, 84546,  0, -1083.7812, -3553.5815, 50.545025, 2.1282, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+34, 249245, 2927, 16432, 16432, '0', 0, 0, 99693,  0, -1095.2067, -3534.9358, 52.150093, 4.1114, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+35, 249245, 2927, 16432, 16432, '0', 0, 0, 99693,  0, -1098.3038, -3531.007,  52.349316, 4.1114, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+36, 249245, 2927, 16432, 16432, '0', 0, 0, 99693,  0, -1105.4531, -3515.2778, 51.70594,  3.2898, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+37, 249245, 2927, 16432, 16432, '0', 0, 0, 99693,  0, -1104.4705, -3521.9202, 52.152374, 3.4049, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
(@CGUID+38, 249245, 2927, 16432, 16432, '0', 0, 0, 99693,  0, -1101.7101, -3526.342,  52.122147, 4.1114, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 68453),
-- Farm objectives (Horde capture)
(@CGUID+39, 244956, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1514.288, -2971.832, 14.023, 0.0, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+40, 244956, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1534.602, -3002.938, 14.025, 0.0, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+41, 244956, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1553.7274, -2959.6580, 14.0230, 0.0, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+42, 244674, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1437.175, -2943.42, 14.082, 6.048, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+43, 244676, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1564.078, -3025.773, 14.032, 0.316, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+44, 249255, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1524.0903, -3097.3801, 26.0778, 3.0874, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+45, 249255, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1528.5521, -3094.2605, 26.0383, 1.6089, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+46, 249255, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1515.0104, -3094.2935, 27.6268, 3.2719, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+47, 249255, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1521.3229, -3079.894, 25.7415, 1.1354, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+48, 249255, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1527.1302, -3082.8594, 25.7332, 6.1616, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+49, 244677, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1468.59, -2890.844, 14.518, 5.761, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+50, 244675, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1457.769, -3005.009, 14.383, 6.094, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+51, 244674, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1600.613, -2976.786, 22.345, 0.668, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+52, 244674, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1417.783, -2982.183, 19.155, 2.625, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+53, 244674, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1524.95, -2862.679, 14.025, 1.528, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+54, 244674, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1614.679, -2876.821, 19.783, 2.412, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+55, 244674, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1534.097, -2928.875, 14.243, 1.606, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+56, 244677, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1470.748, -2944.911, 14.661, 1.482, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+57, 244677, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1565.342, -2917.422, 14.023, 4.179, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+58, 244677, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1517.613, -2944.642, 14.023, 0.981, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+59, 244677, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1509.53, -2917.193, 14.023, 4.832, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+60, 244677, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1463.666, -2952.973, 14.797, 1.764, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
-- Worn Catapults (three captured points; a fourth captured stand is still missing)
(@CGUID+61, 249269, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1212.0104, -1869.9601, 91.6107, 2.74, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+62, 249269, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1308.5868, -1787.2188, 62.8026, 3.14, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404),
(@CGUID+63, 249269, 2927, 16432, 16432, '0', 0, 0, 0, 0, -1201.0156, -1773.5504, 59.4456, 2.9809, 120, 0, 0, 0, NULL, NULL, NULL, NULL, 69404);

INSERT INTO `creature_addon` (`guid`, `PathId`, `mount`, `MountCreatureID`, `StandState`, `AnimTier`, `VisFlags`, `SheathState`, `PvPFlags`, `emote`, `aiAnimKit`, `movementAnimKit`, `meleeAnimKit`, `visibilityDistanceType`, `auras`) VALUES
(@CGUID+31, 0, 0, 0, 8, 0, 0, 1, 0, 0, 0, 0, 0, 0, NULL), -- Win'sa kneel (sniff J)
(@CGUID+32, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, NULL), -- Horde Grunt sit (sniff J)
(@CGUID+33, 0, 0, 0, 7, 0, 0, 1, 0, 0, 0, 0, 0, 0, NULL); -- Horde Grunt dead (sniff J)

-- ---------------------------------------------------------------------------
-- Quests
-- ---------------------------------------------------------------------------
UPDATE `quest_template` SET `RewardNextQuest`=90883 WHERE `ID`=90882 AND `RewardNextQuest`=0;
UPDATE `quest_template` SET `RewardNextQuest`=90885 WHERE `ID`=90883 AND `RewardNextQuest`=0;

DELETE FROM `quest_template_addon` WHERE `ID`=90883;
INSERT INTO `quest_template_addon` (`ID`, `MaxLevel`, `AllowableClasses`, `SourceSpellID`, `PrevQuestID`, `NextQuestID`, `ExclusiveGroup`, `BreadcrumbForQuestId`, `RewardMailTemplateID`, `RewardMailDelay`, `RequiredSkillID`, `RequiredSkillPoints`, `RequiredMinRepFaction`, `RequiredMaxRepFaction`, `RequiredMinRepValue`, `RequiredMaxRepValue`, `ProvidedItemCount`, `SpecialFlags`, `ScriptName`) VALUES
(90883, 0, 0, 0, 90882, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '');

DELETE FROM `creature_queststarter` WHERE `quest` IN (90882, 90883, 90885, 90886, 90887) AND `id` IN (244642, 244643, 244729, 244656, 244655);
INSERT INTO `creature_queststarter` (`id`, `quest`, `VerifiedBuild`) VALUES
(244642, 90882, 68453),
(244643, 90882, 68453),
(244642, 90883, 68453),
(244643, 90883, 68453),
(244729, 90885, 68453),
(244656, 90886, 68453),
(244655, 90887, 68453);

DELETE FROM `creature_questender` WHERE `quest`=90882 AND `id` IN (244642, 244643);
INSERT INTO `creature_questender` (`id`, `quest`, `VerifiedBuild`) VALUES
(244642, 90882, 68453),
(244643, 90882, 68453);

DELETE FROM `creature_questender` WHERE `quest`=90883 AND `id`=244729;
INSERT INTO `creature_questender` (`id`, `quest`, `VerifiedBuild`) VALUES
(244729, 90883, 68453);

DELETE FROM `creature_questender` WHERE `quest`=90885 AND `id`=244729;
INSERT INTO `creature_questender` (`id`, `quest`, `VerifiedBuild`) VALUES
(244729, 90885, 68453);

-- ---------------------------------------------------------------------------
-- Scenes, launch dest, spellclicks
-- ---------------------------------------------------------------------------
DELETE FROM `scene_template` WHERE `SceneId` IN (3692, 3749);
INSERT INTO `scene_template` (`SceneId`, `Flags`, `ScriptPackageID`, `Encrypted`, `ScriptName`) VALUES
(3692, 16, 4617, 0, ''),
(3749, 17, 4681, 0, '');

DELETE FROM `spell_target_position` WHERE `ID`=1260320;
INSERT INTO `spell_target_position` (`ID`, `EffectIndex`, `OrderIndex`, `MapID`, `PositionX`, `PositionY`, `PositionZ`, `Orientation`, `VerifiedBuild`) VALUES
(1260320, 0, 0, 2927, -1101.67, -3554.37, 48.9203, 6.2583666, 68453);

DELETE FROM `npc_spellclick_spells` WHERE `npc_entry` IN (244956, 249269);
INSERT INTO `npc_spellclick_spells` (`npc_entry`, `spell_id`, `cast_flags`, `user_type`) VALUES
(244956, 1236722, 1, 0), -- Recovering
(249269, 1248670, 1, 0); -- Placing Rune

-- ---------------------------------------------------------------------------
-- Phases (sniff H PhaseShift)
-- ---------------------------------------------------------------------------
DELETE FROM `phase_name` WHERE `ID` IN (26596, 26618, 27217, 26588, 26599);
INSERT INTO `phase_name` (`ID`, `Name`) VALUES
(26596, 'Arathi Catch Up - Hammerfall login'),
(26618, 'Arathi Catch Up - Hammerfall login'),
(27217, 'Arathi Catch Up - persistent through farm'),
(26588, 'Arathi Catch Up - Go''shek Farm arrive'),
(26599, 'Arathi Catch Up - Go''shek Farm persistent');

DELETE FROM `phase_area` WHERE `PhaseId` IN (26596, 26618, 27217, 26588, 26599) AND `AreaId`=16432;
INSERT INTO `phase_area` (`AreaId`, `PhaseId`, `Comment`) VALUES
(16432, 26596, 'Arathi Catch Up zone - login phases'),
(16432, 26618, 'Arathi Catch Up zone - login phases'),
(16432, 27217, 'Arathi Catch Up zone - login phases'),
(16432, 26588, 'Arathi Catch Up - farm arrive phase'),
(16432, 26599, 'Arathi Catch Up - farm persistent phase');

DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId`=26 AND `SourceGroup` IN (26618, 26596, 26588, 26599);
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `ConditionStringValue1`, `NegativeCondition`, `ErrorType`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(26, 26618, 0, 0, 0, 47, 0, 90882, 64, 0, '', 1, 0, 0, '', 'Arathi Catch Up: phase 26618 while 90882 is not rewarded'),
(26, 26596, 0, 0, 0, 47, 0, 90883, 74, 0, '', 1, 0, 0, '', 'Arathi Catch Up: phase 26596 while 90883 is not started'),
(26, 26588, 0, 0, 0, 47, 0, 90883, 74, 0, '', 0, 0, 0, '', 'Arathi Catch Up: phase 26588 after 90883 starts'),
(26, 26588, 0, 0, 0, 47, 0, 90885, 74, 0, '', 1, 0, 0, '', 'Arathi Catch Up: phase 26588 while 90885 is not started'),
(26, 26599, 0, 0, 0, 47, 0, 90883, 74, 0, '', 0, 0, 0, '', 'Arathi Catch Up: phase 26599 after 90883 starts');

DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId`=18 AND `SourceGroup` IN (244956, 249269);
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `ConditionStringValue1`, `NegativeCondition`, `ErrorType`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(18, 244956, 1236722, 0, 0, 47, 0, 90885, 8, 0, '', 0, 0, 0, '', 'Prized Pumpkin spellclick while 90885 is incomplete'),
(18, 249269, 1248670, 0, 0, 47, 0, 90895, 8, 0, '', 0, 0, 0, '', 'Worn Catapult spellclick while 90895 is incomplete');
