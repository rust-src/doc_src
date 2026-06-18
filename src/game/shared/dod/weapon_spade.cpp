//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#include "cbase.h"
#include "weapon_dodbasemelee.h"
#include "convar.h"

#if defined( CLIENT_DLL )
	#define CWeaponSpade C_WeaponSpade
	#define CWeaponMedkit C_WeaponMedkit
	#include "c_dod_player.h"
#else
	#include "dod_player.h"
	#include "util.h"
	#include "recipientfilter.h"
#endif

class CWeaponSpade : public CWeaponDODBaseMelee
{
public:
	DECLARE_CLASS(CWeaponSpade, CWeaponDODBaseMelee);
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	DECLARE_ACTTABLE();

	CWeaponSpade();
	virtual Activity GetMeleeActivity(void) { return ACT_VM_PRIMARYATTACK; }
	virtual DODWeaponID GetWeaponID(void) const { return WEAPON_SPADE; }

private:
	CWeaponSpade(const CWeaponSpade&);
};

IMPLEMENT_NETWORKCLASS_ALIASED(WeaponSpade, DT_WeaponSpade)

BEGIN_NETWORK_TABLE(CWeaponSpade, DT_WeaponSpade)
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA(CWeaponSpade)
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS(weapon_spade, CWeaponSpade);
PRECACHE_WEAPON_REGISTER(weapon_spade);

acttable_t CWeaponSpade::m_acttable[] =
{
	{ ACT_DOD_STAND_AIM,				ACT_DOD_STAND_AIM_SPADE,			false },
	{ ACT_DOD_CROUCH_AIM,				ACT_DOD_CROUCH_AIM_SPADE,			false },
	{ ACT_DOD_CROUCHWALK_AIM,			ACT_DOD_CROUCHWALK_AIM_SPADE,		false },
	{ ACT_DOD_WALK_AIM,					ACT_DOD_WALK_AIM_SPADE,				false },
	{ ACT_DOD_RUN_AIM,					ACT_DOD_RUN_AIM_SPADE,				false },
	{ ACT_PRONE_IDLE,					ACT_DOD_PRONE_AIM_SPADE,			false },
	{ ACT_PRONE_FORWARD,				ACT_DOD_PRONEWALK_AIM_SPADE,		false },
	{ ACT_DOD_STAND_IDLE,				ACT_DOD_STAND_AIM_SPADE,			false },
	{ ACT_DOD_CROUCH_IDLE,				ACT_DOD_CROUCH_AIM_SPADE,			false },
	{ ACT_DOD_CROUCHWALK_IDLE,			ACT_DOD_CROUCHWALK_AIM_SPADE,		false },
	{ ACT_DOD_WALK_IDLE,				ACT_DOD_WALK_AIM_SPADE,				false },
	{ ACT_DOD_RUN_IDLE,					ACT_DOD_RUN_AIM_SPADE,				false },
	{ ACT_SPRINT,						ACT_DOD_SPRINT_AIM_SPADE,			false },
	{ ACT_RANGE_ATTACK2,				ACT_DOD_PRIMARYATTACK_SPADE,		false },
	{ ACT_DOD_SECONDARYATTACK_CROUCH,	ACT_DOD_PRIMARYATTACK_CROUCH_SPADE,	false },
	{ ACT_DOD_SECONDARYATTACK_PRONE,	ACT_DOD_PRIMARYATTACK_PRONE_SPADE,	false },
	{ ACT_DOD_HS_IDLE,					ACT_DOD_HS_IDLE_STICKGRENADE,		false },
	{ ACT_DOD_HS_CROUCH,				ACT_DOD_HS_CROUCH_STICKGRENADE,		false },
};

IMPLEMENT_ACTTABLE(CWeaponSpade);

CWeaponSpade::CWeaponSpade()
{

}
class CWeaponMedkit : public CWeaponDODBaseMelee
{
public:
	DECLARE_CLASS(CWeaponMedkit, CWeaponDODBaseMelee);
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	DECLARE_ACTTABLE();

	CWeaponMedkit();

	virtual void PrimaryAttack(void);
	virtual Activity GetMeleeActivity(void) { return ACT_VM_PRIMARYATTACK; }
	virtual DODWeaponID GetWeaponID(void) const { return WEAPON_MEDKIT; }

private:
	CWeaponMedkit(const CWeaponMedkit&);

#ifndef CLIENT_DLL
	float m_flNextHealTime;
#endif
};

IMPLEMENT_NETWORKCLASS_ALIASED(WeaponMedkit, DT_WeaponMedkit)

BEGIN_NETWORK_TABLE(CWeaponMedkit, DT_WeaponMedkit)
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA(CWeaponMedkit)
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS(weapon_medkit, CWeaponMedkit);
PRECACHE_WEAPON_REGISTER(weapon_medkit);

acttable_t CWeaponMedkit::m_acttable[] =
{
	{ ACT_DOD_STAND_AIM,				ACT_DOD_STAND_AIM_SPADE,			false },
	{ ACT_DOD_CROUCH_AIM,				ACT_DOD_CROUCH_AIM_SPADE,			false },
	{ ACT_DOD_CROUCHWALK_AIM,			ACT_DOD_CROUCHWALK_AIM_SPADE,		false },
	{ ACT_DOD_WALK_AIM,					ACT_DOD_WALK_AIM_SPADE,				false },
	{ ACT_DOD_RUN_AIM,					ACT_DOD_RUN_AIM_SPADE,				false },
	{ ACT_PRONE_IDLE,					ACT_DOD_PRONE_AIM_SPADE,			false },
	{ ACT_PRONE_FORWARD,				ACT_DOD_PRONEWALK_AIM_SPADE,		false },
	{ ACT_DOD_STAND_IDLE,				ACT_DOD_STAND_AIM_SPADE,			false },
	{ ACT_DOD_CROUCH_IDLE,				ACT_DOD_CROUCH_AIM_SPADE,			false },
	{ ACT_DOD_CROUCHWALK_IDLE,			ACT_DOD_CROUCHWALK_AIM_SPADE,		false },
	{ ACT_DOD_WALK_IDLE,				ACT_DOD_WALK_AIM_SPADE,				false },
	{ ACT_DOD_RUN_IDLE,					ACT_DOD_RUN_AIM_SPADE,				false },
	{ ACT_SPRINT,						ACT_DOD_SPRINT_AIM_SPADE,			false },
	{ ACT_RANGE_ATTACK2,				ACT_DOD_PRIMARYATTACK_SPADE,		false },
	{ ACT_DOD_SECONDARYATTACK_CROUCH,	ACT_DOD_PRIMARYATTACK_CROUCH_SPADE,	false },
	{ ACT_DOD_SECONDARYATTACK_PRONE,	ACT_DOD_PRIMARYATTACK_PRONE_SPADE,	false },
	{ ACT_DOD_HS_IDLE,					ACT_DOD_HS_IDLE_STICKGRENADE,		false },
	{ ACT_DOD_HS_CROUCH,				ACT_DOD_HS_CROUCH_STICKGRENADE,		false },
};

IMPLEMENT_ACTTABLE(CWeaponMedkit);

CWeaponMedkit::CWeaponMedkit()
{
#ifndef CLIENT_DLL
	m_flNextHealTime = 0.0f;
#endif
}

void CWeaponMedkit::PrimaryAttack()
{
	CDODPlayer* pPlayer = GetDODPlayerOwner();
	if (!pPlayer)
		return;

#ifndef CLIENT_DLL
	// Enforce cooldown: 0.5s between heals
	if (gpGlobals->curtime < m_flNextHealTime)
		return;
#endif

	Vector vecSrc = pPlayer->Weapon_ShootPosition();
	Vector vecForward;
	pPlayer->EyeVectors(&vecForward);

	Vector vecEnd = vecSrc + vecForward * 64; // Melee range

	trace_t tr;
	UTIL_TraceLine(vecSrc, vecEnd, MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &tr);

	CBaseEntity* pHitEnt = tr.m_pEnt;
	CDODPlayer* pTarget = ToDODPlayer(pHitEnt);

#ifndef CLIENT_DLL
	if (pTarget && pTarget != pPlayer && pPlayer->InSameTeam(pTarget))
	{
		int currentHealth = pTarget->GetHealth();

		// Only heal if target is below 95 HP
		if (currentHealth >= 95)
			return;

		int maxHealth = pTarget->GetMaxHealth();
		int healAmount = 0;

		if (currentHealth < maxHealth)
		{
			healAmount = MIN(WEAP_MEDIKIT_HEAL, maxHealth - currentHealth);
			pTarget->TakeHealth(healAmount, DMG_GENERIC);
		}
		else if (currentHealth < maxHealth + WEAP_MEDIKIT_OVERHEAL)
		{
			healAmount = MIN(50, (maxHealth + WEAP_MEDIKIT_OVERHEAL) - currentHealth);
			pTarget->TakeHealth(healAmount, DMG_GENERIC);
		}

		if (healAmount > 0)
		{
			m_flNextHealTime = gpGlobals->curtime + 0.5f;

			pTarget->EmitSound("Weapon_Greasegun.Shoot");

			const char* pszName = pTarget->GetPlayerName();
			char msg[128];
			Q_snprintf(msg, sizeof(msg), "You healed %s for %d HP\n", pszName, healAmount);

			// Send chat message to healer
			UTIL_SayText(msg, static_cast<CBasePlayer*>(pPlayer));

			// Print to server console with color
			ConColorMsg(Color(0, 255, 0, 255), "[Heal] %s healed %s for %d HP\n",
				pPlayer->GetPlayerName(), pszName, healAmount);
		}
		return; // Skip melee if healing succeeds
	}
#endif
}