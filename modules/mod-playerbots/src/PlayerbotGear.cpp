/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "PlayerbotGear.h"
#include "PlayerbotClient.h"
#include "PlayerbotUpdateCost.h"
#include "Playerbots.h"
#include "Config.h"
#include "DB2Stores.h"
#include "GameTables.h"
#include "Item.h"
#include "ItemEnchantmentMgr.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Player.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include "Util.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace
{
    float UpgradeMargin = PLAYERBOT_GEAR_DEFAULT_UPGRADE_MARGIN_PCT;
    // By ChrSpecialization id. Filled once at startup and only read afterwards, from any thread a brain runs on.
    std::unordered_map<uint32, PlayerbotGearStats> WeightsBySpec;

    PlayerbotGearPrimary PrimaryFromPriority(int8 primaryStatPriority)
    {
        // The same reading as Player::GetPrimaryStat.
        if (primaryStatPriority >= 4)
            return PlayerbotGearPrimary::Strength;
        if (primaryStatPriority >= 2)
            return PlayerbotGearPrimary::Agility;
        return PlayerbotGearPrimary::Intellect;
    }

    PlayerbotGearRole RoleOf(ChrSpecializationEntry const* spec)
    {
        switch (spec->GetRole())
        {
            case ChrSpecializationRole::Tank: return PlayerbotGearRole::Tank;
            case ChrSpecializationRole::Healer: return PlayerbotGearRole::Healer;
            default: return PlayerbotGearRole::Damage;
        }
    }

    // The character sheet slots an item of this kind can go in. Only gear she wears for its stats; shirts, tabards,
    // bags, and profession gear are not upgrades.
    std::vector<uint8> GearSlotsFor(Player const* player, ItemTemplate const* proto, bool titanGrip)
    {
        switch (proto->GetInventoryType())
        {
            case INVTYPE_HEAD: return { EQUIPMENT_SLOT_HEAD };
            case INVTYPE_NECK: return { EQUIPMENT_SLOT_NECK };
            case INVTYPE_SHOULDERS: return { EQUIPMENT_SLOT_SHOULDERS };
            case INVTYPE_CHEST:
            case INVTYPE_ROBE: return { EQUIPMENT_SLOT_CHEST };
            case INVTYPE_WAIST: return { EQUIPMENT_SLOT_WAIST };
            case INVTYPE_LEGS: return { EQUIPMENT_SLOT_LEGS };
            case INVTYPE_FEET: return { EQUIPMENT_SLOT_FEET };
            case INVTYPE_WRISTS: return { EQUIPMENT_SLOT_WRISTS };
            case INVTYPE_HANDS: return { EQUIPMENT_SLOT_HANDS };
            case INVTYPE_FINGER: return { EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2 };
            case INVTYPE_TRINKET: return { EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2 };
            case INVTYPE_CLOAK: return { EQUIPMENT_SLOT_BACK };
            case INVTYPE_WEAPON:
                if (player->CanDualWield())
                    return { EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND };
                return { EQUIPMENT_SLOT_MAINHAND };
            case INVTYPE_2HWEAPON:
                if (titanGrip)
                    return { EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND };
                return { EQUIPMENT_SLOT_MAINHAND };
            case INVTYPE_WEAPONMAINHAND:
            case INVTYPE_RANGED:
            case INVTYPE_RANGEDRIGHT: return { EQUIPMENT_SLOT_MAINHAND };
            case INVTYPE_SHIELD:
            case INVTYPE_WEAPONOFFHAND:
            case INVTYPE_HOLDABLE: return { EQUIPMENT_SLOT_OFFHAND };
            default: return {};
        }
    }

    // Takes both hands, the way Player::IsTwoHandUsed reads the main hand.
    bool TakesBothHands(ItemTemplate const* proto, bool titanGrip)
    {
        switch (proto->GetInventoryType())
        {
            case INVTYPE_2HWEAPON: return !titanGrip;
            case INVTYPE_RANGED: return true;
            case INVTYPE_RANGEDRIGHT: return proto->GetClass() == ITEM_CLASS_WEAPON && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_WAND;
            default: return false;
        }
    }

    // Cloth, leather, mail, or plate on a body slot; 0 for anything else (rings, cloaks, shields, weapons).
    uint8 ArmorKindOf(ItemTemplate const* proto)
    {
        if (proto->GetClass() != ITEM_CLASS_ARMOR || proto->GetInventoryType() == INVTYPE_CLOAK)
            return 0;
        uint32 const kind = proto->GetSubClass();
        return kind >= ITEM_SUBCLASS_ARMOR_CLOTH && kind <= ITEM_SUBCLASS_ARMOR_PLATE ? uint8(kind) : 0;
    }

    uint8 ClassArmorOf(Player const* player)
    {
        ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(player->GetClass());
        return entry ? PlayerbotGearClassArmor(entry->ArmorTypeMask) : 0;
    }

    // Suits her specialization the way the game marks loot for her. Gear with no stats is marked for nobody and suits
    // everyone who can use it. Before she picks a specialization she has only her class's starting one, which item marks
    // never name, so gear marked for any specialization of her class suits her then (owner's pick, 30 September 2026).
    bool SuitsHer(Player const* player, ItemTemplate const* proto)
    {
        if (!proto->ItemSpecClassMask || proto->IsUsableByLootSpecialization(player, true))
            return true;
        if (player->GetLootSpecId())
            return false;

        uint32 specId = uint32(AsUnderlyingType(player->GetPrimarySpecialization()));
        if (!specId)
            specId = player->GetDefaultSpecId();
        ChrSpecializationEntry const* spec = sChrSpecializationStore.LookupEntry(specId);
        if (!spec || spec->OrderIndex != INITIAL_SPECIALIZATION_INDEX)
            return false;

        // The same level bands as ItemTemplate::IsUsableByLootSpecialization.
        std::size_t levelIndex = 0;
        if (player->GetLevel() >= 110)
            levelIndex = 2;
        else if (player->GetLevel() > 40)
            levelIndex = 1;

        for (uint32 i = 0; i < MAX_SPECIALIZATIONS; ++i)
        {
            if (i == INITIAL_SPECIALIZATION_INDEX)
                continue;
            if (ChrSpecializationEntry const* classSpec = sDB2Manager.GetChrSpecializationByIndex(player->GetClass(), i))
                if (proto->Specializations[levelIndex].test(ItemTemplate::CalculateItemSpecBit(classSpec)))
                    return true;
        }
        return false;
    }

    // The item level this item would have in her hands at her level, as the tooltip on her client shows it.
    uint32 TemplateItemLevelForHer(Player const* player, ItemTemplate const* proto, BonusData const& bonus)
    {
        return Item::GetItemLevel(proto, bonus, player->GetLevel(), 0, 0, 0, 0, false, 0, 0);
    }

    // One stat line as Player::_ApplyItemBonuses applies it: stamina and ratings are scaled for the slot and item level.
    void AddStat(PlayerbotGearStats& stats, int32 statType, float value, uint32 itemLevel, InventoryType inventoryType)
    {
        switch (statType)
        {
            case ITEM_MOD_STAMINA:
                if (GtStaminaMultByILvl const* mult = sStaminaMultByILvlGameTable.GetRow(itemLevel))
                    value *= GetIlvlStatMultiplier(mult, inventoryType);
                break;
            case ITEM_MOD_CRIT_RATING:
            case ITEM_MOD_HASTE_RATING:
            case ITEM_MOD_MASTERY_RATING:
            case ITEM_MOD_VERSATILITY:
            case ITEM_MOD_CR_LIFESTEAL:
            case ITEM_MOD_CR_AVOIDANCE:
            case ITEM_MOD_CR_SPEED:
                if (GtCombatRatingsMultByILvl const* mult = sCombatRatingsMultByILvlGameTable.GetRow(itemLevel))
                    value *= GetIlvlStatMultiplier(mult, inventoryType);
                break;
            default:
                break;
        }

        value = std::round(value);
        switch (statType)
        {
            case ITEM_MOD_STRENGTH: GearStat(stats, PlayerbotGearStat::Strength) += value; break;
            case ITEM_MOD_AGILITY: GearStat(stats, PlayerbotGearStat::Agility) += value; break;
            case ITEM_MOD_INTELLECT: GearStat(stats, PlayerbotGearStat::Intellect) += value; break;
            case ITEM_MOD_STAMINA: GearStat(stats, PlayerbotGearStat::Stamina) += value; break;
            case ITEM_MOD_CRIT_RATING: GearStat(stats, PlayerbotGearStat::CriticalStrike) += value; break;
            case ITEM_MOD_HASTE_RATING: GearStat(stats, PlayerbotGearStat::Haste) += value; break;
            case ITEM_MOD_MASTERY_RATING: GearStat(stats, PlayerbotGearStat::Mastery) += value; break;
            case ITEM_MOD_VERSATILITY: GearStat(stats, PlayerbotGearStat::Versatility) += value; break;
            case ITEM_MOD_CR_LIFESTEAL: GearStat(stats, PlayerbotGearStat::Leech) += value; break;
            case ITEM_MOD_CR_AVOIDANCE: GearStat(stats, PlayerbotGearStat::Avoidance) += value; break;
            case ITEM_MOD_CR_SPEED: GearStat(stats, PlayerbotGearStat::Speed) += value; break;
            case ITEM_MOD_EXTRA_ARMOR: GearStat(stats, PlayerbotGearStat::Armor) += value; break;
            // A stat line for several main stats gives her each of them; her weights say which one she wants.
            case ITEM_MOD_AGI_STR_INT:
                GearStat(stats, PlayerbotGearStat::Agility) += value;
                GearStat(stats, PlayerbotGearStat::Strength) += value;
                GearStat(stats, PlayerbotGearStat::Intellect) += value;
                break;
            case ITEM_MOD_AGI_STR:
                GearStat(stats, PlayerbotGearStat::Agility) += value;
                GearStat(stats, PlayerbotGearStat::Strength) += value;
                break;
            case ITEM_MOD_AGI_INT:
                GearStat(stats, PlayerbotGearStat::Agility) += value;
                GearStat(stats, PlayerbotGearStat::Intellect) += value;
                break;
            case ITEM_MOD_STR_INT:
                GearStat(stats, PlayerbotGearStat::Strength) += value;
                GearStat(stats, PlayerbotGearStat::Intellect) += value;
                break;
            default:
                break;
        }
    }

    // Armor and weapon damage, which the item's template gives at an item level.
    void AddTemplateValues(PlayerbotGearStats& stats, ItemTemplate const* proto, uint32 itemLevel)
    {
        GearStat(stats, PlayerbotGearStat::Armor) += float(proto->GetArmor(itemLevel));
        // GetDPS asserts on an item level the damage tables do not have; the one-hand table has every row the others do.
        if (proto->GetClass() == ITEM_CLASS_WEAPON && sItemDamageOneHandStore.LookupEntry(itemLevel))
            GearStat(stats, PlayerbotGearStat::WeaponDps) += proto->GetDPS(itemLevel);
    }

    PlayerbotGearStats StatsOfItem(Player const* player, Item const* item)
    {
        PlayerbotGearStats stats{};
        ItemTemplate const* proto = item->GetTemplate();
        uint32 const itemLevel = item->GetItemLevel(player);
        for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
        {
            int32 const statType = item->GetItemStatType(i);
            if (statType == -1)
                continue;
            AddStat(stats, statType, item->GetItemStatValue(i, player), itemLevel, proto->GetInventoryType());
        }
        AddTemplateValues(stats, proto, itemLevel);
        return stats;
    }

    PlayerbotGearStats StatsOfTemplate(Player const* player, ItemTemplate const* proto)
    {
        PlayerbotGearStats stats{};
        BonusData bonus{};
        bonus.Initialize(proto);
        uint32 const itemLevel = TemplateItemLevelForHer(player, proto, bonus);
        // The same value Item::GetItemStatValue gives an item made from this template with no bonus lists.
        float const randomPropPoints = GetRandomPropertyPoints(itemLevel, bonus.Quality, proto->GetInventoryType(), proto->GetSubClass());
        GtItemSocketCostPerLevelEntry const* socketCost = sItemSocketCostPerLevelGameTable.GetRow(itemLevel);
        for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
        {
            int32 const statType = bonus.ItemStatType[i];
            if (statType == -1 || !randomPropPoints)
                continue;
            float value = float(bonus.StatPercentEditor[i] * randomPropPoints) * 0.0001f;
            if (socketCost)
                value -= float(bonus.ItemStatSocketCostMultiplier[i] * socketCost->SocketCost);
            AddStat(stats, statType, value, itemLevel, proto->GetInventoryType());
        }
        AddTemplateValues(stats, proto, itemLevel);
        return stats;
    }

    // What she has on now, scored once for a look.
    struct WornGear
    {
        std::array<PlayerbotGearWorn, EQUIPMENT_SLOT_END> Slots = {};
        std::array<uint8, EQUIPMENT_SLOT_END> ArmorKind = {};
        std::array<Item const*, EQUIPMENT_SLOT_END> Items = {};
        bool MainIsTwoHand = false;
    };

    WornGear ReadWorn(Player const* player, PlayerbotGearStats const& weights)
    {
        WornGear worn;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item const* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item)
                continue;
            worn.Items[slot] = item;
            worn.Slots[slot] = { true, ScorePlayerbotGear(StatsOfItem(player, item), weights) };
            worn.ArmorKind[slot] = ArmorKindOf(item->GetTemplate());
        }
        worn.MainIsTwoHand = player->IsTwoHandUsed();
        return worn;
    }

    char const* SlotName(uint8 slot)
    {
        switch (slot)
        {
            case EQUIPMENT_SLOT_HEAD: return "head";
            case EQUIPMENT_SLOT_NECK: return "neck";
            case EQUIPMENT_SLOT_SHOULDERS: return "shoulders";
            case EQUIPMENT_SLOT_CHEST: return "chest";
            case EQUIPMENT_SLOT_WAIST: return "waist";
            case EQUIPMENT_SLOT_LEGS: return "legs";
            case EQUIPMENT_SLOT_FEET: return "feet";
            case EQUIPMENT_SLOT_WRISTS: return "wrists";
            case EQUIPMENT_SLOT_HANDS: return "hands";
            case EQUIPMENT_SLOT_FINGER1: return "first ring";
            case EQUIPMENT_SLOT_FINGER2: return "second ring";
            case EQUIPMENT_SLOT_TRINKET1: return "first trinket";
            case EQUIPMENT_SLOT_TRINKET2: return "second trinket";
            case EQUIPMENT_SLOT_BACK: return "back";
            case EQUIPMENT_SLOT_MAINHAND: return "main hand";
            case EQUIPMENT_SLOT_OFFHAND: return "off hand";
            default: return "slot";
        }
    }

    std::string CannotUseWhy(InventoryResult result)
    {
        switch (result)
        {
            case EQUIP_ERR_PROFICIENCY_NEEDED: return "she has not learned the weapon or armor skill it needs";
            case EQUIP_ERR_CANT_EQUIP_SKILL: return "her skill is too low for it";
            case EQUIP_ERR_CANT_EQUIP_LEVEL_I: return "she is not high enough level for it";
            case EQUIP_ERR_CANT_EQUIP_EVER: return "her class or race can never use it";
            case EQUIP_ERR_CANT_EQUIP_REPUTATION: return "she does not have the reputation it needs";
            case EQUIP_ERR_NOT_OWNER: return "it is bound to someone else";
            default: return Trinity::StringFormat("the server would not let her use it (equip error {})", uint32(result));
        }
    }

    // The slot where it gains her most, when that is better by the margin than what she has on there.
    Optional<PlayerbotGear::Upgrade> Judge(Player const* player, ItemTemplate const* proto, float score, bool titanGrip,
        WornGear const& worn, std::string* whyNot)
    {
        uint8 const classArmor = ClassArmorOf(player);
        uint8 const itemArmor = ArmorKindOf(proto);
        bool const bothHands = TakesBothHands(proto, titanGrip);
        float const margin = PlayerbotGear::UpgradeMarginPct();

        Optional<PlayerbotGear::Upgrade> best;
        std::string why = "it is not gear she wears for its stats";
        for (uint8 slot : GearSlotsFor(player, proto, titanGrip))
        {
            PlayerbotGearHand hand = PlayerbotGearHand::NotAHand;
            if (slot == EQUIPMENT_SLOT_MAINHAND)
                hand = bothHands ? PlayerbotGearHand::TwoHand : PlayerbotGearHand::MainHand;
            else if (slot == EQUIPMENT_SLOT_OFFHAND)
                hand = PlayerbotGearHand::OffHand;

            PlayerbotGearReplaced const replaced = PlayerbotGearWhatItReplaces(hand, worn.Slots[slot],
                worn.Slots[EQUIPMENT_SLOT_MAINHAND], worn.MainIsTwoHand, worn.Slots[EQUIPMENT_SLOT_OFFHAND]);
            if (!replaced.Allowed)
            {
                why = "her two-hand weapon fills both hands";
                continue;
            }

            if (!PlayerbotGearArmorKindAllows(classArmor, itemArmor, worn.ArmorKind[slot]))
            {
                why = Trinity::StringFormat("it is lighter than her class armor and would replace {} in her {} slot",
                    worn.Items[slot] ? worn.Items[slot]->GetTemplate()->GetDefaultLocaleName() : "a piece of it", SlotName(slot));
                continue;
            }

            if (score <= 0.0f)
            {
                why = "nothing on it is worth anything by her stat weights";
                continue;
            }

            bool const better = replaced.Nothing || PlayerbotGearIsUpgrade(score, replaced.Score, margin);
            if (!better)
            {
                why = Trinity::StringFormat("it scores {:.1f}, not {:.0f}% better than the {:.1f} of what she wears in her {} slot",
                    score, margin, replaced.Score, SlotName(slot));
                continue;
            }

            float const gain = PlayerbotGearGain(score, replaced.Score, replaced.Nothing);
            if (!best || gain > best->Gain)
            {
                best.emplace();
                best->Slot = slot;
                best->Score = score;
                best->Replaced = replaced.Score;
                best->ReplacesNothing = replaced.Nothing;
                best->Gain = gain;
            }
        }

        if (!best && whyNot)
            *whyNot = why;
        return best;
    }

    std::string DescribeUpgrade(PlayerbotGear::Upgrade const& upgrade)
    {
        if (upgrade.ReplacesNothing)
            return Trinity::StringFormat("her {} slot is empty; it scores {:.1f} by her stat weights", SlotName(upgrade.Slot), upgrade.Score);
        return Trinity::StringFormat("it scores {:.1f} by her stat weights against {:.1f} for what she wore in her {} slot",
            upgrade.Score, upgrade.Replaced, SlotName(upgrade.Slot));
    }
}

void PlayerbotGear::LoadConfig()
{
    UpgradeMargin = sConfigMgr->GetFloatDefault(PLAYERBOTS_GEAR_UPGRADE_MARGIN_PCT, PLAYERBOT_GEAR_DEFAULT_UPGRADE_MARGIN_PCT);
    if (!std::isfinite(UpgradeMargin) || UpgradeMargin < 0.0f || UpgradeMargin > 1000.0f)
    {
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: {} is {}; it must be between 0 and 1000. Using {}.",
            PLAYERBOTS_GEAR_UPGRADE_MARGIN_PCT, UpgradeMargin, PLAYERBOT_GEAR_DEFAULT_UPGRADE_MARGIN_PCT);
        UpgradeMargin = PLAYERBOT_GEAR_DEFAULT_UPGRADE_MARGIN_PCT;
    }

    WeightsBySpec.clear();
    for (ChrSpecializationEntry const* spec : sChrSpecializationStore)
    {
        if (spec->IsPetSpecialization())
            continue;
        if (PlayerbotGearSpecRow const* row = FindPlayerbotGearSpecRow(spec->ID))
            WeightsBySpec[spec->ID] = PlayerbotGearRowWeights(*row);
        else
            WeightsBySpec[spec->ID] = PlayerbotGearArchetypeWeights(PrimaryFromPriority(spec->PrimaryStatPriority), RoleOf(spec));
    }

    uint32 changed = 0;
    std::string const prefix = PLAYERBOTS_GEAR_WEIGHTS_PREFIX;
    for (std::string const& key : sConfigMgr->GetKeysByString(prefix))
    {
        std::string_view const idText = std::string_view(key).substr(prefix.size());
        uint32 specId = 0;
        auto const [ptr, ec] = std::from_chars(idText.data(), idText.data() + idText.size(), specId);
        auto itr = WeightsBySpec.end();
        if (ec == std::errc() && ptr == idText.data() + idText.size())
            itr = WeightsBySpec.find(specId);
        if (itr == WeightsBySpec.end())
        {
            TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: {} does not name a specialization id; it is not used.", key);
            continue;
        }

        std::string const text = sConfigMgr->GetStringDefault(key, "");
        std::string error;
        if (!ParsePlayerbotGearWeights(text, itr->second, error))
        {
            TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: {} cannot be read ({}). That specialization keeps the module's weights.", key, error);
            continue;
        }

        ++changed;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: gear weights for specialization {} from the conf: {}.",
            specId, DescribePlayerbotGearWeights(itr->second));
    }

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: bots judge gear by stat weights for {} specializations ({} changed by the conf) and change a piece only for one at least {}% better.",
        WeightsBySpec.size(), changed, UpgradeMargin);
}

float PlayerbotGear::UpgradeMarginPct()
{
    return UpgradeMargin;
}

PlayerbotGearStats const& PlayerbotGear::WeightsFor(Player const* player)
{
    uint32 spec = uint32(AsUnderlyingType(player->GetPrimarySpecialization()));
    if (!spec)
        spec = player->GetDefaultSpecId();

    auto itr = WeightsBySpec.find(spec);
    if (itr != WeightsBySpec.end())
        return itr->second;

    // No row for her specialization (or none loaded): her main stat as the server reads it.
    static PlayerbotGearStats const strength = PlayerbotGearArchetypeWeights(PlayerbotGearPrimary::Strength, PlayerbotGearRole::Damage);
    static PlayerbotGearStats const agility = PlayerbotGearArchetypeWeights(PlayerbotGearPrimary::Agility, PlayerbotGearRole::Damage);
    static PlayerbotGearStats const intellect = PlayerbotGearArchetypeWeights(PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Damage);
    switch (player->GetPrimaryStat())
    {
        case STAT_STRENGTH: return strength;
        case STAT_AGILITY: return agility;
        default: return intellect;
    }
}

bool PlayerbotGear::CanUseTemplate(Player const* player, ItemTemplate const* proto)
{
    if (player->CanUseItem(proto) != EQUIP_ERR_OK)
        return false;
    // The template check does not look at weapon and armor skills; the check on a real item does.
    if (uint32 const skill = proto->GetSkill())
        if (!player->GetSkillValue(skill))
            return false;
    return true;
}

Optional<PlayerbotGear::Upgrade> PlayerbotGear::UpgradeFromTemplate(Player const* player, ItemTemplate const* proto)
{
    if (!player || !proto || !CanUseTemplate(player, proto) || !SuitsHer(player, proto))
        return {};

    PlayerbotGearStats const& weights = WeightsFor(player);
    WornGear const worn = ReadWorn(player, weights);
    return Judge(player, proto, ScorePlayerbotGear(StatsOfTemplate(player, proto), weights), false, worn, nullptr);
}

Optional<PlayerbotGear::Upgrade> PlayerbotGear::UpgradeFromBags(Player const* player, Item const* item, std::string* whyNot)
{
    if (!player || !item)
        return {};

    ItemTemplate const* proto = item->GetTemplate();
    InventoryResult const use = player->CanUseItem(const_cast<Item*>(item));
    if (use != EQUIP_ERR_OK)
    {
        if (whyNot)
            *whyNot = CannotUseWhy(use);
        return {};
    }
    if (!SuitsHer(player, proto))
    {
        if (whyNot)
            *whyNot = "the game does not mark it for her specialization";
        return {};
    }

    PlayerbotGearStats const& weights = WeightsFor(player);
    WornGear const worn = ReadWorn(player, weights);
    return Judge(player, proto, ScorePlayerbotGear(StatsOfItem(player, item), weights), player->CanTitanGrip(item), worn, whyNot);
}

PlayerbotGear::Look PlayerbotGear::TryWearBestFromBags(Player* player, std::unordered_set<ObjectGuid>& judged,
    std::unordered_set<ObjectGuid>& refused, ObjectGuid& lastQueued)
{
    PlayerbotCostTimer const cost(PlayerbotCostStep::Gear);
    if (!player || !player->IsInWorld() || !player->GetSession())
        return Look::Nothing;

    PlayerbotGearStats const& weights = WeightsFor(player);
    WornGear const worn = ReadWorn(player, weights);

    struct Candidate
    {
        Item* It;
        Upgrade Up;
    };
    std::vector<Candidate> candidates;
    ObjectGuid const queuedBefore = lastQueued;
    lastQueued.Clear();

    player->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || (proto->GetClass() != ITEM_CLASS_WEAPON && proto->GetClass() != ITEM_CLASS_ARMOR))
            return ItemSearchCallbackResult::Continue;
        bool const titanGrip = player->CanTitanGrip(item);
        if (GearSlotsFor(player, proto, titanGrip).empty())
            return ItemSearchCallbackResult::Continue;

        ObjectGuid const guid = item->GetGUID();
        if (refused.contains(guid))
            return ItemSearchCallbackResult::Continue;
        if (guid == queuedBefore)
        {
            refused.insert(guid);
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} asked to put on {} ({}), but it is still in her bags. She does not try it again at this level.",
                player->GetName(), proto->GetDefaultLocaleName(), proto->GetId());
            return ItemSearchCallbackResult::Continue;
        }

        InventoryResult const use = player->CanUseItem(item);
        std::string why;
        Optional<Upgrade> upgrade;
        if (use != EQUIP_ERR_OK)
            why = CannotUseWhy(use);
        else if (!SuitsHer(player, proto))
            why = "the game does not mark it for her specialization";
        else
            upgrade = Judge(player, proto, ScorePlayerbotGear(StatsOfItem(player, item), weights), titanGrip, worn, &why);

        if (upgrade)
            candidates.push_back({ item, *upgrade });
        else if (judged.insert(guid).second)
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} keeps {} ({}) in her bags: {}.",
                player->GetName(), proto->GetDefaultLocaleName(), proto->GetId(), why);
        return ItemSearchCallbackResult::Continue;
    });

    std::stable_sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b)
    {
        return a.Up.Gain > b.Up.Gain;
    });

    for (Candidate const& candidate : candidates)
    {
        ItemTemplate const* proto = candidate.It->GetTemplate();
        uint16 dest = 0;
        InventoryResult const result = player->CanEquipItem(candidate.Up.Slot, dest, candidate.It, true);
        if (result != EQUIP_ERR_OK)
        {
            refused.insert(candidate.It->GetGUID());
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} would put on {} ({}), but the server would refuse it in her {} slot (equip error {}). She keeps it in her bags.",
                player->GetName(), proto->GetDefaultLocaleName(), proto->GetId(), SlotName(candidate.Up.Slot), uint32(result));
            continue;
        }

        Item const* old = worn.Items[candidate.Up.Slot];
        PlayerbotClient::QueueAutoEquipItemSlot(player->GetSession(), candidate.It, candidate.Up.Slot);
        lastQueued = candidate.It->GetGUID();
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_AUTO_EQUIP_ITEM_SLOT to put on {} ({}) in place of {}: {}.",
            player->GetName(), proto->GetDefaultLocaleName(), proto->GetId(),
            old ? old->GetTemplate()->GetDefaultLocaleName() : "nothing", DescribeUpgrade(candidate.Up));
        return Look::Queued;
    }

    return Look::Nothing;
}
