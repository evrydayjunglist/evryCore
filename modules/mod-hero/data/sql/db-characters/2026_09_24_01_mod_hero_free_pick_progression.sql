-- Approved regular Free Pick progression: 9 AE at creation, then one AE and
-- one TE per level from 10 through 90. Existing purchases retain their price.
ALTER TABLE `character_hero_freepick`
  DROP CHECK `hero_freepick_balance`,
  ALTER COLUMN `balance` SET DEFAULT 9,
  ALTER COLUMN `rules_revision` SET DEFAULT 2,
  ADD COLUMN `talent_balance` INT UNSIGNED NOT NULL DEFAULT 0,
  ADD COLUMN `earned_level` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  ADD CONSTRAINT `hero_freepick_balance` CHECK (`balance` <= 90),
  ADD CONSTRAINT `hero_freepick_talent_balance` CHECK (`talent_balance` <= 81),
  ADD CONSTRAINT `hero_freepick_earned_level` CHECK (`earned_level` BETWEEN 1 AND 90);

ALTER TABLE `character_hero_freepick_owned`
  DROP CHECK `hero_freepick_paid`,
  ADD CONSTRAINT `hero_freepick_paid` CHECK (`paid` BETWEEN 1 AND 90);

ALTER TABLE `character_hero_freepick_request`
  DROP CHECK `hero_freepick_mask`,
  MODIFY COLUMN `desired_mask` INT UNSIGNED NOT NULL,
  ADD CONSTRAINT `hero_freepick_mask` CHECK (`desired_mask` <= 8191);

-- Advance the existing profile instead of creating a second wallet. Login
-- catches up later levels once; original request receipts remain immutable.
START TRANSACTION;
UPDATE `character_hero_freepick`
SET `previous_version`=`version`, `version`=`version`+1,
    `balance`=`balance`+1, `rules_revision`=2
WHERE `profile`='hero-free-pick-starter' AND `rules_revision`=1;
UPDATE `character_hero_freepick_owned`
SET `rules_revision`=2
WHERE `profile`='hero-free-pick-starter' AND `rules_revision`=1;
COMMIT;
