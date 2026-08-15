-- Chromie Time: show gossip 25426 options from UiChromieTimeExpansionID, plus Orgrimmar Chromie's Hourglass
-- Sniff 12.0.7.68887: present → 51901; in Chromie Time → 51902+51903. Hourglass GO 350063 under Chromie.

-- Gossip option visibility (SourceEntry = OptionID / OrderIndex)
DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 15 AND `SourceGroup` = 25426 AND `SourceEntry` IN (0, 1, 2);
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `ConditionStringValue1`, `NegativeCondition`, `ErrorType`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(15, 25426, 0, 0, 0, 61, 0, 0, 0, 0, '', 0, 0, 0, '', 'Chromie 25426 option 51901: show when UiChromieTimeExpansionID = 0 (present)'),
(15, 25426, 1, 0, 0, 61, 0, 0, 0, 0, '', 1, 0, 0, '', 'Chromie 25426 option 51902: show when not in present (any Chromie Time)'),
(15, 25426, 2, 0, 0, 61, 0, 0, 0, 0, '', 1, 0, 0, '', 'Chromie 25426 option 51903: show when not in present (any Chromie Time)');

-- Orgrimmar Chromie's Hourglass (retail create under Chromie 167032)
DELETE FROM `gameobject` WHERE `guid` = 11800157;
INSERT INTO `gameobject` (`guid`, `id`, `map`, `zoneId`, `areaId`, `spawnDifficulties`, `phaseUseFlags`, `PhaseId`, `PhaseGroup`, `terrainSwapMap`, `position_x`, `position_y`, `position_z`, `orientation`, `rotation0`, `rotation1`, `rotation2`, `rotation3`, `spawntimesecs`, `animprogress`, `state`, `ScriptName`, `StringId`, `VerifiedBuild`) VALUES
(11800157, 350063, 1, 1637, 5170, '0', 0, 0, 0, -1, 1557.0938, -4216.905, 54.11084, 0.08049467, 0, 0, 0.040236473, 0.99919015, 120, 255, 1, '', NULL, 68887);
