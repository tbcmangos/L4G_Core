/* ScriptData
SDName: Boss_Netherspite
SD%Complete: 90
SDComment: Not sure about timing and portals placing
SDCategory: Karazhan
EndScriptData */

#include "precompiled.h"
#include "def_karazhan.h"
#include <vector>

enum Netherspite
{
    AGGRO_RANGE             = 40,

    RED_PORTAL              = 0, // Perseverence
    GREEN_PORTAL            = 1, // Serenity
    BLUE_PORTAL             = 2, // Dominance
    
    EMOTE_PHASE_PORTAL      = -1532089,
    EMOTE_PHASE_BANISH      = -1532090,

    SPELL_NETHERBURN_AURA   = 30522,
    SPELL_VOIDZONE          = 37063,
    SPELL_NETHER_INFUSION   = 38688,
    SPELL_NETHERBREATH      = 38523,
    SPELL_BANISH_VISUAL     = 39833,
    SPELL_BANISH_ROOT       = 42716,
    SPELL_EMPOWERMENT       = 38549,
    SPELL_NETHERSPITE_ROAR  = 38684
};

const float PortalCoord[3][3] =
{
    {-11195.353516, -1613.237183, 278.237258}, // Left side
    {-11137.846680, -1685.607422, 278.239258}, // Right side
    {-11094.493164, -1591.969238, 279.949188} // Back side
};

const uint32 PortalID[3] = {17369, 17367, 17368};
const uint32 PortalVisual[3] = {30487,30490,30491};
const uint32 PortalBeam[3] = {30465,30464,30463};
const uint32 PlayerBuff[3] = {30421,30422,30423};
const uint32 NetherBuff[3] = {30466,30467,30468};
const uint32 PlayerDebuff[3] = {38637,38638,38639};

struct boss_netherspiteAI : public ScriptedAI
{
    boss_netherspiteAI(Creature* c) : ScriptedAI(c)
    {
        pInstance = (c->GetInstanceData());
        m_creature->SetAggroRange(AGGRO_RANGE);
    }

    ScriptedInstance* pInstance;

    bool PortalPhase;
    bool Berserk;
    uint32 PhaseTimer; // timer for phase switching
    uint32 VoidZoneTimer;
    uint32 NetherInfusionTimer; // berserking timer
    uint32 NetherbreathTimer;
    uint32 EmpowermentTimer;
    uint32 ExhaustCheckTimer;
    uint32 PortalTimer; // timer for beam checking
    uint64 PortalGUID[3]; // guid's of portals
    uint64 BeamerGUID[3]; // guid's of auxiliary beaming portals
    uint64 BeamTarget[3]; // guid's of portals' current targets

    void Reset()
    {
        ClearCastQueue();
        me->GetMotionMaster()->Initialize();

        Berserk = false;
        NetherInfusionTimer = 540000;
        VoidZoneTimer = 15000;
        NetherbreathTimer = 3000;
        ExhaustCheckTimer = 1000;
        HandleDoors(true);
        DestroyPortals();

        for(int i=0; i<3; ++i)
        {
            PortalGUID[i] = 0;
            BeamTarget[i] = 0;
            BeamerGUID[i] = 0;
        }

        if(pInstance && pInstance->GetData(DATA_NETHERSPITE_EVENT) != DONE)
            pInstance->SetData(DATA_NETHERSPITE_EVENT, NOT_STARTED);
    }

    void SummonPortals()
    {
        uint8 r = rand()%4;
        uint8 pos[3];
        pos[RED_PORTAL] = (r%2 ? (r>1 ? 2: 1): 0);
        pos[GREEN_PORTAL] = (r%2 ? 0: (r>1 ? 2: 1));
        pos[BLUE_PORTAL] = (r>1 ? 1: 2); // Blue Portal not on the left side (0)

        for(int i=0; i<3; ++i)
        {
            if(Creature *portal = m_creature->SummonCreature(PortalID[i],PortalCoord[pos[i]][0],PortalCoord[pos[i]][1],PortalCoord[pos[i]][2],0,TEMPSUMMON_TIMED_DESPAWN,60000))
            {
                PortalGUID[i] = portal->GetGUID();
                portal->AddAura(PortalVisual[i], portal);
                portal->AddAura(42176,portal);
            }
        }
    }

    void DestroyPortals()
    {
        for(int i=0; i<3; ++i)
        {
            if(Creature *portal = Unit::GetCreature(*m_creature, PortalGUID[i]))
            {                
                portal->SetVisibility(VISIBILITY_OFF);
                portal->DealDamage(portal, portal->GetMaxHealth());
                portal->RemoveFromWorld();
            }

            if(Creature *portal = Unit::GetCreature(*m_creature, BeamerGUID[i]))
            {
                Unit *current = Unit::GetUnit(*portal, BeamTarget[i]);
                if (current && current->GetTypeId() == TYPEID_PLAYER)
                {                    
                    current->AddAura(PlayerDebuff[i], current);
                }
                portal->SetVisibility(VISIBILITY_OFF);
                portal->DealDamage(portal, portal->GetMaxHealth());
                portal->RemoveFromWorld();
            }

            PortalGUID[i] = 0;
            BeamTarget[i] = 0;
        }
    }
    void UpdatePortals() // Here we handle the beams' behavior
    {
        for(int j=0; j<3; ++j) // j = color
        {
            if(Creature *portal = Unit::GetCreature(*m_creature, PortalGUID[j]))
            {
                // the one who's been casted upon before
                Unit *current = Unit::GetUnit(*portal, BeamTarget[j]);
                // temporary store for the best suitable beam reciever
                Unit *target = m_creature;

                if(Map* map = m_creature->GetMap())
                {
                    Map::PlayerList const& players = map->GetPlayers();

                    // get the best suitable target
                    for(Map::PlayerList::const_iterator i = players.begin(); i!=players.end(); ++i)
                    {
                        Player* p = i->getSource();
                        if(p && p->isAlive() // alive
                            && (!target || target->GetExactDistance2d(portal->GetPositionX(), portal->GetPositionY()) > p->GetExactDistance2d(portal->GetPositionX(), portal->GetPositionY())) // closer than current best
                            && (!p->HasAura(PlayerDebuff[j],0) || (p->HasAura(PlayerDebuff[j],0) && p->HasAura(PlayerBuff[j],0))) // not exhausted or exhausted AND has SAME stacks!
                            && !p->HasAura(PlayerBuff[(j+1)%3],0) // not on another beam
                            && !p->HasAura(PlayerBuff[(j+2)%3],0)
                            && p->isBetween(m_creature, portal)) // on the beam
                            target = p;
                    }
                }

                // buff the target
                if(target->GetTypeId() == TYPEID_PLAYER)
                    target->AddAura(PlayerBuff[j], target);
                else
                    target->AddAura(NetherBuff[j], target);

                //(Re-)Debuff last target
                if(current && target != current)
                {
                    if(current->GetTypeId()== TYPEID_PLAYER)
                        current->AddAura(PlayerDebuff[j], current);
                }

                // cast visual beam on the chosen target if switched
                // simple target switching isn't working -> using BeamerGUID to cast (workaround)
                if(!current || target != current)
                {
                    BeamTarget[j] = target->GetGUID();
                    // remove currently beaming portal
                    if(Creature *beamer = Unit::GetCreature(*portal, BeamerGUID[j]))
                    {
                        beamer->CastSpell(target, PortalBeam[j], true);
                        //beamer->CastSpell(target, PortalBeam[j], false); //Original Line
                        beamer->SetVisibility(VISIBILITY_OFF);
                        beamer->DealDamage(beamer, beamer->GetMaxHealth()); //Original Line
                        beamer->RemoveFromWorld(); //Original Line
                        BeamerGUID[j] = 0;
                    }
                    // create new one and start beaming on the target
                    if(Creature *beamer = portal->SummonCreature(PortalID[j],portal->GetPositionX(),portal->GetPositionY(),portal->GetPositionZ(),portal->GetOrientation(),TEMPSUMMON_TIMED_DESPAWN,60000))
                    {
                        beamer->CastSpell(target, PortalBeam[j], true);
                        //beamer->CastSpell(target, PortalBeam[j], false); //Original Line
                        BeamerGUID[j] = beamer->GetGUID();
                    }
                }
                // aggro target if Red Beam
                if(j == RED_PORTAL && m_creature->getVictim() != target && target->GetTypeId() == TYPEID_PLAYER)
                    m_creature->getThreatManager().addThreat(target, 100000.0f+DoGetThreat(m_creature->getVictim()));
            }
        }
    }

    void SwitchToPortalPhase()
    {
        m_creature->RemoveAurasDueToSpell(SPELL_BANISH_ROOT);
        m_creature->RemoveAurasDueToSpell(SPELL_BANISH_VISUAL);
        SummonPortals();
        PhaseTimer = 60000;
        PortalPhase = true;
        PortalTimer = 10000;
        EmpowermentTimer = 10000;
        DoScriptText(EMOTE_PHASE_PORTAL,m_creature);
        DoResetThreat();
        m_creature->GetMotionMaster()->MoveChase(m_creature->getVictim());
    }

    void SwitchToBanishPhase()
    {
        m_creature->RemoveAurasDueToSpell(SPELL_EMPOWERMENT);
        //m_creature->RemoveAurasDueToSpell(SPELL_NETHERBURN_AURA); prenerf
        DoCast(m_creature,SPELL_BANISH_VISUAL,true);
        DoCast(m_creature,SPELL_BANISH_ROOT,true);
        DestroyPortals();
        PhaseTimer = 30000;
        PortalPhase = false;
        DoScriptText(EMOTE_PHASE_BANISH,m_creature);

        for(int i=0; i<3; ++i)
            m_creature->RemoveAurasDueToSpell(NetherBuff[i]);
    }

    void HandleDoors(bool open) // Massive Door switcher
    {
        if(GameObject *Door = GameObject::GetGameObject((*m_creature),pInstance->GetData64(DATA_GAMEOBJECT_MASSIVE_DOOR)))
            Door->SetUInt32Value(GAMEOBJECT_STATE, open ? 0 : 1);
    }

    void EnterCombat(Unit *who)
    {
        HandleDoors(false);
        SwitchToPortalPhase();

        if (pInstance)
            pInstance->SetData(DATA_NETHERSPITE_EVENT, IN_PROGRESS);
    }


    void JustDied(Unit* killer)
    {
        HandleDoors(true);
        DestroyPortals();
        if (pInstance)
            pInstance->SetData(DATA_NETHERSPITE_EVENT, DONE);
    }

    void UpdateAI(const uint32 diff)
    {
        if(!UpdateVictim())
            return;

        DoZoneInCombat();

        if (VoidZoneTimer < diff)
        {
            if (Unit *target = SelectUnit(SELECT_TARGET_RANDOM, 0, GetSpellMaxRange(SPELL_VOIDZONE), true))
                AddSpellToCast(target, SPELL_VOIDZONE, true);

            VoidZoneTimer = 15000;
        }
        else
            VoidZoneTimer -= diff;

        if(!Berserk && NetherInfusionTimer < diff)
        {
            m_creature->AddAura(SPELL_NETHER_INFUSION, m_creature);
            ForceSpellCast(m_creature, SPELL_NETHERSPITE_ROAR, INTERRUPT_AND_CAST_INSTANTLY);
            Berserk = true;
        }
        else
            NetherInfusionTimer -= diff;

        if(PortalPhase) // PORTAL PHASE
        {
            // Distribute beams and buffs
            if(PortalTimer < diff)
            {
                UpdatePortals();
                PortalTimer = 1000;
            }
            else
                PortalTimer -= diff;

            if(EmpowermentTimer < diff)
            {
                ForceSpellCast(m_creature, SPELL_EMPOWERMENT);
                m_creature->AddAura(SPELL_NETHERBURN_AURA, m_creature);
                EmpowermentTimer = 90000;
            }
            else
                EmpowermentTimer -= diff;

            if(PhaseTimer < diff)
            {
                if(!m_creature->IsNonMeleeSpellCasted(false))
                {
                    SwitchToBanishPhase();
                    return;
                }
            }
            else
                PhaseTimer -= diff;

            DoMeleeAttackIfReady();
        }
        else // BANISH PHASE
        {
            if(m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
            {
                m_creature->GetMotionMaster()->Clear();
                m_creature->GetMotionMaster()->MoveIdle();
            }

            if(NetherbreathTimer < diff)
            {
                if (Unit* target = SelectUnit(SELECT_TARGET_RANDOM, 0, GetSpellMaxRange(SPELL_NETHERBREATH), true))
                {
                    m_creature->SetSelection(target->GetGUID());
                    m_creature->SetInFront(target);
                    AddSpellToCast(target, SPELL_NETHERBREATH);
                }
                NetherbreathTimer = urand(4000, 5000);
            }
            else
                NetherbreathTimer -= diff;

            if(PhaseTimer < diff)
            {
                if(!m_creature->IsNonMeleeSpellCasted(false))
                {
                    SwitchToPortalPhase();
                    return;
                }
            }
            else
                PhaseTimer -= diff;
        }
        CastNextSpellIfAnyAndReady();
    }
};

CreatureAI* GetAI_boss_netherspite(Creature *_Creature)
{
    return new boss_netherspiteAI(_Creature);
}

void AddSC_boss_netherspite()
{
    Script *newscript;

    newscript = new Script;
    newscript->Name="boss_netherspite";
    newscript->GetAI = GetAI_boss_netherspite;
    newscript->RegisterSelf();
}
