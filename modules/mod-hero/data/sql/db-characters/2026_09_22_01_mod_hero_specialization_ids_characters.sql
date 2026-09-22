-- Bring saved Heroes up to date with Hero's specialisations. This goes with
-- db-hotfixes\2026_09_21_00_mod_hero_three_specializations.sql, which gives
-- Hero the block 600 to 603: Initial is 600 instead of 1479, and the three
-- modes are 601, 602 and 603.
--
-- Running the file again is safe: the moves only match an id that no longer
-- exists, and the loadouts it clears are empty ones the server rebuilds.

-- A Hero still on 1479 goes to 600, the same Initial specialisation under its
-- new id. Talent group 4 is Initial's.
UPDATE `characters` SET `primarySpecialization`=600, `activeTalentGroup`=4
    WHERE `class`=16 AND `primarySpecialization`=1479;

-- 0 means "loot for my current specialisation". 1479 no longer exists.
UPDATE `characters` SET `lootSpecId`=0 WHERE `class`=16 AND `lootSpecId`=1479;

-- At login the server gives each Hero an empty combat loadout for each mode,
-- named after the mode, and makes one again if it is missing. Clearing the
-- empty ones lets the next login rebuild them under the modes' current ids and
-- names. A loadout that holds talents, or that action bars or outfits point
-- at, is kept.
DELETE t FROM `character_trait_config` t
JOIN `characters` c ON c.`guid`=t.`guid`
WHERE c.`class`=16 AND t.`type`=1
  AND NOT EXISTS (SELECT 1 FROM `character_trait_entry` e WHERE e.`guid`=t.`guid` AND e.`traitConfigId`=t.`traitConfigId`)
  AND NOT EXISTS (SELECT 1 FROM `character_action` a WHERE a.`guid`=t.`guid` AND a.`traitConfigId`=t.`traitConfigId`)
  AND NOT EXISTS (SELECT 1 FROM `character_transmog_outfit_situation` s WHERE s.`guid`=t.`guid` AND s.`loadoutID`=t.`traitConfigId`);
