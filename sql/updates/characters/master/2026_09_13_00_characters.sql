-- Preserve the character's season independently of the realm's current schedule.
-- Existing characters remain standard characters. Ending a season never clears this value.
ALTER TABLE `characters` ADD COLUMN `timerunningSeasonId` int NOT NULL DEFAULT 0;
