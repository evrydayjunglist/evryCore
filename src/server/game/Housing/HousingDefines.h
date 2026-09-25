/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TRINITYCORE_HOUSING_DEFINES_H
#define TRINITYCORE_HOUSING_DEFINES_H

#include "Define.h"
#include "SharedDefines.h"

// Comments in the housing code cite retail packet captures by a short name and a line of the parsed dump, as in
// "hbcd3 1442", or by packet number, as in "hbcd3 Number 13864". The short name is the last word of the capture's file
// name. All five are from the retail 12.0.7.68887 client, recorded on 25 July 2026:
//   hbcd3      dump_12.0.7.68887_2026-07-25_10-42-53 hbcd3_parsed.txt
//   hf1        dump_12.0.7.68887_2026-07-25_11-03-11 hf1_parsed.txt
//   hbst1      dump_12.0.7.68887_2026-07-25_12-07-53 hbst1_parsed.txt
//   hled1      dump_12.0.7.68887_2026-07-25_20-46-56 hled1_parsed.txt
//   erhousing  dump_12.0.7.68887_2026-07-25_01-00-26 erhousing_parsed.txt
// Other captures are named by their whole file name where they are cited.

// HousingResult enum - 12.1.0.69587 client values (Enum.HousingResult, 112 values). 12.1 inserted AccountBanned and the
// Blueprint* results near the top, shifting every later value; the client's blueprint system itself returns 3, 7, 8,
// 11, 12, 13, 87, 95 and 99 for CodeInvalid, LocationInvalid, NameInvalid, RoomPlacementRequired, TypeInvalid,
// TypeLocationInvalid, PermissionDenied, RoomNotFound and ServiceNotAvailable.
enum HousingResult : uint8
{
    HOUSING_RESULT_SUCCESS                                   = 0,
    HOUSING_RESULT_ACCOUNT_BANNED                            = 1,
    HOUSING_RESULT_ACTION_LOCKED_BY_COMBAT                   = 2,
    HOUSING_RESULT_BLUEPRINT_CODE_INVALID                    = 3,
    HOUSING_RESULT_BLUEPRINT_DYE_FAILED                      = 4,
    HOUSING_RESULT_BLUEPRINT_GENERIC_EXPORT_ERROR            = 5,
    HOUSING_RESULT_BLUEPRINT_GENERIC_IMPORT_ERROR            = 6,
    HOUSING_RESULT_BLUEPRINT_LOCATION_INVALID                = 7,
    HOUSING_RESULT_BLUEPRINT_NAME_INVALID                    = 8,
    HOUSING_RESULT_BLUEPRINT_NOT_FOUND                       = 9,
    HOUSING_RESULT_BLUEPRINT_REQUIREMENTS_UNMET              = 10,
    HOUSING_RESULT_BLUEPRINT_ROOM_PLACEMENT_REQUIRED         = 11,
    HOUSING_RESULT_BLUEPRINT_TYPE_INVALID                    = 12,
    HOUSING_RESULT_BLUEPRINT_TYPE_LOCATION_INVALID           = 13,
    HOUSING_RESULT_BLUEPRINT_STORAGE_LIMIT                   = 14,
    HOUSING_RESULT_BLUEPRINT_VERSION_INVALID                 = 15,
    HOUSING_RESULT_BOUNDS_FAILURE_CHILDREN                   = 16,
    HOUSING_RESULT_BOUNDS_FAILURE_PLOT                       = 17,
    HOUSING_RESULT_BOUNDS_FAILURE_ROOM                       = 18,
    HOUSING_RESULT_BOUND_TO_STARTING_AREA                    = 19,
    HOUSING_RESULT_CANNOT_AFFORD                             = 20,
    HOUSING_RESULT_CHARTER_COMPLETE                          = 21,
    HOUSING_RESULT_COLLISION_INVALID                         = 22,
    HOUSING_RESULT_DB_ERROR                                  = 23,
    HOUSING_RESULT_DECOR_CANNOT_BE_REDEEMED                  = 24,
    HOUSING_RESULT_DECOR_ITEM_NOT_DESTROYABLE                = 25,
    HOUSING_RESULT_DECOR_NOT_FOUND                           = 26,
    HOUSING_RESULT_DECOR_NOT_FOUND_IN_STORAGE                = 27,
    HOUSING_RESULT_DUPLICATE_CHARTER_SIGNATURE               = 28,
    HOUSING_RESULT_FILTER_REJECTED                           = 29,
    HOUSING_RESULT_FIXTURE_CANT_DELETE_DOOR                  = 30,
    HOUSING_RESULT_FIXTURE_HOOK_EMPTY                        = 31,
    HOUSING_RESULT_FIXTURE_HOOK_OCCUPIED                     = 32,
    HOUSING_RESULT_FIXTURE_HOUSE_TYPE_MISMATCH               = 33,
    HOUSING_RESULT_FIXTURE_NOT_FOUND                         = 34,
    HOUSING_RESULT_FIXTURE_SIZE_MISMATCH                     = 35,
    HOUSING_RESULT_FIXTURE_TYPE_MISMATCH                     = 36,
    HOUSING_RESULT_GENERIC_FAILURE                           = 37,
    HOUSING_RESULT_GUILD_MORE_ACCOUNTS_NEEDED                = 38,
    HOUSING_RESULT_GUILD_MORE_ACTIVE_PLAYERS_NEEDED          = 39,
    HOUSING_RESULT_GUILD_NOT_LOADED                          = 40,
    HOUSING_RESULT_HOUSE_EDIT_LOCK_FAILED                    = 41,
    HOUSING_RESULT_HOUSE_EXTERIOR_ALREADY_THAT_SIZE          = 42,
    HOUSING_RESULT_HOUSE_EXTERIOR_ALREADY_THAT_TYPE          = 43,
    HOUSING_RESULT_HOUSE_EXTERIOR_ROOT_NOT_FOUND             = 44,
    HOUSING_RESULT_HOUSE_EXTERIOR_TYPE_NEIGHBORHOOD_MISMATCH = 45,
    HOUSING_RESULT_HOUSE_EXTERIOR_TYPE_NOT_FOUND             = 46,
    HOUSING_RESULT_HOUSE_EXTERIOR_TYPE_SIZE_MISMATCH         = 47,
    HOUSING_RESULT_HOUSE_EXTERIOR_SIZE_NOT_AVAILABLE         = 48,
    HOUSING_RESULT_HOOK_NOT_CHILD_OF_FIXTURE                 = 49,
    HOUSING_RESULT_HOUSE_NOT_FOUND                           = 50,
    HOUSING_RESULT_INCORRECT_FACTION                         = 51,
    HOUSING_RESULT_INVALID_DECOR_ITEM                        = 52,
    HOUSING_RESULT_INVALID_DISTANCE                          = 53,
    HOUSING_RESULT_INVALID_EXTERIOR_DOCUMENT                 = 54,
    HOUSING_RESULT_INVALID_GUILD                             = 55,
    HOUSING_RESULT_INVALID_HOUSE                             = 56,
    HOUSING_RESULT_INVALID_INSTANCE                          = 57,
    HOUSING_RESULT_INVALID_INTERACTION                       = 58,
    HOUSING_RESULT_INVALID_INTERIOR_DOCUMENT                 = 59,
    HOUSING_RESULT_INVALID_LIGHT_OVERLAP                     = 60,
    HOUSING_RESULT_INVALID_MAP                               = 61,
    HOUSING_RESULT_INVALID_NEIGHBORHOOD_NAME                 = 62,
    HOUSING_RESULT_INVALID_ROOM_LAYOUT                       = 63,
    HOUSING_RESULT_INSUFFICIENT_ROOM_BUDGET                  = 64,
    HOUSING_RESULT_LOCKED_BY_OTHER_PLAYER                    = 65,
    HOUSING_RESULT_LOCK_OPERATION_FAILED                     = 66,
    HOUSING_RESULT_MAX_PLACED_DECOR_REACHED                  = 67,
    HOUSING_RESULT_MAX_PET_DECOR_REACHED                     = 68,
    HOUSING_RESULT_MAX_PREVIEW_DECOR_REACHED                 = 69,
    HOUSING_RESULT_MAX_STORAGE_DECOR_REACHED                 = 70,
    HOUSING_RESULT_MISSING_CORE_FIXTURE                      = 71,
    HOUSING_RESULT_MISSING_DYE                               = 72,
    HOUSING_RESULT_MISSING_EXPANSION_ACCESS                  = 73,
    HOUSING_RESULT_MISSING_FACTION_MAP                       = 74,
    HOUSING_RESULT_MISSING_PRIVATE_NEIGHBORHOOD_INVITE       = 75,
    HOUSING_RESULT_MORE_HOUSE_SLOTS_NEEDED                   = 76,
    HOUSING_RESULT_MORE_SIGNATURES_NEEDED                    = 77,
    HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND                    = 78,
    HOUSING_RESULT_NO_NEIGHBORHOOD_OWNERSHIP_REQUESTS        = 79,
    HOUSING_RESULT_NOT_IN_DECOR_EDIT_MODE                    = 80,
    HOUSING_RESULT_NOT_IN_FIXTURE_EDIT_MODE                  = 81,
    HOUSING_RESULT_NOT_IN_LAYOUT_EDIT_MODE                   = 82,
    HOUSING_RESULT_NOT_INSIDE_HOUSE                          = 83,
    HOUSING_RESULT_NOT_ON_OWNED_PLOT                         = 84,
    HOUSING_RESULT_OPERATION_ABORTED                         = 85,
    HOUSING_RESULT_OWNER_NOT_IN_GUILD                        = 86,
    HOUSING_RESULT_PERMISSION_DENIED                         = 87,
    HOUSING_RESULT_PLACEMENT_TARGET_INVALID                  = 88,
    HOUSING_RESULT_PLAYER_NOT_FOUND                          = 89,
    HOUSING_RESULT_PLAYER_NOT_IN_INSTANCE                    = 90,
    HOUSING_RESULT_PLOT_NOT_FOUND                            = 91,
    HOUSING_RESULT_PLOT_NOT_VACANT                           = 92,
    HOUSING_RESULT_PLOT_RESERVATION_COOLDOWN                 = 93,
    HOUSING_RESULT_PLOT_RESERVED                             = 94,
    HOUSING_RESULT_ROOM_NOT_FOUND                            = 95,
    HOUSING_RESULT_ROOM_PLACEMENT_OUT_OF_BOUNDS              = 96,
    HOUSING_RESULT_ROOM_UPDATE_FAILED                        = 97,
    HOUSING_RESULT_RPC_FAILURE                               = 98,
    HOUSING_RESULT_SERVICE_NOT_AVAILABLE                     = 99,
    HOUSING_RESULT_STATIC_DATA_NOT_FOUND                     = 100,
    HOUSING_RESULT_TIMEOUT_LIMIT                             = 101,
    HOUSING_RESULT_TIMERUNNING_NOT_ALLOWED                   = 102,
    HOUSING_RESULT_TOKEN_REQUIRED                            = 103,
    HOUSING_RESULT_TOO_MANY_REQUESTS                         = 104,
    HOUSING_RESULT_TRANSACTION_FAILURE                       = 105,
    HOUSING_RESULT_UNCOLLECTED_EXTERIOR_FIXTURE              = 106,
    HOUSING_RESULT_UNCOLLECTED_HOUSE_TYPE                    = 107,
    HOUSING_RESULT_UNCOLLECTED_ROOM                          = 108,
    HOUSING_RESULT_UNCOLLECTED_ROOM_MATERIAL                 = 109,
    HOUSING_RESULT_UNCOLLECTED_ROOM_THEME                    = 110,
    HOUSING_RESULT_UNLOCK_OPERATION_FAILED                   = 111
};

// HouseEditorMode enum - 7 values
enum HousingEditorMode : uint8
{
    HOUSING_EDITOR_MODE_NONE                    = 0,
    HOUSING_EDITOR_MODE_BASIC_DECOR             = 1,
    HOUSING_EDITOR_MODE_EXPERT_DECOR            = 2,
    HOUSING_EDITOR_MODE_LAYOUT                  = 3,
    HOUSING_EDITOR_MODE_CUSTOMIZE               = 4,
    HOUSING_EDITOR_MODE_CLEANUP                 = 5,
    HOUSING_EDITOR_MODE_EXTERIOR_CUSTOMIZATION  = 6
};

// The EditorMode update field retail sent while its character was in fixture edit: 3 (hled1 817196), though the
// client's own list calls 3 Layout and 6 ExteriorCustomization. The server keeps the client's names for the mode it
// tracks and sends this value for fixture edit. Decor edit sent 1, its own value.
static constexpr uint8 HOUSING_EDITOR_MODE_FIELD_FIXTURE_EDIT = 3;

// HouseEditingContext enum - 4 values
enum HouseEditingContext : uint8
{
    HOUSE_EDITING_CONTEXT_NONE      = 0,
    HOUSE_EDITING_CONTEXT_DECOR     = 1,
    HOUSE_EDITING_CONTEXT_ROOM      = 2,
    HOUSE_EDITING_CONTEXT_FIXTURE   = 3
};

// HousingFixtureSize enum - 5 values
enum HousingFixtureSize : uint8
{
    HOUSING_FIXTURE_SIZE_NONE       = 0,
    HOUSING_FIXTURE_SIZE_ANY        = 1,
    HOUSING_FIXTURE_SIZE_SMALL      = 2,
    HOUSING_FIXTURE_SIZE_MEDIUM     = 3,
    HOUSING_FIXTURE_SIZE_LARGE      = 4
};

// HousingFixtureType enum - 9 values (sparse)
enum HousingFixtureType : uint8
{
    HOUSING_FIXTURE_TYPE_NONE           = 0,
    HOUSING_FIXTURE_TYPE_BASE           = 9,
    HOUSING_FIXTURE_TYPE_ROOF           = 10,
    HOUSING_FIXTURE_TYPE_DOOR           = 11,
    HOUSING_FIXTURE_TYPE_WINDOW         = 12,
    HOUSING_FIXTURE_TYPE_ROOF_DETAIL    = 13,
    HOUSING_FIXTURE_TYPE_ROOF_WINDOW    = 14,
    HOUSING_FIXTURE_TYPE_TOWER          = 15,
    HOUSING_FIXTURE_TYPE_CHIMNEY        = 16
};

// HousingRoomComponentType enum - 8 values
enum HousingRoomComponentType : uint8
{
    HOUSING_ROOM_COMPONENT_NONE         = 0,
    HOUSING_ROOM_COMPONENT_WALL         = 1,
    HOUSING_ROOM_COMPONENT_FLOOR        = 2,
    HOUSING_ROOM_COMPONENT_CEILING      = 3,
    HOUSING_ROOM_COMPONENT_STAIRS       = 4,
    HOUSING_ROOM_COMPONENT_PILLAR       = 5,
    HOUSING_ROOM_COMPONENT_DOORWAY_WALL = 6,
    HOUSING_ROOM_COMPONENT_DOORWAY      = 7
};

// HousingRoomComponentDoorType enum - 3 values
enum HousingRoomComponentDoorType : uint8
{
    HOUSING_ROOM_DOOR_TYPE_NONE         = 0,
    HOUSING_ROOM_DOOR_TYPE_DOORWAY      = 1,
    HOUSING_ROOM_DOOR_TYPE_THRESHOLD    = 2
};

// HousingRoomComponentCeilingType enum - 2 values
enum HousingRoomComponentCeilingType : uint8
{
    HOUSING_ROOM_CEILING_TYPE_FLAT      = 0,
    HOUSING_ROOM_CEILING_TYPE_VAULTED   = 1
};

// HousingRoomComponentStairType enum - 5 values
enum HousingRoomComponentStairType : uint8
{
    HOUSING_ROOM_STAIR_TYPE_NONE            = 0,
    HOUSING_ROOM_STAIR_TYPE_START_TO_END    = 1,
    HOUSING_ROOM_STAIR_TYPE_START_TO_MIDDLE = 2,
    HOUSING_ROOM_STAIR_TYPE_MIDDLE_TO_MIDDLE = 3,
    HOUSING_ROOM_STAIR_TYPE_MIDDLE_TO_END   = 4
};

// HousingRoomComponentOptionType enum - 3 values
enum HousingRoomComponentOptionType : uint8
{
    HOUSING_ROOM_COMPONENT_OPTION_COSMETIC      = 0,
    HOUSING_ROOM_COMPONENT_OPTION_DOORWAY_WALL  = 1,
    HOUSING_ROOM_COMPONENT_OPTION_DOORWAY       = 2
};

// How the account got a piece of decor, as its storage entry says (DecorStoragePersistedData.SourceType).
enum DecorSourceType : uint8
{
    // What a placed piece's own decor data carries, whatever its storage entry says (hbcd3 1402938, 1411644).
    DECOR_SOURCE_NONE           = 0,
    // Pre-placed in a house when it was bought (hbcd3 1431714-1431809). After a relog the value reads
    // "4-2-0-A584" (hf1 393546); what it stands for is not known, so none is written.
    DECOR_SOURCE_STARTER        = 2,
    // Owed decor turned into a piece by a redeem request; no value (hled1 789024).
    DECOR_SOURCE_REDEEMED       = 3,
    // A grant from a spell not cast by an item, with the spell id as the value. How agatho's housing code read it; none
    // of the owner's captures has one.
    DECOR_SOURCE_SPELL          = 5,
    // A grant from a spell an item cast, with the item's GUID as the value (hled1 789060-789062).
    DECOR_SOURCE_ITEM           = 6,
    // Shop licenses; the value is a number whose meaning is not known (hbcd3 352155, 352175).
    DECOR_SOURCE_SHOP_LICENSE   = 7,
    DECOR_SOURCE_SHOP_LICENSE_2 = 8,
};

// DecorStoragePersistedData.PlacementStatus is new in 12.1 and no 12.1 capture of the storage exists, so which value
// means placed and which means in storage is not known. These are the values this server sends until one is seen.
enum DecorPlacementStatus : uint8
{
    DECOR_PLACEMENT_STATUS_STORED = 0,
    DECOR_PLACEMENT_STATUS_PLACED = 1,
};

// Arg1 of every decor GUID: 1443 on all of the owner's account's decor (hbcd3 1443668; hled1 788898-809213). Another
// account's placed decor carried 1437 (hbcd3 847418), so what the number stands for is not known.
static constexpr uint32 HOUSING_DECOR_GUID_ARG1 = 1443;

// FHousingStorage_C.DecorMaxOwnedCount (hbcd3 352181, 1431809).
static constexpr uint32 HOUSING_DECOR_MAX_OWNED_COUNT = 7500;

// HouseDecor.Flags of the twelve "[DNT] ... Platform - WMO - DO NOT USE" rows with a starting quantity. They are never
// owed and never listed to the client.
static constexpr int32 HOUSE_DECOR_FLAGS_DO_NOT_USE = 192;

// HousingCatalogEntryType enum - 3 values
enum HousingCatalogEntryType : uint8
{
    HOUSING_CATALOG_ENTRY_INVALID   = 0,
    HOUSING_CATALOG_ENTRY_DECOR     = 1,
    HOUSING_CATALOG_ENTRY_ROOM      = 2
};

// HousingCatalogEntrySize enum - 6 values
enum HousingCatalogEntrySize : uint8
{
    HOUSING_CATALOG_SIZE_NONE       = 0,
    HOUSING_CATALOG_SIZE_TINY       = 65,
    HOUSING_CATALOG_SIZE_SMALL      = 66,
    HOUSING_CATALOG_SIZE_MEDIUM     = 67,
    HOUSING_CATALOG_SIZE_LARGE      = 68,
    HOUSING_CATALOG_SIZE_HUGE       = 69
};

// HousingDecorTheme enum - 6 values
enum HousingDecorTheme : uint8
{
    HOUSING_DECOR_THEME_NONE        = 0,
    HOUSING_DECOR_THEME_FOLK        = 1,
    HOUSING_DECOR_THEME_RUGGED      = 2,
    HOUSING_DECOR_THEME_GENERIC     = 3,
    HOUSING_DECOR_THEME_NIGHT_ELF   = 4,
    HOUSING_DECOR_THEME_BLOOD_ELF   = 5
};

// RoomConnectionType enum - 2 values
enum RoomConnectionType : uint8
{
    ROOM_CONNECTION_NONE    = 0,
    ROOM_CONNECTION_ALL     = 1
};

// HousingRoomFlags enum - 5 values (bitmask)
enum HousingRoomFlags : uint32
{
    HOUSING_ROOM_FLAG_NONE                  = 0x00,
    HOUSING_ROOM_FLAG_BASE_ROOM             = 0x01,
    HOUSING_ROOM_FLAG_HAS_STAIRS            = 0x02,
    HOUSING_ROOM_FLAG_UNLOCKED_BY_DEFAULT   = 0x04,
    HOUSING_ROOM_FLAG_HAS_CUSTOM_GEOMETRY   = 0x08
};

// HousingLayoutRestriction enum - 10 values
enum HousingLayoutRestriction : uint8
{
    HOUSING_LAYOUT_RESTRICTION_NONE                 = 0,
    HOUSING_LAYOUT_RESTRICTION_ROOM_NOT_FOUND       = 1,
    HOUSING_LAYOUT_RESTRICTION_NOT_INSIDE_HOUSE     = 2,
    HOUSING_LAYOUT_RESTRICTION_NOT_HOUSE_OWNER      = 3,
    HOUSING_LAYOUT_RESTRICTION_IS_BASE_ROOM         = 4,
    HOUSING_LAYOUT_RESTRICTION_ROOM_NOT_LEAF        = 5,
    HOUSING_LAYOUT_RESTRICTION_STAIRWELL_CONNECTION = 6,
    HOUSING_LAYOUT_RESTRICTION_LAST_ROOM            = 7,
    HOUSING_LAYOUT_RESTRICTION_UNREACHABLE_ROOM     = 8,
    HOUSING_LAYOUT_RESTRICTION_SINGLE_DOOR          = 9
};

// NeighborhoodInviteResult enum - 11 values (0-10), verified against client binary
enum NeighborhoodInviteResult : uint8
{
    NEIGHBORHOOD_INVITE_SUCCESS                 = 0,
    NEIGHBORHOOD_INVITE_DB_ERROR                = 1,
    NEIGHBORHOOD_INVITE_RPC_FAILURE             = 2,
    NEIGHBORHOOD_INVITE_GENERIC_FAILURE         = 3,
    NEIGHBORHOOD_INVITE_PERMISSION              = 4,
    NEIGHBORHOOD_INVITE_FACTION                 = 5,
    NEIGHBORHOOD_INVITE_PENDING_INVITATION      = 6,
    NEIGHBORHOOD_INVITE_INVITE_LIMIT            = 7,
    NEIGHBORHOOD_INVITE_NOT_ENOUGH_PLOTS        = 8,
    NEIGHBORHOOD_INVITE_NOT_FOUND               = 9,
    NEIGHBORHOOD_INVITE_TOO_MANY_REQUESTS       = 10
};

// Enum.BulkRefundResult (12.1.0.69587, BattlepayConstantsDocumentation.lua)
enum BulkRefundResult : uint8
{
    BULK_REFUND_RESULT_SUCCESS                  = 0,
    BULK_REFUND_RESULT_FAILED                   = 1,
    BULK_REFUND_RESULT_INVALID_REQUEST          = 2,
    BULK_REFUND_RESULT_REFUND_WINDOW_EXPIRED    = 3,
    BULK_REFUND_RESULT_SYSTEM_DISABLED          = 4,
    BULK_REFUND_RESULT_TIMEOUT                  = 5
};

// HouseOwnerError enum - 4 values
enum HouseOwnerError : uint8
{
    HOUSE_OWNER_ERROR_NONE                  = 0,
    HOUSE_OWNER_ERROR_FACTION               = 1,
    HOUSE_OWNER_ERROR_GUILD                 = 2,
    HOUSE_OWNER_ERROR_GENERIC_PERMISSION    = 3
};

// CreateNeighborhoodErrorType enum - 4 values
enum CreateNeighborhoodErrorType : uint8
{
    CREATE_NEIGHBORHOOD_ERROR_NONE              = 0,
    CREATE_NEIGHBORHOOD_ERROR_PROFANITY         = 1,
    CREATE_NEIGHBORHOOD_ERROR_UNDERSIZED_GUILD  = 2,
    CREATE_NEIGHBORHOOD_ERROR_OVERSIZED_GUILD   = 3
};

// NeighborhoodMemberRole enum - 3 values
enum NeighborhoodMemberRole : uint8
{
    NEIGHBORHOOD_ROLE_RESIDENT  = 0,
    NEIGHBORHOOD_ROLE_MANAGER   = 1,
    NEIGHBORHOOD_ROLE_OWNER     = 2
};

// NeighborhoodFactionRestriction enum - 3 values
enum NeighborhoodFactionRestriction : int32
{
    NEIGHBORHOOD_FACTION_NONE       = 0,
    NEIGHBORHOOD_FACTION_HORDE      = 1,
    NEIGHBORHOOD_FACTION_ALLIANCE   = 2
};

// HouseSettingFlags enum - 11 values (bitmask), verified against client binary
// Two groups: HouseAccess (bits 0-4) for interior, PlotAccess (bits 5-9) for exterior
enum HouseSettingFlags : uint32
{
    HOUSE_SETTING_NONE                      = 0x000,
    HOUSE_SETTING_HOUSE_ACCESS_ANYONE       = 0x001,
    HOUSE_SETTING_HOUSE_ACCESS_NEIGHBORS    = 0x002,
    HOUSE_SETTING_HOUSE_ACCESS_GUILD        = 0x004,
    HOUSE_SETTING_HOUSE_ACCESS_FRIENDS      = 0x008,
    HOUSE_SETTING_HOUSE_ACCESS_PARTY        = 0x010,
    HOUSE_SETTING_PLOT_ACCESS_ANYONE        = 0x020,
    HOUSE_SETTING_PLOT_ACCESS_NEIGHBORS     = 0x040,
    HOUSE_SETTING_PLOT_ACCESS_GUILD         = 0x080,
    HOUSE_SETTING_PLOT_ACCESS_FRIENDS       = 0x100,
    HOUSE_SETTING_PLOT_ACCESS_PARTY         = 0x200,
    // 12.1 (Enum.HouseSettingFlags registration 0x7FF7CE1216E0): who may export this house as a blueprint.
    HOUSE_SETTING_BLUEPRINT_EXPORT_ANYONE   = 0x400,
    HOUSE_SETTING_BLUEPRINT_EXPORT_NEIGHBORS = 0x800,
    HOUSE_SETTING_BLUEPRINT_EXPORT_GUILD    = 0x1000,
    HOUSE_SETTING_BLUEPRINT_EXPORT_FRIENDS  = 0x2000,
    HOUSE_SETTING_BLUEPRINT_EXPORT_PARTY    = 0x4000
};

constexpr uint32 HOUSE_SETTING_DEFAULT    = HOUSE_SETTING_PLOT_ACCESS_ANYONE; // 0x020 — sniff-verified default
constexpr uint32 HOUSE_SETTING_VALID_MASK = 0x7FFF; // bits 0-14

// HousingDecorPlacementFlags enum - 5 values (bitmask)
enum HousingDecorPlacementFlags : int32
{
    DECOR_PLACEMENT_FLOOR       = 0x01,
    DECOR_PLACEMENT_WALL        = 0x02,
    DECOR_PLACEMENT_CEILING     = 0x04,
    DECOR_PLACEMENT_OUTDOOR     = 0x08,
    DECOR_PLACEMENT_STACKABLE   = 0x10
};

// HousingRoomSize enum - 3 values
enum HousingRoomSize : int8
{
    ROOM_SIZE_SMALL     = 0,
    ROOM_SIZE_MEDIUM    = 1,
    ROOM_SIZE_LARGE     = 2
};

// HousingPlotSize enum - 3 values
enum HousingPlotSize : int32
{
    PLOT_SIZE_SMALL     = 0,
    PLOT_SIZE_MEDIUM    = 1,
    PLOT_SIZE_LARGE     = 2
};

// HousingInitiativeType enum - 4 values
enum HousingInitiativeType : int32
{
    INITIATIVE_TYPE_GATHERING       = 0,
    INITIATIVE_TYPE_CRAFTING        = 1,
    INITIATIVE_TYPE_COMBAT          = 2,
    INITIATIVE_TYPE_EXPLORATION     = 3
};

// HousingFixtureFlags enum - 3 values (bitmask)
enum HousingFixtureFlags : uint32
{
    HOUSING_FIXTURE_FLAG_NONE               = 0x00,
    HOUSING_FIXTURE_FLAG_IS_DEFAULT         = 0x01,
    HOUSING_FIXTURE_FLAG_UNLOCKED_BY_DEFAULT = 0x02
};

// HousingRoomComponentFlags enum - 2 values (bitmask)
enum HousingRoomComponentFlags : uint32
{
    HOUSING_ROOM_COMPONENT_FLAG_NONE                    = 0x00,
    HOUSING_ROOM_COMPONENT_FLAG_HIDDEN_IN_LAYOUT_MODE   = 0x01
};

// HousingDecorPlacementRestriction enum - 7 values (bitmask) - 12.0.7 (68275) client-verified,
// server-sent placement-failure reasons.
enum HousingDecorPlacementRestriction : uint32
{
    HOUSING_DECOR_PLACEMENT_RESTRICTION_TOO_FAR_AWAY          = 0x01,
    HOUSING_DECOR_PLACEMENT_RESTRICTION_OUTSIDE_ROOM_BOUNDS   = 0x02,
    HOUSING_DECOR_PLACEMENT_RESTRICTION_OUTSIDE_PLOT_BOUNDS   = 0x04,
    HOUSING_DECOR_PLACEMENT_RESTRICTION_CHILD_OUTSIDE_BOUNDS  = 0x08,
    HOUSING_DECOR_PLACEMENT_RESTRICTION_INVALID_TARGET        = 0x10,
    HOUSING_DECOR_PLACEMENT_RESTRICTION_INVALID_COLLISION     = 0x20,
    HOUSING_DECOR_PLACEMENT_RESTRICTION_INVALID_LIGHT_OVERLAP = 0x40
};

// HousingRoomComponentOptionFlags enum - 2 values (bitmask)
enum HousingRoomComponentOptionFlags : uint32
{
    HOUSING_ROOM_COMPONENT_OPTION_FLAG_NONE         = 0x00,
    HOUSING_ROOM_COMPONENT_OPTION_FLAG_IS_DEFAULT   = 0x01
};

// HousingRoomComponentTextureFlags enum - 2 values (bitmask)
enum HousingRoomComponentTextureFlags : uint32
{
    HOUSING_ROOM_COMPONENT_TEXTURE_FLAG_NONE                    = 0x00,
    HOUSING_ROOM_COMPONENT_TEXTURE_FLAG_UNLOCKED_BY_DEFAULT     = 0x01
};

// NeighborhoodFlags enum - 3 values (bitmask)
enum NeighborhoodFlags : uint32
{
    NEIGHBORHOOD_FLAG_NONE              = 0x00,
    NEIGHBORHOOD_FLAG_POOL_PARENT       = 0x01,
    NEIGHBORHOOD_FLAG_OPEN_TO_PUBLIC    = 0x02
};

// HouseExteriorWMODataFlags enum - 4 values (bitmask)
enum HouseExteriorWMODataFlags : uint32
{
    HOUSE_EXTERIOR_WMO_FLAG_NONE                            = 0x00,
    HOUSE_EXTERIOR_WMO_FLAG_UNLOCKED_BY_DEFAULT             = 0x01,
    HOUSE_EXTERIOR_WMO_FLAG_ALLOWED_IN_HORDE_NEIGHBORHOODS  = 0x02,
    HOUSE_EXTERIOR_WMO_FLAG_ALLOWED_IN_ALLIANCE_NEIGHBORHOODS = 0x04
};

// ============================================================================
// New enums from client binary analysis (previously missing)
// ============================================================================

// HousingDecorModelType enum - 3 values
enum HousingDecorModelType : uint8
{
    HOUSING_DECOR_MODEL_TYPE_NONE   = 0,
    HOUSING_DECOR_MODEL_TYPE_M2     = 1,
    HOUSING_DECOR_MODEL_TYPE_WMO    = 2
};

// NeighborhoodInitiativeUpdateStatus enum — sent via SMSG_INITIATIVE_UPDATE_STATUS
enum NeighborhoodInitiativeUpdateStatus : uint8
{
    NI_UPDATE_STATUS_STARTED                = 0,
    NI_UPDATE_STATUS_MILESTONE_COMPLETED    = 1,
    NI_UPDATE_STATUS_COMPLETED              = 2,
    NI_UPDATE_STATUS_FAILED                 = 3
};

// NeighborhoodInitiativeChestResult enum — sent via SMSG_INITIATIVE_CHEST_RESULT
enum NeighborhoodInitiativeChestResult : uint32
{
    NI_CHEST_SUCCESS                = 0,
    NI_CHEST_UNSPECIFIED_FAILURE    = 1,
    NI_CHEST_NO_HOUSE_FOUND        = 2,
    NI_CHEST_NO_REWARDS             = 3,
    NI_CHEST_THROTTLED              = 4,
    NI_CHEST_SERVICE_DISABLED       = 5
};

// NeighborhoodInitiativeTaskType enum — from InitiativeTask DB2 TaskType field
enum NeighborhoodInitiativeTaskType : int32
{
    NI_TASK_TYPE_SINGLE                 = 0,
    NI_TASK_TYPE_REPEATABLE_FINITE      = 1,
    NI_TASK_TYPE_REPEATABLE_INFINITE    = 2
};

// NeighborhoodInitiativeCompletionState enum — per-task completion state
enum NeighborhoodInitiativeCompletionState : uint8
{
    NI_COMPLETION_NOT_COMPLETED         = 0,
    NI_COMPLETION_PLAYER_COMPLETED      = 1,
    NI_COMPLETION_SYSTEM_ABANDONED      = 2
};

// NeighborhoodInitiativeFlags enum — from NeighborhoodInitiative DB2 Flags field
enum NeighborhoodInitiativeFlags : uint32
{
    NI_FLAG_DISABLED    = 0x1,
    NI_FLAG_NO_ABANDON  = 0x2,
    NI_FLAG_NO_REPEAT   = 0x4
};

// InitiativeMilestoneFlags enum — from InitiativeMilestone DB2 Flags field
enum InitiativeMilestoneFlags : int32
{
    INITIATIVE_MILESTONE_FLAG_FINAL = 0x1
};

// InitiativeRewardFlags enum — from InitiativeReward DB2 Flags field
enum InitiativeRewardFlags : int32
{
    INITIATIVE_REWARD_FLAG_PERMANENT_WORLD_STATE = 0x1
};

// NeighborhoodInitiativeNeighborhoodType enum
enum NeighborhoodInitiativeNeighborhoodType : uint8
{
    NI_NEIGHBORHOOD_TYPE_SINGLETON  = 0,
    NI_NEIGHBORHOOD_TYPE_POOL       = 1
};

// HousingFavorUpdateSource enum - 8 values
enum HousingFavorUpdateSource : uint8
{
    HOUSING_FAVOR_SOURCE_UNKNOWN            = 0,
    HOUSING_FAVOR_SOURCE_DECOR_COLLECTION   = 1,
    HOUSING_FAVOR_SOURCE_DEFERRED_REWARDS   = 2,
    HOUSING_FAVOR_SOURCE_RETROACTIVE_DECOR  = 3,
    HOUSING_FAVOR_SOURCE_NEW_HOUSE_DECOR    = 4,
    HOUSING_FAVOR_SOURCE_INITIATIVE_TASK    = 5,
    HOUSING_FAVOR_SOURCE_INITIATIVE_CHEST   = 6,
    HOUSING_FAVOR_SOURCE_QUEST              = 7
};

// HousingFavorUpdateType enum - 3 values
enum HousingFavorUpdateType : uint8
{
    HOUSING_FAVOR_UPDATE_NONE           = 0,
    HOUSING_FAVOR_UPDATE_INITIATIVE_ADD = 1,
    HOUSING_FAVOR_UPDATE_SET            = 2
};

// HousingPlotOwnerType enum - 4 values
enum HousingPlotOwnerType : uint8
{
    HOUSING_PLOT_OWNER_NONE     = 0,
    HOUSING_PLOT_OWNER_STRANGER = 1,
    HOUSING_PLOT_OWNER_FRIEND   = 2,
    HOUSING_PLOT_OWNER_SELF     = 3
};

// HousingTeleportReason enum - 12 values
enum HousingTeleportReason : uint8
{
    HOUSING_TELEPORT_NONE                   = 0,
    HOUSING_TELEPORT_CHEAT                  = 1,
    HOUSING_TELEPORT_UNSPECIFIED_SPELLCAST  = 2,
    HOUSING_TELEPORT_BOOTED                 = 3,
    HOUSING_TELEPORT_HOMESTONE              = 4,
    HOUSING_TELEPORT_VISIT                  = 5,
    HOUSING_TELEPORT_FRIEND                 = 6,
    HOUSING_TELEPORT_GUILD_MEMBER           = 7,
    HOUSING_TELEPORT_PARTY_MEMBER           = 8,
    HOUSING_TELEPORT_EXITING_HOUSE          = 9,
    HOUSING_TELEPORT_PORTAL                 = 10,
    HOUSING_TELEPORT_TUTORIAL               = 11
};

// HousingThrottleType enum - 2 values
enum HousingThrottleType : uint8
{
    HOUSING_THROTTLE_GENERAL    = 0,
    HOUSING_THROTTLE_DECORATION = 1
};

// HousingThemeFlags enum - 3 values (bitmask)
enum HousingThemeFlags : uint32
{
    HOUSING_THEME_FLAG_NONE                     = 0x00,
    HOUSING_THEME_FLAG_UNLOCKED_BY_DEFAULT      = 0x01,
    HOUSING_THEME_FLAG_SHOW_IN_STYLE_SELECTOR   = 0x02
};

// NeighborhoodMapFlags enum - 4 values (bitmask)
enum NeighborhoodMapFlags : uint32
{
    NEIGHBORHOOD_MAP_FLAG_NONE                  = 0x00,
    NEIGHBORHOOD_MAP_FLAG_ALLIANCE_PURCHASABLE  = 0x01,
    NEIGHBORHOOD_MAP_FLAG_HORDE_PURCHASABLE     = 0x02,
    NEIGHBORHOOD_MAP_FLAG_CAN_SYSTEM_GENERATE   = 0x04
};

// NeighborhoodOwnerType enum - 3 values
enum NeighborhoodOwnerType : uint8
{
    NEIGHBORHOOD_OWNER_NONE     = 0,
    NEIGHBORHOOD_OWNER_GUILD    = 1,
    NEIGHBORHOOD_OWNER_CHARTER  = 2
};

// NeighborhoodType enum - 3 values
enum NeighborhoodType : uint8
{
    NEIGHBORHOOD_TYPE_OPEN      = 0,
    NEIGHBORHOOD_TYPE_PRIVATE   = 1,
    NEIGHBORHOOD_TYPE_PUBLIC    = 2
};

// PurchaseHouseDisabledReason enum - 10 values
enum PurchaseHouseDisabledReason : uint8
{
    PURCHASE_HOUSE_DISABLED_NONE                = 0,
    PURCHASE_HOUSE_DISABLED_WRONG_FACTION       = 1,
    PURCHASE_HOUSE_DISABLED_WRONG_GUILD         = 2,
    PURCHASE_HOUSE_DISABLED_NOT_INVITED         = 3,
    PURCHASE_HOUSE_DISABLED_NO_EXPANSION        = 4,
    PURCHASE_HOUSE_DISABLED_RESERVED            = 5,
    PURCHASE_HOUSE_DISABLED_GUILD_LOCKOUT       = 6,
    PURCHASE_HOUSE_DISABLED_CHARTER_LOCKOUT     = 7,
    PURCHASE_HOUSE_DISABLED_MAX_HOUSES          = 8,
    PURCHASE_HOUSE_DISABLED_NO_GAME_TIME        = 9
};

// ReservationFlags enum - 4 values (bitmask)
enum ReservationFlags : uint32
{
    RESERVATION_FLAG_NONE       = 0x00,
    RESERVATION_FLAG_RELINQUISH = 0x01,
    RESERVATION_FLAG_CANCELED   = 0x02,
    RESERVATION_FLAG_PLOTLESS   = 0x04
};

// RetroactiveDecorRewardFlags enum - 2 values (bitmask)
enum RetroactiveDecorRewardFlags : uint32
{
    RETROACTIVE_DECOR_REWARD_FLAG_NONE                  = 0x00,
    RETROACTIVE_DECOR_REWARD_FLAG_ALL_CRITERIA_REQUIRED = 0x01
};

// InvalidPlotScreenshotReason enum - 5 values
enum InvalidPlotScreenshotReason : uint8
{
    INVALID_PLOT_SCREENSHOT_NONE                = 0,
    INVALID_PLOT_SCREENSHOT_OUT_OF_BOUNDS       = 1,
    INVALID_PLOT_SCREENSHOT_FACING              = 2,
    INVALID_PLOT_SCREENSHOT_NO_NEIGHBORHOOD     = 3,
    INVALID_PLOT_SCREENSHOT_NO_ACTIVE_PLAYER    = 4
};

// HouseFinderSuggestionReason enum - 7 values (bitmask)
enum HouseFinderSuggestionReason : uint32
{
    HOUSE_FINDER_SUGGESTION_NONE            = 0x00,
    HOUSE_FINDER_SUGGESTION_OWNER           = 0x01,
    HOUSE_FINDER_SUGGESTION_CHARTER_INVITE  = 0x02,
    HOUSE_FINDER_SUGGESTION_GUILD           = 0x04,
    HOUSE_FINDER_SUGGESTION_BNET_FRIENDS    = 0x08,
    HOUSE_FINDER_SUGGESTION_PARTY_SYNC      = 0x10,
    HOUSE_FINDER_SUGGESTION_RANDOM          = 0x20
};

// CornerstonePurchaseMode enum - 3 values
enum CornerstonePurchaseMode : uint8
{
    CORNERSTONE_PURCHASE_BASIC  = 0,
    CORNERSTONE_PURCHASE_IMPORT = 1,
    CORNERSTONE_PURCHASE_MOVE   = 2
};

// HouseLevelRewardType enum - 2 values
enum HouseLevelRewardType : uint8
{
    HOUSE_LEVEL_REWARD_VALUE    = 0,
    HOUSE_LEVEL_REWARD_OBJECT   = 1
};

// HouseVisitType enum - 4 values
enum HouseVisitType : uint8
{
    HOUSE_VISIT_UNKNOWN = 0,
    HOUSE_VISIT_FRIEND  = 1,
    HOUSE_VISIT_GUILD   = 2,
    HOUSE_VISIT_PARTY   = 3
};

// HousingItemToastType enum - 5 values
enum HousingItemToastType : uint8
{
    HOUSING_ITEM_TOAST_ROOM          = 0,
    HOUSING_ITEM_TOAST_FIXTURE       = 1,
    HOUSING_ITEM_TOAST_CUSTOMIZATION = 2,
    HOUSING_ITEM_TOAST_DECOR         = 3,
    HOUSING_ITEM_TOAST_HOUSE         = 4
};

// HousingRoomComponentFloorType enum - 1 value
enum HousingRoomComponentFloorType : uint8
{
    HOUSING_ROOM_COMPONENT_FLOOR_TYPE_FLOOR = 0
};

// HousingDecorType enum - 5 values
enum HousingDecorType : uint8
{
    HOUSING_DECOR_TYPE_NONE     = 0,
    HOUSING_DECOR_TYPE_FLOOR    = 1,
    HOUSING_DECOR_TYPE_WALL     = 2,
    HOUSING_DECOR_TYPE_CEILING  = 3,
    HOUSING_DECOR_TYPE_FLOORING = 4
};

// HouseLevelRewardValueType enum - 4 values
enum HouseLevelRewardValueType : uint8
{
    HOUSE_LEVEL_REWARD_EXTERIOR_DECOR   = 0,
    HOUSE_LEVEL_REWARD_INTERIOR_DECOR   = 1,
    HOUSE_LEVEL_REWARD_ROOMS            = 2,
    HOUSE_LEVEL_REWARD_FIXTURES         = 3
};

// Constants
// Decor positions are the map coordinates the client sends: neighborhood map coordinates for a piece outside
// (hled1 791438 places one at 908.2863, -567.74677 on plot 13 of Razorwind Shores) and interior map coordinates for a
// piece inside (hbcd3 1443057 places one at -979.10394, -993.3236). A piece has to stand inside the geobox of its room,
// the plot's room outside and the house's own room inside, turned and placed as that room stands on the map. The
// client checks the same boxes before it sends a placement; this margin only absorbs rounding.
static constexpr float HOUSING_DECOR_BOUNDS_MARGIN = 1.0f;
// The geobox retail sends for a plot's room (-35, -30, -1.01) to (35, 30, 125.01), used for a room whose RoomWmoData
// row is missing.
static constexpr float HOUSING_ROOM_FALLBACK_GEOBOX_MIN_X = -35.0f;
static constexpr float HOUSING_ROOM_FALLBACK_GEOBOX_MIN_Y = -30.0f;
static constexpr float HOUSING_ROOM_FALLBACK_GEOBOX_MIN_Z = -1.01f;
static constexpr float HOUSING_ROOM_FALLBACK_GEOBOX_MAX_X = 35.0f;
static constexpr float HOUSING_ROOM_FALLBACK_GEOBOX_MAX_Y = 30.0f;
static constexpr float HOUSING_ROOM_FALLBACK_GEOBOX_MAX_Z = 125.01f;
// A neighborhood's name can be changed once in this many seconds. Every rename tells its members and the players on
// its map to forget the old name, so a client sending renames without pause would otherwise make the server send
// those packets without pause. Retail's own limit is not known; this is a server guard, not a retail rule.
static constexpr uint32 HOUSING_NEIGHBORHOOD_RENAME_COOLDOWN = 60;
// Outdoor lighting (12.0.7): DecorCategory.db2 id 4 "Lighting" (subcategories
// 16-21: Large/Wall/Ceiling/Small/Misc Lights). 12.0.7 lets Lighting decor be
// placed outdoors on the plot; the placement path classifies a decor as Lighting
// through DecorXDecorSubcategory -> DecorSubcategory.DecorCategoryID.
static constexpr uint32 HOUSING_DECOR_CATEGORY_LIGHTING = 4;
// The 12.0.7 rule "two lights cannot overlap".
// The exact light-to-light overlap radius is NOT in the DB2 files or in any
// capture we hold, so it waits for a capture. This is a documented default minimum
// separation between two exterior lights, in local decor space (yards) — replace
// with the sniffed value once an outdoor-light placement capture exists.
static constexpr float HOUSING_LIGHT_OVERLAP_RADIUS = 3.0f;

// Decor edit limit per session: at most BURST place/move/remove requests per WINDOW_MS.
// Generous enough for rapid legitimate redecorating, tight enough to cap the
// AddToMap + synchronous-DB-write amplification a scripted client can drive.
static constexpr uint32 HOUSING_DECOR_THROTTLE_WINDOW_MS = 10000;
static constexpr uint32 HOUSING_DECOR_THROTTLE_BURST     = 40;
static constexpr uint32 MAX_HOUSING_DECOR_PER_ROOM      = 50;
static constexpr uint32 MAX_HOUSING_ROOMS_PER_HOUSE     = 20;
static constexpr uint32 MAX_HOUSING_FIXTURES_PER_HOUSE  = 10;
static constexpr uint32 MAX_HOUSING_DYE_SLOTS           = 3;
static constexpr uint32 MAX_NEIGHBORHOOD_PLOTS          = 55;
static constexpr uint32 MAX_NEIGHBORHOOD_MANAGERS       = 5;
static constexpr uint32 MAX_PENDING_INVITES             = 20;
// A charter needs the signatures of 10 Battle.net accounts, one per account and none from the creator's account: the
// wiki's Housing page asks for "ten separate player accounts (not characters)", and the client draws as many signature
// slots as the server says are needed. Whether the creator counts as one of the ten is not known.
static constexpr uint32 MIN_CHARTER_SIGNATURES          = 10;
static constexpr uint8  INVALID_PLOT_INDEX              = 255;
static constexpr uint32 HOUSING_MAX_NAME_LENGTH         = 64;
// Houses reach level 12 in 12.1 (Blizzard's 12.1 notes, https://news.blizzard.com/en-us/article/24293281), and
// HouseLevelData.db2 holds levels 1 to 12.
static constexpr uint32 MAX_HOUSE_LEVEL                 = 12;

// A guild neighborhood is created by the guild master of a guild with at least 10 Battle.net accounts among its
// members, 10 of them active (the client's errors GuildMoreAccountsNeeded and GuildMoreActivePlayersNeeded say "at
// least 10 unique Battle.net accounts" and "10 active Battle.net accounts"). Icy Veins counts a member as active when
// they played in the last 30 days.
static constexpr uint32 GUILD_NEIGHBORHOOD_MIN_ACCOUNTS        = 10;
static constexpr uint32 GUILD_NEIGHBORHOOD_MIN_ACTIVE_ACCOUNTS = 10;
static constexpr uint32 GUILD_NEIGHBORHOOD_ACTIVE_DAYS         = 30;

// Neighborhood initiative ("Endeavor" in the client UI) progress is reported to the client on a
// 0..1000 point scale — sniff-verified: PlayerInitiativeInfo.ProgressRequired == 1000.
// InitiativeTask.ProgressContributionAmount (12.0.7 DB2 values 10/25/50/75/100/150/300) is how
// many of those points ONE completion of that task is worth.
static constexpr float INITIATIVE_PROGRESS_REQUIRED     = 1000.0f;

// InitiativeMilestone.RequiredContributionAmount is a PERCENTAGE of initiative completion, not a
// 0..1 fraction: the 12.0.7 DB2 holds exactly 25/50/75/100 and only the 100.0 rows carry
// INITIATIVE_MILESTONE_FLAG_FINAL. ActiveInitiative::Progress is a 0..1 fraction, so it has to be
// scaled by this before being compared against a milestone threshold.
static constexpr float INITIATIVE_MILESTONE_SCALE       = 100.0f;

// Floating world-text shown when a player earns neighborhood contribution credit. Reproduced
// byte-for-byte from the retail build-68275 housing capture (housing12.0.7.pkt, four
// SMSG_DISPLAY_WORLD_TEXT records, each immediately followed by the SMSG_CRITERIA_UPDATE batch for
// the deed that earned it): null anchor guid, Arg1 = Arg2 = 0, and this exact 34-byte string. The
// colour token is resolved client-side; the "+Neighborly" wording matches the CriteriaTree strings
// "Neighborly deeds performed" / "Good Neighbor Points". Only the enUS sample exists, so this is not
// localized — retail presumably sends the client's locale here.
constexpr char const HOUSING_WORLD_TEXT_NEIGHBORLY[] = "|cnYELLOW_FONT_COLOR:+Neighborly|r";

// House Purchase Cover Spell, cast on the buyer after a purchase (hbcd3 Numbers 13866-13867). Its effects cast
// 1248306 (kill credit 248858, "Acquire a house" of quest 91863), 1253658, 1253555 and the scene 1260705.
static constexpr uint32 SPELL_HOUSE_PURCHASE_COVER      = 1253572;

// Start Tutorial: a 10 second cast that takes the character to her faction's district. The destination is effect 0's
// spell_target_position row (TELEPORT_UNITS towards TARGET_DEST_DB), and effect 1 force-casts 1262673, which keeps
// where she came from (hbcd3 352402-356147).
static constexpr uint32 SPELL_HOUSING_TELEPORT_TO_RAZORWIND_SHORES = 1258484;
static constexpr uint32 SPELL_HOUSING_TELEPORT_TO_FOUNDERS_POINT   = 1258476;

// Teleport Home: the 10 second cast retail answers CMSG_HOUSING_SVCS_TELEPORT_TO_PLOT with. Its teleport effect has
// no database destination, so the server gives the cast the plot's arrival point (hbcd3 2044258).
static constexpr uint32 SPELL_HOUSING_TELEPORT_HOME = 1233637;

// The front door's goober spell, cast by the character on herself after the door opens (hbcd3 1343025-1343064).
// Retail's client has no record of it; the world database supplies a server-side one.
static constexpr uint32 SPELL_HOUSING_ENTER_HOUSE = 1234192;

// "Exit House", the goober spell of the door inside the house (hbcd3 1456142-1456181).
static constexpr uint32 SPELL_HOUSING_EXIT_HOUSE = 1234193;

// Where a character lands inside a house interior (hbcd3 1344674, NEW_WORLD to map 2783).
static constexpr float HOUSE_INTERIOR_ARRIVAL_X = -1000.0f;
static constexpr float HOUSE_INTERIOR_ARRIVAL_Y = -1000.0f;
static constexpr float HOUSE_INTERIOR_ARRIVAL_Z = 0.1f;
static constexpr float HOUSE_INTERIOR_ARRIVAL_O = 0.0f;

// "A House For You", an auto-accept breadcrumb completed by entering either district, and "My First Home", the
// tutorial quest it leads to through RewardNextQuest.
static constexpr uint32 QUEST_HOUSING_A_HOUSE_FOR_YOU = 93057;
static constexpr uint32 QUEST_HOUSING_MY_FIRST_HOME   = 91863;

// "[DNT] Decorating - Disable All the Things - Fixture Editor": retail put it on the character for as long as she was in
// fixture edit (hled1 817057, removed at 826289). Its effects hold her in place in the air with the hover animation,
// pacify and silence her, stop her actions and clear who targets her, and it ends when she leaves the world.
static constexpr uint32 SPELL_HOUSING_FIXTURE_EDITOR_LOCKOUT = 1270200;

// Cast on her for as long as decor edit mode is on (hbcd3 1431558-1431622, and again at 1441667).
static constexpr uint32 SPELL_HOUSING_EDIT_MODE_AURA    = 1263303;

// The housing auras retail casts on a character, each for real, from her to herself: an aura update, a spell start and
// a spell go, in a slot the server picks. Here HousingMap::CastHousingAura casts them so that the start and the go go
// out as well. Names are 12.1 SpellName.
// On a plot: "[DNT] In Plot", whose linked effects bring "[DNT] Visiting Neighbor Plot" or "[DNT] In Own Plot"
// (hbcd3 1339732-1339912). The core casts a linked aura's spell as a triggered cast, so "[DNT] In Own Plot" arrives
// with its aura update only; retail sent a start and a go for it too.
static constexpr uint32 SPELL_HOUSING_IN_PLOT                 = 1239847;
static constexpr uint32 SPELL_HOUSING_VISITING_NEIGHBOR_PLOT  = 469226;
static constexpr uint32 SPELL_HOUSING_IN_OWN_PLOT             = 468939;
// On a neighborhood map, for everyone (hbcd3 421786-421956), and in a house (hbcd3 1403785).
static constexpr uint32 SPELL_HOUSING_FIXUP_AURA              = 1272741;  // "Housing Fixup Aura", neighborhood only
static constexpr uint32 SPELL_HOUSING_SOUND_SQUISHER          = 1266699;  // "[DNT] 11.2.7 Housing - Sound Squisher - Game Object - RTPC Aura"
// On a neighborhood map where her account has a house (hbcd3 1300176-1300450, 1517509-1517783).
static constexpr uint32 SPELL_HOUSING_PLAYER_ACTION_REACT     = 1263578;  // "Player Action React (DNT)"
static constexpr uint32 SPELL_HOUSING_ENDEAVOR_COVER          = 1276064;  // "[DNT] Endeavor Cover Aura"
static constexpr uint32 SPELL_HOUSING_IN_YOUR_NEIGHBORHOOD    = 1227147;  // "In Your Neighborhood"
// In a house of her own account (hbcd3 1403680-1403744).
static constexpr uint32 SPELL_HOUSING_HOMEOWNER_IS_PRESENT    = 1285424;  // "[DNT] Homeowner is Present"

// Quest that completes the housing tutorial ("Home at Last"); turning it in unlocks every editor mode.
static constexpr uint32 QUEST_HOUSING_TUTORIAL_COMPLETE = 94455; // "Home at Last"

// WS[30906]: Toggled 1 when inside a house interior (MapID=2783), 0 when leaving.
static constexpr uint32 WORLDSTATE_HOUSING_INTERIOR     = 30906;

// Every plot's cornerstone is this one shared gameobject entry; the plot it stands for is in its cornerstone data
// (PlotIndex) and in its CreatedBy. All 55 cornerstones retail created on Razorwind Shores are 457142 (hbcd3
// 439189-615537, also hf1, hled1 and erhousing), and the 12.0.1 world data (build 65940) from agatho's housing branch
// has 457142 at every plot of Founder's Point too.
static constexpr uint32 GAMEOBJECT_HOUSING_CORNERSTONE = 457142;

// A cornerstone's CreatedBy is a client actor: owner type 1, owner id the neighborhood's world map, and the plot's
// NeighborhoodPlot.CornerstoneGameObjectID as its counter. Retail's high half is 0x5000042AC0000000 on map 2736.
static constexpr uint16 HOUSING_CORNERSTONE_CREATOR_OWNER_TYPE = 1;

// ------------------------------------------------------------------
// The house exterior on a plot (hbcd3 1299598-1311080, hled1 257038-282103)
// ------------------------------------------------------------------
// The room, at the plot's anchor, carries one exterior root Entity whose local pose is the house's placement. The
// house's structural pieces hang on that root, a piece on a hook hangs on the piece that owns the hook, and the front
// door rides an Entity placed at its entry's EntryOffset.

// Counters of exterior root Entity GUIDs start here, above the counters a map hands out to other entities.
static constexpr uint64 HOUSING_EXTERIOR_ROOT_GUID_COUNTER_BASE = UI64LIT(0x100000000);

// Attachment flags retail sends: 3 on the root, the pieces and the entry-offset Entity, 7 on the door that rides it.
static constexpr uint8 HOUSING_ATTACHMENT_FLAGS_PIECE = 3;
static constexpr uint8 HOUSING_ATTACHMENT_FLAGS_DOOR  = 7;

// A saved house placement is the root's pose inside the room. The room's geobox (RoomWmoData 172) spans 35 yards each
// way along x and 30 along y; a saved pose outside it is not a placement on this plot (agatho's housing code once saved world
// coordinates there) and the house stands at the default placement instead.
static constexpr float HOUSING_ROOT_MAX_LOCAL_X = 35.0f;
static constexpr float HOUSING_ROOT_MAX_LOCAL_Y = 30.0f;
// The same geobox reaches from 1 yard below the room's anchor to 125 above it.
static constexpr float HOUSING_ROOT_MIN_LOCAL_Z = -1.0f;
static constexpr float HOUSING_ROOT_MAX_LOCAL_Z = 125.0f;

// The plot's area trigger stands this far above the room anchor, turned like the room: 31.5 yards on plots 31, 44 and
// 54 (hbcd3 815084, 820799, 1019792) and plot 1 (hled1).
static constexpr float HOUSING_PLOT_AREATRIGGER_HEIGHT = 31.5f;

// Room grid spacing for interior maps (sniff-verified: ~24 yards between room centers)
static constexpr float HOUSING_ROOM_GRID_SPACING = 24.0f;

// ------------------------------------------------------------------
// Horde House Interior Mesh Data (from retail sniff, HouseExteriorWmoDataID=87)
// ------------------------------------------------------------------
// Attachment hierarchy:
//   Root: House GO (spawned at plot position, e.g. entry 582075)
//     └── Building shell WMO (FileDataID 6322976, attached to house GO)
//           ├── Interior room WMOs (6426xxx, attached to building shell)
//           └── Exterior fixture M2s (attached to building shell)
//
// The client uses the house GO as the root anchor. MeshObjects carry
// FHousingFixture_C fragment data (ExteriorComponentID, HouseExteriorWmoDataID)
// that the client uses to resolve which art assets to render.

// Main building shell WMO (approx bounding box: ±35x30x126)
static constexpr int32 HORDE_HOUSE_BUILDING_SHELL_FDI = 6322976;

// Interior room WMOs — each approximately 24x24 unit rooms arranged on a 24-unit grid
// Vertical floor height: 7.0 units between stacked rooms
static constexpr int32 HORDE_HOUSE_INTERIOR_ROOM_FDIS[] = {
    6426613,    // Main room / wall section (also used as corner)
    6426431,    // Small room variant
    6426641,    // Small room variant
    6426647,    // Small room variant
    6426665,    // Large room corner
    6426605,    // Room ceiling
    6426671,    // Room wall with door opening
    6426452,    // Small room with specific configuration
    6426672     // Room section variant
};
static constexpr uint32 HORDE_HOUSE_INTERIOR_ROOM_COUNT = sizeof(HORDE_HOUSE_INTERIOR_ROOM_FDIS) / sizeof(HORDE_HOUSE_INTERIOR_ROOM_FDIS[0]);
static constexpr float  HORDE_HOUSE_FLOOR_HEIGHT = 7.0f;  // Vertical spacing between stacked rooms

// HouseExteriorWmoDataID for Horde theme (from sniff)
static constexpr int32 HORDE_HOUSE_EXTERIOR_WMO_DATA_ID = 87;

// Max players allowed on a housing map (exterior neighborhood + interior combined)
static constexpr uint32 MAX_HOUSING_MAP_PLAYERS = 40;

// Housing warning flags — reasons why housing features may be restricted
enum HousingWarningFlag : uint32
{
    HOUSING_WARNING_NONE                    = 0x00,
    HOUSING_WARNING_EXPANSION_REQUIRED      = 0x01, // Player needs The War Within expansion
    HOUSING_WARNING_LEVEL_TOO_LOW           = 0x02, // Player below minimum housing level
    HOUSING_WARNING_FACTION_RESTRICTED      = 0x04, // Faction-specific restriction
    HOUSING_WARNING_SERVICE_DISABLED        = 0x08, // Housing service disabled via CVar
};

// Minimum player level to access housing features
static constexpr uint32 HOUSING_MIN_PLAYER_LEVEL = 10;

// Housing needs the Midnight expansion (Wowhead's housing overview; the wiki's Housing page). Below it the client is
// only shown the housing warning.
static constexpr uint32 HOUSING_REQUIRED_EXPANSION = EXPANSION_MIDNIGHT;

// Kill credit that completes QUEST_HOUSING_TUTORIAL_COMPLETE. Packet-attested in the retail
// Horde starter capture: creature "[DNT] Kill Credit: Housing - Tutorial - 01 - House Entered"
// is credited the moment the player first stands inside their house, and the quest - which is
// AUTO_ACCEPT|AUTO_COMPLETE and has no quest-giver NPC on either end - then auto-submits.
static constexpr uint32 NPC_HOUSING_TUTORIAL_HOUSE_ENTERED_CREDIT = 257763;

// House interior instance map (MAP_HOUSE_INTERIOR = 7)
static constexpr uint32 HOUSE_INTERIOR_MAP_ID = 2783;

// Vertical spacing between stacked interior rooms. Must match the value HouseInteriorMap
// actually positions rooms with (roomZ = origin + FloorIndex * this) or anything deriving a
// room origin from FloorIndex lands on the wrong floor.
static constexpr float HOUSE_INTERIOR_FLOOR_HEIGHT = 12.0f;

// Doors inside the house, picked by faction in HouseInteriorMap. Unlike the exterior doors they are not reachable
// from ExteriorComponent (Type 11). Using one casts its goober spell, Exit House.
static constexpr uint32 INTERIOR_DOOR_GO_ALLIANCE = 575017; // displayId 113554
static constexpr uint32 INTERIOR_DOOR_GO_HORDE    = 587318;

// ============================================================================
// Housing blueprints (12.1.0.69587). Enum values are the client's (HousingBlueprintConstantsDocumentation.lua,
// PlayerHousingConstantsDocumentation.lua); limits are the Constants.HousingConsts values the client registers
// (0x7FF7CE1254A0).
// ============================================================================

enum class HousingBlueprintType : uint8
{
    None     = 0,
    House    = 1,
    Room     = 2,
    Interior = 3,
    Exterior = 4,
};

enum HousingBlueprintFlag : uint8
{
    HOUSING_BLUEPRINT_FLAG_NONE             = 0x0,
    HOUSING_BLUEPRINT_FLAG_AUTOMATIC_BACKUP = 0x1,  // client: isAutoSave = flags & 1
};

enum class HousingBlueprintContentType : uint8
{
    None      = 0,
    HouseType = 1,
    Room      = 2,
    Decor     = 3,
    Dye       = 4,
    Fixture   = 5,
    Other     = 6,
};

enum HousingBlueprintUnmetRequirementFlags : uint32
{
    HOUSING_BLUEPRINT_UNMET_NONE                        = 0x00,
    HOUSING_BLUEPRINT_UNMET_INSUFFICIENT_BUDGET         = 0x01,
    HOUSING_BLUEPRINT_UNMET_MISSING_ROOM                = 0x02,
    HOUSING_BLUEPRINT_UNMET_MISSING_FIXTURE             = 0x04,
    HOUSING_BLUEPRINT_UNMET_MISSING_DECOR               = 0x08,
    HOUSING_BLUEPRINT_UNMET_MISSING_DYE                 = 0x10,
    HOUSING_BLUEPRINT_UNMET_MISMATCHED_EXTERIOR_FACTION = 0x20,
    HOUSING_BLUEPRINT_UNMET_HOUSE_TYPE_LOCKED           = 0x40,
    HOUSING_BLUEPRINT_UNMET_HOUSE_SIZE_LOCKED           = 0x80,
    // The client derives blockingRequirementFlags as unmet & 0xE7: missing decor and dyes do not stop an import, the
    // missing pieces are left out.
    HOUSING_BLUEPRINT_UNMET_BLOCKING_MASK               = 0xE7,
};

enum class HousingBudgetType : uint8
{
    RoomPlacement  = 0,
    DecorPlacement = 1,
    PetDecor       = 2,
};

static constexpr uint32 HOUSING_BLUEPRINTS_MAX_PER_BNET_ACCOUNT         = 50;
static constexpr uint32 HOUSING_BLUEPRINTS_MAX_BACKUPS_PER_BNET_ACCOUNT = 10;
static constexpr uint32 HOUSING_BLUEPRINT_NAME_MIN_CHARACTERS           = 3;
static constexpr uint32 HOUSING_BLUEPRINT_NAME_MAX_CHARACTERS           = 50;

// Pet beds (12.1) have their own placement limit: up to 100 inside a house and 25 outside (Blizzard's 12.1 notes,
// https://news.blizzard.com/en-us/article/24293281, and https://news.blizzard.com/en-us/article/24295382).
static constexpr uint32 HOUSING_MAX_PET_BEDS_INTERIOR = 100;
static constexpr uint32 HOUSING_MAX_PET_BEDS_EXTERIOR = 25;


#endif // TRINITYCORE_HOUSING_DEFINES_H
