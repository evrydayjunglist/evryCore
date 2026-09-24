#ifndef EVRY_HERO_FREE_PICK_H
#define EVRY_HERO_FREE_PICK_H

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
inline constexpr std::uint32_t RulesRevision = 1;
inline constexpr std::uint32_t StartingEssence = 8;
inline constexpr std::uint32_t AllEntries = 1023;
inline constexpr std::string_view Profile = "hero-free-pick-starter";
inline constexpr std::string_view Catalog = "hero-reference-20260921-r1:area52";
inline constexpr std::string_view Prefix = "evryCA";

struct Entry
{
    std::uint32_t Advancement;
    std::uint32_t Spell;
    std::uint32_t Cost;
    std::uint32_t Level;
    bool Reviewed;
};

// The transport bit order is fixed for V1; the database stores advancement identity.
inline constexpr std::array<Entry, 10> Entries = {{
    {2663, 100, 2, 1, false}, {2664, 116, 2, 1, true},
    {2667, 122, 2, 10, false}, {2672, 133, 2, 1, true},
    {2731, 774, 2, 1, true}, {2778, 193315, 2, 1, true},
    {2791, 1953, 3, 1, false}, {2801, 196819, 2, 1, true},
    {2830, 185358, 2, 1, true}, {2911, 8921, 2, 1, false}
}};

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
    std::uint32_t Desired = 0;
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
    if (tokens[0] != "V1" || (tokens[1] != "S" && tokens[1] != "C") ||
        !HexId(tokens[2]) || !HexId(tokens[3]))
        return {};
    Request request;
    request.Commit = tokens[1] == "C";
    request.Epoch = tokens[2];
    request.Id = tokens[3];
    if (!request.Commit)
        return count == 4 ? std::optional(request) : std::nullopt;
    if (count != 6 || !Number(tokens[4], request.Version) || !Number(tokens[5], request.Desired) ||
        (request.Desired & ~AllEntries))
        return {};
    return request;
}

struct Snapshot
{
    std::uint32_t Version = 0;
    std::uint32_t Essence = StartingEssence;
    std::uint32_t Owned = 0;
    std::array<std::uint32_t, 10> Paid = {};

    bool operator==(Snapshot const&) const = default;
};

enum class Grant { None, Dependent, Permanent };

inline Grant DesiredGrant(std::uint32_t bit, std::uint32_t owned, std::uint32_t permanent,
    std::uint32_t dependent, bool freePickActive, bool authoritative)
{
    if (permanent & bit)
        return Grant::Permanent;
    if ((dependent & bit) || (authoritative && freePickActive && (owned & bit)))
        return Grant::Dependent;
    return Grant::None;
}

inline bool Valid(Snapshot const& snapshot)
{
    if ((snapshot.Owned & ~AllEntries) || snapshot.Essence > StartingEssence)
        return false;
    std::uint32_t total = snapshot.Essence;
    for (std::size_t i = 0; i < Entries.size(); ++i)
    {
        bool owned = (snapshot.Owned & (1u << i)) != 0;
        if ((owned && !snapshot.Paid[i]) || (!owned && snapshot.Paid[i]) || snapshot.Paid[i] > StartingEssence)
            return false;
        total += snapshot.Paid[i];
    }
    return total == StartingEssence;
}

struct Change
{
    Result Code = Result::Ok;
    Snapshot State;
    std::uint32_t Added = 0;
    std::uint32_t Removed = 0;
};

inline Change Reduce(Snapshot const& current, Request const& request, std::uint32_t available,
    std::array<Entry, 10> const& catalog = Entries)
{
    Change result{Result::Ok, current};
    auto fail = [&](Result code) { result.Code = code; return result; };
    if (!Valid(current) || current.Version == std::numeric_limits<std::uint32_t>::max())
        return fail(Result::Unavailable);
    if (!request.Commit || (request.Desired & ~AllEntries))
        return fail(Result::BadRequest);
    if (request.Version != current.Version)
        return fail(Result::Stale);
    std::uint32_t added = request.Desired & ~current.Owned;
    std::uint32_t removed = current.Owned & ~request.Desired;
    if (added & ~available)
        return fail(Result::Locked);
    std::uint32_t credit = current.Essence;
    std::uint32_t debit = 0;
    Snapshot candidate = current;
    for (std::size_t i = 0; i < catalog.size(); ++i)
    {
        if (removed & (1u << i))
        {
            credit += current.Paid[i];
            candidate.Paid[i] = 0;
        }
        if (added & (1u << i))
        {
            if (!catalog[i].Reviewed || !catalog[i].Cost || catalog[i].Cost > StartingEssence)
                return fail(Result::InvalidEntry);
            debit += catalog[i].Cost;
            candidate.Paid[i] = catalog[i].Cost;
        }
    }
    if (debit > credit)
        return fail(Result::Insufficient);
    candidate.Essence = credit - debit;
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
