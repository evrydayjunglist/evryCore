-- Demon Hunter annihilator spell bindings.

DELETE FROM `spell_script_names` WHERE `ScriptName` IN (
  'spell_dh_voidfall',
  'spell_dh_voidfall_generator',
  'spell_dh_voidfall_spender',
  'spell_dh_spirit_bomb_annihilator',
  'spell_dh_collapsing_star_annihilator',
  'spell_dh_annihilator_metamorphosis',
  'spell_dh_doomsayer_cast',
  'spell_dh_meteoric_rise_fel_devastation',
  'spell_dh_meteoric_rise_void_ray',
  'spell_dh_voidfall_meteor_damage',
  'spell_dh_otherworldly_focus_damage'
);

INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Keystone / stacks
(1253304, 'spell_dh_voidfall'),
(263642, 'spell_dh_voidfall_generator'),
(473662, 'spell_dh_voidfall_generator'),
(228477, 'spell_dh_voidfall_spender'),
(1226019, 'spell_dh_voidfall_spender'),
-- Spirit Bomb / Collapsing Star
(247454, 'spell_dh_spirit_bomb_annihilator'),
(1221150, 'spell_dh_collapsing_star_annihilator'),
(247455, 'spell_dh_otherworldly_focus_damage'),
(1221162, 'spell_dh_otherworldly_focus_damage'),
-- Meta / Doomsayer / Meteoric Rise
(187827, 'spell_dh_annihilator_metamorphosis'),
(1217607, 'spell_dh_annihilator_metamorphosis'),
(189110, 'spell_dh_doomsayer_cast'),
(473728, 'spell_dh_doomsayer_cast'),
(212084, 'spell_dh_meteoric_rise_fel_devastation'),
(473728, 'spell_dh_meteoric_rise_void_ray'),
-- Meteor damage (normal + World Killer enlarged)
(1256305, 'spell_dh_voidfall_meteor_damage'),
(1256306, 'spell_dh_voidfall_meteor_damage'),
(1256617, 'spell_dh_voidfall_meteor_damage'),
(1256619, 'spell_dh_voidfall_meteor_damage');
