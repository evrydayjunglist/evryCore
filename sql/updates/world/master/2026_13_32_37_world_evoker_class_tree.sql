-- Evoker class talents: dummy effects and Landslide.
-- Regenerative Magic 387787 uses the core leech aura.
-- Overawe 374346: core OVERRIDE_ACTIONBAR → 406971 + ADD_FLAT_MODIFIER CD (no script).
-- Source of Magic / Leaping Flames / Twin Guardian / Strike from Above / Recall / Stretch Time /
-- Unravel / Forger talent hosts: scripted via sibling hooks (empower / Rescue / Glide / flight).

DELETE FROM `spell_script_names` WHERE `ScriptName` IN (
  'spell_evo_rescue',
  'spell_evo_time_spiral',
  'spell_evo_oppressing_roar',
  'spell_evo_landslide',
  'spell_evo_landslide_root',
  'spell_evo_unravel_fire_breath',
  'spell_evo_recall_flight',
  'spell_evo_recall_travel',
  'spell_evo_stretch_time_absorb',
  'spell_evo_leaping_flames_living_flame',
  'spell_evo_scarlet_adaptation',
  'spell_evo_scarlet_adaptation_living_flame'
);

INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(370665, 'spell_evo_rescue'),
(374968, 'spell_evo_time_spiral'),
(372048, 'spell_evo_oppressing_roar'),
(358385, 'spell_evo_landslide'),
(355689, 'spell_evo_landslide_root'),
(357209, 'spell_evo_unravel_fire_breath'),
(357210, 'spell_evo_recall_flight'),
(403631, 'spell_evo_recall_flight'),
(359816, 'spell_evo_recall_flight'),
(433874, 'spell_evo_recall_flight'),
(442204, 'spell_evo_recall_flight'),
(371838, 'spell_evo_recall_travel'),
(410355, 'spell_evo_stretch_time_absorb'),
(361500, 'spell_evo_leaping_flames_living_flame'),
(372469, 'spell_evo_scarlet_adaptation'),
(361500, 'spell_evo_scarlet_adaptation_living_flame');

-- Stretch Time repays the accumulated debt and removes its otherwise permanent damage aura.
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_evo_stretch_time_debt';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(413924, 'spell_evo_stretch_time_debt');
