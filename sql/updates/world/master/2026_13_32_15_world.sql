-- Chromie Time: FAQ gossip tree + sub-10 npc_text (ct-lvl3 12.0.7.68887)
-- Model: FAQ available whenever Dorn select-lock is NOT active; ChromieTimeNpc select
--        still gated by CanSelectChromieTimeExpansion() in npc_chromie_time.
-- Flow 2R dual-check PASS: NN=15 free on master+evry (tip world 10-14; master/evry max 09).

-- npc_text: BT 206524 (low-level / not ready) + FAQ answer bodies
DELETE FROM `npc_text` WHERE `ID` IN (40349, 40352, 40353, 40354, 40357);
INSERT INTO `npc_text` (`ID`, `Probability0`, `Probability1`, `Probability2`, `Probability3`, `Probability4`, `Probability5`, `Probability6`, `Probability7`, `BroadcastTextID0`, `BroadcastTextID1`, `BroadcastTextID2`, `BroadcastTextID3`, `BroadcastTextID4`, `BroadcastTextID5`, `BroadcastTextID6`, `BroadcastTextID7`, `VerifiedBuild`) VALUES
(40349, 1, 0, 0, 0, 0, 0, 0, 0, 206524, 0, 0, 0, 0, 0, 0, 0, 68887), -- little more experience (ct-lvl3)
(40352, 1, 0, 0, 0, 0, 0, 0, 0, 240872, 0, 0, 0, 0, 0, 0, 0, 68887), -- FAQ hub
(40353, 1, 0, 0, 0, 0, 0, 0, 0, 240784, 0, 0, 0, 0, 0, 0, 0, 68887), -- What are Timewalking Campaigns?
(40354, 1, 0, 0, 0, 0, 0, 0, 0, 240843, 0, 0, 0, 0, 0, 0, 0, 68887), -- Can friends join?
(40357, 1, 0, 0, 0, 0, 0, 0, 0, 240841, 0, 0, 0, 0, 0, 0, 0, 68887); -- Don't want to stay

-- FAQ menus (retail GossipIDs from ct-lvl3)
DELETE FROM `gossip_menu` WHERE `MenuID` IN (31336, 31368, 31369, 31370);
INSERT INTO `gossip_menu` (`MenuID`, `TextID`, `VerifiedBuild`) VALUES
(31336, 40352, 68887),
(31370, 40353, 68887),
(31368, 40354, 68887),
(31369, 40357, 68887);

-- Root FAQ entry on Chromie menu 25426 (OrderIndex 5 in sniff)
DELETE FROM `gossip_menu_option` WHERE `MenuID` = 25426 AND `GossipOptionID` = 109278;
DELETE FROM `gossip_menu_option` WHERE `MenuID` IN (31336, 31368, 31369, 31370);
INSERT INTO `gossip_menu_option` (`MenuID`, `GossipOptionID`, `OptionID`, `OptionNpc`, `OptionText`, `OptionBroadcastTextID`, `Language`, `Flags`, `ActionMenuID`, `ActionPoiID`, `GossipNpcOptionID`, `BoxCoded`, `BoxMoney`, `BoxText`, `BoxBroadcastTextID`, `SpellID`, `OverrideIconID`, `VerifiedBuild`) VALUES
-- 25426
(25426, 109278, 5, 0, 'I have a question about Timewalking Campaigns.', 0, 0, 0, 31336, 0, NULL, 0, 0, NULL, 0, NULL, NULL, 68887),
-- 31336 FAQ hub
(31336, 109315, 1, 0, 'What are Timewalking Campaigns?', 0, 0, 0, 31370, 0, NULL, 0, 0, NULL, 0, NULL, NULL, 68887),
(31336, 109313, 2, 0, 'Can my friends join me?', 0, 0, 0, 31368, 0, NULL, 0, 0, NULL, 0, NULL, NULL, 68887),
(31336, 109314, 3, 0, 'What if I don''t want to stay in the timeline I chose?', 0, 0, 0, 31369, 0, NULL, 0, 0, NULL, 0, NULL, NULL, 68887),
(31336, 109276, 4, 0, 'I want to talk about something else.', 0, 0, 0, 25426, 0, NULL, 0, 0, NULL, 0, NULL, NULL, 68887),
-- answer leaves
(31370, 109328, 1, 0, 'I have another question.', 0, 0, 0, 31336, 0, NULL, 0, 0, NULL, 0, NULL, NULL, 68887),
(31368, 109327, 0, 0, 'I have another question.', 0, 0, 0, 31336, 0, NULL, 0, 0, NULL, 0, NULL, NULL, 68887),
(31369, 109326, 0, 0, 'I have another question.', 0, 0, 0, 31336, 0, NULL, 0, 0, NULL, 0, NULL, NULL, 68887);
