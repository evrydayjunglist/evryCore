-- Housing spell script bindings.
-- 1253555 "[DNT] Skip First Housing Tutorial" is cast on the buyer through 1253572 when a house is bought. Retail
-- refused it (SMSG_CAST_FAILED, reason 32), and the script refuses it the same way so the purchase does not complete
-- "My First Home" (91863).

DELETE FROM `spell_script_names` WHERE `spell_id`=1253555 AND `ScriptName`='spell_housing_skip_first_housing_tutorial';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(1253555, 'spell_housing_skip_first_housing_tutorial');

-- 1234192 is the goober spell of the house front doors (602705 and 602706). The character casts it on herself
-- when the door opens, and it takes her into the house (hbcd3 1343025-1343064). The 12.1 client has no record of it
-- and no hotfix adds one, so the server gets its own: instant, no range, one effect on the caster that the
-- spell_housing_enter_house script carries out. The name is only a label for the server; retail's is not known.
DELETE FROM `serverside_spell` WHERE `Id`=1234192;
INSERT INTO `serverside_spell` (`Id`, `DifficultyID`, `CastingTimeIndex`, `RangeIndex`, `SchoolMask`, `SpellName`) VALUES
(1234192, 0, 1, 1, 1, 'Enter House');

DELETE FROM `serverside_spell_effect` WHERE `SpellID`=1234192;
INSERT INTO `serverside_spell_effect` (`SpellID`, `EffectIndex`, `DifficultyID`, `Effect`, `EffectChainAmplitude`, `PvpMultiplier`, `GroupSizeBasePointsCoefficient`, `ImplicitTarget1`) VALUES
(1234192, 0, 0, 3, 1, 1, 1, 1);

-- 1234193 "Exit House" is the goober spell of the door inside the house (587318). Its only effect, 343, does nothing
-- in the core, so spell_housing_exit_house moves the character to her plot.
DELETE FROM `spell_script_names` WHERE `spell_id` IN (1234192, 1234193) AND `ScriptName` IN ('spell_housing_enter_house', 'spell_housing_exit_house');
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(1234192, 'spell_housing_enter_house'),
(1234193, 'spell_housing_exit_house');

-- 1233637 "Teleport Home" is cast by the server for the plot teleport of the housing dashboard, with the plot's arrival
-- point and the neighborhood as its targets. Its teleport effect would keep the character in the instance she is in, so
-- spell_housing_teleport_home sends her into the instance of the neighborhood the cast names.
DELETE FROM `spell_script_names` WHERE `spell_id`=1233637 AND `ScriptName`='spell_housing_teleport_home';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(1233637, 'spell_housing_teleport_home');

-- 1239847 "[DNT] In Plot" is cast on a character standing on a plot. Its two linked effects would bring both
-- 469226 "[DNT] Visiting Neighbor Plot" and 468939 "[DNT] In Own Plot"; spell_housing_in_plot keeps 468939 for a plot of
-- her own account, as retail applied it (hbcd3 1339732-1339912), and holds 469226 back until a capture shows it.
DELETE FROM `spell_script_names` WHERE `spell_id`=1239847 AND `ScriptName`='spell_housing_in_plot';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(1239847, 'spell_housing_in_plot');

-- 1227147 "In Your Neighborhood" is cast on a character on a neighborhood map where her account has a house. Its second
-- effect makes area trigger 40326 on her. Retail's (hbcd3 578477-578586, on another resident): area trigger 41610, a
-- sphere of radius 40 riding the character, with spell visual 503683 from the spell. What it does is not captured, so
-- it has no actions and no script.
DELETE FROM `areatrigger_template` WHERE `Id`=41610 AND `IsCustom`=0;
INSERT INTO `areatrigger_template` (`Id`, `IsCustom`, `Flags`, `ActionSetId`, `ActionSetFlags`, `VerifiedBuild`) VALUES
(41610, 0, 0, 0, 0, 68887);

DELETE FROM `areatrigger_create_properties` WHERE `Id`=40326 AND `IsCustom`=0;
INSERT INTO `areatrigger_create_properties` (`Id`, `IsCustom`, `AreaTriggerId`, `IsAreatriggerCustom`, `Flags`, `MoveCurveId`, `ScaleCurveId`, `MorphCurveId`, `FacingCurveId`, `AnimId`, `AnimKitId`, `DecalPropertiesId`, `SpellForVisuals`, `TimeToTargetScale`, `Speed`, `SpeedIsTime`, `Shape`, `ShapeData0`, `ShapeData1`, `ShapeData2`, `ShapeData3`, `ShapeData4`, `ShapeData5`, `ShapeData6`, `ShapeData7`, `ScriptName`, `VerifiedBuild`) VALUES
(40326, 0, 41610, 0, 0, 0, 0, 0, 0, -1, 0, 0, NULL, 0, 1, 0, 0, 40, 40, 0, 0, 0, 0, 0, 0, '', 68887);
