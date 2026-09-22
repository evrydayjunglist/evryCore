-- The native 69814 setter registers effect 162 only after a known spell is
-- classified under skill 934 (All Specializations). Hero already has that
-- skill, but spell 200749's SkillLineAbility row excludes class 16. Learning
-- the spell alone therefore leaves the native activation spell at zero.
-- Preserve the standard row and add only Hero's class-mask bit. The source
-- record is decoded from the current server's SkillLineAbility.db2; its
-- provenance and the pinned native code trace are in activation-fix-20260922.
-- This grants no combat kit and changes no character-owned spells or builds.

START TRANSACTION;
CREATE TEMPORARY TABLE `hero_activation_guard` (`ok` TINYINT NOT NULL CHECK (`ok`=1));
INSERT INTO `hero_activation_guard`
SELECT IF(
    NOT EXISTS (SELECT 1 FROM `skill_line_ability` WHERE `ID`=35162 AND
       (`VerifiedBuild`<>0 OR `AbilityVerb` IS NULL OR `AbilityVerb`<>'' OR `AbilityAllVerb` IS NULL OR `AbilityAllVerb`<>''
        OR `SkillLine`<>934 OR `Spell`<>200749 OR `MinSkillLineRank`<>1 OR `ClassMask`<>34815
        OR `SupercedesSpell`<>0 OR `AcquireMethod`<>2 OR `TrivialSkillLineRankHigh`<>0 OR `TrivialSkillLineRankLow`<>0
        OR `Flags`<>0 OR `NumSkillUps`<>0 OR `UniqueBit`<>0 OR `TradeSkillCategoryID`<>0 OR `SkillupSkillLineID`<>0
        OR `RaceMask1`<>-1 OR `RaceMask2`<>-1))
    AND NOT EXISTS (SELECT 1 FROM `hotfix_blob` WHERE `TableHash`=4282664694 AND `RecordId`=35162)
    AND NOT EXISTS (SELECT 1 FROM `hotfix_data` WHERE `TableHash`=4282664694 AND `RecordId`=35162 AND `UniqueId`<>1609394652)
    AND NOT EXISTS (SELECT 1 FROM `hotfix_data` WHERE `UniqueId` IN (1609394652,1609394653) AND
       (`UniqueId`<>1609394652 OR `TableHash`<>4282664694 OR `RecordId`<>35162 OR `Status`<>1)), 1, 0);

INSERT INTO `skill_line_ability` (`AbilityVerb`,`AbilityAllVerb`,`ID`,`SkillLine`,`Spell`,`MinSkillLineRank`,
    `ClassMask`,`SupercedesSpell`,`AcquireMethod`,`TrivialSkillLineRankHigh`,`TrivialSkillLineRankLow`,
    `Flags`,`NumSkillUps`,`UniqueBit`,`TradeSkillCategoryID`,`SkillupSkillLineID`,`RaceMask1`,`RaceMask2`,`VerifiedBuild`)
SELECT '','',35162,934,200749,1,34815,0,2,0,0,0,0,0,0,0,-1,-1,0
WHERE NOT EXISTS (SELECT 1 FROM `skill_line_ability` WHERE `ID`=35162);

SET @HeroActivationPush := (SELECT `Id` FROM `hotfix_data` WHERE `UniqueId`=1609394652 LIMIT 1);
SET @HeroActivationPush := COALESCE(@HeroActivationPush,(SELECT COALESCE(MAX(`Id`),0)+1 FROM `hotfix_data`));
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`)
SELECT @HeroActivationPush,1609394652,4282664694,35162,1
WHERE NOT EXISTS (SELECT 1 FROM `hotfix_data` WHERE `UniqueId`=1609394652);
DROP TEMPORARY TABLE `hero_activation_guard`;
COMMIT;
