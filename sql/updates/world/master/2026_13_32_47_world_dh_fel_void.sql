-- Demon Hunter fel void spell bindings.

DELETE FROM `spell_script_names` WHERE `ScriptName` IN (
  'spell_dh_demonsurge',
  'spell_dh_demonsurge_metamorphosis',
  'spell_dh_demonsurge_annihilation',
  'spell_dh_demonsurge_death_sweep',
  'spell_dh_demonsurge_abyssal_gaze',
  'spell_dh_demonsurge_consuming_fire',
  'spell_dh_demonsurge_voidblade',
  'spell_dh_demonsurge_hungering_slash',
  'spell_dh_demonsurge_the_hunt',
  'spell_dh_demonsurge_damage',
  'spell_dh_pursuit_of_angriness',
  'spell_dh_set_fire_to_the_pain',
  'spell_dh_burning_blades',
  'spell_dh_undying_embers_host'
);

INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Keystone + Meta hosts
(452402, 'spell_dh_demonsurge'),
(162264, 'spell_dh_demonsurge_metamorphosis'),
(1217607, 'spell_dh_demonsurge_metamorphosis'),
-- Empowered ability first-cast hosts
(201427, 'spell_dh_demonsurge_annihilation'),
(210152, 'spell_dh_demonsurge_death_sweep'),
(452497, 'spell_dh_demonsurge_abyssal_gaze'),
(452487, 'spell_dh_demonsurge_consuming_fire'),
(456640, 'spell_dh_demonsurge_consuming_fire'),
(1245412, 'spell_dh_demonsurge_voidblade'),
(1245483, 'spell_dh_demonsurge_voidblade'),
(1239519, 'spell_dh_demonsurge_hungering_slash'),
(370965, 'spell_dh_demonsurge_the_hunt'),
(1246167, 'spell_dh_demonsurge_the_hunt'),
-- Explode damage (+ Focused Hatred / softcap / intensity gate)
(452416, 'spell_dh_demonsurge_damage'),
(1246160, 'spell_dh_demonsurge_damage'),
-- Dependents
(452404, 'spell_dh_pursuit_of_angriness'),
(452406, 'spell_dh_set_fire_to_the_pain'),
(452408, 'spell_dh_burning_blades'),
(258920, 'spell_dh_undying_embers_host'),
(1241937, 'spell_dh_undying_embers_host');
