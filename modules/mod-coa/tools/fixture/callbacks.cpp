// Compiles the production module unchanged against the bounded host above.
#include "spell_reaper.cpp"
#include <iostream>

static void Cast(Player& caster,Unit& target,uint32 id,int damage=10,bool landed=true)
{
    spell_coa_reaper_combat script;
    script.Caster=&caster;script.Target=&target;script.Info.Id=id;script.Damage=damage;
    script.Register();
    assert(script.OnCheckCast.Run(&script)==SPELL_CAST_OK);
    if (landed) script.OnEffectHitTarget.Run(&script,0);
    script.AfterHit.Run(&script);
    script.AfterHit.Run(&script); // Repeated target/effect callback cannot reward twice.
    script.AfterCast.Run(&script);
}

int main()
{
    AuraRemoved=[](Unit* target,uint32 id,ObjectGuid owner)
    {
        if (id!=ReapedSoul && id!=SoulInfusion) return;
        spell_coa_reaper_souls script;
        script.Target=target;script.Owner=owner;script.Id=id;script.Register();
        script.AfterEffectRemove.Run(&script,nullptr,AURA_EFFECT_HANDLE_REAL);
    };
    Player a,b;
    b.Guid.Value=2;
    Unit enemy;enemy.Guid.Value=3;
    a.AddAura(SoulCollector,&a);
    b.AddAura(SoulCollector,&b);
    for (int cycle=0;cycle<2;++cycle)
    {
        for (int n=0;n<9;++n) Cast(a,enemy,Reap);
        assert(Stacks(&a,ReapedSoul)==3 && Stacks(&a,SoulInfusion)==1);
        assert(a.Children.size()==size_t(9+10*cycle));
        Cast(a,enemy,Soulrend);
        assert(Stacks(&a,ReapedSoul)==0 && Stacks(&a,SoulFragment)==0 && !Stacks(&a,SoulInfusion));
    }
    assert(a.Power==1000); // Native host saturation, not a second resource counter.
    assert(b.Power==0 && Stacks(&b,ReapedSoul)==0);
    Cast(b,enemy,Reap,0);
    assert(b.Children.empty() && Stacks(&b,SoulFragment)==0);
    Cast(b,enemy,Murder,0);
    assert(Stacks(&b,ReapedSoul)==1 && enemy.HasAura(MurderDebuff,b.Guid));
    Cast(b,enemy,SoulStrike,100);
    assert(Stacks(&b,ReapedSoul)==2 && b.Health==630);
    Cast(b,enemy,SoulStrike,0);
    assert(Stacks(&b,ReapedSoul)==3 && b.Health==667);
    Cast(b,enemy,Soulrend,0,false); // Miss/dodge/parry have no effect hit.
    assert(Stacks(&b,ReapedSoul)==3);
    enemy.SpellImmune=true;
    Cast(b,enemy,Soulrend,0,false);
    assert(Stacks(&b,ReapedSoul)==0 && b.Power==0);
    enemy.SpellImmune=false;
    enemy.DamageImmune=true;
    Cast(b,enemy,SoulStrike,0);
    assert(Stacks(&b,ReapedSoul)==0 && b.Health==667);
    enemy.DamageImmune=false;
    b.AddAura(ReapedSoul,&a)->SetStackAmount(3); // Foreign-owned state survives.
    GainSouls(&a,2,3);
    a.RemoveAurasDueToSpell(SoulInfusion,a.Guid);
    assert(!Stacks(&a,ReapedSoul) && !Stacks(&a,SoulFragment));
    assert(a.GetAura(ReapedSoul,b.Guid)->GetStackAmount()==3);
    GainSouls(&a,1,0);
    int refreshes=a.GetAura(SoulFragment,a.Guid)->Refreshes;
    GainSouls(&a,0,1);
    assert(a.GetAura(SoulFragment,a.Guid)->Refreshes==refreshes);
    GainSouls(&a,0,3);
    a.RemoveAurasDueToSpell(ReapedSoul,a.Guid);
    assert(!Stacks(&a,SoulInfusion));

    spell_coa_reaper_combat gated;gated.Caster=&a;gated.Info.Id=Soulrend;gated.Register();
    assert(gated.OnCheckCast.Run(&gated)==SPELL_FAILED_CASTER_AURASTATE);
    a.Class=16;
    assert(gated.OnCheckCast.Run(&gated)==SPELL_FAILED_SPELL_UNAVAILABLE);
    a.Class=17;
    CoaReaperPlayerScript learning;
    a.Known.insert(133);
    a.AddAura(ReapedSoul,&a)->SetStackAmount(3);
    a.AddAura(SoulInfusion,&a);
    learning.OnLogin(&a,false);
    assert(!a.GetAura(ReapedSoul,a.GetGUID()) && !a.GetAura(SoulInfusion,a.GetGUID()));
    assert(!a.HasSpell(EquipmentReceipt) && a.ItemGrants==0 && a.Saves==0 && a.Skills==0);
    a.Items.clear(); // Creation gear is never granted by logging in.
    learning.OnLogin(&a,false);
    assert(a.ItemGrants==0 && a.Saves==0 && a.HasSpell(133) && a.Actions=="owner action bars");
    a.Class=16;int skills=a.Skills;
    learning.OnLogin(&a,false);
    assert(a.Skills==skills);
    std::cout << "Reaper production callbacks: combat, ownership, removal and login without item/spell grants PASS\n";
}
