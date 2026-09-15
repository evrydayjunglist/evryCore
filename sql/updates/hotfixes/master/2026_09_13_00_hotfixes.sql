-- The installed Pandaria loadouts require four Timerunner's Satchels (216653).
-- Item.db2 contains their basic record, but ItemSparse.db2 lacks the detailed row.
-- Reconstructed bag properties: 36 slots, soulbound, common quality, level 10.
-- This is not a sniffed record. VerifiedBuild remains zero. Remove this custom
-- fallback when supplying verified replacement data: custom rows load last.
-- Item identity and visible properties: https://www.wowhead.com/item=216653
INSERT INTO `item_sparse` (`ID`,`Display`,`Description`,`Display1`,`Display2`,`Display3`,
    `ExpansionID`,`Stackable`,`AllowableRace1`,`AllowableRace2`,`AllowableClass`,
    `VendorStackCount`,`PriceVariance`,`PriceRandomValue`,`ContentTuningID`,
    `ItemLevel`,`RequiredLevel`,`InventoryType`,`OverallQualityID`,`Material`,`Bonding`,`ContainerSlots`,
    `StatModifierBonusStat1`,`StatModifierBonusStat2`,`StatModifierBonusStat3`,`StatModifierBonusStat4`,`StatModifierBonusStat5`,
    `StatModifierBonusStat6`,`StatModifierBonusStat7`,`StatModifierBonusStat8`,`StatModifierBonusStat9`,`StatModifierBonusStat10`,`VerifiedBuild`)
SELECT 216653,'Timerunner''s Satchel','','','','',4,1,-1,-1,-1,1,1,1,2905,1,10,18,1,7,1,36,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0
WHERE NOT EXISTS (SELECT 1 FROM `item_sparse` WHERE `ID`=216653);
SET @AddedTimerunnerSatchel := ROW_COUNT();
SET @TimerunnerHotfixId := (SELECT COALESCE(MAX(`Id`),0)+1 FROM `hotfix_data`);
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`)
SELECT @TimerunnerHotfixId,0,0x919BE54E,216653,1 WHERE @AddedTimerunnerSatchel=1;
