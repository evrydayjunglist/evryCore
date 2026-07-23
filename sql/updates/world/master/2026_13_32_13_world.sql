-- Chromie Time Phase 4: remaining UIChromieTimeExpansionInfo → faction breadcrumb maps
-- Ui 6 (BC) already present from 2026_13_32_12 (60959/60961; CT-A AutoLaunched Onward).
-- Classic 5/7–10: Onward-to-Adventure pairs — AllowableRaces confirmed (do not assign A/H by ID order).
-- 14–16: no Onward templates; wiki Timewalking Campaigns start quests present in world DB.
-- Flow 2R dual-check PASS: NN=13 free on master (no 13) + evry (max 09) + tip (10–12).

DELETE FROM `chromie_time_expansion_quest` WHERE `UiExpansionId` IN (5, 7, 8, 9, 10, 14, 15, 16);
INSERT INTO `chromie_time_expansion_quest` (`UiExpansionId`, `AllianceQuestId`, `HordeQuestId`, `VerifiedBuild`) VALUES
(5, 60891, 60887, 67808),  -- Cata: Onward EK / Kalimdor
(7, 60962, 60963, 67808),  -- WotLK: Onward Northrend
(8, 60965, 60964, 67808),  -- MoP: Onward Pandaria (Horde ID lower)
(9, 60969, 60968, 67808),  -- WoD: Onward Draenor (Horde ID lower)
(10, 60971, 60970, 67808), -- Legion: Onward Broken Isles (Horde ID lower)
(14, 60545, 61874, 67808), -- SL: A Chilling Summons (wiki)
(15, 46727, 51443, 67808), -- BfA: Tides of War / Mission Statement (wiki)
(16, 65436, 65435, 67808); -- DF: The Dragon Isles Await (Horde ID lower)
