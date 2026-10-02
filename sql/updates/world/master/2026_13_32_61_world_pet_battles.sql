--
-- Pet battles: NPC trainer teams, wild battle pet spawns and flags, and two battle pet trainer fixes.
--

-- Teams fought by the pet battle tamer NPCs. One row per pet; slot is 0 to 2.
CREATE TABLE IF NOT EXISTS `battle_pet_npc_team` (
    `npcEntry` INT UNSIGNED NOT NULL COMMENT 'Creature template entry',
    `slot` TINYINT UNSIGNED NOT NULL COMMENT 'Team slot (0-2)',
    `speciesId` INT UNSIGNED NOT NULL COMMENT 'BattlePetSpecies.db2 ID',
    `level` SMALLINT UNSIGNED NOT NULL DEFAULT 1,
    `breedId` SMALLINT UNSIGNED NOT NULL DEFAULT 3 COMMENT 'BattlePetBreedState breed ID',
    `quality` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=Poor, 1=Common, 2=Uncommon, 3=Rare',
    `ability1` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'BattlePetAbility.db2 ID for slot 0',
    `ability2` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'BattlePetAbility.db2 ID for slot 1',
    `ability3` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'BattlePetAbility.db2 ID for slot 2',
    `npcTeamMemberID` INT NOT NULL DEFAULT 0 COMMENT 'BattlePetNPCTeamMember.db2 ID for pet name',
    `creatureId` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Creature template entry override for model (0=species default)',
    PRIMARY KEY (`npcEntry`, `slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Pet Battle NPC Trainer Teams';

-- Tamer teams (species, level and abilities from the Wowhead NPC pages; breed 3 and rare quality
-- are placeholders until retail data is captured).
DELETE FROM `battle_pet_npc_team` WHERE `npcEntry` IN (63194,65648,66412,66422,66442,66515,66518,66522,66552,66557,66636,66639,66730,66733,66734,66738,66739,66741,66815);
INSERT INTO `battle_pet_npc_team`
    (`npcEntry`, `slot`, `speciesId`, `level`, `breedId`, `quality`, `ability1`, `ability2`, `ability3`, `npcTeamMemberID`, `creatureId`)
VALUES
-- Old MacDonald (65648) -- Westfall, level 3
(65648, 0, 874,  3, 3, 3, 119, 360, 283, 0, 65659),  -- Teensy (Critter)
(65648, 1, 875,  3, 3, 3, 112, 524, 581, 0, 65660),  -- Clucks (Flying)
(65648, 2, 876,  3, 3, 3, 384, 389, 390, 0, 65661),  -- Foe Reaper 800 (Mechanical)

-- Steven Lisbane (63194) -- Northern Stranglethorn, level 9
(63194, 0, 885,  9, 3, 3, 349, 124, 354, 0, 65677),  -- Nanners (Beast)
(63194, 1, 884,  9, 3, 3, 429, 535, 536, 0, 66983),  -- Moonstalker (Beast)
(63194, 2, 883,  9, 3, 3, 432, 538, 431, 0, 66982),  -- Emeralda (Magic)

-- Elena Flutterfly (66412) -- Moonglade, level 17
(66412, 0, 926, 17, 3, 3, 115, 122, 347, 0, 66417),  -- Willow (Dragonkin)
(66412, 1, 925, 17, 3, 3, 114, 460, 463, 0, 66416),  -- Beacon (Magic)
(66412, 2, 924, 17, 3, 3, 420, 506, 162, 0, 66414),  -- Lacewing (Flying)

-- Cassandra Kaboom (66422) -- Southern Barrens, level 11
(66422, 0, 907, 11, 3, 3, 455, 208, 278, 0, 66427),  -- Whirls (Mechanical)
(66422, 1, 908, 11, 3, 3, 777, 640, 645, 0, 66428),  -- Cluckatron (Mechanical)
(66422, 2, 909, 11, 3, 3, 389, 390, 392, 0, 66429),  -- Gizmo (Mechanical)

-- Zoltan (66442) -- Felwood, level 16
(66442, 0, 921, 16, 3, 3, 406, 409, 407, 0, 66443),  -- Ultramus (Magic)
(66442, 1, 922, 16, 3, 3, 473, 474, 475, 0, 66444),  -- Beamer (Magic)
(66442, 2, 923, 16, 3, 3, 202, 208, 644, 0, 66445),  -- Hatewalker (Mechanical)

-- Kortas Darkhammer (66515) -- Searing Gorge, level 15
(66515, 0, 939, 15, 3, 3, 115, 168, 122, 0, 66490),  -- Garnestrasz (Dragonkin)
(66515, 1, 937, 15, 3, 3, 393, 256, 792, 0, 66488),  -- Obsidion (Dragonkin)
(66515, 2, 938, 15, 3, 3, 525, 597, 598, 0, 66489),  -- Veridia (Dragonkin)

-- Everessa (66518) -- Swamp of Sorrows, level 16
(66518, 0, 941, 16, 3, 3, 110, 156, 152, 0, 66492),  -- Anklor (Beast)
(66518, 1, 942, 16, 3, 3, 233, 228, 232, 0, 66493),  -- Croaker (Aquatic)
(66518, 2, 943, 16, 3, 3, 507, 504, 162, 0, 66494),  -- Dampwing (Flying)

-- Lydia Accoste (66522) -- Deadwind Pass, level 19
(66522, 0, 947, 19, 3, 3, 422, 657, 121, 0, 66498),  -- Nightstalker (Undead)
(66522, 1, 948, 19, 3, 3, 210, 592, 476, 0, 66499),  -- Bishibosh (Undead)
(66522, 2, 949, 19, 3, 3, 318, 303, 398, 0, 66500),  -- Jack (Elemental)

-- Narrok (66552) -- Nagrand (Outland), level 22
(66552, 0, 956, 22, 3, 3, 571, 377, 375, 0, 66537),  -- Stompy (Beast)
(66552, 1, 957, 22, 3, 3, 367, 165, 253, 0, 66538),  -- Dramaticus (Critter)
(66552, 2, 958, 22, 3, 3, 233, 228, 232, 0, 66539),  -- Prince Wart (Aquatic)

-- Bloodknight Antari (66557) -- Shadowmoon Valley, level 24
(66557, 0, 962, 24, 3, 3, 608, 764, 751, 0, 66543),  -- Netherbite (Dragonkin)
(66557, 1, 963, 24, 3, 3, 113, 178, 179, 0, 66544),  -- Jadefire (Elemental)
(66557, 2, 964, 24, 3, 3, 484, 486, 488, 0, 66545),  -- Arcanus (Magic)

-- Nearly Headless Jacob (66636) -- Crystalsong Forest, level 25
(66636, 0, 968, 25, 3, 3, 654, 442, 212, 0, 66616),  -- Mort (Undead)
(66636, 1, 969, 25, 3, 3, 256, 471, 650, 0, 66618),  -- Stitch (Undead)
(66636, 2, 970, 25, 3, 3, 218, 468, 780, 0, 66619),  -- Spooky Strangler (Undead)

-- Gutretch (66639) -- Zul'Drak, level 25
(66639, 0, 974, 25, 3, 3, 360, 253, 359, 0, 66624),  -- Blight (Critter)
(66639, 1, 975, 25, 3, 3, 369, 364, 160, 0, 66626),  -- Fleshrender (Beast)
(66639, 2, 976, 25, 3, 3, 369, 364, 159, 0, 66627),  -- Cadavus (Beast)

-- Hyuna of the Shrines (66730) -- Jade Forest, level 25
(66730, 0, 992, 25, 3, 3, 310, 376, 123, 0, 66709),  -- Dor the Wall (Aquatic)
(66730, 1, 993, 25, 3, 3, 155, 156, 159, 0, 66710),  -- Fangor (Beast)
(66730, 2, 994, 25, 3, 3, 632, 420, 270, 0, 66711),  -- Skyshaper (Flying)

-- Mo'ruk (66733) -- Krasarang Wilds, level 25
(66733, 0, 1000, 25, 3, 3, 504, 507, 508, 0, 66714),  -- Lightstalker (Flying)
(66733, 1, 999,  25, 3, 3, 376, 249, 566, 0, 66713),  -- Needleback (Aquatic)
(66733, 2, 998,  25, 3, 3, 369, 160, 159, 0, 66712),  -- Woodcarver (Beast)

-- Farmer Nishi (66734) -- Valley of the Four Winds, level 25
(66734, 0, 995, 25, 3, 3, 369, 160, 159, 0, 66715),  -- Brood of Mothallus (Critter)
(66734, 1, 997, 25, 3, 3, 268, 753, 404, 0, 66718),  -- Siren (Elemental)
(66734, 2, 996, 25, 3, 3, 745, 298, 828, 0, 66716),  -- Toothbreaker (Elemental)

-- Courageous Yon (66738) -- Kun-Lai Summit, level 25
(66738, 0, 1001, 25, 3, 3, 539, 541, 163, 0, 66719),  -- Bleat (Beast)
(66738, 1, 1002, 25, 3, 3, 360, 162, 159, 0, 66720),  -- Lapin (Critter)
(66738, 2, 1003, 25, 3, 3, 524, 170, 581, 0, 66721),  -- Piqua (Flying)

-- Wastewalker Shu (66739) -- Dread Wastes, level 25
(66739, 0, 1009, 25, 3, 3, 511, 509, 513, 0, 66724),  -- Crusher (Aquatic)
(66739, 1, 1007, 25, 3, 3, 315, 158, 566, 0, 66722),  -- Mutilator (Beast)
(66739, 2, 1008, 25, 3, 3, 453, 814, 644, 0, 66723),  -- Pounder (Elemental)

-- Aki the Chosen (66741) -- Vale of Eternal Blossoms, level 25
(66741, 0, 1010, 25, 3, 3, 509, 283, 564, 0, 66725),  -- Whiskers (Aquatic)
(66741, 1, 1011, 25, 3, 3, 204, 347, 122, 0, 66726),  -- Stormlash (Dragonkin)
(66741, 2, 1012, 25, 3, 3, 706, 573, 298, 0, 66728),  -- Chirrup (Critter)

-- Bordin Steadyfist (66815) -- Deepholm, level 25
(66815, 0, 985, 25, 3, 3, 263, 621, 617, 0, 66805),  -- Ruby (Elemental)
(66815, 1, 984, 25, 3, 3, 155, 193, 519, 0, 66804),  -- Crystallus (Critter)
(66815, 2, 983, 25, 3, 3, 484, 488, 606, 0, 66802);  -- Fracture (Elemental)

-- Wild battle pet spawns seen in a retail 12.0.7 capture along the Elwynn Forest to Duskwood road.
-- Each point is the centre of the sightings of one creature within 5 yards (wild pets wander).
-- No existing spawn of the same creature is within 30 yards of any of these points.
SET @CGUID := 9100000;

DELETE FROM `creature` WHERE `guid` BETWEEN @CGUID+0 AND @CGUID+30;
INSERT INTO `creature` (`guid`, `id`, `map`, `zoneId`, `areaId`, `spawnDifficulties`, `PhaseId`, `PhaseGroup`, `modelid`, `equipment_id`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecs`, `wander_distance`, `currentwaypoint`, `MovementType`, `npcflag`, `unit_flags`, `unit_flags2`, `unit_flags3`, `VerifiedBuild`) VALUES
(@CGUID+0, 61071, 0, 0, 0, '0', 0, 0, 0, 0, -10116.1074, 372.5624, 25.3465, 2.3451, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+1, 61071, 0, 0, 0, '0', 0, 0, 0, 0, -10036.9365, 433.689, 25.0377, 2.5509, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+2, 61071, 0, 0, 0, '0', 0, 0, 0, 0, -10033.377, 407.3526, 26.7229, 4.939, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+3, 61080, 0, 0, 0, '0', 0, 0, 0, 0, -10046.7715, 160.4433, 28.0217, 4.3687, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+4, 61080, 0, 0, 0, '0', 0, 0, 0, 0, -10016.9263, 154.536, 34.2566, 2.4611, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+5, 61080, 0, 0, 0, '0', 0, 0, 0, 0, -9834.4443, 33.1531, 31.5575, 4.6835, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+6, 61080, 0, 0, 0, '0', 0, 0, 0, 0, -9534.5684, 300.4945, 53.2899, 5.7548, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+7, 61080, 0, 0, 0, '0', 0, 0, 0, 0, -9362.5791, 250.6715, 63.6632, 2.2983, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+8, 61080, 0, 0, 0, '0', 0, 0, 0, 0, -9168.568, 472.3054, 104.3422, 4.7124, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+9, 61080, 0, 0, 0, '0', 0, 0, 0, 0, -9146.6383, 420.951, 94.2842, 4.2324, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+10, 61080, 0, 0, 0, '0', 0, 0, 0, 0, -9138.3851, 356.1178, 91.718, 2.2334, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+11, 61080, 0, 0, 0, '0', 0, 0, 0, 0, -9076.6178, 380.9993, 92.562, 4.3695, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+12, 61080, 0, 0, 0, '0', 0, 0, 0, 0, -8325.0645, 467.8264, 123.4492, 4.4977, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+13, 61080, 0, 0, 0, '0', 0, 0, 0, 0, -8102.9658, 506.4588, 119.336, 0.0962, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+14, 61080, 0, 0, 0, '0', 0, 0, 0, 0, -8075.4746, 519.543, 118.5309, 0.6021, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+15, 61081, 0, 0, 0, '0', 0, 0, 0, 0, -9464.0049, 322.6101, 53.577, 3.6446, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+16, 61081, 0, 0, 0, '0', 0, 0, 0, 0, -8263.9199, 504.8769, 119.9567, 0.9904, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+17, 61081, 0, 0, 0, '0', 0, 0, 0, 0, -8080.5684, 435.959, 127.2043, 3.7843, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+18, 61143, 0, 0, 0, '0', 0, 0, 0, 0, -11150.751, 242.2912, 38.3512, 2.9867, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+19, 61143, 0, 0, 0, '0', 0, 0, 0, 0, -11010.9717, 219.958, 27.1959, 3.2658, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+20, 61165, 0, 0, 0, '0', 0, 0, 0, 0, -9950.998, -7.8566, 34.0685, 1.7614, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+21, 61165, 0, 0, 0, '0', 0, 0, 0, 0, -9745.5947, 318.1782, 44.9754, 1.604, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+22, 61165, 0, 0, 0, '0', 0, 0, 0, 0, -9242.1074, 249.0106, 71.4421, 5.4259, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+23, 61165, 0, 0, 0, '0', 0, 0, 0, 0, -9181.835, 414.1699, 89.4263, 1.2696, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+24, 61253, 0, 0, 0, '0', 0, 0, 0, 0, -10688.374, 76.593, 39.5603, 1.153, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+25, 61257, 0, 0, 0, '0', 0, 0, 0, 0, -10686.1816, 238.2191, 41.8843, 5.2407, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+26, 61257, 0, 0, 0, '0', 0, 0, 0, 0, -10345.7305, 245.12, 35.4894, 2.914, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+27, 61258, 0, 0, 0, '0', 0, 0, 0, 0, -11123.5928, 144.5677, 26.0957, 4.9171, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+28, 61258, 0, 0, 0, '0', 0, 0, 0, 0, -11108.9131, 103.072, 29.0787, 3.8786, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+29, 61258, 0, 0, 0, '0', 0, 0, 0, 0, -10974.9893, 283.4061, 28.9281, 4.1198, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974),
(@CGUID+30, 110826, 0, 0, 0, '0', 0, 0, 0, 0, -9870.5684, 92.9841, 32.3032, 5.6119, 120, 5, 0, 1, NULL, NULL, NULL, NULL, 68974);

-- Midnight battle pet creatures that were imported as non-combat companions (type 12). Their entry
-- is the CreatureID of a BattlePetSpecies row, so they are wild pets (type 14). 255832 is left out
-- because the same entry is a real NPC.
UPDATE `creature_template` SET `type` = 14 WHERE `entry` IN (
    240137,
    240994,
    241150,
    241297,
    241346,
    241416,
    242176,
    242180,
    242452,
    242652,
    244145,
    244146,
    244263,
    245043,
    245204,
    245215,
    245217,
    245474,
    245475,
    245476,
    245477,
    245479,
    245480,
    245481,
    245482,
    245494,
    245495,
    245496,
    245497,
    245498,
    245499,
    245500,
    245501,
    245502,
    245503,
    245504,
    245505,
    245506,
    245545,
    245616,
    245647,
    246660,
    246661,
    246662,
    246663,
    246696,
    246983,
    247463,
    247465,
    248495,
    249488,
    250583,
    251804,
    251817,
    251819,
    251820,
    251821,
    251822,
    251889,
    252859,
    253374,
    253399,
    254356,
    254359,
    254647,
    254689,
    254979,
    254986,
    255119,
    255257,
    255689,
    255736,
    255750,
    255921,
    256014,
    256059,
    256237,
    256265,
    256269,
    256271,
    256559,
    256560,
    256565,
    256566,
    256567,
    256663,
    256759,
    256985,
    257493,
    257616,
    257695,
    257802,
    257857,
    258281,
    258803,
    259728,
    260899
) AND `type` = 12;

-- A wild pet can only be challenged with UNIT_NPC_FLAG_WILD_BATTLE_PET. Give it to every wild pet
-- template that lacks it; a summoned companion has the flag removed in TempSummon::InitStats.
UPDATE `creature_template`
   SET `npcflag` = `npcflag` | 0x40000000
 WHERE `type` = 14
   AND (`npcflag` & 0x40000000) = 0;

-- Zarg Bonecrunch (86056) already has creature_trainer (86056, 580, 14991, 0) but no gossip menu, so
-- the trainer option never appeared. Menu 14991 is the one its own creature_trainer row names.
DELETE FROM `creature_template_gossip` WHERE `CreatureID` = 86056 AND `MenuID` = 14991;
INSERT INTO `creature_template_gossip` (`CreatureID`, `MenuID`, `VerifiedBuild`) VALUES
(86056, 14991, 0);

-- Ansel Fincap (185960) has the trainer npcflag and menu 14991 but no creature_trainer row. Same
-- values as the 22 other battle pet trainers, including 63073, the other Ansel Fincap.
DELETE FROM `creature_trainer` WHERE `CreatureID` = 185960;
INSERT INTO `creature_trainer` (`CreatureID`, `TrainerID`, `MenuID`, `OptionID`) VALUES
(185960, 580, 14991, 0);
