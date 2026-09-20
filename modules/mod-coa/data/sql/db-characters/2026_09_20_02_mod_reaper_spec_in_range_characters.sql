-- Move the Reapers that already exist onto the new specialisation ids.
--
-- This goes with 2026_09_20_01_mod_reaper_spec_in_range.sql in db-hotfixes,
-- which replaces Reaper's specialisations 9001 to 9004 with 610 to 613. A
-- character stores the specialisation it is on in
-- `characters`.`primarySpecialization`, so a Reaper made before that change
-- would log in pointing at a specialisation that no longer exists.
--
-- Every case is listed rather than using arithmetic, so a row holding something
-- unexpected is left alone instead of being moved somewhere wrong.
UPDATE `characters` SET `primarySpecialization`=610 WHERE `class`=17 AND `primarySpecialization`=9001;
UPDATE `characters` SET `primarySpecialization`=611 WHERE `class`=17 AND `primarySpecialization`=9002;
UPDATE `characters` SET `primarySpecialization`=612 WHERE `class`=17 AND `primarySpecialization`=9003;
UPDATE `characters` SET `primarySpecialization`=613 WHERE `class`=17 AND `primarySpecialization`=9004;

-- Loot specialisation is 0 on every Reaper here, which means "use my current
-- specialisation", so there is nothing to move. These only matter if one was
-- ever set explicitly.
UPDATE `characters` SET `lootSpecId`=610 WHERE `class`=17 AND `lootSpecId`=9001;
UPDATE `characters` SET `lootSpecId`=611 WHERE `class`=17 AND `lootSpecId`=9002;
UPDATE `characters` SET `lootSpecId`=612 WHERE `class`=17 AND `lootSpecId`=9003;
UPDATE `characters` SET `lootSpecId`=613 WHERE `class`=17 AND `lootSpecId`=9004;
