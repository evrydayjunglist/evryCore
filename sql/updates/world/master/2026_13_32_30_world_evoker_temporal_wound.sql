-- Evoker Temporal Wound: Breath of Eons damage-copy accumulate/release
-- tip max was 29 (ebon_might).

DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_evo_temporal_wound';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(409560, 'spell_evo_temporal_wound');
