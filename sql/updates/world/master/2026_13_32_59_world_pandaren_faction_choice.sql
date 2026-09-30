-- 108897 "Pandaren Faction Choice" opens the Horde or Alliance choice for a neutral pandaren.
-- The client answers with CMSG_NEUTRAL_PLAYER_SELECT_FACTION.

DELETE FROM `spell_script_names` WHERE `spell_id`=108897 AND `ScriptName`='spell_pandaren_faction_choice';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(108897, 'spell_pandaren_faction_choice');
