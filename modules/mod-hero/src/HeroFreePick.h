#ifndef EVRY_HERO_FREE_PICK_H
#define EVRY_HERO_FREE_PICK_H

#include "HeroEntryMask.h"
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace HeroFreePick
{
inline constexpr std::uint32_t Mode = 601;
inline constexpr std::uint32_t RulesRevision = 3;
inline constexpr std::uint32_t StartingEssence = 9;
inline constexpr std::uint32_t MaximumLevel = 90;
using EntryMask = evry::advancement::EntryMask;
inline constexpr std::string_view Profile = "hero-free-pick-starter";
inline constexpr std::string_view Catalog = "hero-reference-20260921-r1:area52";
inline constexpr std::string_view RetailAbilityCatalog = "retail-12.1.0.69814:abilities";
inline constexpr std::string_view TalentCatalog = "retail-12.1.0.69814:talents";
inline constexpr std::string_view MasteryCatalog = "retail-12.1.0.69814:masteries";
inline constexpr std::string_view Prefix = "evryCA";

struct Entry
{
    std::uint32_t Advancement;
    std::uint32_t Spell;
    std::uint32_t Cost;
    std::uint32_t Level;
    bool Reviewed;
    bool Talent = false;
    bool RetailAbility = false;
    std::uint32_t Mastery = 0; // One-based parent position, including the parent itself.
    bool PurchaseEnabled = true; // Acquisition policy does not revoke an existing reviewed grant.
    std::uint32_t ValidationNode = 0; // Current retail node; the persisted advancement identity stays stable.
};

#include "HeroFreePickCatalog.h"

inline std::string Hex(EntryMask bits)
{
    char value[33]; bits.write(value); return value;
}

inline constexpr std::string_view CatalogFor(Entry const& entry)
{
    return !entry.Spell ? MasteryCatalog : entry.RetailAbility ? RetailAbilityCatalog : entry.Talent ? TalentCatalog : Catalog;
}

inline std::optional<std::size_t> OwnedIndex(std::string_view catalog, std::uint32_t advancement, std::uint32_t spell)
{
    for (std::size_t i = 0; i < Entries.size(); ++i)
        if (CatalogFor(Entries[i]) == catalog && Entries[i].Advancement == advancement && Entries[i].Spell == spell)
            return i;
    return {};
}

inline bool ValidMasteries(EntryMask selected)
{
    for (std::size_t i = 0; i < Entries.size(); ++i)
    {
        Entry const& entry = Entries[i];
        if (!entry.Mastery)
            continue;
        if (entry.Spell)
        {
            if ((selected & EntryMask::Bit(i)) && !(selected & EntryMask::Bit(entry.Mastery - 1)))
                return false;
        }
        else
        {
            bool hasMember = false;
            for (std::size_t j = 0; j < Entries.size(); ++j)
                if (Entries[j].Spell && Entries[j].Mastery == i + 1 && (selected & EntryMask::Bit(j)))
                    hasMember = true;
            if (bool(selected & EntryMask::Bit(i)) != hasMember)
                return false;
        }
    }
    return true;
}

inline constexpr std::uint32_t TalentAllowance(std::uint32_t level)
{
    return level < 10 ? 0 : (level > MaximumLevel ? MaximumLevel : level) - 9;
}

inline constexpr std::uint32_t AbilityAllowance(std::uint32_t level)
{
    return StartingEssence + TalentAllowance(level);
}

enum class Result
{
    Ok, BadRequest, NotHero, WrongMode, Unavailable, Busy, Dead, Combat,
    Casting, Stale, InvalidEntry, Locked, Insufficient, IdReuse, Unknown
};

inline constexpr std::string_view Name(Result result)
{
    constexpr std::array names = {"OK", "BAD_REQUEST", "NOT_HERO", "WRONG_MODE", "UNAVAILABLE",
        "BUSY", "DEAD", "COMBAT", "CASTING", "STALE", "INVALID_ENTRY", "LOCKED",
        "INSUFFICIENT", "ID_REUSE", "UNKNOWN"};
    return names[static_cast<std::size_t>(result)];
}

struct Request
{
    bool Commit = false;
    std::string Epoch;
    std::string Id;
    std::uint32_t Version = 0;
    EntryMask Desired = 0;
};

inline bool HexId(std::string_view value)
{
    if (value.size() != 32)
        return false;
    for (char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

inline bool Number(std::string_view value, std::uint32_t& output)
{
    if (value.empty() || (value.size() > 1 && value.front() == '0'))
        return false;
    auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), output);
    return error == std::errc() && end == value.data() + value.size();
}

inline std::optional<Request> Parse(std::string_view body)
{
    if (body.empty() || body.size() > 255)
        return {};
    std::array<std::string_view, 6> tokens;
    std::size_t count = 0;
    while (true)
    {
        if (count == tokens.size())
            return {};
        auto separator = body.find(' ');
        tokens[count++] = body.substr(0, separator);
        if (tokens[count - 1].empty())
            return {};
        if (separator == std::string_view::npos)
            break;
        body.remove_prefix(separator + 1);
    }
    if (count < 4 || tokens[0] != "V3" || (tokens[1] != "S" && tokens[1] != "C") ||
        !HexId(tokens[2]) || !HexId(tokens[3]))
        return {};
    Request request;
    request.Commit = tokens[1] == "C";
    request.Epoch = tokens[2];
    request.Id = tokens[3];
    if (!request.Commit)
        return count == 4 ? std::optional(request) : std::nullopt;
    if (count != 6 || !Number(tokens[4], request.Version) || !EntryMask::Parse(tokens[5], request.Desired) ||
        (request.Desired & ~AllEntries))
        return {};
    return request;
}

struct Snapshot
{
    std::uint32_t Version = 0;
    std::uint32_t Essence = StartingEssence;
    std::uint32_t TalentEssence = 0;
    std::uint32_t EarnedLevel = 1;
    EntryMask Owned = 0;
    std::array<std::uint32_t, EntryCount> Paid = {};

    bool operator==(Snapshot const&) const = default;
};

enum class Grant { None, Dependent, Permanent };

inline Grant DesiredGrant(EntryMask bit, EntryMask owned, EntryMask permanent,
    EntryMask dependent, bool freePickActive, bool authoritative)
{
    if (permanent & bit)
        return Grant::Permanent;
    if ((dependent & bit) || (authoritative && freePickActive && (owned & bit)))
        return Grant::Dependent;
    return Grant::None;
}

inline bool Valid(Snapshot const& snapshot)
{
    if ((snapshot.Owned & ~AllEntries) || !ValidMasteries(snapshot.Owned) ||
        !snapshot.EarnedLevel || snapshot.EarnedLevel > MaximumLevel ||
        snapshot.Essence > AbilityAllowance(snapshot.EarnedLevel) ||
        snapshot.TalentEssence > TalentAllowance(snapshot.EarnedLevel))
        return false;
    std::uint32_t total = snapshot.Essence;
    std::uint32_t talentTotal = snapshot.TalentEssence;
    for (std::size_t i = 0; i < Entries.size(); ++i)
    {
        bool owned = (snapshot.Owned & EntryMask::Bit(i)) != 0;
        if ((owned && !snapshot.Paid[i]) || (!owned && snapshot.Paid[i]) || snapshot.Paid[i] > MaximumLevel)
            return false;
        (Entries[i].Talent ? talentTotal : total) += snapshot.Paid[i];
    }
    return total == AbilityAllowance(snapshot.EarnedLevel) && talentTotal == TalentAllowance(snapshot.EarnedLevel);
}

inline Snapshot EarnThrough(Snapshot current, std::uint32_t level)
{
    level = level > MaximumLevel ? MaximumLevel : level;
    if (Valid(current) && level > current.EarnedLevel && current.Version != std::numeric_limits<std::uint32_t>::max())
    {
        current.Essence += AbilityAllowance(level) - AbilityAllowance(current.EarnedLevel);
        current.TalentEssence += TalentAllowance(level) - TalentAllowance(current.EarnedLevel);
        current.EarnedLevel = level;
        ++current.Version;
    }
    return current;
}

struct Change
{
    Result Code = Result::Ok;
    Snapshot State;
    EntryMask Added = 0;
    EntryMask Removed = 0;
};

inline Change Reduce(Snapshot const& current, Request const& request, EntryMask available,
    std::array<Entry, EntryCount> const& catalog = Entries)
{
    Change result{Result::Ok, current};
    auto fail = [&](Result code) { result.Code = code; return result; };
    if (!Valid(current) || current.Version == std::numeric_limits<std::uint32_t>::max())
        return fail(Result::Unavailable);
    if (!request.Commit || (request.Desired & ~AllEntries))
        return fail(Result::BadRequest);
    if (!ValidMasteries(request.Desired))
        return fail(Result::InvalidEntry);
    if (request.Version != current.Version)
        return fail(Result::Stale);
    EntryMask added = request.Desired & ~current.Owned;
    EntryMask removed = current.Owned & ~request.Desired;
    if (added & ~available)
        return fail(Result::Locked);
    std::uint32_t credit = current.Essence;
    std::uint32_t debit = 0;
    std::uint32_t talentCredit = current.TalentEssence;
    std::uint32_t talentDebit = 0;
    Snapshot candidate = current;
    for (std::size_t i = 0; i < catalog.size(); ++i)
    {
        if (removed & EntryMask::Bit(i))
        {
            (catalog[i].Talent ? talentCredit : credit) += current.Paid[i];
            candidate.Paid[i] = 0;
        }
        if (added & EntryMask::Bit(i))
        {
            if (!catalog[i].Reviewed || !catalog[i].Cost || catalog[i].Cost > MaximumLevel ||
                catalog[i].Talent != Entries[i].Talent)
                return fail(Result::InvalidEntry);
            if (!catalog[i].PurchaseEnabled)
                return fail(Result::Locked);
            (catalog[i].Talent ? talentDebit : debit) += catalog[i].Cost;
            candidate.Paid[i] = catalog[i].Cost;
        }
    }
    if (debit > credit || talentDebit > talentCredit)
        return fail(Result::Insufficient);
    candidate.Essence = credit - debit;
    candidate.TalentEssence = talentCredit - talentDebit;
    candidate.Owned = request.Desired;
    ++candidate.Version;
    if (!Valid(candidate))
        return fail(Result::Unavailable);
    result.State = candidate;
    result.Added = added;
    result.Removed = removed;
    return result;
}

inline bool SamePayload(Request const& a, Request const& b)
{
    return a.Commit == b.Commit && a.Version == b.Version && a.Desired == b.Desired;
}
}
#endif
