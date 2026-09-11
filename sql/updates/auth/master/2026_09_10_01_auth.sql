CREATE TABLE IF NOT EXISTS `battlenet_account_currency` (
  `battlenetAccountId` int unsigned NOT NULL,
  `Currency` smallint unsigned NOT NULL,
  `Quantity` int unsigned NOT NULL,
  `WeeklyQuantity` int unsigned NOT NULL DEFAULT '0',
  `TrackedQuantity` int unsigned NOT NULL DEFAULT '0',
  `IncreasedCapQuantity` int unsigned NOT NULL DEFAULT '0',
  `EarnedQuantity` int unsigned NOT NULL DEFAULT '0',
  `Flags` tinyint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`battlenetAccountId`, `Currency`),
  CONSTRAINT `fk_battlenet_account_currency__accountId`
    FOREIGN KEY (`battlenetAccountId`) REFERENCES `battlenet_accounts` (`id`)
    ON DELETE RESTRICT ON UPDATE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
