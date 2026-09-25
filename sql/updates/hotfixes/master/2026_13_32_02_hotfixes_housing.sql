-- Housing hotfix schemas for the selected 12.1.0 layouts.
-- Existing rows are preserved; installed DB2 files supply the initial records.

CREATE TABLE IF NOT EXISTS `data_tag_x_house_decor_record` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `DataTagID` int NOT NULL DEFAULT 0,
  `HouseDecorID` int unsigned NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `decor_category` (
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `UiTextureAtlasElementID` int NOT NULL DEFAULT 0,
  `OrderIndex` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `decor_category_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `decor_dye_slot` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `DyeColorCategoryID` int NOT NULL DEFAULT 0,
  `HouseDecorID` int unsigned NOT NULL DEFAULT 0,
  `OrderIndex` int NOT NULL DEFAULT 0,
  `Channel` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `decor_subcategory` (
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `UiTextureAtlasElementID` int NOT NULL DEFAULT 0,
  `DecorCategoryID` int unsigned NOT NULL DEFAULT 0,
  `OrderIndex` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `decor_subcategory_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `decor_x_decor_subcategory` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `HouseDecorID` int unsigned NOT NULL DEFAULT 0,
  `DecorSubcategoryID` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `dye_color` (
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `DyeColorCategoryID` int unsigned NOT NULL DEFAULT 0,
  `GradientTextureIndex` int NOT NULL DEFAULT 0,
  `ItemID` int NOT NULL DEFAULT 0,
  `SwatchColorStart` int NOT NULL DEFAULT 0,
  `SwatchColorEnd` int NOT NULL DEFAULT 0,
  `SortOrder` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `dye_color_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `dye_color_category` (
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `dye_color_category_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `exterior_component` (
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `PositionX` float NOT NULL DEFAULT 0,
  `PositionY` float NOT NULL DEFAULT 0,
  `PositionZ` float NOT NULL DEFAULT 0,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `Size` tinyint unsigned NOT NULL DEFAULT 0,
  `HouseExteriorWmoDataID` int unsigned NOT NULL DEFAULT 0,
  `ParentComponentID` int NOT NULL DEFAULT 0,
  `ModelFileDataID` int NOT NULL DEFAULT 0,
  `Flags` int NOT NULL DEFAULT 0,
  `Field_7` tinyint unsigned NOT NULL DEFAULT 0,
  `Type` tinyint unsigned NOT NULL DEFAULT 0,
  `Field_9` int NOT NULL DEFAULT 0,
  `GameObjectID` int NOT NULL DEFAULT 0,
  `Field_11` int NOT NULL DEFAULT 0,
  `ItemID` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `exterior_component_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `exterior_component_exit_point` (
  `PositionX` float NOT NULL DEFAULT 0,
  `PositionY` float NOT NULL DEFAULT 0,
  `PositionZ` float NOT NULL DEFAULT 0,
  `RotationX` float NOT NULL DEFAULT 0,
  `RotationY` float NOT NULL DEFAULT 0,
  `RotationZ` float NOT NULL DEFAULT 0,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `ExteriorComponentID` int unsigned NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `exterior_component_group` (
  `PositionX` float NOT NULL DEFAULT 0,
  `PositionY` float NOT NULL DEFAULT 0,
  `PositionZ` float NOT NULL DEFAULT 0,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `HouseExteriorWmoDataID` int unsigned NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `exterior_component_group_x_hook` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `ExteriorComponentGroupID` int unsigned NOT NULL DEFAULT 0,
  `ExteriorComponentHookID` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `exterior_component_hook` (
  `PositionX` float NOT NULL DEFAULT 0,
  `PositionY` float NOT NULL DEFAULT 0,
  `PositionZ` float NOT NULL DEFAULT 0,
  `RotationX` float NOT NULL DEFAULT 0,
  `RotationY` float NOT NULL DEFAULT 0,
  `RotationZ` float NOT NULL DEFAULT 0,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `ExteriorComponentTypeID` int NOT NULL DEFAULT 0,
  `ExteriorComponentID` int unsigned NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `exterior_component_type` (
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `ParentComponentType` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `exterior_component_type_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `exterior_component_x_group` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `ExteriorComponentGroupID` int NOT NULL DEFAULT 0,
  `ExteriorComponentID` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `InternalName` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `HouseTypeID` int NOT NULL DEFAULT 0,
  `MapID` int NOT NULL DEFAULT 0,
  `Flags` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house_decor` (
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `InitialRotationX` float NOT NULL DEFAULT 0,
  `InitialRotationY` float NOT NULL DEFAULT 0,
  `InitialRotationZ` float NOT NULL DEFAULT 0,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `GameObjectID` int NOT NULL DEFAULT 0,
  `Flags` int NOT NULL DEFAULT 0,
  `Type` tinyint unsigned NOT NULL DEFAULT 0,
  `ModelType` tinyint unsigned NOT NULL DEFAULT 0,
  `ModelFileDataID` int NOT NULL DEFAULT 0,
  `ThumbnailFileDataID` int NOT NULL DEFAULT 0,
  `WeightCost` int NOT NULL DEFAULT 0,
  `ItemID` int NOT NULL DEFAULT 0,
  `InitialScale` float NOT NULL DEFAULT 0,
  `FirstAcquisitionBonus` int NOT NULL DEFAULT 0,
  `OrderIndex` int NOT NULL DEFAULT 0,
  `Size` tinyint NOT NULL DEFAULT 0,
  `StartingQuantity` int NOT NULL DEFAULT 0,
  `UiModelSceneID` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house_decor_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house_decor_material` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `WMOMaterialReference` bigint unsigned NOT NULL DEFAULT 0,
  `MaterialTextureIndex` int NOT NULL DEFAULT 0,
  `HouseThemeID` int NOT NULL DEFAULT 0,
  `TextureAFileDataID` int NOT NULL DEFAULT 0,
  `TextureBFileDataID` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house_decor_theme_set` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `ThemeID` int NOT NULL DEFAULT 0,
  `IconFileDataID` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house_decor_theme_set_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house_exterior_wmo_data` (
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `Flags` int NOT NULL DEFAULT 0,
  `Field_003` int NOT NULL DEFAULT 0,
  `Field_004` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house_exterior_wmo_data_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house_level_data` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `Level` int NOT NULL DEFAULT 0,
  `QuestID` int NOT NULL DEFAULT 0,
  `Field_12_0_7_67808_003` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house_level_reward_info` (
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Description` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `HouseLevelDataID` int NOT NULL DEFAULT 0,
  `Field_12_0_0_63967_004` int NOT NULL DEFAULT 0,
  `IconFileDataID` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house_level_reward_info_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Description_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house_room` (
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `Size` tinyint NOT NULL DEFAULT 0,
  `Flags` int NOT NULL DEFAULT 0,
  `Field_002` int NOT NULL DEFAULT 0,
  `RoomWmoDataID` int NOT NULL DEFAULT 0,
  `UiTextureAtlasElementID` int NOT NULL DEFAULT 0,
  `WeightCost` int NOT NULL DEFAULT 0,
  `ItemID` int NOT NULL DEFAULT 0,
  `SortPriority` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house_room_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house_theme` (
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `Flags` int NOT NULL DEFAULT 0,
  `ParentThemeID` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `house_theme_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `initiative_cycle` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `RewardGroupID` int NOT NULL DEFAULT 0,
  `CycleIndex` int NOT NULL DEFAULT 0,
  `StartDay` int NOT NULL DEFAULT 0,
  `HouseXPCap` int NOT NULL DEFAULT 0,
  `InitiativeID` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `initiative_cycle_priority` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `Priority` int NOT NULL DEFAULT 0,
  `Weight` int NOT NULL DEFAULT 0,
  `Flags` int NOT NULL DEFAULT 0,
  `InitiativeCycleID` int unsigned NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `initiative_milestone` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `MilestoneOrderIndex` int NOT NULL DEFAULT 0,
  `RequiredContributionAmount` float NOT NULL DEFAULT 0,
  `Field_12_0_0_63534_003` int NOT NULL DEFAULT 0,
  `NeighborhoodInitiativeID` int unsigned NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `initiative_reward` (
  `Money` bigint NOT NULL DEFAULT 0,
  `Title` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Description` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `DecorID` int NOT NULL DEFAULT 0,
  `DecorQuantity` int NOT NULL DEFAULT 0,
  `Field_12_0_0_63534_006` int NOT NULL DEFAULT 0,
  `Favor` int NOT NULL DEFAULT 0,
  `RewardQuestID` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `initiative_reward_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Title_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Description_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `initiative_reward_x_milestone` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `InitiativeRewardID` int NOT NULL DEFAULT 0,
  `InitiativeMilestoneID` int unsigned NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `initiative_task` (
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Description` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `CriteriaTreeID` int NOT NULL DEFAULT 0,
  `QuestID` int NOT NULL DEFAULT 0,
  `ProgressContributionAmount` int NOT NULL DEFAULT 0,
  `RepetitionContributionDampeningCurve` int NOT NULL DEFAULT 0,
  `Supersedes` int NOT NULL DEFAULT 0,
  `Field_12_0_0_63534_008` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `initiative_task_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Description_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `initiative_x_task` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `InitiativeTaskID` int NOT NULL DEFAULT 0,
  `SortOrder` int NOT NULL DEFAULT 0,
  `NeighborhoodInitiativeID` int unsigned NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_initiative` (
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Description` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `InitiativeType` int NOT NULL DEFAULT 0,
  `Duration` int NOT NULL DEFAULT 0,
  `RequiredParticipants` int NOT NULL DEFAULT 0,
  `RewardCurrencyID` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_initiative_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Description_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_map` (
  `PositionX` float NOT NULL DEFAULT 0,
  `PositionY` float NOT NULL DEFAULT 0,
  `PositionZ` float NOT NULL DEFAULT 0,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `MapID` int NOT NULL DEFAULT 0,
  `EntryRotation` float NOT NULL DEFAULT 0,
  `UiTextureKitID` int unsigned NOT NULL DEFAULT 0,
  `Flags` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_name_gen` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `Prefix` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Middle` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Suffix` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `NeighborhoodMapID` int unsigned NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_name_gen_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Prefix_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Middle_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Suffix_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_plot` (
  `Cost` bigint unsigned NOT NULL DEFAULT 0,
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `HousePositionX` float NOT NULL DEFAULT 0,
  `HousePositionY` float NOT NULL DEFAULT 0,
  `HousePositionZ` float NOT NULL DEFAULT 0,
  `HouseRotationX` float NOT NULL DEFAULT 0,
  `HouseRotationY` float NOT NULL DEFAULT 0,
  `HouseRotationZ` float NOT NULL DEFAULT 0,
  `CornerstonePositionX` float NOT NULL DEFAULT 0,
  `CornerstonePositionY` float NOT NULL DEFAULT 0,
  `CornerstonePositionZ` float NOT NULL DEFAULT 0,
  `CornerstoneRotationX` float NOT NULL DEFAULT 0,
  `CornerstoneRotationY` float NOT NULL DEFAULT 0,
  `CornerstoneRotationZ` float NOT NULL DEFAULT 0,
  `TeleportPositionX` float NOT NULL DEFAULT 0,
  `TeleportPositionY` float NOT NULL DEFAULT 0,
  `TeleportPositionZ` float NOT NULL DEFAULT 0,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `NeighborhoodMapID` int unsigned NOT NULL DEFAULT 0,
  `Field_010` int NOT NULL DEFAULT 0,
  `CornerstoneGameObjectID` int NOT NULL DEFAULT 0,
  `PlotIndex` int NOT NULL DEFAULT 0,
  `WorldState` int NOT NULL DEFAULT 0,
  `PlotGameObjectID` int NOT NULL DEFAULT 0,
  `TeleportFacing` float NOT NULL DEFAULT 0,
  `Field_016` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `room_component` (
  `OffsetPosX` float NOT NULL DEFAULT 0,
  `OffsetPosY` float NOT NULL DEFAULT 0,
  `OffsetPosZ` float NOT NULL DEFAULT 0,
  `OffsetRotX` float NOT NULL DEFAULT 0,
  `OffsetRotY` float NOT NULL DEFAULT 0,
  `OffsetRotZ` float NOT NULL DEFAULT 0,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `RoomWmoDataID` int unsigned NOT NULL DEFAULT 0,
  `ModelFileDataID` int NOT NULL DEFAULT 0,
  `Type` tinyint unsigned NOT NULL DEFAULT 0,
  `MeshStyleFilterID` int NOT NULL DEFAULT 0,
  `ConnectionType` tinyint unsigned NOT NULL DEFAULT 0,
  `Flags` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `room_component_option` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `Type` tinyint unsigned NOT NULL DEFAULT 0,
  `SubType` tinyint unsigned NOT NULL DEFAULT 0,
  `ModelFileDataID` int NOT NULL DEFAULT 0,
  `RoomComponentID` int NOT NULL DEFAULT 0,
  `MeshStyleFilterID` int NOT NULL DEFAULT 0,
  `HouseThemeID` int NOT NULL DEFAULT 0,
  `Flags` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `room_component_option_texture` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `RoomComponentOptionID` int NOT NULL DEFAULT 0,
  `RoomComponentTextureID` int NOT NULL DEFAULT 0,
  `Flags` int NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `room_component_texture` (
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `ID` int unsigned NOT NULL DEFAULT 0,
  `Type` int NOT NULL DEFAULT 0,
  `FileDataID` int NOT NULL DEFAULT 0,
  `Flags` int NOT NULL DEFAULT 0,
  `UiOrder` int NOT NULL DEFAULT 0,
  `RoomComponentID` int unsigned NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `room_component_texture_locale` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `locale` varchar(4) NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `locale`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `room_wmo_data` (
  `ID` int unsigned NOT NULL DEFAULT 0,
  `BoundingBoxMinX` float NOT NULL DEFAULT 0,
  `BoundingBoxMinY` float NOT NULL DEFAULT 0,
  `BoundingBoxMinZ` float NOT NULL DEFAULT 0,
  `BoundingBoxMaxX` float NOT NULL DEFAULT 0,
  `BoundingBoxMaxY` float NOT NULL DEFAULT 0,
  `BoundingBoxMaxZ` float NOT NULL DEFAULT 0,
  `Height` float NOT NULL DEFAULT 0,
  `VerifiedBuild` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`, `VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
