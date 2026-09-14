-- QuestPOIPoint parent index is unsigned.
ALTER TABLE `quest_poi_point`
  MODIFY `QuestPOIBlobID` int unsigned NOT NULL DEFAULT '0';
