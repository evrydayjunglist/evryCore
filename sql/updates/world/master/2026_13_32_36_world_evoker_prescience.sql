-- Evoker Prescience cast (409311) + Fate Mirror echo on Prescience buff (410089).
-- Living Chronowarden bind spell_evo_prescience_double_time on 410089 is left intact.

DELETE FROM `spell_script_names` WHERE `ScriptName` IN (
  'spell_evo_prescience',
  'spell_evo_prescience_fate_mirror'
);
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(409311, 'spell_evo_prescience'),
(410089, 'spell_evo_prescience_fate_mirror');
