-- Owner-run recovery, not an automatic module migration.
-- Stop worldserver and back up the characters database before applying this file.
-- Run only in the intended characters database, before restoring a server binary
-- that does not reconstruct permanent Hero spell sources at login.
-- Keep the advancement tables so a later upgrade retains paid ownership and receipts.

START TRANSACTION;
-- A durable revocation is authoritative even if a crash left character_spell
-- unchanged. Restrict cleanup to this Hero catalog; keep the source ledger.
DELETE cs FROM `character_spell` cs
INNER JOIN `characters` c ON c.`guid`=cs.`guid`
INNER JOIN `character_hero_spell_source` s ON s.`guid`=cs.`guid` AND s.`spell`=cs.`spell`
INNER JOIN `character_hero_freepick` p ON p.`realm`=s.`realm` AND p.`guid`=s.`guid`
  AND p.`profile`='hero-free-pick-starter'
WHERE s.`independent`=0 AND c.`class`=16 AND c.`deleteDate` IS NULL
AND s.`spell` IN (100,116,122,133,774,193315,1953,196819,185358,8921);

INSERT IGNORE INTO `character_spell` (`guid`,`spell`,`active`,`disabled`)
SELECT s.`guid`,s.`spell`,1,0
FROM `character_hero_spell_source` s
INNER JOIN `characters` c ON c.`guid`=s.`guid`
WHERE s.`independent`=1 AND c.`class`=16 AND c.`deleteDate` IS NULL
AND NOT EXISTS (
  SELECT 1 FROM `character_hero_freepick_deleted` d
  WHERE d.`realm`=s.`realm` AND d.`guid`=s.`guid`
);
COMMIT;
