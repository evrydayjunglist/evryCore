// A bounded host for executing the actual module callbacks. This is not a
// substitute for a real client cast, native hit roll, or native database load.
#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using uint8=std::uint8_t; using uint32=std::uint32_t; using int32=std::int32_t;
struct ObjectGuid { uint32 Value=0; bool operator==(ObjectGuid const&) const = default; };
using SpellEffIndex=int; using AuraEffectHandleModes=int; using Races=int;
constexpr int EFFECT_0=0, SPELL_EFFECT_ANY=0, SPELL_AURA_DUMMY=4, AURA_EFFECT_HANDLE_REAL=1;
constexpr int CLASS_REAPER=17, TRIGGERED_FULL_MASK=1;
enum SpellCastResult { SPELL_CAST_OK, SPELL_FAILED_SPELL_UNAVAILABLE, SPELL_FAILED_CASTER_AURASTATE };
enum class ItemContext { NONE };
struct SpellInfo { uint32 Id=0; };
struct SpellEffectInfo { int EffectIndex=0; };
struct AuraEffect {};
struct Spell {};
struct CastSpellExtraArgs
{
    int32 Amount=0;
    CastSpellExtraArgs(int) { }
    CastSpellExtraArgs& AddSpellBP0(int32 value) { Amount=value; return *this; }
};
inline uint32 urand(uint32 low,uint32 high) { return (low+high)/2; }
struct Aura
{
    uint8 Count=1;
    int Refreshes=0;
    uint8 GetStackAmount() const { return Count; }
    void SetStackAmount(uint8 count) { Count=count; }
    void RefreshDuration() { ++Refreshes; }
};
class Player;
class Unit;
inline std::function<void(Unit*,uint32,ObjectGuid)> AuraRemoved;
class Unit
{
public:
    virtual ~Unit()=default;
    ObjectGuid Guid{1};
    bool Friendly=false, Alive=true, SpellImmune=false, DamageImmune=false;
    uint32 Health=500, Maximum=1000, Power=0;
    std::map<std::pair<uint32,uint32>,std::unique_ptr<Aura>> Auras;
    std::vector<uint32> Children;
    ObjectGuid GetGUID() const { return Guid; }
    virtual Player* ToPlayer() { return nullptr; }
    bool IsAlive() const { return Alive; }
    bool IsFriendlyTo(Unit* other) const { return this==other || other->Friendly; }
    uint32 GetHealth() const { return Health; }
    uint32 GetMaxHealth() const { return Maximum; }
    bool IsImmunedToDamage(Unit*,SpellInfo const*) { return DamageImmune; }
    bool IsImmunedToSpell(SpellInfo const*,uint32,Unit*) { return SpellImmune; }
    Aura* GetAura(uint32 spell,ObjectGuid owner)
    {
        auto it=Auras.find({spell,owner.Value});
        return it==Auras.end()?nullptr:it->second.get();
    }
    bool HasAura(uint32 spell,ObjectGuid owner) { return GetAura(spell,owner)!=nullptr; }
    Aura* AddAura(uint32 spell,Unit* target)
    {
        auto& aura=target->Auras[{spell,Guid.Value}];
        aura=std::make_unique<Aura>();
        return aura.get();
    }
    void RemoveAurasDueToSpell(uint32 spell,ObjectGuid owner)
    {
        if (Auras.erase({spell,owner.Value}) && AuraRemoved)
            AuraRemoved(this,spell,owner);
    }
    void CastSpell(Unit* target,uint32 spell,CastSpellExtraArgs args)
    {
        Children.push_back(spell);
        if (spell==600008) Power=std::min(1000u,Power+uint32(args.Amount));
        if (spell==600009) Power=std::min(1000u,Power+150);
        if (spell==600010) Health=std::min(Maximum,Health+uint32(args.Amount));
        if (spell==600011) AddAura(spell,target);
    }
};
class Player : public Unit
{
public:
    int Class=17, Level=6, Skills=0, Saves=0, ItemGrants=0;
    bool Room=true;
    std::set<uint32> Known, Items;
    std::string Actions="owner action bars";
    Player* ToPlayer() override { return this; }
    int GetClass() const { return Class; }
    int GetRace() const { return 5; }
    int GetSkillValue(uint32) const { return 1; }
    void LearnDefaultSkills() { ++Skills; }
    void LearnSkillRewardedSpells(uint32,int,Races) { }
    bool HasSpell(uint32 spell) const { return Known.contains(spell); }
    bool HasItemCount(uint32 item,int,bool) const { return Items.contains(item); }
    bool StoreNewItemInBestSlots(uint32 item,int,ItemContext)
    {
        if (!Room) return false;
        Items.insert(item);++ItemGrants;return true;
    }
    void LearnSpell(uint32 spell,bool) { Known.insert(spell); }
    void SaveToDB() { ++Saves; }
    void* GetSession() { return nullptr; }
};
struct ChatHandler { ChatHandler(void*) { } void SendSysMessage(char const*) { } };
struct PlayerScript
{
    PlayerScript(char const*) { }
    virtual void OnLogin(Player*,bool) { }
};
template<class Signature> struct Hook;
template<class R,class... Args> struct Hook<R(Args...)>
{
    std::vector<std::function<R(void*,Args...)>> Calls;
    template<class F> void operator+=(F f) { Calls.emplace_back(f); }
    R Run(void* owner,Args... args)
    {
        if constexpr (std::is_void_v<R>)
            for (auto& f:Calls) f(owner,args...);
        else { assert(Calls.size()==1); return Calls[0](owner,args...); }
    }
};
template<class C,class R,class... Args> auto Bind(R(C::*fn)(Args...))
{
    return [fn](void* owner,Args... args)->R { return (static_cast<C*>(owner)->*fn)(args...); };
}
struct SpellScript
{
    Unit* Caster=nullptr; Unit* Target=nullptr; SpellInfo Info; int32 Damage=0;
    Hook<SpellCastResult()> OnCheckCast;
    Hook<void(SpellEffIndex)> OnEffectHitTarget;
    Hook<void()> AfterHit,AfterCast;
    Hook<void(SpellEffectInfo const&,Unit*,int32&,int32&,float&)> CalcDamage;
    Unit* GetCaster() { return Caster; }
    Unit* GetHitUnit() { return Target; }
    Unit* GetExplTargetUnit() { return Target; }
    SpellInfo const* GetSpellInfo() { return &Info; }
    int32 GetHitDamage() { return Damage; }
    bool ValidateSpellInfo(std::initializer_list<uint32>) { return true; }
    virtual bool Validate(SpellInfo const*) { return true; }
    virtual void Register() { }
};
struct AuraScript
{
    Unit* Target=nullptr; ObjectGuid Owner; uint32 Id=0;
    Hook<void(AuraEffect const*,AuraEffectHandleModes)> AfterEffectRemove;
    Unit* GetTarget() { return Target; }
    ObjectGuid GetCasterGUID() { return Owner; }
    uint32 GetId() { return Id; }
    virtual void Register() { }
};
#define PrepareSpellScript(C) public:
#define PrepareAuraScript(C) public:
#define SpellCheckCastFn(F) Bind(&F)
#define SpellEffectFn(F,I,N) Bind(&F)
#define SpellHitFn(F) Bind(&F)
#define SpellCastFn(F) Bind(&F)
#define SpellCalcDamageFn(F) Bind(&F)
#define AuraEffectRemoveFn(F,I,N,M) Bind(&F)
#define RegisterSpellScript(C) ((void)0)
