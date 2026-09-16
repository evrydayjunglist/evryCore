-- Pandaria Remix opening, first correction pass after the first playthrough.
--
-- Spawns move onto Blizzard's own quest markers for these quests. Eternus, Moratari, Horos, Momentus,
-- Archaios and Eratus stand on the quest_poi points of the quests they give, end, or are objectives of.
-- Erus, the Infinite Ravagers, the Time Rifts and the Unstable Rift object use map coordinates recorded
-- on Wowhead and in the RestedXP guide, converted with the Timeless Isle map bounds. Heights come from
-- the installed terrain. Facings, respawn times and the number of Ravagers are reconstructed.
DELETE FROM `creature` WHERE `guid` BETWEEN 11801064 AND 11801079;
INSERT INTO `creature` (`guid`,`id`,`map`,`zoneId`,`areaId`,`spawnDifficulties`,`position_x`,`position_y`,`position_z`,`orientation`,`spawntimesecs`,`ScriptName`,`VerifiedBuild`) VALUES
(11801064,216591,870,6757,6832,'0',-602,-4672,4.5447,5.588447,120,'npc_timerunning_pandaria_guide',0),
(11801065,216594,870,6757,6832,'0',-592,-4700,5.7445,1.91382,120,'npc_timerunning_pandaria_guide',0),
(11801066,217051,870,6757,6832,'0',-628,-4694,3.7398,0.702257,120,'npc_timerunning_pandaria_guide',0),
(11801067,217668,870,6757,6832,'0',-613,-4682,5.1766,0.737815,120,'npc_timerunning_pandaria_guide',0),
(11801068,217663,870,6757,6832,'0',-624.27,-4697.73,3.9846,0.857357,120,'npc_timerunning_pandaria_guide',0),
(11801069,217666,870,6757,6757,'0',-513.87,-4702.53,1.601,2.808111,60,'',0),
(11801070,217564,870,6757,6757,'0',-507,-4704,1.597,2.816688,30,'npc_timerunning_pandaria_archaios',0),
(11801071,217557,870,6757,6832,'0',-525.07,-4620.93,6.4149,3.727643,30,'',0),
(11801072,217557,870,6757,6832,'0',-553.87,-4644.93,3.6682,3.653933,30,'',0),
(11801073,217557,870,6757,6832,'0',-601.87,-4630.53,3.8171,4.709254,30,'',0),
(11801074,217557,870,6757,6832,'0',-637.07,-4654.53,1.8031,5.821022,30,'',0),
(11801075,217557,870,6757,6832,'0',-509.07,-4606.53,5.3988,3.755338,30,'',0),
(11801076,217782,870,6757,6832,'0',-489.07,-4586.13,7.1039,0,30,'',0),
(11801077,217782,870,6757,6832,'0',-571.79,-4622.61,3.8181,0,30,'',0),
(11801078,217782,870,6757,6832,'0',-651.47,-4637.73,1.168,0,30,'',0),
(11801079,217190,870,6757,6832,'0',-698,-4702,4.1023,0.302885,60,'',0);

-- The clickable Unstable Rift is Blizzard's gameobject. Investigating it casts the object's own spell,
-- which gives the optional objective's credit. The untargetable rift creature stays as scenery.
DELETE FROM `gameobject` WHERE `guid`=11801080;
INSERT INTO `gameobject` (`guid`,`id`,`map`,`zoneId`,`areaId`,`spawnDifficulties`,`position_x`,`position_y`,`position_z`,`orientation`,`rotation0`,`rotation1`,`rotation2`,`rotation3`,`spawntimesecs`,`animprogress`,`state`,`VerifiedBuild`) VALUES
(11801080,423343,870,6757,6757,'0',-513.87,-4702.53,1.601,2.808111,0,0,0.986131,0.165969,120,255,1,0);
DELETE FROM `spawn_group` WHERE `spawnType`=1 AND `spawnId`=11801080;
INSERT INTO `spawn_group` (`groupId`,`spawnType`,`spawnId`) VALUES
(1286,1,11801080);

-- The object's interaction condition is not in the client data, so database conditions stand in for it:
-- the rift can be investigated while It's About Time is in progress and until it has been investigated.
-- Each player sees either the clickable object or the scenery creature, never both at once.
DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId`=34 AND `SourceGroup`=0 AND `SourceEntry`=119615;
DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId`=32 AND ((`SourceGroup`=8 AND `SourceEntry`=423343) OR (`SourceGroup`=5 AND `SourceEntry`=217666));
INSERT INTO `conditions` (`SourceTypeOrReferenceId`,`SourceGroup`,`SourceEntry`,`SourceId`,`ElseGroup`,`ConditionTypeOrReference`,`ConditionTarget`,`ConditionValue1`,`ConditionValue2`,`ConditionValue3`,`NegativeCondition`,`Comment`) VALUES
(34,0,119615,0,0,47,0,79432,8,0,0,'Unstable Rift can be investigated while It''s About Time is in progress'),
(34,0,119615,0,0,48,0,447709,0,0,0,'Unstable Rift can be investigated until it has been investigated'),
(32,8,423343,0,0,47,0,79432,8,0,0,'Unstable Rift object is shown while It''s About Time is in progress'),
(32,8,423343,0,0,48,0,447709,0,0,0,'Unstable Rift object is shown until the rift has been investigated'),
(32,5,217666,0,0,47,0,79432,8,0,1,'Unstable Rift creature is shown when It''s About Time is not in progress'),
(32,5,217666,0,1,48,0,447709,0,0,1,'Unstable Rift creature is shown once the rift has been investigated');

-- Moratari gives Knot My Problem, Goodbyes Are Hard When You Live Forever and Recalling the War.
-- Knot My Problem is offered together with Weave It To Me. Erus is a vendor and gives no quests.
DELETE FROM `creature_queststarter` WHERE (`id`=216591 AND `quest` IN (79438,79440)) OR (`id`=217663 AND `quest`=79437) OR (`id`=216594 AND `quest` IN (79437,79438,79440));
INSERT INTO `creature_queststarter` (`id`,`quest`,`VerifiedBuild`) VALUES
(216594,79437,0),(216594,79438,0),(216594,79440,0);
DELETE FROM `creature_questender` WHERE (`id`=216591 AND `quest`=79438) OR (`id`=217663 AND `quest`=79437) OR (`id`=216594 AND `quest` IN (79437,79438));
INSERT INTO `creature_questender` (`id`,`quest`,`VerifiedBuild`) VALUES
(216594,79437,0),(216594,79438,0);
UPDATE `quest_template_addon` SET `PrevQuestID`=79433 WHERE `ID`=79437;

-- The Time Rift's display has no model record, so the creature could not be created.
-- The size values are reconstructed, not captured.
DELETE FROM `creature_model_info` WHERE `DisplayID`=118479;
INSERT INTO `creature_model_info` (`DisplayID`,`BoundingRadius`,`CombatReach`,`DisplayID_Other_Gender`,`VerifiedBuild`) VALUES
(118479,0.5,1,0,0);

-- Conversation windows. Every text below is Blizzard's own record from the Remix. Which NPC shows which
-- record, and when Moratari's text changes, is reconstructed from what each text says.
DELETE FROM `npc_text` WHERE `ID` BETWEEN 900100 AND 900106;
INSERT INTO `npc_text` (`ID`,`Probability0`,`BroadcastTextID0`,`VerifiedBuild`) VALUES
(900100,1,254742,0),
(900101,1,255800,0),
(900102,1,256658,0),
(900103,1,255821,0),
(900104,1,257951,0),
(900105,1,256156,0),
(900106,1,255768,0);

DELETE FROM `gossip_menu` WHERE `MenuID` BETWEEN 90100 AND 90104;
INSERT INTO `gossip_menu` (`MenuID`,`TextID`,`VerifiedBuild`) VALUES
(90100,900100,0),
(90101,900101,0),
(90101,900102,0),
(90101,900103,0),
(90102,900104,0),
(90103,900105,0),
(90104,900106,0);

-- Momentus' option uses Blizzard's gossip option id; its wording comes from Wowhead.
DELETE FROM `gossip_menu_option` WHERE `MenuID` BETWEEN 90100 AND 90104;
INSERT INTO `gossip_menu_option` (`MenuID`,`GossipOptionID`,`OptionID`,`OptionNpc`,`OptionText`,`OptionBroadcastTextID`,`VerifiedBuild`) VALUES
(90102,-23066112,0,1,'I want to browse your goods.',3370,0),
(90103,-23066368,0,1,'I want to browse your goods.',3370,0),
(90104,120769,0,0,'Can you forge this thread and chronobadge into a cloak?',0,0);

DELETE FROM `creature_template_gossip` WHERE `CreatureID` IN (216591,216594,217051,217663,217668);
INSERT INTO `creature_template_gossip` (`CreatureID`,`MenuID`,`VerifiedBuild`) VALUES
(216591,90100,0),
(216594,90101,0),
(217051,90102,0),
(217663,90103,0),
(217668,90104,0);

DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` IN (14,15) AND `SourceGroup` BETWEEN 90100 AND 90104;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`,`SourceGroup`,`SourceEntry`,`SourceId`,`ElseGroup`,`ConditionTypeOrReference`,`ConditionTarget`,`ConditionValue1`,`ConditionValue2`,`ConditionValue3`,`NegativeCondition`,`Comment`) VALUES
(14,90101,900101,0,0,47,0,79433,64,0,1,'Moratari: research text before Seeking Expert Advice is rewarded'),
(14,90101,900102,0,0,47,0,79433,64,0,0,'Moratari: cloak promise after Seeking Expert Advice is rewarded'),
(14,90101,900102,0,0,47,0,79435,64,0,1,'Moratari: cloak promise until What''s Hours Is Yours is rewarded'),
(14,90101,900103,0,0,47,0,79435,64,0,0,'Moratari: thread explanation after What''s Hours Is Yours is rewarded'),
(15,90104,0,0,0,47,0,79435,8,0,0,'Momentus: forge option while What''s Hours Is Yours is in progress'),
(15,90104,0,0,0,2,0,215438,1,0,0,'Momentus: forge option while carrying the Chronobadge'),
(15,90104,0,0,0,48,0,446826,0,0,0,'Momentus: forge option until the cloak has been forged');

-- Spoken lines with no Blizzard text record. The wording is quoted from Wowhead and Warcraft Wiki.
DELETE FROM `creature_text` WHERE `CreatureID` IN (216591,216594,217564);
INSERT INTO `creature_text` (`CreatureID`,`GroupID`,`ID`,`Text`,`Type`,`Language`,`Probability`,`Emote`,`Duration`,`Sound`,`SoundPlayType`,`BroadcastTextId`,`TextRange`,`comment`) VALUES
(216591,0,0,'Another rift sealed. Now for the one who is opening them...',12,0,100,0,0,0,0,0,0,'Eternus - a Timerunner arrives'),
(216591,1,0,'Our efforts cannot continue until Archaios is dealt with.',12,0,100,0,0,0,0,0,0,'Eternus - It''s About Time accepted'),
(216591,2,0,'We cannot allow the other infinites to unravel the timeway. Moratori will know what to do.',12,0,100,0,0,0,0,0,0,'Eternus - Seeking Expert Advice accepted'),
(216594,0,0,'I have an idea for how to use the relic you found. But first, we need to clean up this mess!',12,0,100,0,0,0,0,0,0,'Moratari - Weave It To Me or Knot My Problem accepted'),
(217564,0,0,'Eternus, you traitor! We could reshape this entire timeline!',12,0,100,0,0,0,0,0,0,'Archaios - combat starts'),
(217564,1,0,'But... last time...',12,0,100,0,0,0,0,0,0,'Archaios - dies');
