/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "tc_catch2.h"
#include "SystemPackets.h"

TEST_CASE("Glue feature packet preserves boost and Timerunning fields for 12.1.0", "[packets][glue]")
{
    bool const hasTicket = GENERATE(false, true);
    bool const hasBoost = GENERATE(false, true);
    CAPTURE(hasTicket, hasBoost);

    WorldPackets::System::FeatureSystemStatusGlueScreen features;
    features.BoostEnabled = hasBoost;
    features.TrialBoostEnabled = hasBoost;
    features.ActiveBoostType = hasBoost ? 11 : 0;
    features.TrialBoostType = hasBoost ? 11 : 0;
    features.MaxCharactersOnThisRealm = 60;
    features.MaximumExpansionLevel = 11;
    features.AvailableGameModeIDs.push_back(8);
    features.LaunchDurationETA = 77;
    features.RealmHiddenAlert = "Realm notice";

    if (hasTicket)
    {
        auto& ticket = features.EuropaTicketSystemStatus.emplace();
        ticket.ThrottleState.MaxTries = 10;
        ticket.ThrottleState.PerMilliseconds = 60000;
        ticket.ThrottleState.TryCount = 1;
        ticket.ThrottleState.LastResetTimeBeforeNow = 111111;
    }

    WorldPacket const* packet = features.Write();

    // These are payload offsets from the 12.1.0 wire layout, independent of Write().
    // Putting the 33-byte ticket first used to turn boost type 11 into season 2816.
    constexpr size_t flagsSize = 6;
    constexpr size_t ticketOffset = 98;
    size_t const launchOffset = ticketOffset + (hasTicket ? 33 : 0);
    size_t const gameModeOffset = launchOffset + 4 + features.RealmHiddenAlert.size() + 1;
    REQUIRE(packet->size() == gameModeOffset + 4);
    CHECK((packet->read<uint8>(1) & 0x60) == (hasBoost ? 0x60 : 0));
    CHECK((packet->read<uint8>(2) & 0x10) == (hasTicket ? 0x10 : 0));
    CHECK((packet->read<uint8>(2) & 0x02) == 0);
    CHECK(packet->read<int32>(flagsSize + 16) == 60);
    CHECK(packet->read<int32>(flagsSize + 24) == (hasBoost ? 11 : 0));
    CHECK(packet->read<int32>(flagsSize + 28) == (hasBoost ? 11 : 0));
    CHECK(packet->read<int32>(flagsSize + 36) == 11);
    CHECK(packet->read<uint32>(flagsSize + 52) == 1);
    CHECK(packet->read<int32>(flagsSize + 56) == 0);
    CHECK(packet->read<int32>(flagsSize + 60) == 0);
    CHECK(packet->read<int32>(flagsSize + 64) == 86400);
    CHECK(packet->read<int32>(flagsSize + 68) == -1);

    if (hasTicket)
    {
        CHECK(packet->read<uint8>(ticketOffset) == 0);
        CHECK(packet->read<uint32>(ticketOffset + 1) == 10);
        CHECK(packet->read<uint32>(ticketOffset + 5) == 60000);
        CHECK(packet->read<uint32>(ticketOffset + 9) == 1);
        CHECK(packet->read<uint32>(ticketOffset + 13) == 111111);
        CHECK(packet->read<uint64>(ticketOffset + 17) == 0);
        CHECK(packet->read<uint64>(ticketOffset + 25) == 0);
    }

    CHECK(packet->read<int32>(launchOffset) == 77);
    CHECK(packet->read<uint8>(gameModeOffset - 1) == 0);
    CHECK(packet->read<int32>(gameModeOffset) == 8);
}
