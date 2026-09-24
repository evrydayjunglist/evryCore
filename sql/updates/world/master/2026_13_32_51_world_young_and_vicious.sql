-- Young and Vicious (24626): capture Swiftclaw with the lasso and ride it to the pens.
-- The retail capture uses vehicle 617. Vehicle.db2 gives seat 7346 to the rope bunny
-- and seat 7347 to the player. The scripts handle spell 70927 and area trigger 5675.

DELETE FROM `spell_script_names` WHERE `spell_id`=70927 AND `ScriptName`='spell_q24626_bloodtalon_lasso';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(70927, 'spell_q24626_bloodtalon_lasso');

UPDATE `creature_template` SET `VehicleId`=617, `npcflag`=0 WHERE `entry`=38002; -- Swiftclaw (vehicle)

DELETE FROM `areatrigger_scripts` WHERE `entry`=5675;
INSERT INTO `areatrigger_scripts` (`entry`, `ScriptName`) VALUES
(5675, 'at_raptor_pens_young_and_vicious');
