-- Classic Store BattlePay distributions. Pending apply is consumed=1, applied=0 until login GiveLevel.

CREATE TABLE IF NOT EXISTS `battlepay_account_distribution` (
  `distributionId` bigint unsigned NOT NULL,
  `accountId` int unsigned NOT NULL,
  `productId` int unsigned NOT NULL,
  `purchaseId` bigint unsigned NOT NULL DEFAULT '0',
  `status` tinyint unsigned NOT NULL DEFAULT '1' COMMENT '1=available, 2=process, 3=complete, 4=finished',
  `consumed` tinyint unsigned NOT NULL DEFAULT '0',
  `applied` tinyint unsigned NOT NULL DEFAULT '0',
  `targetCharacter` bigint unsigned NOT NULL DEFAULT '0',
  `specId` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`distributionId`),
  KEY `idx_account` (`accountId`,`consumed`,`applied`),
  KEY `idx_target` (`targetCharacter`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
