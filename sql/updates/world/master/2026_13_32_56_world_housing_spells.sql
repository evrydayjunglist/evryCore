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
