-- Demon Hunter devourer spell bindings.

DELETE FROM `spell_script_names` WHERE `ScriptName` IN (
  'spell_dh_consume_soul_devourer',
  'spell_dh_void_metamorphosis_cast',
  'spell_dh_void_metamorphosis_devourer',
  'spell_dh_collapsing_star',
  'spell_dh_collapsing_star_damage',
  'spell_dh_soul_immolation',
  'spell_dh_spontaneous_immolation',
  'spell_dh_entropy',
  'spell_dh_reap_devourer_talents',
  'spell_dh_reap_damage_devourer',
  'spell_dh_eradicate_void_ray',
  'spell_dh_void_ray_damage_devourer',
  'spell_dh_devourer_voidblade_hunt_talents',
  'spell_dh_hungering_slash',
  'spell_dh_hungering_slash_damage',
  'spell_dh_emptiness_haste',
  'spell_dh_void_nova'
);

INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Keystones / access
(1223423, 'spell_dh_consume_soul_devourer'),
(1217605, 'spell_dh_void_metamorphosis_cast'),
(1217607, 'spell_dh_void_metamorphosis_devourer'),
(1221150, 'spell_dh_collapsing_star'),
(1221162, 'spell_dh_collapsing_star_damage'),
-- Soul Immolation / Entropy / Spontaneous
(1241937, 'spell_dh_soul_immolation'),
(1246556, 'spell_dh_spontaneous_immolation'),
(1261684, 'spell_dh_entropy'),
-- Reap / Eradicate / Void Ray
(1226019, 'spell_dh_reap_devourer_talents'),
(1225823, 'spell_dh_reap_damage_devourer'),
(473728, 'spell_dh_eradicate_void_ray'),
(1213649, 'spell_dh_void_ray_damage_devourer'),
-- Voidblade / Hunt / Hungering
(1245414, 'spell_dh_devourer_voidblade_hunt_talents'),
(1246169, 'spell_dh_devourer_voidblade_hunt_talents'),
(1239123, 'spell_dh_hungering_slash'),
(1239127, 'spell_dh_hungering_slash_damage'),
-- Emptiness haste stacks / Void Nova softcap
(1242504, 'spell_dh_emptiness_haste'),
(1234195, 'spell_dh_void_nova');
