CREATE TABLE IF NOT EXISTS `account_currency_transfer_log` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `accountId` int unsigned NOT NULL,
  `sourceCharacterGuid` bigint unsigned NOT NULL,
  `destinationCharacterGuid` bigint unsigned NOT NULL,
  `sourceCharacterName` varchar(12) NOT NULL,
  `fullSourceCharacterName` varchar(64) NOT NULL,
  `destinationCharacterName` varchar(12) NOT NULL,
  `fullDestinationCharacterName` varchar(64) NOT NULL,
  `currencyId` smallint unsigned NOT NULL,
  `quantityTransferred` int unsigned NOT NULL,
  `totalQuantityConsumed` int unsigned NOT NULL,
  `timestamp` bigint unsigned NOT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_account_timestamp` (`accountId`, `timestamp`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
