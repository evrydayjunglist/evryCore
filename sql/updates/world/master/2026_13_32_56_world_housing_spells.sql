-- Housing spell script bindings.
-- 1253555 "[DNT] Skip First Housing Tutorial" is cast on the buyer through 1253572 when a house is bought. Retail
-- refused it (SMSG_CAST_FAILED, reason 32), and the script refuses it the same way so the purchase does not complete
-- "My First Home" (91863).

DELETE FROM `spell_script_names` WHERE `spell_id`=1253555 AND `ScriptName`='spell_housing_skip_first_housing_tutorial';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(1253555, 'spell_housing_skip_first_housing_tutorial');
