-- Reaper uses Player::Create's ordinary equipment path. Never grant items on login.
DROP PROCEDURE IF EXISTS `coa_reaper_20260925_creation`;
DELIMITER $$
CREATE PROCEDURE `coa_reaper_20260925_creation`()
BEGIN
  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
  START TRANSACTION;
  IF EXISTS (SELECT 1 FROM `playercreateinfo_item` WHERE `class`=17 AND `itemid`=2092
    AND NOT (`race`=0 AND `amount`=1)) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Conflicting Reaper starter item; nothing changed';
  END IF;
  INSERT INTO `playercreateinfo_item` (`race`,`class`,`itemid`,`amount`)
    SELECT 0,17,2092,1 WHERE NOT EXISTS
      (SELECT 1 FROM `playercreateinfo_item` WHERE `race`=0 AND `class`=17 AND `itemid`=2092);
  COMMIT;
END$$
DELIMITER ;
CALL `coa_reaper_20260925_creation`();
DROP PROCEDURE `coa_reaper_20260925_creation`;
