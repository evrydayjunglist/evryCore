-- Demon Hunter baseline spell bindings.

DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_dh_chaos_brand';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(255260, 'spell_dh_chaos_brand');
