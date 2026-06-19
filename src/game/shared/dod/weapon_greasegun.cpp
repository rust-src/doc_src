//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Grease Gun and M3 Shotgun implementation.
//
//=============================================================================//

#include "cbase.h"
#include "weapon_dodfullauto_punch.h"
#ifdef GAME_DLL
#include "dod_player.h"
#endif
#include "fx_dod_shared.h"

#if defined( CLIENT_DLL )

	#define CWeaponGreaseGun C_WeaponGreaseGun

#endif


class CWeaponGreaseGun : public CDODFullAutoPunchWeapon
{
public:
	DECLARE_CLASS(CWeaponGreaseGun, CDODFullAutoPunchWeapon);
	DECLARE_NETWORKCLASS(); 
	DECLARE_PREDICTABLE();
	DECLARE_ACTTABLE();
	
	CWeaponGreaseGun()  {}

	virtual DODWeaponID GetWeaponID( void ) const		{ return WEAPON_GREASEGUN; }
	virtual DODWeaponID GetAltWeaponID(void) const		{ return WEAPON_GREASEGUN_PUNCH; }
	// Giggle

	virtual float GetRecoil(void) { return 1.5f; }
private:
	CWeaponGreaseGun( const CWeaponGreaseGun & );
};

IMPLEMENT_NETWORKCLASS_ALIASED( WeaponGreaseGun, DT_WeaponGreaseGun )

BEGIN_NETWORK_TABLE( CWeaponGreaseGun, DT_WeaponGreaseGun )
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CWeaponGreaseGun )
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS( weapon_greasegun, CWeaponGreaseGun );
PRECACHE_WEAPON_REGISTER( weapon_greasegun );

acttable_t CWeaponGreaseGun::m_acttable[] = 
{
	{ ACT_DOD_STAND_AIM,					ACT_DOD_STAND_AIM_TOMMY,				false },
	{ ACT_DOD_CROUCH_AIM,					ACT_DOD_CROUCH_AIM_TOMMY,				false },
	{ ACT_DOD_CROUCHWALK_AIM,				ACT_DOD_CROUCHWALK_AIM_TOMMY,			false },
	{ ACT_DOD_WALK_AIM,						ACT_DOD_WALK_AIM_TOMMY,					false },
	{ ACT_DOD_RUN_AIM,						ACT_DOD_RUN_AIM_TOMMY,					false },
	{ ACT_PRONE_IDLE,						ACT_DOD_PRONE_AIM_TOMMY,				false },
	{ ACT_PRONE_FORWARD,					ACT_DOD_PRONEWALK_IDLE_TOMMY,			false },
	{ ACT_DOD_STAND_IDLE,					ACT_DOD_STAND_IDLE_TOMMY,				false },
	{ ACT_DOD_CROUCH_IDLE,					ACT_DOD_CROUCH_IDLE_TOMMY,				false },
	{ ACT_DOD_CROUCHWALK_IDLE,				ACT_DOD_CROUCHWALK_IDLE_TOMMY,			false },
	{ ACT_DOD_WALK_IDLE,					ACT_DOD_WALK_IDLE_TOMMY,				false },
	{ ACT_DOD_RUN_IDLE,						ACT_DOD_RUN_IDLE_TOMMY,					false },
	{ ACT_SPRINT,							ACT_DOD_SPRINT_IDLE_TOMMY,				false },

	{ ACT_RANGE_ATTACK1,					ACT_DOD_PRIMARYATTACK_TOMMY,			false },
	{ ACT_DOD_PRIMARYATTACK_CROUCH,			ACT_DOD_PRIMARYATTACK_TOMMY,			false },
	{ ACT_DOD_PRIMARYATTACK_PRONE,			ACT_DOD_PRIMARYATTACK_PRONE_TOMMY,		false },
	{ ACT_RANGE_ATTACK2,					ACT_DOD_SECONDARYATTACK_TOMMY,			false },
	{ ACT_DOD_SECONDARYATTACK_CROUCH,		ACT_DOD_SECONDARYATTACK_CROUCH_TOMMY,	false },
	{ ACT_DOD_SECONDARYATTACK_PRONE,		ACT_DOD_SECONDARYATTACK_PRONE_TOMMY,	false },

	{ ACT_RELOAD,							ACT_DOD_RELOAD_TOMMY,					false },
	{ ACT_DOD_RELOAD_CROUCH,				ACT_DOD_RELOAD_CROUCH_TOMMY,			false },
	{ ACT_DOD_RELOAD_PRONE,					ACT_DOD_RELOAD_PRONE_TOMMY,				false },

	// Hand Signals
	{ ACT_DOD_HS_IDLE,						ACT_DOD_HS_IDLE_TOMMY,				false },
	{ ACT_DOD_HS_CROUCH,					ACT_DOD_HS_CROUCH_TOMMY,			false },
};

IMPLEMENT_ACTTABLE( CWeaponGreaseGun );

#if defined( CLIENT_DLL )
#define CWeaponTrenchGun C_WeaponTrenchGun
#endif

class CWeaponTrenchGun : public CWeaponDODBaseGun
{
public:
	DECLARE_CLASS(CWeaponTrenchGun, CWeaponDODBaseGun);
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	DECLARE_ACTTABLE();

	CWeaponTrenchGun();

	virtual void PrimaryAttack();
	virtual bool Reload();
	virtual void WeaponIdle();

	virtual DODWeaponID GetWeaponID(void) const { return WEAPON_TRENCHGUN; }


private:

	CWeaponTrenchGun(const CWeaponTrenchGun&);

	float m_flPumpTime;
	CNetworkVar(int, m_fInSpecialReload);

};

IMPLEMENT_NETWORKCLASS_ALIASED(WeaponTrenchGun, DT_WeaponTrenchGun)

BEGIN_NETWORK_TABLE(CWeaponTrenchGun, DT_WeaponTrenchGun)

#ifdef CLIENT_DLL
RecvPropInt(RECVINFO(m_fInSpecialReload))
#else
SendPropInt(SENDINFO(m_fInSpecialReload), 2, SPROP_UNSIGNED)
#endif

END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA(CWeaponTrenchGun)
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS(weapon_trenchgun, CWeaponTrenchGun);
PRECACHE_WEAPON_REGISTER(weapon_trenchgun);



CWeaponTrenchGun::CWeaponTrenchGun()
{
	m_flPumpTime = 0;
	m_bReloadsSingly = true;
}

void CWeaponTrenchGun::PrimaryAttack()
{
	CDODPlayer* pPlayer = ToDODPlayer(GetPlayerOwner());
	if (!pPlayer)
		return;

	// don't fire underwater
	if (pPlayer->GetWaterLevel() == 3)
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = gpGlobals->curtime + 0.15;
		return;
	}

	// Out of ammo?
	if (m_iClip1 <= 0)
	{
		Reload();
		if (m_iClip1 == 0)
		{
			PlayEmptySound();
			m_flNextPrimaryAttack = gpGlobals->curtime + 0.2;
		}

		return;
	}

	SendWeaponAnim(ACT_VM_PRIMARYATTACK);

	m_iClip1--;
	pPlayer->DoMuzzleFlash();

	// player "shoot" animation
	pPlayer->SetAnimation(PLAYER_ATTACK1);

	// Dispatch the FX right away with full accuracy.
	FX_FireBullets(
		pPlayer->entindex(),
		pPlayer->Weapon_ShootPosition(),
		pPlayer->EyeAngles() + 2.0f * pPlayer->GetPunchAngle(),
		GetWeaponID(),
		Primary_Mode,
		CBaseEntity::GetPredictionRandomSeed() & 255, // wrap it for network traffic so it's the same between client and server
		0.0675);

	if (!m_iClip1 && pPlayer->GetAmmoCount(m_iPrimaryAmmoType) <= 0)
	{
		// HEV suit - indicate out of ammo condition
		pPlayer->SetSuitUpdate("!HEV_AMO0", false, 0);
	}

	if (m_iClip1 != 0)
		m_flPumpTime = gpGlobals->curtime + 0.5;

	m_flNextPrimaryAttack = gpGlobals->curtime + 0.875;
	m_flNextSecondaryAttack = gpGlobals->curtime + 0.875;
	if (m_iClip1 != 0)
		SetWeaponIdleTime(gpGlobals->curtime + 2.5);
	else
		SetWeaponIdleTime(gpGlobals->curtime + 0.875);
	m_fInSpecialReload = 0;

	// Update punch angles.
	QAngle angle = pPlayer->GetPunchAngle();

	if (pPlayer->GetFlags() & FL_ONGROUND)
	{
		angle.x -= SharedRandomInt("M3PunchAngleGround", 4, 6);
	}
	else
	{
		angle.x -= SharedRandomInt("M3PunchAngleAir", 8, 11);
	}

	pPlayer->SetPunchAngle(angle);
}

bool CWeaponTrenchGun::Reload()
{
	CDODPlayer* pPlayer = ToDODPlayer(GetPlayerOwner());
	if (!pPlayer)
		return false;

	if (pPlayer->GetAmmoCount(m_iPrimaryAmmoType) <= 0 || m_iClip1 == GetMaxClip1())
		return true;

	// don't reload until recoil is done
	if (m_flNextPrimaryAttack > gpGlobals->curtime)
		return true;

	// check to see if we're ready to reload
	if (m_fInSpecialReload == 0)
	{
		pPlayer->SetAnimation(PLAYER_RELOAD);

		SendWeaponAnim(ACT_SHOTGUN_RELOAD_START);
		m_fInSpecialReload = 1;
		pPlayer->m_flNextAttack = gpGlobals->curtime + 0.5;
		m_flNextPrimaryAttack = gpGlobals->curtime + 0.5;
		m_flNextSecondaryAttack = gpGlobals->curtime + 0.5;
		SetWeaponIdleTime(gpGlobals->curtime + 0.5);

#ifdef GAME_DLL
		//pPlayer->DoAnimationEvent(PLAYERANIMEVENT_RELOAD_START);
		pPlayer->DoAnimationEvent(PLAYERANIMEVENT_RELOAD);
#endif

		return true;
	}
	else if (m_fInSpecialReload == 1)
	{
		if (m_flTimeWeaponIdle > gpGlobals->curtime)
			return true;
		// was waiting for gun to move to side
		m_fInSpecialReload = 2;

		SendWeaponAnim(ACT_VM_RELOAD);
		SetWeaponIdleTime(gpGlobals->curtime + 0.5);
#ifdef GAME_DLL
		if (m_iClip1 == 7)
		{
			//pPlayer->DoAnimationEvent(PLAYERANIMEVENT_RELOAD_END);
		}
		else
		{
			//pPlayer->DoAnimationEvent(PLAYERANIMEVENT_RELOAD_LOOP);
		}
#endif
	}
	else
	{
		// Add them to the clip
		m_iClip1 += 1;


		CDODPlayer* pPlayer = ToDODPlayer(GetPlayerOwner());

		if (pPlayer)
			pPlayer->RemoveAmmo(1, m_iPrimaryAmmoType);

		m_fInSpecialReload = 1;
	}

	return true;
}


void CWeaponTrenchGun::WeaponIdle()
{
	CDODPlayer* pPlayer = ToDODPlayer(GetPlayerOwner());
	if (!pPlayer)
		return;

	if (m_flPumpTime && m_flPumpTime < gpGlobals->curtime)
	{
		// play pumping sound
		m_flPumpTime = 0;
	}

	if (m_flTimeWeaponIdle < gpGlobals->curtime)
	{
		if (m_iClip1 == 0 && m_fInSpecialReload == 0 && pPlayer->GetAmmoCount(m_iPrimaryAmmoType))
		{
			Reload();
		}
		else if (m_fInSpecialReload != 0)
		{
			if (m_iClip1 != 8 && pPlayer->GetAmmoCount(m_iPrimaryAmmoType))
			{
				Reload();
			}
			else
			{
				// reload debounce has timed out
				//MIKETODO: shotgun anims
				SendWeaponAnim(ACT_SHOTGUN_RELOAD_FINISH);

				// play cocking sound
				m_fInSpecialReload = 0;
				SetWeaponIdleTime(gpGlobals->curtime + 1.5);
			}
		}
		else
		{
			SendWeaponAnim(ACT_VM_IDLE);
		}
	}
}
acttable_t CWeaponTrenchGun::m_acttable[] =
{
	{ ACT_DOD_STAND_AIM,					ACT_DOD_STAND_AIM_TOMMY,				false },
	{ ACT_DOD_CROUCH_AIM,					ACT_DOD_CROUCH_AIM_TOMMY,				false },
	{ ACT_DOD_CROUCHWALK_AIM,				ACT_DOD_CROUCHWALK_AIM_TOMMY,			false },
	{ ACT_DOD_WALK_AIM,						ACT_DOD_WALK_AIM_TOMMY,					false },
	{ ACT_DOD_RUN_AIM,						ACT_DOD_RUN_AIM_TOMMY,					false },
	{ ACT_PRONE_IDLE,						ACT_DOD_PRONE_AIM_TOMMY,				false },
	{ ACT_PRONE_FORWARD,					ACT_DOD_PRONEWALK_IDLE_TOMMY,			false },
	{ ACT_DOD_STAND_IDLE,					ACT_DOD_STAND_IDLE_TOMMY,				false },
	{ ACT_DOD_CROUCH_IDLE,					ACT_DOD_CROUCH_IDLE_TOMMY,				false },
	{ ACT_DOD_CROUCHWALK_IDLE,				ACT_DOD_CROUCHWALK_IDLE_TOMMY,			false },
	{ ACT_DOD_WALK_IDLE,					ACT_DOD_WALK_IDLE_TOMMY,				false },
	{ ACT_DOD_RUN_IDLE,						ACT_DOD_RUN_IDLE_TOMMY,					false },
	{ ACT_SPRINT,							ACT_DOD_SPRINT_IDLE_TOMMY,				false },

	{ ACT_RANGE_ATTACK1,					ACT_DOD_PRIMARYATTACK_TOMMY,			false },
	{ ACT_DOD_PRIMARYATTACK_CROUCH,			ACT_DOD_PRIMARYATTACK_TOMMY,			false },
	{ ACT_DOD_PRIMARYATTACK_PRONE,			ACT_DOD_PRIMARYATTACK_PRONE_TOMMY,		false },
	{ ACT_RANGE_ATTACK2,					ACT_DOD_SECONDARYATTACK_TOMMY,			false },
	{ ACT_DOD_SECONDARYATTACK_CROUCH,		ACT_DOD_SECONDARYATTACK_CROUCH_TOMMY,	false },
	{ ACT_DOD_SECONDARYATTACK_PRONE,		ACT_DOD_SECONDARYATTACK_PRONE_TOMMY,	false },

	{ ACT_RELOAD,							ACT_DOD_RELOAD_TOMMY,					false },
	{ ACT_DOD_RELOAD_CROUCH,				ACT_DOD_RELOAD_CROUCH_TOMMY,			false },
	{ ACT_DOD_RELOAD_PRONE,					ACT_DOD_RELOAD_PRONE_TOMMY,				false },

	// Hand Signals
	{ ACT_DOD_HS_IDLE,						ACT_DOD_HS_IDLE_TOMMY,				false },
	{ ACT_DOD_HS_CROUCH,					ACT_DOD_HS_CROUCH_TOMMY,			false },
};

IMPLEMENT_ACTTABLE(CWeaponTrenchGun);