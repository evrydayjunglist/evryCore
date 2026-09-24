-- DK hooks whose DB2 effect moved or disappeared in 12.0.x (logged at boot as
-- "did not match dbc effect data").

-- Obliteration: the proc aura is on the talent 281238 (EFFECT_0 SPELL_AURA_PROC_TRIGGER_SPELL ->
-- 207256, EFFECT_1 dummy 20 = the rune chance the script already reads), not on the triggered
-- 207256 whose three effects are all SPELL_AURA_ADD_FLAT_MODIFIER_BY_SPELL_LABEL cost modifiers.
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_dk_obliteration';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(281238, 'spell_dk_obliteration'); -- Obliteration (talent)

-- Soul Reaper (343294) lost its delayed execute in 12.0.x - it is now damage plus the 1241521
-- damage-amp debuff, with no SPELL_AURA_PERIODIC_DUMMY left to tick on. 469180 keeps the classic
-- shape, so its own binding (spell_dk_soul_reaper_reaper_of_souls) stays.
-- Reaper of Souls (440002) no longer matches 343294 either: there is no EFFECT_3 for its launch
-- hook, and its surviving EFFECT_1 target-clear would suppress the new debuff whenever the talent
-- proc was active. Its Midnight behaviour (reset cooldown, free runes, ignore the health gate)
-- needs reimplementation against the current data.
DELETE FROM `spell_script_names` WHERE `ScriptName` IN ('spell_dk_soul_reaper', 'spell_dk_reaper_of_souls');
