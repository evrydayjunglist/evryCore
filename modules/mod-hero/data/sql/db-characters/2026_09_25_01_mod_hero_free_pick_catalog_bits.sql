-- Append-only catalog expansion; existing ownership rows and paid prices are untouched.
-- Keep the old low-word field so old receipts can be read without rewriting history.
ALTER TABLE `character_hero_freepick_request`
  DROP CHECK `hero_freepick_mask`,
  ADD COLUMN `desired_bits` CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NULL,
  ADD CONSTRAINT `hero_freepick_bits` CHECK (`desired_bits` IS NULL OR
    (CHAR_LENGTH(`desired_bits`)=32 AND REGEXP_LIKE(`desired_bits`, '^[0-9a-f]{32}$', 'c')));
