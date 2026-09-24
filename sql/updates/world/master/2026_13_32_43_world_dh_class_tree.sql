-- Demon Hunter class tree spell bindings.

DELETE FROM `spell_script_names` WHERE `ScriptName` IN (
  'spell_dh_demon_muzzle',
  'spell_dh_focused_ire',
  'spell_dh_swallowed_anger',
  'spell_dh_immolation_cleanse',
  'spell_dh_infernal_armor',
  'spell_dh_final_breath'
);

INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(1266329, 'spell_dh_demon_muzzle'),
(179057, 'spell_dh_focused_ire'),
(1234195, 'spell_dh_focused_ire'),
(278326, 'spell_dh_swallowed_anger'),
(258920, 'spell_dh_immolation_cleanse'),
(1241937, 'spell_dh_immolation_cleanse'),
(320331, 'spell_dh_infernal_armor'),
(198030, 'spell_dh_final_breath'),
(212105, 'spell_dh_final_breath'),
(1213649, 'spell_dh_final_breath');

-- Demon Muzzle: Disrupt interrupt (family mask from legacy 388111) → script casts 1266616.
DELETE FROM `spell_proc` WHERE `SpellId` IN (388111, 1266329, 320331);
INSERT INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`) VALUES
(1266329,0x00,107,0x00000000,0x00000002,0x00000000,0x00000000,0x11010,0x0,0x0,0x2,0x0,0x2,0x0,0,100,0,0), -- Demon Muzzle (Disrupt)
(320331,0x00,0,0x00000000,0x00000000,0x00000000,0x00000000,0x00000028,0x0,0x0,0x2,0x0,0x0,0x0,0,100,0,0); -- Infernal Armor (taken melee swing/ability)
