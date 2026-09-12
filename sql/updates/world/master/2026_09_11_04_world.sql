-- Avatar of Destruction (1245089) triggers Summon Overfiend (434587) on a successful cast.
-- SpellAuraOptions ProcTypeMask is 0;4 (PROC_FLAG_2_CAST_SUCCESSFUL) and ProcChance 101.
-- SpellEffect class mask is empty, so generated spell_proc matches every successful cast.
-- Restrict to Soul Fire. Client SpellClassOptions (build 12.1.0.68914):
--   6353 Soul Fire            family 5  mask 0;131072;2;0
--   348 Immolate              family 5  mask 4;131072;0;4194304
--   29722 Incinerate          family 5  mask 0;131136;0;4194304
--   17962 Conflagrate         family 5  mask 0;8519680;0;4194304
--   116858 Chaos Bolt         family 5  mask 0;139264;0;4194304
--   387976 / 1280868 Dimensional Rift, 1245089 / 434587: family 5, empty mask
-- Mask1 bit 131072 is shared with Immolate, Incinerate, Conflagrate, and Chaos Bolt.
-- IsAffected is any-bit overlap, so keep only Mask2 value 2, which is unique among those spells.
-- Chance 0 leaves DB2 ProcChance in place. Dimensional Rift is not encoded.

DELETE FROM `spell_proc` WHERE `SpellId` IN (1245089);
INSERT INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`) VALUES
(1245089,0x00,5,0x00000000,0x00000000,0x00000002,0x00000000,0x0,0x4,0x0,0x0,0x0,0x0,0x0,0,0,0,0); -- Avatar of Destruction
