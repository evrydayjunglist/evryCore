#include "HeroFreePick.h"
#include "HeroFreePickQueue.h"
#include "HeroFreePickSql.h"
#include "AsyncCallbackProcessor.h"
#include "ChatPackets.h"
#include "CryptoRandom.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace
{
using namespace HeroFreePick;

struct State
{
    ObjectGuid Guid;
    uint64 Generation = 0;
    Snapshot Wallet;
    uint32 Independent = 0;
    uint32 Dependent = 0;
    uint32 InitialPermanent = 0;
    uint32 Touched = 0;
    std::array<uint64, 10> SourceRevision = {};
    uint64 SourceEpoch = 0;
    std::vector<std::string> SessionTokens;
    bool HasSnapshot = false;
    bool Ready = false;
    bool Busy = false;
    bool Internal = false;
};

std::mutex StatesMutex;
std::unordered_map<ObjectGuid, std::shared_ptr<State>> States;
std::unordered_map<ObjectGuid, std::shared_ptr<State>> RetainedSources;
std::atomic<uint64> NextGeneration = 1;
OrderedOperations<ObjectGuid> Operations;
using Done = OrderedOperations<ObjectGuid>::Done;
AsyncCallbackProcessor<TransactionCallback> Transactions;
QueryCallbackProcessor Queries;

std::shared_ptr<State> Find(ObjectGuid guid)
{
    std::lock_guard lock(StatesMutex);
    auto it = States.find(guid);
    return it == States.end() ? nullptr : it->second;
}

std::shared_ptr<State> Create(Player* player)
{
    std::lock_guard lock(StatesMutex);
    auto& value = States[player->GetGUID()];
    if (!value)
    {
        value = std::make_shared<State>();
        value->Guid = player->GetGUID();
        value->Generation = NextGeneration++;
        if (auto retained = RetainedSources.find(value->Guid); retained != RetainedSources.end())
        {
            if (!retained->second->SourceEpoch)
                value->SessionTokens = retained->second->SessionTokens;
            value->Touched = retained->second->Touched;
            value->Independent = retained->second->Independent & value->Touched;
            for (std::size_t i = 0; i < Entries.size(); ++i)
                if (value->Touched & (1u << i))
                    value->SourceRevision[i] = 1;
            RetainedSources.erase(retained);
        }
        std::string token;
        for (uint8 byte : Trinity::Crypto::GetRandomBytes<16>())
        {
            token += "0123456789abcdef"[byte >> 4];
            token += "0123456789abcdef"[byte & 15];
        }
        value->SessionTokens.push_back(std::move(token));
        // Module grants have not been reconstructed at login. Existing dependent
        // spells here belong to the core's normal skill, talent, or other providers.
        for (std::size_t i = 0; i < Entries.size(); ++i)
            if (auto spell = player->GetSpellMap().find(Entries[i].Spell); spell != player->GetSpellMap().end() &&
                spell->second.state != PLAYERSPELL_REMOVED && spell->second.state != PLAYERSPELL_TEMPORARY)
            {
                if (spell->second.dependent)
                    value->Dependent |= 1u << i;
                else
                {
                    value->InitialPermanent |= 1u << i;
                    if (spell->second.state != PLAYERSPELL_UNCHANGED)
                    {
                        value->Independent |= 1u << i;
                        value->Touched |= 1u << i;
                        value->SourceRevision[i] = 1;
                    }
                }
            }
    }
    return value;
}

// Called only by the module's world-thread processors, after map updates join.
Player* Current(std::shared_ptr<State> const& state)
{
    Player* player = ObjectAccessor::FindConnectedPlayer(state->Guid);
    auto current = Find(state->Guid);
    return player && current == state ? player : nullptr;
}

std::string Key(ObjectGuid guid)
{
    return Trinity::StringFormat("`realm`={} AND `guid`={} AND `profile`='{}'",
        guid.GetRealmId(), guid.GetCounter(), Profile);
}

std::optional<std::size_t> Index(uint32 spell)
{
    for (std::size_t i = 0; i < Entries.size(); ++i)
        if (Entries[i].Spell == spell)
            return i;
    return {};
}

SpellPowerEntry const* RejuvenationPower()
{
    SpellPowerEntry const* power = sSpellPowerStore.LookupEntry(305256);
    if (!power || power->SpellID != 774 || power->PowerType != POWER_MANA || power->PowerCostPct != 5.0f ||
        power->RequiredAuraSpellID != 137011 || power->ManaCost || power->ManaCostPerLevel || power->OptionalCost ||
        power->OptionalCostPct != 0.0f || power->PowerCostMaxPct != 0.0f)
        return nullptr;
    return power;
}

uint32 Available(Player const* player)
{
    if (player->GetClass() != 16 || uint32(player->GetPrimarySpecialization()) != Mode)
        return 0;
    uint32 mask = 0;
    for (std::size_t i = 0; i < Entries.size(); ++i)
    {
        Entry const& entry = Entries[i];
        if (!entry.Reviewed || player->GetLevel() < entry.Level)
            continue;
        // Equipped weapons are checked by the normal spell cast after acquisition.
        SpellInfo const* info = sSpellMgr->GetSpellInfo(entry.Spell, DIFFICULTY_NONE);
        if (!info || !SpellMgr::IsSpellValid(info, const_cast<Player*>(player), false) || info->IsRanked() ||
            info->HasEffect(SPELL_EFFECT_LEARN_SPELL))
            continue;
        auto learned = sSpellMgr->GetSpellLearnSpellMapBounds(entry.Spell);
        auto required = sSpellMgr->GetSpellsRequiringSpellBounds(entry.Spell);
        if (learned.first != learned.second || required.first != required.second)
            continue;
        if (entry.Spell == 774 && (!RejuvenationPower() || !player->GetCreateMana() || info->IsSingleTarget() ||
            info->HasAuraInterruptFlag(SpellAuraInterruptFlags2::ChangeSpec)))
            continue;
        if (entry.Spell == 116)
        {
            SpellInfo const* damage = sSpellMgr->GetSpellInfo(228597, DIFFICULTY_NONE);
            if (!damage || !damage->HasEffect(SPELL_EFFECT_SCHOOL_DAMAGE) ||
                !SpellMgr::IsSpellValid(damage, const_cast<Player*>(player), false) ||
                !std::ranges::any_of(info->GetEffects(), [](SpellEffectInfo const& effect) { return effect.TriggerSpell == 228597; }))
                continue;
        }
        auto costs = info->CalcPowerCost(player, info->GetSchoolMask(), nullptr);
        if (costs.empty() || std::ranges::any_of(costs, [player](SpellPowerCost const& cost)
            { return cost.Power < POWER_MANA || cost.Power >= MAX_POWERS || player->GetMaxPower(cost.Power) <= 0; }))
            continue;
        mask |= 1u << i;
    }
    return mask;
}

void Send(Player* player, State const& state, Request const& request, Result result)
{
    Snapshot snapshot = state.Wallet;
    if (!state.HasSnapshot)
    {
        snapshot = {};
        snapshot.Essence = 0;
    }
    std::string paid;
    for (uint32 amount : snapshot.Paid)
    {
        if (!paid.empty())
            paid += ',';
        paid += std::to_string(amount);
    }
    std::string message = Trinity::StringFormat("V1 {} {} {} {} {} {} {} {} {} {} {}",
        request.Epoch, request.Id, player->GetGUID().ToString(), uint32(player->GetPrimarySpecialization()), Name(result),
        snapshot.Version, snapshot.Essence, snapshot.Owned, state.Ready ? Available(player) : 0,
        RulesRevision, paid);
    if (message.size() > 255)
    {
        TC_LOG_ERROR("module.hero", "Hero advancement reply exceeds the addon limit for {}", player->GetGUID().ToString());
        return;
    }
    WorldPackets::Chat::Chat packet;
    packet.Initialize(CHAT_MSG_WHISPER, LANG_ADDON, player, player, message, 0, "", LOCALE_enUS, Prefix);
    player->GetSession()->SendPacket(packet.Write());
}

void AppendSources(CharacterDatabaseTransaction transaction, State const& state)
{
    if (!state.Touched)
        return;
    // The first ordinary save can beat the asynchronous login read. Allocate
    // the same immutable session epoch inside that save, then persist source
    // intent atomically with character_spell. Late saves reuse their old token.
    if (!state.SourceEpoch)
        for (std::string const& sql : InitializeSessionSql(state.Guid.GetRealmId(), state.Guid.GetCounter(), state.SessionTokens))
            transaction->Append(sql.c_str());
    for (std::string const& sql : SourceSql(state.Guid.GetRealmId(), state.Guid.GetCounter(), state.Touched,
        state.Independent, state.SourceRevision, state.SourceEpoch, state.SessionTokens.back()))
        transaction->Append(sql.c_str());
}
void Reconcile(Player* player, State& state)
{
    if (!state.HasSnapshot)
        return;
    state.Internal = true;
    for (std::size_t i = 0; i < Entries.size(); ++i)
    {
        if (!Entries[i].Reviewed)
            continue;
        uint32 bit = 1u << i;
        Grant grant = DesiredGrant(bit, state.Wallet.Owned, state.Independent, state.Dependent,
            uint32(player->GetPrimarySpecialization()) == Mode, state.Ready);
        if (grant != Grant::None)
            player->LearnSpell(Entries[i].Spell, grant != Grant::Permanent);
        else if (player->HasSpell(Entries[i].Spell))
            player->RemoveSpell(Entries[i].Spell, false, false);
    }
    state.Internal = false;
}

void CleanActions(Player* player, State const& state, bool send)
{
    if (!state.Ready || player->IsLoadingActionButtons() || uint32(player->GetPrimarySpecialization()) != Mode)
        return;
    std::vector<uint8> buttons;
    for (auto const& [button, action] : player->GetActionButtons())
        if (action.GetType() == ACTION_BUTTON_SPELL)
            if (auto index = Index(uint32(action.GetAction())); index && Entries[*index].Reviewed &&
                !((state.Wallet.Owned | state.Independent | state.Dependent) & (1u << *index)))
                buttons.push_back(button);
    for (uint8 button : buttons)
        player->RemoveActionButton(button);
    if (send && !buttons.empty())
        player->SendActionButtons(1);
}

struct ReadResult
{
    Snapshot Wallet;
    uint32 Independent = 0;
    uint32 SourcePresent = 0;
    uint64 SourceEpoch = 0;
    std::array<uint64, 10> SourceRevision = {};
    bool ProfileFound = false;
    bool ReceiptFound = false;
    Request Receipt;
    Result ReceiptResult = Result::Unknown;
    uint32 ReceiptVersion = 0;
};

std::optional<ReadResult> Decode(QueryResult const& rows)
{
    if (!rows)
        return {};
    ReadResult result;
    do
    {
        Field* fields = rows->Fetch();
        switch (fields[0].GetUInt32())
        {
            case 0:
                if (result.ProfileFound || fields[3].GetUInt32() != RulesRevision)
                    return {};
                result.ProfileFound = true;
                result.Wallet.Essence = fields[1].GetUInt32();
                result.Wallet.Version = fields[2].GetUInt32();
                result.SourceEpoch = fields[4].GetUInt64();
                break;
            case 1:
            {
                auto index = Index(fields[2].GetUInt32());
                if (!index || fields[1].GetUInt32() != Entries[*index].Advancement ||
                    (result.Wallet.Owned & (1u << *index)) || fields[4].GetUInt32() != RulesRevision)
                    return {};
                result.Wallet.Owned |= 1u << *index;
                result.Wallet.Paid[*index] = fields[3].GetUInt32();
                break;
            }
            case 2:
                if (auto index = Index(fields[1].GetUInt32()))
                {
                    result.SourcePresent |= 1u << *index;
                    if (fields[2].GetUInt32())
                        result.Independent |= 1u << *index;
                    result.SourceRevision[*index] = fields[3].GetUInt64();
                }
                break;
            case 3:
                if (result.ReceiptFound || fields[3].GetUInt32() > uint32(Result::Unknown))
                    return {};
                result.ReceiptFound = true;
                result.Receipt.Commit = true;
                result.Receipt.Version = fields[1].GetUInt32();
                result.Receipt.Desired = fields[2].GetUInt32();
                result.ReceiptResult = Result(fields[3].GetUInt32());
                result.ReceiptVersion = fields[4].GetUInt32();
                if (result.ReceiptResult == Result::Ok && fields[4].GetUInt32() == 0)
                    return {};
                break;
            default:
                return {};
        }
    } while (rows->NextRow());
    return result.ProfileFound && result.SourceEpoch && Valid(result.Wallet) && result.ReceiptVersion <= result.Wallet.Version ?
        std::optional(result) : std::nullopt;
}

void Read(std::shared_ptr<State> const& state, std::optional<Request> request, bool afterCommit, Done done);

void CommitChange(Player* player, std::shared_ptr<State> const& state, Request request, Done done)
{
    Result outcome = Result::Ok;
    if (uint32(player->GetPrimarySpecialization()) != Mode)
        outcome = Result::WrongMode;
    else if (!player->IsAlive())
        outcome = Result::Dead;
    else if (player->IsInCombat())
        outcome = Result::Combat;
    else if (player->IsNonMeleeSpellCast(false))
        outcome = Result::Casting;
    Change change{outcome, state->Wallet};
    if (outcome == Result::Ok)
        change = Reduce(state->Wallet, request, Available(player));
    outcome = change.Code;

    CharacterDatabaseTransaction transaction = CharacterDatabase.BeginTransaction();
    for (std::string const& sql : ApplySql(state->Guid.GetRealmId(), state->Guid.GetCounter(), request, change,
        state->Independent | state->Dependent))
        transaction->Append(sql.c_str());
    AppendSources(transaction, *state);
    Transactions.AddCallback(CharacterDatabase.AsyncCommitTransaction(transaction)).AfterComplete(
        [state, request, done = std::move(done)](bool /*success*/)
        {
            // The receipt readback is required even when COMMIT's result is
            // unknown, and survives destruction of the submitting session.
            Read(state, request, true, done);
        });
}

void Read(std::shared_ptr<State> const& state, std::optional<Request> request, bool afterCommit, Done done)
{
    state->Busy = true;
    std::string query = Trinity::StringFormat(
        "SELECT 0 AS kind,`balance` AS n1,`version` AS n2,`rules_revision` AS n3,"
        "(SELECT `epoch` FROM `character_hero_source_session` WHERE {} AND `token`='{}') AS n4 FROM `character_hero_freepick` WHERE {} "
        "UNION ALL SELECT 1,`advancement`,`spell`,`paid`,`rules_revision` FROM `character_hero_freepick_owned` WHERE {} AND `catalog`='{}' "
        "UNION ALL SELECT 2,`spell`,`independent`,`revision`,0 FROM `character_hero_spell_source` WHERE `realm`={} AND `guid`={}",
        Key(state->Guid), state->SessionTokens.back(), Key(state->Guid), Key(state->Guid), Catalog,
        state->Guid.GetRealmId(), state->Guid.GetCounter());
    if (request)
        query += Trinity::StringFormat(
            " UNION ALL SELECT 3,`expected_version`,`desired_mask`,`result`,COALESCE(`committed_version`,0) FROM `character_hero_freepick_request` WHERE {} AND `request_id`='{}'",
            Key(state->Guid), request->Id);
    Queries.AddCallback(CharacterDatabase.AsyncQuery(query.c_str()).WithCallback(
        [state, request, afterCommit, done = std::move(done)](QueryResult rows)
        {
            Player* current = Current(state);
            state->Busy = false;
            auto result = Decode(rows);
            if (!result || result->Wallet.Version < state->Wallet.Version)
            {
                state->Ready = false;
                if (current)
                {
                    Reconcile(current, *state);
                    if (request)
                        Send(current, *state, *request, afterCommit ? Result::Unknown : Result::Unavailable);
                }
                done();
                return;
            }
            state->Wallet = result->Wallet;
            uint32 initial = state->InitialPermanent & ~result->SourcePresent;
            state->Independent = ((result->Independent | initial) & ~state->Touched) | (state->Independent & state->Touched);
            state->Touched |= initial;
            state->SourceEpoch = result->SourceEpoch;
            state->SessionTokens = {state->SessionTokens.back()};
            for (std::size_t i = 0; i < Entries.size(); ++i)
                if (!state->HasSnapshot)
                    state->SourceRevision[i] += result->SourceRevision[i];
                else if (!(state->Touched & (1u << i)))
                    state->SourceRevision[i] = result->SourceRevision[i];
            state->HasSnapshot = true;
            state->Ready = true;
            if (current)
            {
                Reconcile(current, *state);
                CleanActions(current, *state, true);
            }
            if (!request || !current)
            {
                done();
                return;
            }
            if (result->ReceiptFound)
            {
                Result outcome = request->Commit && !SamePayload(*request, result->Receipt) ? Result::IdReuse : result->ReceiptResult;
                Send(current, *state, *request, outcome);
            }
            else if (afterCommit)
                Send(current, *state, *request, Result::Unknown);
            else if (request->Commit)
            {
                state->Busy = true;
                CommitChange(current, state, *request, done);
                return;
            }
            else
                Send(current, *state, *request, Result::Ok);
            done();
        }));
}

void Initialize(std::shared_ptr<State> const& state, Done done)
{
    CharacterDatabaseTransaction transaction = CharacterDatabase.BeginTransaction();
    for (std::string const& sql : InitializeSessionSql(state->Guid.GetRealmId(), state->Guid.GetCounter(), state->SessionTokens))
        transaction->Append(sql.c_str());
    Transactions.AddCallback(CharacterDatabase.AsyncCommitTransaction(transaction)).AfterComplete(
        [state, done = std::move(done)](bool /*success*/)
        {
            Read(state, {}, false, done);
        });
}

void QueueInitialize(std::shared_ptr<State> const& state)
{
    state->Busy = true;
    Operations.Enqueue(state->Guid, [state](Done done) { Initialize(state, std::move(done)); });
}

void FlushSources(std::shared_ptr<State> const& state, Done done)
{
    if (!state->Touched)
    {
        if (state->SourceEpoch)
        {
            std::lock_guard lock(StatesMutex);
            auto it = RetainedSources.find(state->Guid);
            if (it != RetainedSources.end() && it->second == state)
                RetainedSources.erase(it);
        }
        done();
        return;
    }
    CharacterDatabaseTransaction transaction = CharacterDatabase.BeginTransaction();
    AppendSources(transaction, *state);
    Transactions.AddCallback(CharacterDatabase.AsyncCommitTransaction(transaction)).AfterComplete(
        [state, done = std::move(done)](bool success)
        {
            // A failed flush stays in RetainedSources and is inherited on the
            // next login. Ordinary saves also write source intent atomically.
            if (success)
            {
                std::lock_guard lock(StatesMutex);
                auto it = RetainedSources.find(state->Guid);
                if (it != RetainedSources.end() && it->second == state)
                    RetainedSources.erase(it);
            }
            done();
        });
}

class HeroFreePickWorldScript final : public WorldScript
{
public:
    HeroFreePickWorldScript() : WorldScript("mod_hero_FreePick_completions") { }

    void OnUpdate(uint32) override
    {
        // World::Update calls this after MapManager::Update has joined every
        // map worker. Only this thread touches the callback processors.
        Transactions.ProcessReadyCallbacks();
        Queries.ProcessReadyCallbacks();
        Operations.Pump();
    }
};

class HeroFreePickPlayerScript final : public PlayerScript
{
public:
    HeroFreePickPlayerScript() : PlayerScript("mod_hero_FreePick") { }

    void OnLogin(Player* player, bool) override
    {
        if (player->GetClass() == 16)
            QueueInitialize(Create(player));
    }

    void OnLogout(Player* player) override
    {
        auto state = Find(player->GetGUID());
        if (!state)
            return;
        {
            std::lock_guard lock(StatesMutex);
            RetainedSources[state->Guid] = state;
            States.erase(state->Guid);
        }
        Operations.Enqueue(state->Guid, [state](Done done)
        {
            if (state->HasSnapshot)
                FlushSources(state, std::move(done));
            else
                Initialize(state, [state, done = std::move(done)] { FlushSources(state, done); });
        });
    }

    bool OnAddonMessage(Player* player, std::string_view prefix, std::string_view message) override
    {
        if (prefix != Prefix)
            return false;
        auto request = Parse(message);
        if (!request)
            return true;
        if (player->GetClass() != 16)
        {
            Send(player, State{}, *request, Result::NotHero);
            return true;
        }
        auto state = Find(player->GetGUID());
        if (!state)
        {
            state = Create(player);
            QueueInitialize(state);
        }
        if (state->Busy)
            Send(player, *state, *request, Result::Busy);
        else if (!state->Ready)
        {
            QueueInitialize(state);
            Send(player, *state, *request, Result::Unavailable);
        }
        else
        {
            state->Busy = true;
            Operations.Enqueue(state->Guid, [state, request](Done done)
            {
                if (Current(state))
                    Read(state, request, false, std::move(done));
                else
                    done();
            });
        }
        return true;
    }

    bool OnSpellLearn(Player* player, uint32 spell, bool& dependent) override
    {
        auto index = Index(spell);
        auto state = Find(player->GetGUID());
        if (!index || !state || player->GetClass() != 16)
            return false;
        if (state->Internal)
            return !dependent && (state->Independent & (1u << *index));
        if (dependent)
        {
            state->Dependent |= 1u << *index;
            if (state->Independent & (1u << *index))
                dependent = false;
        }
        else
        {
            state->Independent |= 1u << *index;
            state->Touched |= 1u << *index;
            ++state->SourceRevision[*index];
        }
        return true;
    }

    bool OnBeforeSpellRemove(Player* player, uint32 spell, bool& preserveAura) override
    {
        auto index = Index(spell);
        auto state = Find(player->GetGUID());
        if (!index || !state || player->GetClass() != 16)
            return true;
        if (state->Internal)
        {
            if (spell == 774)
                preserveAura = true;
            return true;
        }
        state->Independent &= ~(1u << *index);
        state->Dependent &= ~(1u << *index);
        state->Touched |= 1u << *index;
        ++state->SourceRevision[*index];
        if (state->Ready && (state->Wallet.Owned & (1u << *index)) && uint32(player->GetPrimarySpecialization()) == Mode)
        {
            state->Internal = true;
            player->LearnSpell(spell, true);
            state->Internal = false;
            return false;
        }
        return true;
    }

    void OnTalentGroupChanged(Player* player, bool actionsLoaded) override
    {
        if (auto state = Find(player->GetGUID()))
        {
            Reconcile(player, *state);
            if (actionsLoaded)
                CleanActions(player, *state, false);
        }
    }

    void OnSaveTransaction(Player* player, CharacterDatabaseTransaction transaction, bool create) override
    {
        if (create)
        {
            // The core can reuse a deleted GUID after restart. Reset the old
            // lifecycle atomically with insertion of the new character row.
            for (std::string const& sql : NewCharacterSql(player->GetGUID().GetRealmId(), player->GetGUID().GetCounter()))
                transaction->Append(sql.c_str());
            return;
        }
        if (auto state = Find(player->GetGUID()))
            AppendSources(transaction, *state);
    }

    void OnSpellPowerCost(Player const* player, SpellInfo const* info, uint32 schoolMask,
        Spell* spell, std::vector<SpellPowerCost>& costs) override
    {
        if (player->GetClass() != 16 || info->Id != 774 || std::ranges::any_of(costs,
            [](SpellPowerCost const& cost) { return cost.Power == POWER_MANA; }))
            return;
        if (SpellPowerEntry const* source = RejuvenationPower())
        {
            SpellPowerEntry adapted = *source;
            adapted.RequiredAuraSpellID = 0;
            if (auto cost = info->CalcPowerCost(&adapted, false, player, SpellSchoolMask(schoolMask), spell))
                costs.push_back(*cost);
        }
    }

    void OnDeleteTransaction(ObjectGuid guid, uint32, CharacterDatabaseTransaction transaction) override
    {
        {
            std::lock_guard lock(StatesMutex);
            States.erase(guid);
            RetainedSources.erase(guid);
        }
        // Includes permanent player, GM and aged-character deletion paths.
        // Soft deletion keeps the ledger for restoration, as it keeps spells.
        // A tombstone makes previously queued initialization writes harmless.
        for (std::string const& sql : DeleteSql(guid.GetRealmId(), guid.GetCounter()))
            transaction->Append(sql.c_str());
    }
};
}

void AddHeroFreePickScripts()
{
    new HeroFreePickPlayerScript();
    new HeroFreePickWorldScript();
}
