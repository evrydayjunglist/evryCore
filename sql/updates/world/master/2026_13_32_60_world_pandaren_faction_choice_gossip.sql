-- The Spirit of Master Shang Xi (56013) opens the pandaren faction choice.
-- While "A New Fate" (31450) is in the quest log his gossip offers "I'm ready to decide.";
-- choosing it makes the player cast 108897 on herself, which opens the Horde or Alliance window.
-- The gossip rows are from retail captures of 6.2.0 (VerifiedBuild 20886); the condition, the
-- SmartAI and the quest-giver row are not captured. The retail GossipOptionID
-- is not known, so it follows the placeholder rule -(MenuID * 256 + OptionID).

SET @SHANG_XI := 56013;
SET @MENU := 13726;
SET @QUEST := 31450;

DELETE FROM `gossip_menu` WHERE `MenuID`=@MENU AND `TextID`=19723;
INSERT INTO `gossip_menu` (`MenuID`, `TextID`, `VerifiedBuild`) VALUES
(@MENU, 19723, 20886);

DELETE FROM `gossip_menu_option` WHERE `MenuID`=@MENU AND `OptionID`=0;
INSERT INTO `gossip_menu_option` (`MenuID`, `GossipOptionID`, `OptionID`, `OptionNpc`, `OptionText`, `OptionBroadcastTextID`, `Language`, `Flags`, `ActionMenuID`, `ActionPoiID`, `GossipNpcOptionID`, `BoxCoded`, `BoxMoney`, `BoxText`, `BoxBroadcastTextID`, `SpellID`, `OverrideIconID`, `VerifiedBuild`) VALUES
(@MENU, -3513856, 0, 0, 'I''m ready to decide.', 60279, 0, 0, 0, 0, NULL, 0, 0, NULL, 0, NULL, NULL, 20886);

DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId`=15 AND `SourceGroup`=@MENU AND `SourceEntry`=0;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `NegativeCondition`, `ErrorType`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(15, @MENU, 0, 0, 0, 9, 0, @QUEST, 0, 0, 0, 0, 0, '', 'Show Gossip Option Menu 13726 0 if Quest 31450 is taken');

UPDATE `creature_template` SET `AIName`='SmartAI' WHERE `entry`=@SHANG_XI;

DELETE FROM `smart_scripts` WHERE `entryorguid`=@SHANG_XI AND `source_type`=0;
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `event_param5`, `action_type`, `action_param1`, `action_param2`, `action_param3`, `action_param4`, `action_param5`, `action_param6`, `target_type`, `target_param1`, `target_param2`, `target_param3`, `target_x`, `target_y`, `target_z`, `target_o`, `comment`) VALUES
(@SHANG_XI, 0, 0, 1, 62, 0, 100, 0, @MENU, 0, 0, 0, 0, 134, 108897, 2, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 'Spirit of Master Shang Xi - On gossip option 0 selected - Invoker casts ''Pandaren Faction Choice'''),
(@SHANG_XI, 0, 1, 0, 61, 0, 100, 0, 0, 0, 0, 0, 0, 72, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 'Spirit of Master Shang Xi - On link - Close gossip');

DELETE FROM `creature_queststarter` WHERE `id`=@SHANG_XI AND `quest`=@QUEST;
INSERT INTO `creature_queststarter` (`id`, `quest`, `VerifiedBuild`) VALUES
(@SHANG_XI, @QUEST, 0);
