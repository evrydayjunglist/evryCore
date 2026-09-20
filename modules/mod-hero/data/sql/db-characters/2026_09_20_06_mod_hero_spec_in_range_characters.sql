-- Move the Heroes that already exist onto the new specialisation id.
--
-- This goes with 2026_09_20_05_mod_hero_spec_in_range.sql in db-hotfixes, which
-- replaces Hero's specialisation 9101 with 1479. A character stores the
-- specialisation it is on in `characters`.`primarySpecialization`, so a Hero
-- made before that change would log in pointing at a specialisation that no
-- longer exists.
--
-- Scoped to class 16 on purpose. Reaper is class 17 and its own ids, 9001 to
-- 9004, are untouched by this; if the test below works, Reaper gets the same
-- treatment in its own file rather than being dragged along by this one.
UPDATE `characters` SET `primarySpecialization`=1479
    WHERE `class`=16 AND `primarySpecialization`=9101;

-- Loot specialisation is 0 on every character here, which means "use my current
-- specialisation", so there is nothing to move. This statement only matters if
-- one was ever set explicitly.
UPDATE `characters` SET `lootSpecId`=1479
    WHERE `class`=16 AND `lootSpecId`=9101;
