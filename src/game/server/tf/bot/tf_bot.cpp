//========= Copyright © Valve LLC, All rights reserved. =======================
//
// Purpose:		
//
// $NoKeywords: $
//=============================================================================

#include "cbase.h"
#include "mathlib/mathlib.h"
#include "tf_bot.h"
#include "tf_bot_components.h"
#include "tf_bot_squad.h"
#include "tf_bot_manager.h"
#include "tf/nav_mesh/tf_nav_mesh.h"
#include "behavior/tf_bot_behavior.h"
#include "behavior/tf_bot_use_item.h"
#include "NextBotUtil.h"
#include <viewport_panel_names.h>
#include "dod_shareddefs.h"
#include "dod_gamerules.h"
#include "dod/dod_control_point.h"
#include "dod/dod_control_point_master.h"
#include "dod/dod_objective_resource.h"

void DifficultyChanged( IConVar *var, const char *pOldValue, float flOldValue );
void PrefixNameChanged( IConVar *var, const char *pOldValue, float flOldValue );

bool IsPlayerClassName(char const* str)
{
	return false;
}

int GetClassIndexFromString(char const* name, int maxClass)
{
	return 0;
}

bool IsTeamName(const char* str)
{
	for (int i = 0; i < g_Teams.Size(); ++i)
	{
		if (FStrEq(str, g_Teams[i]->GetName()))
			return true;
	}

	return Q_strcasecmp(str, "spectate") == 0;
}

ConVar doc_bot_difficulty( "doc_bot_difficulty", "1", FCVAR_NONE, "Defines the skill of bots joining the game.  Values are: 0=easy, 1=normal, 2=hard, 3=expert.", &DifficultyChanged );
ConVar doc_bot_force_class( "doc_bot_force_class", "", FCVAR_NONE, "If set to a class name, all TFBots will respawn as that class" );
ConVar doc_bot_keep_class_after_death( "doc_bot_keep_class_after_death", "0" );
ConVar doc_bot_prefix_name_with_difficulty( "doc_bot_prefix_name_with_difficulty", "0", FCVAR_NONE, "Append the skill level of the bot to the bot's name", &PrefixNameChanged );
ConVar doc_bot_path_lookahead_range( "doc_bot_path_lookahead_range", "300", FCVAR_NONE, "", true, 0.0f, true, 1500.0f );
ConVar doc_bot_near_point_travel_distance( "doc_bot_near_point_travel_distance", "750", FCVAR_CHEAT );
ConVar doc_bot_pyro_shove_away_range( "doc_bot_pyro_shove_away_range", "250", FCVAR_CHEAT, "If a Pyro bot's target is closer than this, compression blast them away" );
ConVar doc_bot_pyro_deflect_tolerance( "doc_bot_pyro_deflect_tolerance", "0.5", FCVAR_CHEAT );
ConVar doc_bot_sniper_spot_min_range( "doc_bot_sniper_spot_min_range", "1000", FCVAR_CHEAT );
ConVar doc_bot_sniper_spot_max_count( "doc_bot_sniper_spot_max_count", "10", FCVAR_CHEAT, "Stop searching for sniper spots when each side has found this many" );
ConVar doc_bot_sniper_spot_search_count( "doc_bot_sniper_spot_search_count", "10", FCVAR_CHEAT, "Search this many times per behavior update frame" );
ConVar doc_bot_sniper_spot_point_tolerance( "doc_bot_sniper_spot_point_tolerance", "750", FCVAR_CHEAT );
ConVar doc_bot_sniper_spot_epsilon( "doc_bot_sniper_spot_epsilon", "100", FCVAR_CHEAT );
ConVar doc_bot_sniper_goal_entity_move_tolerance( "doc_bot_sniper_goal_entity_move_tolerance", "500", FCVAR_CHEAT );
ConVar doc_bot_suspect_spy_touch_interval( "doc_bot_suspect_spy_touch_interval", "5", FCVAR_CHEAT, "How many seconds back to look for touches against suspicious spies", true, 0.0f, false, 0.0f );
ConVar doc_bot_suspect_spy_forget_cooldown( "doc_bot_suspect_spy_forced_cooldown", "5", FCVAR_CHEAT, "How long to consider a suspicious spy as suspicious", true, 0.0f, false, 0.0f );

ConVar doc_bot_recoil("doc_bot_recoil", "1", FCVAR_CHEAT | FCVAR_REPLICATED | FCVAR_NOTIFY, "enable recoil for bots");

LINK_ENTITY_TO_CLASS( tf_bot, CTFBot )

CBasePlayer *CTFBot::AllocatePlayerEntity( edict_t *edict, const char *playerName )
{
	CDODPlayer::s_PlayerEdict = edict;
	return (CTFBot *)CreateEntityByName( "tf_bot" );
}


class SelectClosestPotentiallyVisible
{
public:
	SelectClosestPotentiallyVisible( const Vector &origin )
		: m_vecOrigin( origin )
	{
		m_pSelected = NULL;
		m_flMinDist = FLT_MAX;
	}

	bool operator()( CNavArea *area )
	{
		Vector vecClosest;
		area->GetClosestPointOnArea( m_vecOrigin, &vecClosest );
		float flDistance = ( vecClosest - m_vecOrigin ).LengthSqr();

		if ( flDistance < m_flMinDist )
		{
			m_flMinDist = flDistance;
			m_pSelected = area;
		}

		return true;
	}

	Vector m_vecOrigin;
	CNavArea *m_pSelected;
	float m_flMinDist;
};


class CollectReachableObjects : public ISearchSurroundingAreasFunctor
{
public:
	CollectReachableObjects( CTFBot *actor, CUtlVector<EHANDLE> *selectedHealths, CUtlVector<EHANDLE> *outVector, float flMaxLength )
	{
		m_pBot = actor;
		m_flMaxRange = flMaxLength;
		m_pHealths = selectedHealths;
		m_pVector = outVector;
	}

	virtual bool operator() ( CNavArea *area, CNavArea *priorArea, float travelDistanceSoFar )
	{
		for ( int i=0; i<m_pHealths->Count(); ++i )
		{
			CBaseEntity *pEntity = ( *m_pHealths )[i];
			if ( !pEntity || !area->Contains( pEntity->WorldSpaceCenter() ) )
				continue;

			for ( int j=0; j<m_pVector->Count(); ++j )
			{
				CBaseEntity *pSelected = ( *m_pVector )[j];
				if ( ENTINDEX( pEntity ) == ENTINDEX( pSelected ) )
					return true;
			}

			EHANDLE hndl( pEntity );
			m_pVector->AddToTail( hndl );
		}

		return true;
	}

	virtual bool ShouldSearch( CNavArea *adjArea, CNavArea *currentArea, float travelDistanceSoFar )
	{
		if ( adjArea->IsBlocked( m_pBot->GetTeamNumber() ) || travelDistanceSoFar > m_flMaxRange )
			return false;

		return currentArea->IsContiguous( adjArea );
	}

private:
	CTFBot *m_pBot;
	CUtlVector<EHANDLE> *m_pHealths;
	CUtlVector<EHANDLE> *m_pVector;
	float m_flMaxRange;
};


class CountClassMembers
{
public:
	CountClassMembers( CTFBot *bot, int teamNum )
		: m_pBot( bot ), m_iTeam( teamNum )
	{
		Q_memset( &m_aClassCounts, 0, sizeof( m_aClassCounts ) );
	}

	bool operator()( CBasePlayer *player )
	{
		if ( player->GetTeamNumber() == m_iTeam )
		{
			++m_iTotal;
			CDODPlayer *pDODPlayer = static_cast<CDODPlayer *>( player );
			if ( !m_pBot->IsSelf( player ) )
				++m_aClassCounts[ pDODPlayer->m_Shared.PlayerClass() ];
		}

		return true;
	}

	CTFBot *m_pBot;
	int m_iTeam;
	int m_aClassCounts[6];
	int m_iTotal;
};


IMPLEMENT_INTENTION_INTERFACE( CTFBot, CTFBotMainAction )


CTFBot::CTFBot( CDODPlayer *player )
{
	m_controlling = player;

	m_body = new CTFBotBody( this );
	m_vision = new CTFBotVision( this );
	m_locomotor = new CTFBotLocomotion( this );
	m_intention = new CTFBotIntention( this );

	ListenForGameEvent( "teamplay_point_startcapture" );
	ListenForGameEvent( "teamplay_point_captured" );
	ListenForGameEvent( "teamplay_round_win" );
	ListenForGameEvent( "teamplay_flag_event" );
}

CTFBot::~CTFBot()
{
	if ( m_body )
		delete m_body;
	if ( m_vision )
		delete m_vision;
	if ( m_locomotor )
		delete m_locomotor;
	if ( m_intention )
		delete m_intention;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::Spawn( void )
{
	BaseClass::Spawn();

	m_iSkill = (DifficultyType)doc_bot_difficulty.GetInt();
	m_nBotAttrs = AttributeType::NONE;

	m_useWeaponAbilityTimer.Start( 5.0f );
	m_bLookingAroundForEnemies = true;
	m_suspectedSpies.PurgeAndDeleteElements();
	m_cpChangedTimer.Invalidate();
	m_requiredEquipStack.RemoveAll();
	m_hMyControlPoint = NULL;
	m_hMyCaptureZone = NULL;

	GetVisionInterface()->ForgetAllKnownEntities();

	ClearSniperSpots();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::Event_Killed( const CTakeDamageInfo &info )
{
	BaseClass::Event_Killed( info );

	LeaveSquad();

	//TODO: maybe cut this out
	if ( !doc_bot_keep_class_after_death.GetBool() )
	{
		m_bWantsToChangeClass = true;
	}

	CTFNavArea *pArea = (CTFNavArea *)GetLastKnownArea();
	if ( pArea )
	{
		// remove us from old visible set
		NavAreaCollector visibleSet;
		pArea->ForAllPotentiallyVisibleAreas( visibleSet );

		for( CNavArea *pVisible : visibleSet.m_area )
			static_cast<CTFNavArea *>( pVisible )->RemovePotentiallyVisibleActor( this );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::UpdateOnRemove( void )
{
	LeaveSquad();

	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// Purpose: Notify my components
//-----------------------------------------------------------------------------
void CTFBot::FireGameEvent( IGameEvent *event )
{
	if ( FStrEq( event->GetName(), "teamplay_point_startcapture" ) )
	{
		int iCPIndex = event->GetInt( "cp" );
		OnTerritoryContested( iCPIndex );
	}
	else if ( FStrEq( event->GetName(), "teamplay_point_captured" ) )
	{
		ClearMyControlPoint();

		int iCPIndex = event->GetInt( "cp" );
		int iTeam = event->GetInt( "team" );
		if ( iTeam == GetTeamNumber() )
		{
			OnTerritoryCaptured( iCPIndex );
		}
		else
		{
			OnTerritoryLost( iCPIndex );
			m_cpChangedTimer.Start( RandomFloat( 10.0f, 20.0f ) );
		}
	}
	else if ( FStrEq( event->GetName(), "teamplay_round_win" ) )
	{
		int iWinningTeam = event->GetInt( "team" );
		if ( event->GetBool( "full_round" ) )
		{
			if ( iWinningTeam == GetTeamNumber() )
				OnWin();
			else
				OnLose();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CTFBot::DrawDebugTextOverlays( void )
{
	int text_offset = CDODPlayer::DrawDebugTextOverlays();

	if ( m_debugOverlays & OVERLAY_TEXT_BIT )
	{
		EntityText( text_offset, CFmtStr( "FOV: %.2f (%i)", GetVisionInterface()->GetFieldOfView(), GetFOV() ), 0 );
		text_offset++;
	}

	return text_offset;
}

//-----------------------------------------------------------------------------
// Purpose: Perform some updates on physics step
//-----------------------------------------------------------------------------
void CTFBot::PhysicsSimulate( void )
{
	BaseClass::PhysicsSimulate();

	/*if ( m_HomeArea == nullptr )
		m_HomeArea = (CTFNavArea*)GetLastKnownArea();*/

	if ( m_pSquad && ( m_pSquad->GetMemberCount() <= 1 || !m_pSquad->GetLeader() ) )
		LeaveSquad();

	if ( !IsAlive() && m_bWantsToChangeClass )
	{
		HandleCommand_JoinClass(RandomInt(0, 5));

		m_bWantsToChangeClass = false;
	}
}

extern CUtlVector< CHandle<CControlPointMaster> >		g_hControlPointMasters;

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsPointInRound(CControlPoint* pPoint, CControlPointMaster* pMaster)
{
	if (g_hControlPointMasters.IsEmpty())
		return false;

	if (!pMaster || !pMaster->IsActive())
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Fills a vector with valid points that the player can capture right now
// Input:	pPlayer - The player that wants to capture
//			controlPointVector - A vector to fill with results
//-----------------------------------------------------------------------------
void CTFBot::CollectCapturePoints(CUtlVector<CControlPoint*>* controlPointVector)
{
	Assert(ObjectiveResource());
	if (!controlPointVector)
		return;

	controlPointVector->RemoveAll();

	if (g_hControlPointMasters.IsEmpty())
		return;

	CControlPointMaster* pMaster = g_hControlPointMasters[0];	
	if (!pMaster || !pMaster->IsActive())
		return;

	if (pMaster->GetNumPoints() == 1)
	{
		CControlPoint* pPoint = pMaster->GetControlPoint(0);
		if (pPoint && pPoint->GetPointIndex() == 0)
			controlPointVector->AddToTail(pPoint);

		return;
	}

	for (int i = 0; i < pMaster->GetNumPoints(); ++i)
	{
		CControlPoint* pPoint = pMaster->GetControlPoint(i);
		if (IsPointInRound(pPoint, pMaster) &&
			g_pObjectiveResource->GetOwningTeam(pPoint->GetPointIndex()) != GetTeamNumber() &&
			g_pObjectiveResource->TeamCanCapPoint(pPoint->GetPointIndex(), GetTeamNumber()))
		{
			controlPointVector->AddToTail(pPoint);
		}
	}

}

//-----------------------------------------------------------------------------
// Purpose: Fills a vector with valid points that the player needs to defend from capture
// Input:	pPlayer - The player that wants to defend
//			controlPointVector - A vector to fill with results
//-----------------------------------------------------------------------------
void CTFBot::CollectDefendPoints(CUtlVector<CControlPoint*>* controlPointVector)
{
	Assert(ObjectiveResource());
	if (!controlPointVector)
		return;

	controlPointVector->RemoveAll();

	if (g_hControlPointMasters.IsEmpty())
		return;

	CControlPointMaster* pMaster = g_hControlPointMasters[0];
	if (!pMaster || !pMaster->IsActive())
		return;

	for (int i = 0; i < pMaster->GetNumPoints(); ++i)
	{
		CControlPoint* pPoint = pMaster->GetControlPoint(i);
		if (IsPointInRound(pPoint, pMaster) &&
			g_pObjectiveResource->GetOwningTeam(pPoint->GetPointIndex()) == GetTeamNumber() &&
			g_pObjectiveResource->TeamCanCapPoint(pPoint->GetPointIndex(), GetEnemyTeam(this)))
		{
			controlPointVector->AddToTail(pPoint);
		}
	}

}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CTFBot::MedicGetChargeLevel(void)
{
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CBaseEntity* CTFBot::MedicGetHealTarget(void)
{
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Alert us and others we bumped a spy
//-----------------------------------------------------------------------------
void CTFBot::Touch( CBaseEntity *other )
{
	BaseClass::Touch( other );

	CDODPlayer *pOther = ToDODPlayer( other );
	if ( !pOther )
		return;

	if ( IsEnemy( pOther ) )
	{
		// hack nearby bots into reacting to bumping someone
		TheNextBots().OnWeaponFired( pOther, pOther->GetActiveDODWeapon() );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsAllowedToPickUpFlag( void )
{
	if (!m_pSquad || this == m_pSquad->GetLeader())
	{
		//return DWORD( this + 2468 ) == 0;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Disguise as a dead enemy for maximum espionage
//-----------------------------------------------------------------------------
void CTFBot::DisguiseAsEnemy( void )
{
	CUtlVector<CDODPlayer *> enemies;
	CollectPlayers( &enemies, GetEnemyTeam( this ), false );

	int iClass = -1;
	for ( int i=0; i < enemies.Count(); ++i )
	{
		if (!enemies[i]->IsAlive())
			iClass = enemies[i]->m_Shared.PlayerClass();
	}

	if ( iClass == 0 )
		iClass = RandomInt( 1,5 );

	//m_Shared.Disguise( GetEnemyTeam( this ), iClass );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsCombatWeapon( CWeaponDODBase *weapon ) const
{
	if ( weapon == nullptr )
	{
		weapon = GetActiveDODWeapon();
		if ( weapon == nullptr )
		{
			return true;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsQuietWeapon( CWeaponDODBase *weapon ) const
{
	if ( weapon == nullptr )
	{
		weapon = GetActiveDODWeapon();
		if ( weapon == nullptr )
		{
			return false;
		}
	}

	switch ( weapon->GetWeaponID() )
	{
		case WEAPON_AMERKNIFE:
		case WEAPON_SPADE:
			return true;

		default:
			return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsHitScanWeapon( CWeaponDODBase *weapon ) const
{
	if ( weapon == nullptr )
	{
		weapon = GetActiveDODWeapon();
		if ( weapon == nullptr )
		{
			return false;
		}
	}

	if ( !IsCombatWeapon( weapon ) )
	{
		return false;
	}

	switch ( weapon->GetWeaponID() )
	{
		case WEAPON_THOMPSON:
		case WEAPON_MP44:
		case WEAPON_MP40:
		case WEAPON_MG42:
		case WEAPON_M1CARBINE:
		case WEAPON_K98:
		case WEAPON_GARAND:
		case WEAPON_COLT:
		case WEAPON_C96:
		case WEAPON_BAR:
		case WEAPON_30CAL:
		case WEAPON_C96CARBINE:
		case WEAPON_TRENCHGUN:
			return true;

		default:
			return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsExplosiveProjectileWeapon( CWeaponDODBase *weapon ) const
{
	if ( weapon == nullptr )
	{
		weapon = GetActiveDODWeapon();
		if ( weapon == nullptr )
		{
			return false;
		}
	}

	switch ( weapon->GetWeaponID() )
	{
		case WEAPON_RIFLEGREN_GER:
		case WEAPON_RIFLEGREN_US:
		case WEAPON_FRAG_US:
		case WEAPON_FRAG_GER:
		case WEAPON_BAZOOKA:
		case WEAPON_PSCHRECK:
		case WEAPON_SMOKE_US:
		case WEAPON_SMOKE_GER:
			return true;

		default:
			return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsContinuousFireWeapon( CWeaponDODBase *weapon ) const
{
	if ( weapon == nullptr )
	{
		weapon = GetActiveDODWeapon();
		if ( weapon == nullptr )
		{
			return false;
		}
	}

	if ( !IsCombatWeapon( weapon ) )
	{
		return false;
	}

	switch ( weapon->GetWeaponID() )
	{
		case WEAPON_30CAL:
			return true;

		default:
			return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsBarrageAndReloadWeapon( CWeaponDODBase *weapon ) const
{
	if ( weapon == nullptr )
	{
		weapon = GetActiveDODWeapon();
		if ( weapon == nullptr )
		{
			return false;
		}
	}

	return false;
}

//TODO: why does this only care about the current weapon?
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsAmmoLow( void ) const
{

	CWeaponDODBase* weapon = GetActiveDODWeapon();
	if (weapon == nullptr)
		return false;

	if (!weapon->IsMeleeWeapon())
	{
		int clipSize = weapon->GetMaxClip1();
		if (clipSize > 0)
		{
			int current = weapon->GetPrimaryAmmoCount();
			return (static_cast<float>(current) / clipSize) < 0.2f;
		}
		else
		{
			// Maybe treat 0 clip size as "not low" or log a warning
			return false;
		}
	}

	return false;

}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsAmmoFull( void ) const
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::AreAllPointsUncontestedSoFar( void ) const
{
	if (g_hControlPointMasters.IsEmpty())
		return true;

	if (!g_hControlPointMasters[0].IsValid())
		return true;

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsNearPoint( CControlPoint *point ) const
{
	if ( !point )
		return false;

	CTFNavArea *myArea = (CTFNavArea*)GetLastKnownArea();
	if ( !myArea )
		return false;
	
	int iPointIdx = point->GetPointIndex();
	if ( iPointIdx < MAX_CONTROL_POINTS )
	{
		CTFNavArea *cpArea = TFNavMesh()->GetMainControlPointArea( iPointIdx );
		if ( !cpArea )
			return false;

		return abs( myArea->GetIncursionDistance( GetTeamNumber() ) - cpArea->GetIncursionDistance( GetTeamNumber() ) ) < doc_bot_near_point_travel_distance.GetFloat();
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Return a CP that we desire to defend or capture
//-----------------------------------------------------------------------------
CControlPoint *CTFBot::GetMyControlPoint( void )
{
	if (!m_hMyControlPoint || m_myCPValidDuration.IsElapsed())
	{
		m_myCPValidDuration.Start(RandomFloat(1.0f, 2.0f));

		CUtlVector<CControlPoint*> defensePoints;
		CUtlVector<CControlPoint*> attackPoints;
		CollectDefendPoints(&defensePoints);
		CollectCapturePoints(&attackPoints);

		if ((m_Shared.PlayerClass() == 3) && !defensePoints.IsEmpty())
		{
			CControlPoint* pPoint = SelectPointToDefend(defensePoints);
			if (pPoint)
			{
				m_hMyControlPoint = pPoint;
				return pPoint;
			}
		}
		else
		{
			CControlPoint* pPoint = SelectPointToCapture(attackPoints);
			if (pPoint)
			{
				m_hMyControlPoint = pPoint;
				return pPoint;
			}
			else
			{
				m_myCPValidDuration.Invalidate();

				pPoint = SelectPointToDefend(defensePoints);
				if (pPoint)
				{
					m_hMyControlPoint = pPoint;
					return pPoint;
				}
			}
		}

		m_myCPValidDuration.Invalidate();

		return nullptr;
	}

	return m_hMyControlPoint;

}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsAnyPointBeingCaptured( void ) const
{
	if (g_hControlPointMasters.IsEmpty())
		return false;

	CControlPointMaster* pMaster = g_hControlPointMasters[0];
	if (pMaster)
	{
		for (int i = 0; i < pMaster->GetNumPoints(); ++i)
		{
			CControlPoint* pPoint = pMaster->GetControlPoint(i);
			if (IsPointBeingContested(pPoint))
				return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsPointBeingContested( CControlPoint *point ) const
{
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const Vector& CTFBot::EstimateProjectileImpactPosition(CWeaponDODBaseGun* weapon)
{
	return (Vector)NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const Vector& CTFBot::EstimateStickybombProjectileImpactPosition(float pitch, float yaw, float charge)
{
	return (Vector)NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const Vector& CTFBot::EstimateProjectileImpactPosition(float pitch, float yaw, float initVel)
{
	VPROF_BUDGET(__FUNCTION__, "NextBot");

	Vector vecForward, vecRight, vecUp;
	QAngle angles(pitch, yaw, 0.0f);
	AngleVectors(angles, &vecForward, &vecRight, &vecUp);

	Vector vecSrc = Weapon_ShootPosition();
	vecSrc += vecForward * 16.0f + vecRight * 8.0f + vecUp * -6.0f;

	const float initVelScale = 0.9f;
	Vector      vecVelocity = initVelScale * ((vecForward * initVel) + (vecUp * 200.0f));

	Vector      pos = vecSrc;
	Vector      lastPos = pos;

	extern ConVar sv_gravity;
	const float g = sv_gravity.GetFloat();

	Vector alongDir = vecForward;
	alongDir.AsVector2D().NormalizeInPlace();

	float alongVel = vecVelocity.AsVector2D().Length();

	trace_t                        trace;
	NextBotTraceFilterIgnoreActors traceFilter(this, COLLISION_GROUP_NONE);
	const float timeStep = 0.01f;
	const float maxTime = 5.0f;

	float t = 0.0f;
	do
	{
		float along = alongVel * t;
		float height = vecVelocity.z * t - 0.5f * g * Square(t);

		pos.x = vecSrc.x + alongDir.x * along;
		pos.y = vecSrc.y + alongDir.y * along;
		pos.z = vecSrc.z + height;

		UTIL_TraceHull(lastPos, pos, -Vector(8, 8, 8), Vector(8, 8, 8), MASK_SOLID_BRUSHONLY, &traceFilter, &trace);

		if (trace.DidHit())
			break;

		lastPos = pos;
		t += timeStep;
	} while (t < maxTime);

	return trace.endpos;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsCapturingPoint(void)
{
	CAreaCapture* pCapArea = GetControlPointStandingOn();
	if (pCapArea)
	{
		CControlPoint* pPoint = pCapArea->GetControlPoint();
		if (pPoint && DODGameRules()->TeamMayCapturePoint(GetTeamNumber(), pPoint->GetPointIndex()) &&
			DODGameRules()->PlayerMayCapturePoint(this, pPoint->GetPointIndex()))
		{
			return pPoint->GetOwner() != GetTeamNumber();
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CAreaCapture * CTFBot::GetControlPointStandingOn(void)
{
	touchlink_t* root = (touchlink_t*)GetDataObject(TOUCHLINK);
	if (root)
	{
		touchlink_t* next = root->nextLink;
		while (next != root)
		{
			CBaseEntity* pEntity = next->entityTouched;
			if (!pEntity)
				return NULL;

			if (pEntity->IsSolidFlagSet(FSOLID_TRIGGER) && pEntity->IsBSPModel())
			{
				CAreaCapture* pCapArea = dynamic_cast<CAreaCapture*>(pEntity);
				if (pCapArea)
					return pCapArea;
			}

			next = next->nextLink;
		}
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CTFBot::GetTimeLeftToCapture( void )
{
	return 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CControlPoint *CTFBot::SelectPointToCapture( CUtlVector<CControlPoint *> const &candidates )
{
	if (candidates.IsEmpty())
		return nullptr;

	if (candidates.Count() == 1)
		return candidates[0];

	if (IsCapturingPoint())
	{
		CAreaCapture* pCapArea = GetControlPointStandingOn();
		if (pCapArea)
			return pCapArea->GetControlPoint();
	}

	CControlPoint* pClose = SelectClosestPointByTravelDistance(candidates);
	if (pClose && IsPointBeingContested(pClose))
		return pClose;

	float flMaxDanger = FLT_MIN;
	bool bInCombat = false;
	CControlPoint* pDangerous = nullptr;

	for (int i = 0; i < candidates.Count(); ++i)
	{
		CControlPoint* pPoint = candidates[i];
		if (IsPointBeingContested(pPoint))
			return pPoint;

		CTFNavArea* pCPArea = TFNavMesh()->GetMainControlPointArea(pPoint->GetPointIndex());
		if (pCPArea == nullptr)
			continue;

		float flDanger = pCPArea->GetCombatIntensity();
		bInCombat = flDanger > 0.1f ? true : false;

		if (flMaxDanger < flDanger)
		{
			flMaxDanger = flDanger;
			pDangerous = pPoint;
		}
	}

	if (bInCombat)
		return pDangerous;

	// Probaly some Min/Max going on here
	int iSelection = candidates.Count() - 1;
	if (iSelection >= 0)
	{
		int iRandSel = candidates.Count() * TransientlyConsistentRandomValue(60.0f, 0);
		if (iRandSel < 0)
			return candidates[0];

		if (iRandSel <= iSelection)
			iSelection = iRandSel;
	}

	return candidates[iSelection];

}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CControlPoint *CTFBot::SelectPointToDefend( CUtlVector<CControlPoint *> const &candidates )
{
	if ( candidates.IsEmpty() )
		return nullptr;

	if ( ( m_nBotAttrs & CTFBot::AttributeType::DISABLEDODGE ) != 0 )
		return SelectClosestPointByTravelDistance( candidates );

	return candidates.Random();
}

//-----------------------------------------------------------------------------
// Purpose: Return the closest control point to us
//-----------------------------------------------------------------------------
CControlPoint *CTFBot::SelectClosestPointByTravelDistance( CUtlVector<CControlPoint *> const &candidates ) const
{
	CControlPoint *pClosest = nullptr;
	float flMinDist = FLT_MAX;
	CDODPlayerPathCost cost( (CDODPlayer *)this );

	if ( GetLastKnownArea() )
	{
		for ( int i=0; i<candidates.Count(); ++i )
		{
			CTFNavArea *pCPArea = TFNavMesh()->GetMainControlPointArea( candidates[i]->GetPointIndex() );
			if (pCPArea) {

				float flDist = NavAreaTravelDistance(GetLastKnownArea(), pCPArea, cost);

				if (flDist >= 0.0f && flMinDist > flDist)
				{
					flMinDist = flDist;
					pClosest = candidates[i];
				}

				return pClosest;
			}
		}
	}
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CCaptureZone *CTFBot::GetFlagCaptureZone( void )
{
	return m_hMyCaptureZone;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CCaptureFlag *CTFBot::GetFlagToFetch( void )
{
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsLineOfFireClear( CBaseEntity *to )
{
	return IsLineOfFireClear( EyePosition(), to );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsLineOfFireClear( const Vector &to )
{
	return IsLineOfFireClear( EyePosition(), to );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsLineOfFireClear( const Vector &from, CBaseEntity *to )
{
	NextBotTraceFilterIgnoreActors filter( nullptr, COLLISION_GROUP_NONE );

	trace_t trace;
	UTIL_TraceLine( from, to->WorldSpaceCenter(), MASK_SOLID_BRUSHONLY, &filter, &trace );

	return !trace.DidHit() || trace.m_pEnt == to;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsLineOfFireClear( const Vector &from, const Vector &to )
{
	NextBotTraceFilterIgnoreActors filter( nullptr, COLLISION_GROUP_NONE );

	trace_t trace;
	UTIL_TraceLine( from, to, MASK_SOLID_BRUSHONLY, &filter, &trace );

	return !trace.DidHit();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsAnyEnemySentryAbleToAttackMe( void ) const
{
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsThreatAimingTowardsMe( CBaseEntity *threat, float dotTolerance ) const
{
	if ( threat == nullptr )
		return false;

	Vector vecToActor = GetAbsOrigin() - threat->GetAbsOrigin();
	vecToActor.NormalizeInPlace();

	CDODPlayer *player = ToDODPlayer( threat );
	if ( player )
	{
		Vector fwd;
		player->EyeVectors( &fwd );

		return vecToActor.Dot( fwd ) > dotTolerance;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsThreatFiringAtMe( CBaseEntity *threat ) const
{
	if ( !IsThreatAimingTowardsMe( threat ) )
		return false;

	// looking at me, but has it shot at me yet
	if ( threat->IsPlayer() )
		return ( (CBasePlayer *)threat )->IsFiringWeapon();

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsEntityBetweenTargetAndSelf( CBaseEntity *blocker, CBaseEntity *target ) const
{
	Vector vecToTarget = ( target->GetAbsOrigin() - GetAbsOrigin() );
	Vector vecToEntity = ( blocker->GetAbsOrigin() - GetAbsOrigin() );
	if ( vecToEntity.NormalizeInPlace() < vecToTarget.NormalizeInPlace() )
	{
		if ( vecToTarget.Dot( vecToEntity ) > 0.7071f )
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CTFBot::TransientlyConsistentRandomValue( float duration, int seed ) const
{
	CTFNavArea *area = (CTFNavArea*)GetLastKnownArea();
	if ( area == nullptr )
	{
		return 0.0f;
	}

	int time_seed = (int)( gpGlobals->curtime / duration ) + 1;
	seed += ( area->GetID() * time_seed * entindex() );

	return fabs( FastCos( (float)seed ) );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CTFBot::GetMaxAttackRange() const
{
	CWeaponDODBase *weapon = GetActiveDODWeapon();
	if ( weapon == nullptr )
	{
		return 0.0f;
	}

	if ( weapon->IsMeleeWeapon() )
	{
		return 100.0f;
	}

	if ( IsExplosiveProjectileWeapon( weapon ) )
	{
		return 3000.0f;
	}

	return FLT_MAX;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CTFBot::GetDesiredAttackRange( void ) const
{
	CWeaponDODBase *weapon = GetActiveDODWeapon();
	if ( weapon == nullptr )
		return 0.0f;

	if ( !weapon->IsMeleeWeapon() )
	{
		return FLT_MAX;
	}

	return 100.0f;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CTFBot::GetDesiredPathLookAheadRange( void ) const
{
	return GetModelScale() * doc_bot_path_lookahead_range.GetFloat();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsDebugFilterMatch( const char *name ) const
{
	return INextBot::IsDebugFilterMatch( name );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::PressFireButton( float duration )
{
	BaseClass::PressFireButton( duration );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::PressAltFireButton( float duration )
{
	BaseClass::PressAltFireButton( duration );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::PressSpecialFireButton( float duration )
{
	BaseClass::PressSpecialFireButton( duration );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CTFNavArea *CTFBot::FindVantagePoint( float flMaxDist )
{
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::DelayedThreatNotice( CHandle<CBaseEntity> ent, float delay )
{
	const float when = gpGlobals->curtime + delay;

	FOR_EACH_VEC( m_delayedThreatNotices, i )
	{
		DelayedNoticeInfo *info = &m_delayedThreatNotices[i];

		if ( ent == info->m_hEnt )
		{
			if ( when < info->m_flWhen )
			{
				info->m_flWhen = when;
			}

			return;
		}
	}

	m_delayedThreatNotices.AddToTail( {ent, delay} );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::UpdateDelayedThreatNotices()
{
	FOR_EACH_VEC_BACK( m_delayedThreatNotices, i )
	{
		DelayedNoticeInfo *info = &m_delayedThreatNotices[i];

		if ( gpGlobals->curtime >= info->m_flWhen )
		{
			CBaseEntity *ent = info->m_hEnt;
			if ( ent )
			{
				GetVisionInterface()->AddKnownEntity( ent );
			}

			m_delayedThreatNotices.Remove( i );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CTFBot::SuspectedSpyInfo *CTFBot::IsSuspectedSpy( CDODPlayer *spy )
{
	FOR_EACH_VEC( m_suspectedSpies, i )
	{
		SuspectedSpyInfo *info = m_suspectedSpies[i];
		if ( info->m_hSpy == spy )
		{
			return info;
		}
	}

	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::SuspectSpy( CDODPlayer *spy )
{
	SuspectedSpyInfo *info = IsSuspectedSpy( spy );
	if ( info == nullptr )
	{
		info = new SuspectedSpyInfo;
		info->m_hSpy = spy;
		m_suspectedSpies.AddToHead( info );
	}

	info->Suspect();
	if ( info->TestForRealizing() )
	{
		RealizeSpy( spy );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::StopSuspectingSpy( CDODPlayer *spy )
{
	FOR_EACH_VEC( m_suspectedSpies, i )
	{
		SuspectedSpyInfo *info = m_suspectedSpies[i];
		if ( info->m_hSpy == spy )
		{
			delete info;
			m_suspectedSpies.Remove( i );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsKnownSpy( CDODPlayer *spy ) const
{
	return m_knownSpies.HasElement( spy );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::RealizeSpy( CDODPlayer *spy )
{
	if ( IsKnownSpy( spy ) )
		return;

	m_knownSpies.AddToHead( spy );

	SuspectedSpyInfo *info = IsSuspectedSpy( spy );
	if ( info && info->IsCurrentlySuspected() )
	{
		CUtlVector<CDODPlayer *> teammates;
		CollectPlayers( &teammates, GetTeamNumber(), true );

		FOR_EACH_VEC( teammates, i )
		{
			CTFBot *teammate = ToTFBot( teammates[i] );
			if ( teammate && !teammate->IsKnownSpy( spy ) )
			{
				if ( EyePosition().DistToSqr( teammate->EyePosition() ) < Square( 512.0f ) )
				{
					teammate->SuspectSpy( spy );
					teammate->RealizeSpy( spy );
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::ForgetSpy( CDODPlayer *spy )
{
	StopSuspectingSpy( spy );

	CHandle<CDODPlayer> hndl( spy );
	m_knownSpies.FindAndFastRemove( hndl );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::UpdateLookingAroundForEnemies( void )
{
	if ( !m_bLookingAroundForEnemies || ( m_nBotAttrs & AttributeType::DONTLOOKAROUND ) == AttributeType::DONTLOOKAROUND )
		return;

	const CKnownEntity *threat = GetVisionInterface()->GetPrimaryKnownThreat();
	if ( !threat || !threat->GetEntity() )
	{
		UpdateLookingForIncomingEnemies( true );
		return;
	}

	if ( threat->IsVisibleInFOVNow() )
	{
		GetBodyInterface()->AimHeadTowards( threat->GetEntity(), IBody::CRITICAL, 1.0f, nullptr, "Aiming at a visible threat" );
		return;
	}
	else if ( IsLineOfSightClear( threat->GetEntity(), CBaseCombatCharacter::IGNORE_ACTORS ) )
	{
		// ???
		Vector vecToThreat = threat->GetEntity()->GetAbsOrigin() - GetAbsOrigin();
		float sin, trash;
		FastSinCos( BitsToFloat( 0x3F060A92 ), &sin, &trash );
		float flAdjustment = vecToThreat.NormalizeInPlace() * sin;

		Vector vecToTurnTo = threat->GetEntity()->WorldSpaceCenter() + Vector( RandomFloat( -flAdjustment, flAdjustment ), RandomFloat( -flAdjustment, flAdjustment ), 0 );

		GetBodyInterface()->AimHeadTowards( vecToTurnTo, IBody::IMPORTANT, 1.0f, nullptr, "Turning around to find threat out of our FOV" );
		return;
	}

	UpdateLookingForIncomingEnemies( true );

	CTFNavArea *pArea = (CTFNavArea*)GetLastKnownArea();
	if ( pArea )
	{
		SelectClosestPotentiallyVisible functor( threat->GetLastKnownPosition() );
		pArea->ForAllPotentiallyVisibleAreas( functor );

		if ( functor.m_pSelected )
		{
			for ( int i = 0; i < 10; ++i )
			{
				const Vector vSpot = functor.m_pSelected->GetRandomPoint() + Vector( 0, 0, HumanHeight * 0.75f );
				if ( GetVisionInterface()->IsLineOfSightClear( vSpot ) )
				{
					GetBodyInterface()->AimHeadTowards( vSpot, IBody::IMPORTANT, 1.0f, nullptr, "Looking toward potentially visible area near known but hidden threat" );
					return;
				}
			}

			DebugConColorMsg( NEXTBOT_ERRORS|NEXTBOT_VISION, Color( 0xFF, 0xFF, 0, 0xFF ), "%3.2f: %s can't find clear line to look at potentially visible near known but hidden entity %s(#%d)\n",
				gpGlobals->curtime, GetPlayerName(), threat->GetEntity()->GetClassname(), ENTINDEX( threat->GetEntity() ) );

			return;
		}
	}

	DebugConColorMsg( NEXTBOT_ERRORS|NEXTBOT_VISION, Color( 0xFF, 0xFF, 0, 0xFF ), "%3.2f: %s no potentially visible area to look toward known but hidden entity %s(#%d)\n",
		gpGlobals->curtime, GetPlayerName(), threat->GetEntity()->GetClassname(), ENTINDEX( threat->GetEntity() ) );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::UpdateLookingForIncomingEnemies( bool enemy )
{
	if ( !m_lookForEnemiesTimer.IsElapsed() )
		return;

	m_lookForEnemiesTimer.Start( RandomFloat( 0.3f, 1.0f ) );

	CTFNavArea *area = (CTFNavArea*)GetLastKnownArea();
	if ( area == nullptr )
		return;

	int iTeam = enemy ? GetTeamNumber() : GetEnemyTeam( this );
	// really shouldn't happen
	if ( iTeam < 0 || iTeam > 3 )
		iTeam = 0;

	DebugConColorMsg( NEXTBOT_ERRORS|NEXTBOT_VISION, Color( 0xFF, 0, 0, 0xFF ), "%3.2f: %s no invasion areas to look toward to predict oncoming enemies\n",
		gpGlobals->curtime, GetPlayerName() );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::EquipBestWeaponForThreat( const CKnownEntity *threat )
{
	if ( !threat )
		return false;
	if (EquipRequiredWeapon())
		return false;

		CWeaponDODBase *primary = dynamic_cast<CWeaponDODBase *>( Weapon_GetSlot( 0 ) );
		CWeaponDODBase *secondary = dynamic_cast<CWeaponDODBase *>( Weapon_GetSlot( 1 ) );
		CWeaponDODBase *melee = dynamic_cast<CWeaponDODBase *>( Weapon_GetSlot( 2 ) );

		if ( !IsCombatWeapon( primary ) )
			primary = nullptr;
		if ( !IsCombatWeapon( secondary ) )
			secondary = nullptr;
		if ( !IsCombatWeapon( melee ) )
			melee = nullptr;

		CWeaponDODBase *pWeapon = primary;
		if ( !primary )
		{
			pWeapon = secondary;
			if ( !secondary )
				pWeapon = melee;
		}

		if ( pWeapon )
			return Weapon_Switch( pWeapon );

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Swap to a weapon our class uses for range
//-----------------------------------------------------------------------------
bool CTFBot::EquipLongRangeWeapon( void )
{	// This is so terrible
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::PushRequiredWeapon( CWeaponDODBase *weapon )
{
	CHandle<CWeaponDODBase> hndl;
	if ( weapon ) hndl.Set( weapon );

	m_requiredEquipStack.AddToTail( hndl );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::PopRequiredWeapon( void )
{
	m_requiredEquipStack.RemoveMultipleFromTail( 1 );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::EquipRequiredWeapon( void )
{
	if (TheTFBots().IsMeleeOnly())
	{
		// force use of melee weapons
		Weapon_Switch(Weapon_GetSlot(WPN_SLOT_MELEE));
		return true;
	}

	if ( m_requiredEquipStack.Count() <= 0 )
		return false;

	CHandle<CWeaponDODBase> &hndl = m_requiredEquipStack.Tail();
	CWeaponDODBase *weapon = hndl.Get();

	return Weapon_Switch( weapon );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFBot::IsSquadmate( CDODPlayer *player ) const
{
	if ( m_pSquad == nullptr )
		return false;

	CTFBot *bot = ToTFBot( player );
	if ( bot )
		return m_pSquad == bot->m_pSquad;

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::JoinSquad( CTFBotSquad *squad )
{
	if ( squad )
	{
		squad->Join( this );
		m_pSquad = squad;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::LeaveSquad( void )
{
	if ( m_pSquad )
	{
		m_pSquad->Leave( this );
		m_pSquad = nullptr;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::AccumulateSniperSpots( void )
{
	VPROF_BUDGET( __FUNCTION__, "NextBot" );

	SetupSniperSpotAccumulation();

	if ( m_sniperStandAreas.IsEmpty() || m_sniperLookAreas.IsEmpty() )
	{
		if ( m_sniperSpotTimer.IsElapsed() )
			ClearSniperSpots();

		return;
	}

	for ( int i=0; i<doc_bot_sniper_spot_search_count.GetInt(); ++i )
	{
		SniperSpotInfo newInfo{};
		newInfo.m_pHomeArea = m_sniperStandAreas.Random();
		newInfo.m_vecHome = newInfo.m_pHomeArea->GetRandomPoint();
		newInfo.m_pForwardArea = m_sniperLookAreas.Random();
		newInfo.m_vecForward = newInfo.m_pForwardArea->GetRandomPoint();

		newInfo.m_flRange = ( newInfo.m_vecHome - newInfo.m_vecForward ).Length();

		if ( newInfo.m_flRange < doc_bot_sniper_spot_min_range.GetFloat() )
			continue;

		if ( !IsLineOfFireClear( newInfo.m_vecHome + Vector( 0, 0, 60.0f ), newInfo.m_vecForward + Vector( 0, 0, 60.0f ) ) )
			continue;

		float flIncursion1 = newInfo.m_pHomeArea->GetIncursionDistance( GetEnemyTeam( this ) );
		float flIncursion2 = newInfo.m_pForwardArea->GetIncursionDistance( GetEnemyTeam( this ) );

		newInfo.m_flIncursionDiff = flIncursion1 - flIncursion2;

		if ( m_sniperSpots.Count() < doc_bot_sniper_spot_max_count.GetInt() )
			m_sniperSpots.AddToTail( newInfo );

		for ( int j=0; j<m_sniperSpots.Count(); ++j )
		{
			SniperSpotInfo *info = &m_sniperSpots[j];

			if ( flIncursion1 - flIncursion2 <= info->m_flIncursionDiff )
				continue;

			*info = newInfo;
		}
	}

	if ( IsDebugging( NEXTBOT_BEHAVIOR ) )
	{
		for ( int i=0; i<m_sniperSpots.Count(); ++i )
		{
			NDebugOverlay::Cross3D( m_sniperSpots[i].m_vecHome, 5.0f, 255, 0, 255, true, 0.1f );
			NDebugOverlay::Line( m_sniperSpots[i].m_vecHome, m_sniperSpots[i].m_vecForward, 0, 200, 0, true, 0.1f );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::SetupSniperSpotAccumulation( void )
{
	VPROF_BUDGET( __FUNCTION__, "NextBot" );

	CBaseEntity *pObjective = nullptr;

	pObjective = GetMyControlPoint();

	if ( pObjective == nullptr )
	{
		ClearSniperSpots();
		return;
	}

	if ( pObjective == m_sniperGoalEnt && Square( doc_bot_sniper_goal_entity_move_tolerance.GetFloat() ) > ( pObjective->WorldSpaceCenter() - m_sniperGoal ).LengthSqr() )
		return;

	ClearSniperSpots();

	const int iMyTeam = GetTeamNumber();
	const int iEnemyTeam = GetEnemyTeam( this );
	bool bCheckForward = false;
	CTFNavArea *pObjectiveArea = nullptr;

	m_sniperStandAreas.RemoveAll();
	m_sniperLookAreas.RemoveAll();

	if (GetMyControlPoint()->GetPointIndex() >= MAX_CONTROL_POINTS)
		return;

	pObjectiveArea = TFNavMesh()->GetMainControlPointArea(GetMyControlPoint()->GetPointIndex());
	bCheckForward = GetMyControlPoint()->GetOwner() == iMyTeam;

	if ( !pObjectiveArea )
		return;

	for ( int i=0; i<TheNavAreas.Count(); ++i )
	{
		CTFNavArea *area = static_cast<CTFNavArea *>( TheNavAreas[i] );

		float flMyIncursion = area->GetIncursionDistance( iMyTeam );
		if ( flMyIncursion < 0.0f )
			continue;

		float flEnemyIncursion = area->GetIncursionDistance( iEnemyTeam );
		if ( flEnemyIncursion < 0.0f )
			continue;

		if ( flEnemyIncursion <= pObjectiveArea->GetIncursionDistance( iEnemyTeam ) )
			m_sniperLookAreas.AddToTail( area );

		if ( bCheckForward )
		{
			if ( pObjectiveArea->GetIncursionDistance( iMyTeam ) + doc_bot_sniper_spot_point_tolerance.GetFloat() >= flMyIncursion )
				m_sniperStandAreas.AddToTail( area );
		}
		else
		{
			if ( pObjectiveArea->GetIncursionDistance( iMyTeam ) - doc_bot_sniper_spot_point_tolerance.GetFloat() >= flMyIncursion )
				m_sniperStandAreas.AddToTail( area );
		}
	}

	m_sniperGoalEnt = pObjective;
	m_sniperGoal = pObjective->WorldSpaceCenter();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::ClearSniperSpots( void )
{
	m_sniperSpots.Purge();
	m_sniperStandAreas.RemoveAll();
	m_sniperLookAreas.RemoveAll();
	m_sniperGoalEnt = nullptr;

	m_sniperSpotTimer.Start( RandomFloat( 5.0f, 10.0f ) );
}

//-----------------------------------------------------------------------------
// Purpose: Seperate ourselves with minor push forces from teammates
//-----------------------------------------------------------------------------
void CTFBot::AvoidPlayers( CUserCmd *pCmd )
{
	Vector vecFwd, vecRight;
	this->EyeVectors( &vecFwd, &vecRight );

	Vector vecAvoidCenter = vec3_origin;
	const float flRadius = 50.0;

	CUtlVector<CDODPlayer *> teammates;
	CollectPlayers( &teammates, GetTeamNumber(), true );
	for ( int i=0; i<teammates.Count(); i++ )
	{
		if ( IsSelf( teammates[i] ) )
			continue;

		Vector vecToTeamMate = GetAbsOrigin() - teammates[i]->GetAbsOrigin();
		if ( Square( flRadius ) > vecToTeamMate.LengthSqr() )
		{
			vecAvoidCenter += vecToTeamMate.Normalized() * ( 1.0f - ( 1.0f / flRadius ) );
		}
	}

	if ( !vecAvoidCenter.IsZero() )
	{
		vecAvoidCenter.NormalizeInPlace();

		pCmd->forwardmove += vecAvoidCenter.Dot( vecFwd ) * flRadius;
		pCmd->sidemove += vecAvoidCenter.Dot( vecRight ) * flRadius;
	}
}

//-----------------------------------------------------------------------------
// Purpose: If we were assigned to take over a real player, return them
//-----------------------------------------------------------------------------
CBaseCombatCharacter *CTFBot::GetEntity( void ) const
{
	return ToBasePlayer( m_controlling ) ? m_controlling : (CDODPlayer *)this;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFBot::SelectReachableObjects( CUtlVector<EHANDLE> const &knownHealth, CUtlVector<EHANDLE> *outVector, INextBotFilter const &func, CNavArea *pStartArea, float flMaxRange )
{
	if ( !pStartArea || !outVector )
		return;

	CUtlVector<EHANDLE> selectedHealths;
	for ( int i=0; i<knownHealth.Count(); ++i )
	{
		CBaseEntity *pEntity = knownHealth[i];
		if ( !pEntity || !func.IsSelected( pEntity ) )
			continue;

		EHANDLE hndl( pEntity );
		selectedHealths.AddToTail( hndl );
	}

	outVector->RemoveAll();

	CollectReachableObjects collector( this, &selectedHealths, outVector, flMaxRange );
	SearchSurroundingAreas( pStartArea, collector, flMaxRange );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CDODPlayer *CTFBot::SelectRandomReachableEnemy( void )
{
	CUtlVector<CDODPlayer *> enemies;
	CollectPlayers( &enemies, GetEnemyTeam( this ), true );

	CUtlVector<CDODPlayer *> validEnemies;
	for ( int i=0; i<enemies.Count(); ++i )
	{
		CDODPlayer *pEnemy = enemies[i];

		validEnemies.AddToTail( pEnemy );
	}

	if ( !validEnemies.IsEmpty() )
		return validEnemies.Random();

	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Can we change class? If nested or have uber then no
//-----------------------------------------------------------------------------
bool CTFBot::CanChangeClass( void )
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CTFBot::GetNextSpawnClassname( void )
{
	return RandomInt(0, 5);
}

CTFBotPathCost::CTFBotPathCost( CTFBot *actor, RouteType routeType )
	: m_Actor( actor ), m_iRouteType( routeType )
{
	const ILocomotion *loco = m_Actor->GetLocomotionInterface();
	m_flStepHeight = loco->GetStepHeight();
	m_flMaxJumpHeight = loco->GetMaxJumpHeight();
	m_flDeathDropHeight = loco->GetDeathDropHeight();
}

float CTFBotPathCost::operator()( CNavArea *area, CNavArea *fromArea, const CNavLadder *ladder, const CFuncElevator *elevator, float length ) const
{
	VPROF_BUDGET( __FUNCTION__, "NextBot" );

	if ( fromArea == nullptr )
	{
		// first area in path; zero cost
		return 0.0f;
	}

	if ( !m_Actor->GetLocomotionInterface()->IsAreaTraversable( area ) )
	{
		// dead end
		return -1.0f;
	}

	float fDist;
	if ( ladder != nullptr )
		fDist = ladder->m_length;
	else if ( length != 0.0f )
		fDist = length;
	else
		fDist = ( area->GetCenter() - fromArea->GetCenter() ).Length();

	const float dz = fromArea->ComputeAdjacentConnectionHeightChange( area );
	if ( dz >= m_flStepHeight )
	{
		// too high!
		if ( dz >= m_flMaxJumpHeight )
			return -1.0f;

		// jumping is slow
		fDist *= 2;
	}
	else
	{
		// yikes, this drop will hurt too much!
		if ( dz < -m_flDeathDropHeight )
			return -1.0f;
	}

	// consistently random pathing with huge cost modifier
	float fMultiplier = 1.0f;
	if ( m_iRouteType == DEFAULT_ROUTE )
	{
		const float rand = m_Actor->TransientlyConsistentRandomValue( 10.0f, 0 );
		fMultiplier += ( rand + 1.0f ) * 50.0f;
	}

	float fCost = fDist * fMultiplier;

	if ( area->HasAttributes( NAV_MESH_FUNC_COST ) )
		fCost *= area->ComputeFuncNavCost( m_Actor );

	return fromArea->GetCostSoFar() + fCost;
}


void DifficultyChanged( IConVar *var, const char *pOldValue, float flOldValue )
{
	if (doc_bot_difficulty.GetInt() >= CTFBot::EASY && doc_bot_difficulty.GetInt() <= CTFBot::EXPERT )
	{
		CUtlVector<INextBot *> bots;
		TheNextBots().CollectAllBots( &bots );
		for ( int i=0; i<bots.Count(); ++i )
		{
			CTFBot *pBot = dynamic_cast<CTFBot *>( bots[i]->GetEntity() );
			if ( pBot == nullptr )
				continue;

			pBot->m_iSkill = (CTFBot::DifficultyType)doc_bot_difficulty.GetInt();
		}
	}
	else
		Warning( "doc_bot_difficulty value out of range [0,4]: %d", doc_bot_difficulty.GetInt() );
}

void PrefixNameChanged( IConVar *var, const char *pOldValue, float flOldValue )
{
	CUtlVector<INextBot *> bots;
	TheNextBots().CollectAllBots( &bots );
	for ( int i=0; i<bots.Count(); ++i )
	{
		CTFBot *pBot = dynamic_cast<CTFBot *>( bots[i]->GetEntity() );
		if ( pBot == nullptr )
			continue;

		if (doc_bot_prefix_name_with_difficulty.GetBool() )
		{
			const char *szSkillName = DifficultyToName( pBot->m_iSkill );
			const char *szCurrentName = pBot->GetPlayerName();

			engine->SetFakeClientConVarValue( pBot->edict(), "name", CFmtStr( "%s%s", szSkillName, szCurrentName ) );
		}
		else
		{
			const char *szSkillName = DifficultyToName( pBot->m_iSkill );
			const char *szCurrentName = pBot->GetPlayerName();

			engine->SetFakeClientConVarValue( pBot->edict(), "name", &szCurrentName[Q_strlen( szSkillName )] );
		}
	}
}


CON_COMMAND_F( doc_bot_add, "Add a bot.", FCVAR_GAMEDLL )
{
	if ( UTIL_IsCommandIssuedByServerAdmin() )
	{
		int count = Clamp( Q_atoi( args.Arg( 1 ) ), 1, gpGlobals->maxClients );
		for ( int i = 0; i < count; ++i )
		{
			char szBotName[64];
			if ( args.ArgC() > 4 )
				Q_snprintf( szBotName, sizeof szBotName, args.Arg( 4 ) );
			else
				V_strcpy_safe( szBotName, TheTFBots().GetRandomBotName() );

			CTFBot *bot = NextBotCreatePlayerBot<CTFBot>( szBotName );
			if ( bot == nullptr )
				return;

			char szTeam[10];
			if ( args.ArgC() > 2 )
			{
				if ( IsTeamName( args.Arg( 2 ) ) )
					Q_snprintf( szTeam, sizeof szTeam, args.Arg( 2 ) );
				else
				{
					Warning( "Invalid argument '%s'\n", args.Arg( 2 ) );
					Q_snprintf( szTeam, sizeof szTeam, "auto" );
				}
			}
			else
				Q_snprintf( szTeam, sizeof szTeam, "auto" );

			bot->HandleCommand_JoinTeam( DODGameRules()->SelectDefaultTeam() );

			char szClassName[16];
			if ( args.ArgC() > 3 )
			{
				if ( IsPlayerClassName( args.Arg( 3 ) ) )
					Q_snprintf( szClassName, sizeof szClassName, args.Arg( 3 ) );
				else
				{
					Warning( "Invalid argument '%s'\n", args.Arg( 3 ) );
					Q_snprintf( szClassName, sizeof szClassName, "random" );
				}
			}
			else
				Q_snprintf( szClassName, sizeof szClassName, "random" );

			bot->HandleCommand_JoinClass(RandomInt(0, 5));
		}

		TheTFBots().OnForceAddedBots( count );
	}
}

CON_COMMAND_F(doc_bot_add_new, "Add a bot. (currently crashes the game)", FCVAR_GAMEDLL)
{
	if (UTIL_IsCommandIssuedByServerAdmin())
	{
		char const* pszBotName = NULL;
		char const* pszTeamName = "auto";
		char const* pszClassName = "random";
		int nNumBots = 1;
		bool bNoQuota = false;
		int nSkill = doc_bot_difficulty.GetInt();

		for (int i = 0; i < args.ArgC(); ++i)
		{
			int nParsedSkill = NameToDifficulty(args[i]);
			int nParsedNumBots = V_atoi(args[i]);

			if (IsPlayerClassName(args[i]))
			{
				pszClassName = args[i];
			}
			else if (IsTeamName(args[i]))
			{
				pszTeamName = args[i];
			}
			else if (!V_stricmp(args[i], "noquota"))
			{
				bNoQuota = true;
			}
			else if (nParsedSkill != -1)
			{
				nSkill = nParsedSkill;
			}
			else if (nParsedNumBots >= 1)
			{
				nNumBots = nParsedNumBots;
			}
			else if (nNumBots == 1)
			{
				pszBotName = args[i];
			}
			else
			{
				Warning("Invalid argument '%s'\n", args[i]);
			}
		}

		pszClassName = FStrEq(doc_bot_force_class.GetString(), "") ? pszClassName : doc_bot_force_class.GetString();

		int iTeam = TEAM_UNASSIGNED;
		if (FStrEq(pszTeamName, "axis"))
			iTeam = TEAM_AXIS;
		else if (FStrEq(pszTeamName, "allies"))
			iTeam = TEAM_ALLIES;

		nNumBots = Clamp(nNumBots, 1, gpGlobals->maxClients);
		char szBotName[128]; int nCount = 0;
		for (int i = 0; i < nNumBots; ++i)
		{
			if (pszBotName == NULL)
				CreateBotName(iTeam, GetClassIndexFromString(pszClassName), nSkill, szBotName, sizeof szBotName);
			else
				V_strcpy_safe(szBotName, pszBotName);

			CTFBot* pBot = NextBotCreatePlayerBot<CTFBot>(pszBotName);
			if (pBot == nullptr)
				break;
			
			pBot->HandleCommand_JoinTeam(DODGameRules()->SelectDefaultTeam());
			pBot->m_iSkill = (CTFBot::DifficultyType)nSkill;

			nCount++;
		}

		if (!bNoQuota)
			TheTFBots().OnForceAddedBots(nCount);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Only removes INextBots that are CTFBot derivatives with the CTFBotManager
//-----------------------------------------------------------------------------
class TFBotDestroyer
{
public:
	TFBotDestroyer( int team=TEAM_ANY ) : m_team( team ) { }

	bool operator()( CBaseCombatCharacter *bot )
	{
		if ( m_team == TEAM_ANY || bot->GetTeamNumber() == m_team )
		{
			CTFBot *pBot = ToTFBot( bot->GetBaseEntity() );
			if ( pBot == nullptr )
				return true;

			engine->ServerCommand( UTIL_VarArgs( "kickid %d\n", pBot->GetUserID() ) );
			TheTFBots().OnForceKickedBots( 1 );
		}

		return true;
	}

private:
	int m_team;
};

CON_COMMAND_F( doc_bot_kick, "Remove a TFBot by name, or all bots (\"all\").", FCVAR_GAMEDLL )
{
	if ( UTIL_IsCommandIssuedByServerAdmin() )
	{
		const char *arg = args.Arg( 1 );
		if ( !Q_strncmp( arg, "all", 3 ) )
		{
			TFBotDestroyer func;
			TheNextBots().ForEachCombatCharacter( func );
		}
		else
		{
			CBasePlayer *pBot = UTIL_PlayerByName( arg );
			if ( pBot && pBot->IsFakeClient() )
			{
				engine->ServerCommand( UTIL_VarArgs( "kickid %d\n", pBot->GetUserID() ) );
				TheTFBots().OnForceKickedBots( 1 );
			}
			else if ( IsTeamName( arg ) )
			{
				TFBotDestroyer func;
				if ( !Q_stricmp( arg, "red" ) )
					func = TFBotDestroyer( TEAM_ALLIES );
				else if ( !Q_stricmp( arg, "blue" ) )
					func = TFBotDestroyer( TEAM_AXIS );

				TheNextBots().ForEachCombatCharacter( func );
			}
			else
			{
				Msg( "No bot or team with that name\n" );
			}
		}
	}
}
CON_COMMAND_F( doc_bot_kill, "Kill a TFBot by name, or all bots (\"all\").", FCVAR_GAMEDLL )
{
	// Listenserver host or rcon access only!
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	if ( args.ArgC() < 2 )
	{
		DevMsg( "%s <bot name>, \"red\", \"blue\", or \"all\"> <optional: \"moveToSpectatorTeam\"> \n", args.Arg(0) );
		return;
	}

	int iTeam = TEAM_UNASSIGNED;
	int i;
	const char *pPlayerName = "";
	for( i=1; i<args.ArgC(); ++i )
	{
		// each argument could be a classname, a team, or a count
		if ( FStrEq( args.Arg(i), "allies" ) )
		{
			iTeam = TEAM_ALLIES;
		}
		else if ( FStrEq( args.Arg(i), "axis" ) )
		{
			iTeam = TEAM_AXIS;
		}
		else if ( FStrEq( args.Arg(i), "all" ) )
		{
			iTeam = TEAM_ANY;
		}
		else if ( FStrEq( args.Arg(i), "moveToSpectatorTeam" ) )
		{
			// bMoveToSpectatorTeam = true;
		}
		else 
		{
			pPlayerName = args.Arg(i);
		}
	}

	for( int i=1; i<=gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = static_cast<CBasePlayer *>( UTIL_PlayerByIndex( i ) );

		if ( !player )
			continue;

		if ( FNullEnt( player->edict() ) )
			continue;

		if ( player->MyNextBotPointer() )
		{
			if ( iTeam == TEAM_ANY ||
				FStrEq( pPlayerName, player->GetPlayerName() ) ||
				( player->GetTeamNumber() == iTeam ) ||
				( player->GetTeamNumber() == iTeam ) )
			{
				CTakeDamageInfo info( player, player, 9999999.9f, DMG_ENERGYBEAM, DMG_DISSOLVE );
				player->TakeDamage( info );
			}
		}
	}
}

//-----------------------------------------------------------------------------------------------------
void CMD_BotWarpTeamToMe( void )
{
	CBasePlayer *player = UTIL_GetListenServerHost();
	if ( !player )
		return;

	CTeam *myTeam = player->GetTeam();
	for( int i=0; i<myTeam->GetNumPlayers(); ++i )
	{
		if ( !myTeam->GetPlayer(i)->IsAlive() )
			continue;

		myTeam->GetPlayer(i)->SetAbsOrigin( player->GetAbsOrigin() );
	}
}
static ConCommand doc_bot_warp_team_to_me( "doc_bot_warp_team_to_me", CMD_BotWarpTeamToMe, "", FCVAR_GAMEDLL | FCVAR_CHEAT );
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CDODPlayerPathCost::operator()(CNavArea* area, CNavArea* fromArea, const CNavLadder* ladder, const CFuncElevator* elevator, float length) const
{
	VPROF_BUDGET(__FUNCTION__, "NextBot");

	if (fromArea == nullptr)
	{
		// first area in path; zero cost
		return 0.0f;
	}

	const CTFNavArea* tfArea = dynamic_cast<const CTFNavArea*>(area);
	if (tfArea == nullptr)
		return false;

	if (!m_pPlayer->IsAreaTraversable(area))
	{
		// dead end
		return -1.0f;
	}

	if (ladder != nullptr)
		length = ladder->m_length;
	else if (length <= 0.0f)
		length = (area->GetCenter() - fromArea->GetCenter()).Length();

	const float dz = fromArea->ComputeAdjacentConnectionHeightChange(area);
	if (dz >= m_flStepHeight)
	{
		// too high!
		if (dz >= m_flMaxJumpHeight)
			return -1.0f;

		// jumping is slow
		length *= 2;
	}
	else
	{
		// yikes, this drop will hurt too much!
		if (dz < -m_flDeathDropHeight)
			return -1.0f;
	}

	return fromArea->GetCostSoFar() + length;
}