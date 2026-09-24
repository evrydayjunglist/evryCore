-- Fel Rush uses the core's line targets for damage and native caster destination for its helper.
DELETE FROM `spell_script_names` WHERE `ScriptName` IN
('spell_dh_fel_rush', 'spell_dh_fel_rush_aura', 'spell_dh_fel_rush_damage', 'spell_dh_fel_rush_end');
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(195072, 'spell_dh_fel_rush'),
(197922, 'spell_dh_fel_rush_aura'),
(197923, 'spell_dh_fel_rush_aura');
