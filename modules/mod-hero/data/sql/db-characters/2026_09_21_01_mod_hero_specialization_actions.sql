-- Keep each existing Hero on Initial (1479/order 4), including explicit loot
-- references. Preserve every learned spell, trait, item and resource row.
-- Copy existing Initial action bars to unused visible mode slots so the first
-- selection does not make the owner's existing layout disappear. Native spec
-- switching subsequently saves each mode separately. Never overwrite a slot.
INSERT IGNORE INTO `character_action` (`guid`,`spec`,`traitConfigId`,`button`,`action`,`type`)
SELECT a.`guid`,m.`spec`,0,a.`button`,a.`action`,a.`type`
FROM `character_action` a
JOIN `characters` c ON c.`guid`=a.`guid` AND c.`class`=16 AND c.`primarySpecialization`=1479
CROSS JOIN (SELECT 0 AS `spec` UNION ALL SELECT 1 UNION ALL SELECT 2) m
WHERE a.`spec`=4 AND a.`traitConfigId`=0;
