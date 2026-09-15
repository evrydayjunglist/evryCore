-- Reconstructed Pandaria Remix opening. Quest, creature, item and spell identities
-- come from Blizzard data; placements, spawn timing and dialogue are reconstructed.
-- Ordinary Pandaria spawns are copied into separate Map objects, with independent
-- combat, loot, corpses and respawn persistence. New opening spawns are event-only.
INSERT INTO `spawn_group_timerunning` (`GroupId`, `SeasonMask`) VALUES (0,3),(1,3);
INSERT INTO `spawn_group_timerunning` (`GroupId`, `SeasonMask`)
SELECT DISTINCT sg.`groupId`,3 FROM `spawn_group` sg
LEFT JOIN `creature` c ON sg.`spawnType`=0 AND c.`guid`=sg.`spawnId`
LEFT JOIN `gameobject` g ON sg.`spawnType`=1 AND g.`guid`=sg.`spawnId`
WHERE sg.`groupId` NOT IN (0,1) AND (c.`map`=870 OR g.`map`=870);

INSERT INTO `spawn_group_template` (`groupId`, `groupName`, `groupFlags`) VALUES
(1286,'Pandaria Timerunning - Timeless Isle introduction',0);
INSERT INTO `spawn_group_timerunning` (`GroupId`, `SeasonMask`) VALUES (1286,2);

INSERT INTO `creature` (`guid`,`id`,`map`,`zoneId`,`areaId`,`spawnDifficulties`,`position_x`,`position_y`,`position_z`,`orientation`,`spawntimesecs`,`npcflag`,`ScriptName`,`VerifiedBuild`) VALUES
(11801064,216591,870,6757,6832,'0',-930,-4710,1.82367,4.712389,120,3,'npc_timerunning_pandaria_guide',0),
(11801065,216594,870,6757,6832,'0',-910,-4730,2.33545,3.141593,120,3,'npc_timerunning_pandaria_guide',0),
(11801066,217051,870,6757,6832,'0',-910,-4710,2.30933,3.141593,120,129,'npc_timerunning_pandaria_guide',0),
(11801067,217668,870,6757,6832,'0',-910,-4750,2.09381,3.141593,120,1,'npc_timerunning_pandaria_guide',0),
(11801068,217663,870,6757,6832,'0',-930,-4750,2.33545,1.570796,120,3,'npc_timerunning_pandaria_guide',0),
(11801069,217666,870,6757,6832,'0',-890,-4710,2.07971,3.141593,60,1,'npc_timerunning_pandaria_guide',0),
(11801070,217564,870,6757,6833,'0',-890,-4690,5.36066,4.712389,30,0,'',0),
(11801071,217557,870,6757,6832,'0',-890,-4750,2.06775,3.141593,30,0,'',0),
(11801072,217557,870,6757,6757,'0',-890,-4770,3.47034,3.141593,30,0,'',0),
(11801073,217557,870,6757,6757,'0',-910,-4770,3.47986,1.570796,30,0,'',0),
(11801074,217557,870,6757,6757,'0',-930,-4770,5.79663,1.570796,30,0,'',0),
(11801075,217557,870,6757,6757,'0',-950,-4770,3.87299,1.570796,30,0,'',0),
(11801076,217782,870,6757,6832,'0',-870,-4750,2.05115,3.141593,30,0,'',0),
(11801077,217782,870,6757,6757,'0',-870,-4770,5.13812,3.141593,30,0,'',0),
(11801078,217782,870,6757,6757,'0',-850,-4770,2.94177,3.141593,30,0,'',0),
(11801079,217190,870,6757,6832,'0',-870,-4710,9.53522,3.141593,60,0,'',0);

INSERT INTO `spawn_group` (`groupId`,`spawnType`,`spawnId`)
SELECT 1286,0,`guid` FROM `creature` WHERE `guid` BETWEEN 11801064 AND 11801079;

INSERT INTO `creature_queststarter` (`id`,`quest`,`VerifiedBuild`) VALUES
(216591,79433,0),(216594,79434,0),(216594,79435,0),(216594,80380,0),
(217663,79437,0),(216591,79438,0),(216591,79440,0);
INSERT INTO `creature_questender` (`id`,`quest`,`VerifiedBuild`) VALUES
(216594,79433,0),(216594,79434,0),(216594,79435,0),(216594,80380,0),
(217663,79437,0),(216591,79438,0),(216591,79440,0);

INSERT INTO `quest_template_addon` (`ID`,`PrevQuestID`,`ProvidedItemCount`) VALUES
(79432,0,0),(79433,79432,1),(79434,79433,0),(79435,79434,0),
(80380,79435,0),(79437,80380,1),(79438,80380,0),(79440,79438,0);

-- The preceding quest awards ten Bronze. Buying the badge uses the normal vendor
-- transaction and Blizzard's ten-Bronze cost record; its pickup spell grants credit.
INSERT INTO `npc_vendor` (`entry`,`slot`,`item`,`ExtendedCost`,`type`,`VerifiedBuild`) VALUES
(217051,0,215438,8512,1,0);

UPDATE `creature_template_difficulty` SET `LootID`=`Entry` WHERE `DifficultyID`=0 AND `Entry` IN (217564,217557,217190);
INSERT INTO `creature_loot_template` (`Entry`,`ItemType`,`Item`,`Chance`,`QuestRequired`,`LootMode`,`MinCount`,`MaxCount`,`Comment`) VALUES
(217564,0,213631,100,1,1,1,1,'Archaios - Empty Spool of Temporal Threads'),
(217557,0,213571,100,1,1,1,1,'Infinite Ravager - Thread of Time'),
(217557,0,210982,100,0,1,1,1,'Infinite Ravager - Thread of Power after earning the cloak'),
(217190,0,210982,100,0,1,1,1,'Eratus - Thread of Power after earning the cloak');

-- Loot requires the earned cloak; buying equipment elsewhere cannot replace the introduction.
INSERT INTO `conditions` (`SourceTypeOrReferenceId`,`SourceGroup`,`SourceEntry`,`ConditionTypeOrReference`,`ConditionTarget`,`ConditionValue1`,`Comment`) VALUES
(1,217557,210982,47,0,79435,'Thread of Power requires What is Hours Is Yours rewarded'),
(1,217190,210982,47,0,79435,'Thread of Power requires What is Hours Is Yours rewarded');
UPDATE `conditions` SET `ConditionValue2`=64
WHERE `SourceTypeOrReferenceId`=1 AND `SourceGroup` IN (217557,217190) AND `SourceEntry`=210982 AND `ConditionTypeOrReference`=47 AND `ConditionValue1`=79435;

INSERT INTO `spell_script_names` (`spell_id`,`ScriptName`) VALUES
(435393,'spell_timerunning_pandaria_chronostabilizer'),
(435393,'aura_timerunning_pandaria_chronostabilizer'),
(428022,'spell_timerunning_pandaria_thread'),
(440393,'aura_timerunning_pandaria_advantage');
