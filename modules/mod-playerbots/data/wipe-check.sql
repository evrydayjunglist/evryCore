-- Read-only counts to run before and after a bot wipe (Playerbots.DeleteBots = 1).
-- Run with a login that can read both the auth and characters databases. After the wipe every "bot" row is 0,
-- and every "other" row is the same as before.

SET @bot := '^PLAYERBOT[0-9]+@PLAYERBOTS[.]LOCAL$';

SELECT 'bot Battle.net accounts' AS what, COUNT(*) AS n FROM auth.battlenet_accounts WHERE email REGEXP BINARY @bot
UNION ALL SELECT 'other Battle.net accounts', COUNT(*) FROM auth.battlenet_accounts WHERE NOT email REGEXP BINARY @bot
UNION ALL SELECT 'bot game accounts', COUNT(*) FROM auth.account a JOIN auth.battlenet_accounts b ON b.id = a.battlenet_account WHERE b.email REGEXP BINARY @bot
UNION ALL SELECT 'other game accounts', COUNT(*) FROM auth.account a LEFT JOIN auth.battlenet_accounts b ON b.id = a.battlenet_account WHERE b.id IS NULL OR NOT b.email REGEXP BINARY @bot
UNION ALL SELECT 'characters with no game account', COUNT(*) FROM characters.characters c LEFT JOIN auth.account a ON a.id = c.account WHERE c.account <> 0 AND a.id IS NULL
UNION ALL SELECT 'characters (all)', COUNT(*) FROM characters.characters
UNION ALL SELECT 'game accounts with no Battle.net account', COUNT(*) FROM auth.account a LEFT JOIN auth.battlenet_accounts b ON b.id = a.battlenet_account WHERE a.battlenet_account IS NOT NULL AND b.id IS NULL
UNION ALL SELECT 'realmcharacters with no game account', COUNT(*) FROM auth.realmcharacters r LEFT JOIN auth.account a ON a.id = r.acctid WHERE a.id IS NULL
UNION ALL SELECT 'item_instance with no owner', COUNT(*) FROM characters.item_instance i LEFT JOIN characters.characters c ON c.guid = i.owner_guid WHERE i.owner_guid <> 0 AND c.guid IS NULL
UNION ALL SELECT 'character_inventory with no character', COUNT(*) FROM characters.character_inventory i LEFT JOIN characters.characters c ON c.guid = i.guid WHERE c.guid IS NULL
UNION ALL SELECT 'character_queststatus with no character', COUNT(*) FROM characters.character_queststatus q LEFT JOIN characters.characters c ON c.guid = q.guid WHERE c.guid IS NULL
UNION ALL SELECT 'character_spell with no character', COUNT(*) FROM characters.character_spell s LEFT JOIN characters.characters c ON c.guid = s.guid WHERE c.guid IS NULL
UNION ALL SELECT 'mail to no character', COUNT(*) FROM characters.mail m LEFT JOIN characters.characters c ON c.guid = m.receiver WHERE c.guid IS NULL
UNION ALL SELECT 'guild_member with no character', COUNT(*) FROM characters.guild_member g LEFT JOIN characters.characters c ON c.guid = g.guid WHERE c.guid IS NULL
UNION ALL SELECT 'guilds with no leader', COUNT(*) FROM characters.guild g LEFT JOIN characters.characters c ON c.guid = g.leaderguid WHERE c.guid IS NULL
UNION ALL SELECT 'group_member with no character', COUNT(*) FROM characters.group_member g LEFT JOIN characters.characters c ON c.guid = g.memberGuid WHERE c.guid IS NULL
UNION ALL SELECT 'character_social pointing at no character', COUNT(*) FROM characters.character_social s LEFT JOIN characters.characters c ON c.guid = s.friend WHERE c.guid IS NULL
UNION ALL SELECT 'auctions whose seller is gone', COUNT(*) FROM characters.auctionhouse h LEFT JOIN characters.characters c ON c.guid = h.owner WHERE h.owner <> 0 AND c.guid IS NULL
UNION ALL SELECT 'petition_sign with no character', COUNT(*) FROM characters.petition_sign p LEFT JOIN characters.characters c ON c.guid = p.playerguid WHERE c.guid IS NULL
UNION ALL SELECT 'account_data with no game account', COUNT(*) FROM characters.account_data d LEFT JOIN auth.account a ON a.id = d.accountId WHERE a.id IS NULL
UNION ALL SELECT 'battlenet_account_toys with no Battle.net account', COUNT(*) FROM auth.battlenet_account_toys t LEFT JOIN auth.battlenet_accounts b ON b.id = t.accountId WHERE b.id IS NULL
UNION ALL SELECT 'battle_pets with no Battle.net account', COUNT(*) FROM auth.battle_pets p LEFT JOIN auth.battlenet_accounts b ON b.id = p.battlenetAccountId WHERE b.id IS NULL;
