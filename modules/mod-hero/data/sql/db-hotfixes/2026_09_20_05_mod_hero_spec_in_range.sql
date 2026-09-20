-- Move Hero's specialisation id inside the range the client ships.
--
-- What this is for. Every spell a Hero learns draws in the spellbook as spell
-- 205523, Brewmaster Monk's Blackout Kick: wrong name, wrong description, wrong
-- icon, wrong cast animation, and the client refuses the cast for not holding
-- the axe that spell wants. The spell itself is fine. Asking the client with
-- /dump C_SpellBook.GetSpellBookItemInfo(1, 0) returns actionID 185358, the
-- spell the Hero really knows, alongside spellID 205523. So the book holds the
-- right spell and it is the resolution from that spell to the one drawn that
-- fails, and it fails the same way for all five spells tested, including one
-- that has no override anywhere.
--
-- Why the specialisation id is the suspect. Every specialisation this client
-- ships has an id between 62 and 1480; there are sixty of them. Hero's is 9101
-- and Reaper's are 9001 to 9004, and both classes show the fault while an
-- ordinary Warrior on the same client in the same session does not. Asked in
-- game, that Warrior reports specialisation 1446 and the Hero reports 9101.
-- The client finds the 9101 record itself perfectly well, with the right icon
-- and role, so the store that holds specialisations is not the problem; a
-- structure elsewhere that is sized by the real range would be.
--
-- This is a test, not a proven fix. It changes exactly one thing so the answer
-- means something. If the spellbook draws correctly after this, the fault was
-- ours and the fix is data. If it does not, the class id is the remaining
-- suspect, it cannot be moved, and the client work in
-- evryLoader\docs\SPELLBOOK_FALLBACK_69814.md is the way through.
--
-- 1479 is free. It is the highest id at or below the real maximum that no
-- shipped specialisation uses, read out of the client's own
-- ChrSpecialization.db2 rather than chosen. It breaks this module's habit of
-- numbering from 9101, deliberately: the whole point is to be inside the range
-- the client already knows, and that habit is what is under test.
--
-- This keeps Hero on one hidden specialisation, exactly as before. Giving Hero
-- real specialisations for Free Pick and Wildcard is a separate change and must
-- not ride along with this one, or the test proves nothing.

-- The replacement row. Every value is the one the 9101 row already carries;
-- only the id differs.
INSERT INTO `chr_specialization` (`Name`,`FemaleName`,`Description`,`ID`,`ClassID`,`OrderIndex`,
    `PetTalentType`,`Role`,`Flags`,`SpellIconFileID`,`PrimaryStatPriority`,`AnimReplacements`,
    `MasterySpellID1`,`MasterySpellID2`,`VerifiedBuild`)
SELECT 'Initial','','',1479,16,4,0,2,64,135771,0,0,0,0,0
WHERE NOT EXISTS (SELECT 1 FROM `chr_specialization` WHERE `ID`=1479);

-- The old row goes. Leaving it would give class 16 two specialisations, both at
-- order index 4, which is the index character creation asks for by name.
DELETE FROM `chr_specialization` WHERE `ID`=9101 AND `ClassID`=16;

-- The class row points at the new one.
UPDATE `chr_classes` SET `DefaultSpec`=1479 WHERE `ID`=16;

-- One push carrying all three facts: the new specialisation exists, the old one
-- is gone, and the class row changed. Status 1 is valid and status 2 is
-- RecordRemoved, the same marker the Reaper renumber used for ChrClasses 16.
--
-- The push id has to be higher than every push before it, because the core
-- reads them in id order and the last word about a record wins. Taking the next
-- free id gives that, which is 111753 as this is written.
--
-- The unique id is fixed rather than random so re-running this file cannot
-- quietly change it. 1609394610 was checked against the live table and is free;
-- 1609394601, 1609394602 and 1609394604 are already taken by the three Hero
-- files and would have made the guard below skip this whole push in silence.
-- Without a new unique id a client that has already played a Hero would keep
-- every row it cached, including specialisation 9101.
SET @HeroSpecPush := (SELECT `Id` FROM `hotfix_data` WHERE `UniqueId`=1609394610 LIMIT 1);
SET @HeroSpecPush := COALESCE(@HeroSpecPush, (SELECT COALESCE(MAX(`Id`),0)+1 FROM `hotfix_data`));
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`)
SELECT @HeroSpecPush,1609394610,`TableHash`,`RecordId`,`Status` FROM (
             SELECT 2685374048 AS `TableHash`, 1479 AS `RecordId`, 1 AS `Status`  -- ChrSpecialization 0xA00F8E60, the new row
    UNION ALL SELECT 2685374048, 9101, 2                                          -- ChrSpecialization, the old row is gone
    UNION ALL SELECT 4119371148,   16, 1                                          -- ChrClasses 0xF5889D8C, DefaultSpec changed
) AS `push`
WHERE NOT EXISTS (SELECT 1 FROM `hotfix_data` WHERE `UniqueId`=1609394610);
