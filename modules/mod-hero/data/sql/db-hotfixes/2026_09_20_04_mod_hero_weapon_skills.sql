-- Give Hero every weapon proficiency.
--
-- This fixes a real gap in the first Hero file. A Hero could be created and
-- played, but could not hold a weapon: equipping one is refused with "You do
-- not have the required proficiency for that item".
--
-- Why. Armour and weapons are checked two different ways in Player::CanUseItem.
-- Armour goes through the class's ArmorTypeMask, which is 127 for Hero, so a
-- Hero could already wear cloth through plate and a shield. Weapons instead go
-- through GetSkillValue(item's weapon skill), and a class only has a weapon
-- skill if some skill_race_class_info row grants it, which Player::LearnDefault
-- Skills reads at creation and on level up. Hero had exactly one such row: its
-- own class skill line. So every weapon skill was zero.
--
-- Defense (95) and Unarmed (162) are deliberately absent below. Their rows
-- already carry ClassMask -1, every class, so a Hero has them without our help,
-- which is why an unarmed Hero can already swing at things.
--
-- The owner's decision is that a Hero gets all of them, which matches every
-- other decision about this class: every race, every armour type, and as many
-- resources as the ten slots allow.
--
-- Flags are copied per skill from the rows the client already ships rather than
-- chosen: 128 is include-in-sort, 146 adds always-max-value and no skill-up
-- message, 130 adds only the quiet skill-up, 144 adds only always-max-value.
-- Dual Wield is the one with a minimum level, and it is 1. A skill granted this
-- way gets value 1 even without always-max-value, and any non-zero value is
-- enough for the proficiency check.
--
-- This is a new file rather than an edit to 2026_09_20_00_mod_hero_class.sql,
-- because that one has already been applied and the updater keys on filename.
--
-- Existing Heroes do not pick these up on their own: the grant runs at creation
-- and on level up. For one already made, a GM can run `.learn all default` on
-- it once the server has restarted with this file applied.

INSERT INTO `skill_race_class_info` (`ID`,`SkillID`,`ClassMask`,`Flags`,`Availability`,
    `MinLevel`,`SkillTierID`,`RaceMask1`,`RaceMask2`,`VerifiedBuild`)
SELECT `ID`,`SkillID`,32768,`Flags`,1,`MinLevel`,0,-1,-1,0 FROM (
             SELECT 9102 AS `ID`,   43 AS `SkillID`, 128 AS `Flags`, 0 AS `MinLevel`  -- Swords
    UNION ALL SELECT 9103,   44, 128, 0                                               -- Axes
    UNION ALL SELECT 9104,   45, 128, 0                                               -- Bows
    UNION ALL SELECT 9105,   46, 128, 0                                               -- Guns
    UNION ALL SELECT 9106,   54, 128, 0                                               -- Maces
    UNION ALL SELECT 9107,   55, 128, 0                                               -- Two-Handed Swords
    UNION ALL SELECT 9108,  118, 146, 1                                               -- Dual Wield
    UNION ALL SELECT 9109,  136, 128, 0                                               -- Staves
    UNION ALL SELECT 9110,  160, 128, 0                                               -- Two-Handed Maces
    UNION ALL SELECT 9111,  172, 128, 0                                               -- Two-Handed Axes
    UNION ALL SELECT 9112,  173, 128, 0                                               -- Daggers
    UNION ALL SELECT 9113,  226, 128, 0                                               -- Crossbows
    UNION ALL SELECT 9114,  228, 146, 0                                               -- Wands
    UNION ALL SELECT 9115,  229, 128, 0                                               -- Polearms
    UNION ALL SELECT 9116,  473, 130, 0                                               -- Fist Weapons
    UNION ALL SELECT 9117, 2152, 144, 0                                               -- Warglaives
) AS `skill`
WHERE NOT EXISTS (SELECT 1 FROM `skill_race_class_info` `existing`
                  WHERE `existing`.`SkillID`=`skill`.`SkillID` AND `existing`.`ClassMask`=32768);

-- Tell the client about them, so the character sheet lists the proficiencies
-- the way it does for every other class. A higher push id than the Hero files
-- before it, for the same reason as those: the core reads pushes in id order
-- and the last word about a record wins.
SET @HeroSkillsId := (SELECT `Id` FROM `hotfix_data` WHERE `UniqueId`=1609394602 LIMIT 1);
SET @HeroSkillsId := COALESCE(@HeroSkillsId, (SELECT COALESCE(MAX(`Id`),0)+1 FROM `hotfix_data`));
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`)
SELECT @HeroSkillsId,1609394602,112059424,`RecordId`,1 FROM (      -- SkillRaceClassInfo 0x06ADE420
             SELECT 9102 AS `RecordId`
    UNION ALL SELECT 9103 UNION ALL SELECT 9104 UNION ALL SELECT 9105
    UNION ALL SELECT 9106 UNION ALL SELECT 9107 UNION ALL SELECT 9108
    UNION ALL SELECT 9109 UNION ALL SELECT 9110 UNION ALL SELECT 9111
    UNION ALL SELECT 9112 UNION ALL SELECT 9113 UNION ALL SELECT 9114
    UNION ALL SELECT 9115 UNION ALL SELECT 9116 UNION ALL SELECT 9117
) AS `push`
WHERE NOT EXISTS (
    SELECT 1 FROM `hotfix_data` `existing`
    WHERE `existing`.`Id`=@HeroSkillsId
      AND `existing`.`TableHash`=112059424 AND `existing`.`RecordId`=`push`.`RecordId`);
