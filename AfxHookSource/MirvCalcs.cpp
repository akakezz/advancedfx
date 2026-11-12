#include "stdafx.h"

#include "MirvCalcs.h"
#include "MaterialSystemHooks.h"
#include <cstdio>
#include <map>
#include <tf2/sdk_src/public/vstdlib/IKeyValuesSystem.h>
#include <l4d2/sdk_src/public/tier0/memalloc.h>

#include "WrpVEngineClient.h"
#include "WrpConsole.h"
#include "RenderView.h"
#include "MirvTime.h"
#include "addresses.h"
#include "../shared/CamIO.h"
#ifndef _WIN64
#include "csgo/ClientToolsCsgo.h"
#endif //#ifndef _WIN64
#include "tf2/ClientToolsTf2.h"

#include <shared/StringTools.h>
#include <ctype.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <shared/AfxDetours.h>
#include <cstddef>
#include <cstring>

#define _USE_MATH_DEFINES
#include <math.h>
#include <cmath>

#ifndef _WIN64
// g_Entitylist_csgo is only set for CS:GO / CSCO and is null for other games (e.g. TF2).
// Always use these instead of accessing it directly, so unsupported games get "no entity"
// instead of a crash.
static SOURCESDK::IClientEntity_csgo * Mirv_GetClientEntity(int entnum)
{
	return SOURCESDK::g_Entitylist_csgo ? SOURCESDK::g_Entitylist_csgo->GetClientEntity(entnum) : 0;
}

static SOURCESDK::IClientEntity_csgo * Mirv_GetClientEntityFromHandle(SOURCESDK::CSGO::CBaseHandle hEnt)
{
	return SOURCESDK::g_Entitylist_csgo ? SOURCESDK::g_Entitylist_csgo->GetClientEntityFromHandle(hEnt) : 0;
}
#endif //#ifndef _WIN64

#undef min
#undef max

extern WrpVEngineClient * g_VEngineClient;

CMirvHandleCalcs g_MirvHandleCalcs;
CMirvVecAngCalcs g_MirvVecAngCalcs;
CMirvCamCalcs g_MirvCamCalcs;
CMirvFovCalcs g_MirvFovCalcs;
CMirvBoolCalcs g_MirvBoolCalcs;
CMirvIntCalcs g_MirvIntCalcs;
CMirvFloatCalcs g_MirvFloatCalcs;

int g_LevelInitCount = 0;

void CalcDeltaSmooth(double deltaT, double targetDeltaPos, double & resultDeltaPos, double & lastVel, double LimitVelocity, double LimitAcceleration)
{
	if (deltaT <= 0)
	{
		resultDeltaPos = 0;
		return;
	}

	// What we would like to have ideally is this:
	// https://www.physicsforums.com/threads/3rd-order-motion-profile-programming-sinusoidal.868148/

	// However that is way too complicated for now so we try s.th. easier for now,
	// a 2nd order motion profile that has sort of trapezoidal velocity profile:

	// Objective:
	//
	// For the _normal_ case
	// we build the 2nd order motion profile:
	//
	// phaseT_k is the (positive) phase time.
	//
	// accel(k) = c_k
	// velo(k) = vel(k-1) +Int(accel(k),0+eps..phaseT_k) = vel(k-1) +accel(k)*phaseT_k
	// pos(k) = pos(k-1) +Int(velo(k),0+eps..phaseT_k) = pos(k-1) +vel(k-1) * phaseT_k +accel(k)/2 * phaseT_k^2
	//
	// -LimitAccel <= accel(k) <= LimitAccel
	// -LimitVelocity <= vel(k) <= LimitVelocity
	//
	// Possible phases:
	// P1) velocity build-up
	// P2) limit velocity
	// P3) velocity ramp-down

	// Complete programming problem:
	//
	//
	// Input:
	//
	// LimitVelocity - absolute velocity limit, where 0 < LimitVelocity
	// LimitAcceleration - absolute acceleration limit, where 0 < LimitAcceleration
	// targetPos - target position
	// lastPos - last position
	// lastVel - last velocity, where -LimitVelocity <= lastVel <= LimitVelocity
	//
	//
	// Output:
	//
	// phase1T, phase2T, phase3T - Phase times
	// , where 0 <= phase1T, 0 <= phase2T, 0 <= phase3T
	//
	// dir - base accel direction, where dir \in {-1, 1}
	//
	//
	// Further Equations:
	//
	// accel(1) = +dir * LimitAcceleration
	// accel(2) = 0
	// accel(3) = -dir * LimitAcceleration
	//
	// vel(0) = lastVel
	// vel(1) = vel(0) +accel(1) * phase1T = lastVel + dir * LimitAcceleration * phase1T
	// vel(2) = vel(1) +accel(2) * phase2T = lastVel + dir * LimitAcceleration * phase1T
	// vel(3) = vel(2) +accel(3) * phase3T = lastVel + dir * LimitAcceleration * phase1T - dir * LimitAcceleration * phase3T
	// vel(3) = 0
	//
	// -LimitVelocity <= vel(1), vel(2), vel(3) <= LimitVelocity
	// 
	// resultDeltaPos = 0
	// +Int(vel(1),0+eps,phase1T)
	// +Int(vel(2),0+eps,phase2T)
	// +Int(vel(3),0+eps,phase3T)
	// = 0
	// +vel(0) * phase1T +accel(1)/2 * phase1T^2
	// +vel(1) * phase2T +accel(2)/2 * phase2T^2
	// +vel(2) * phase3T +accel(3)/2 * phase3T^2
	// = 0
	// + lastVel * phase1T +dir * LimitAcceleration / 2 * phase1T^2
	// + (lastVel + dir * LimitAcceleration * phase1T) * phase2T
	// + (lastVel + dir * LimitAcceleration * phase1T) * phase3T -dir * LimitAcceleration/2 * phase3T^2
	//
	// deltaPos = targetPos -lastPos
	//
	// Minimize:
	//
	// 1) Position error: abs(deltaPos -resultDeltaPos)
	// 2) Position time: phase1T +phase2T +phase3T +phase4T +phase5T +phase6T +phase7T

	while (0 < deltaT)
	{
		//Tier0_Msg("%f ", deltaT);

		if (lastVel > LimitVelocity)
		{
			// Error condition.

			// Solving Step:
			//
			// lower Velocity until we are within limits:

			// LimitVelocity = lastVel -LimitAcceleration * phaseT

			double phaseT = (LimitVelocity - lastVel) / -LimitAcceleration;

			// Limit by deltaT:

			phaseT = std::min(phaseT, deltaT);

			resultDeltaPos = lastVel * phaseT - LimitAcceleration / 2.0 * phaseT * phaseT;
			lastVel += -LimitAcceleration * phaseT;
			deltaT -= phaseT;
		}
		else
			if (lastVel < -LimitVelocity)
			{
				// Error condition.

				// Solving Step:
				//
				// increase Velocity until we are within limits:

				// -LimitVelocity = lastVel +LimitAcceleration * phaseT

				double phaseT = (-LimitVelocity - lastVel) / +LimitAcceleration;

				// Limit by deltaT:

				phaseT = std::min(phaseT, deltaT);

				resultDeltaPos = lastVel * phaseT + LimitAcceleration / 2.0 * phaseT * phaseT;
				lastVel += +LimitAcceleration * phaseT;
				deltaT -= phaseT;
			}
			else
			{
				double phase1T = 0;
				double phase2T = 0;
				double phase3T = 0;

				// Solving Step 1:
				//
				// Finding a feasible solution, that is an upperBound for the position error
				// and a lower bound for the position time:
				//
				// This equals a full stop, meaning phase1T_1 = 0, phase2T_1 = 0:
				//
				// resultDeltaPos_1 = lastVel * phase3T_1 -dir * LimitAcceleration/2 * phase3T_1^2
				// 0 = lastVel - dir * LimitAcceleration * phase3T_1
				//
				// It follows:
				// phase3T_1 = lastVel / (dir * limitAcceleration)

				double dir = 0 < lastVel ? 1 : (0 > lastVel ? -1 : (0 <= targetDeltaPos ? 1 : -1));
				phase3T = lastVel / (dir * LimitAcceleration);

				resultDeltaPos = lastVel * phase3T - dir * LimitAcceleration / 2.0 * phase3T * phase3T;

				if (
					0 < dir && 0 < targetDeltaPos - resultDeltaPos
					|| 0 > dir && 0 > targetDeltaPos - resultDeltaPos
					)
				{
					// Solving Step 2 (only if we didn't (overshoot or hit) in Step 1):
					//
					// phase1T and phase3T are equally increased until either LimitAcceleration is hit
					// or resultDetlaPos = deltaPos:
					//
					// phase2T_2 = 0, phase3T_2 = phase3T_2 +phase1T_2
					//
					// 2.1) Assume can hit deltaPos:
					// deltaPos = 0
					// + lastVel * phase1T_{2.1} +dir * LimitAcceleration / 2 * phase1T_{2.1}^2
					// + (lastVel + dir * LimitAcceleration *  phase1T_{2.1}) * (phase3T_1 +phase1T_{2.1})
					// -dir * LimitAcceleration/2 * (phase3T_1 +phase1T_{2.1})^2
					// = 0
					// + lastVel * phase1T_{2.1} +dir * LimitAcceleration / 2 * phase1T_{2.1}^2
					// + lastVel * phase3T_1 +dir * LimitAcceleration * phase3T_1 * phase1T_{2.1}
					// + lastVel * phase1T_{2.1} +dir * LimitAcceleration * phase1T_{2.1}^2
					// - dir * LimitAcceleration/2 * (phase3T_1^2 +2*phase3T_1*phase1T_{2.1} +phase1T_{2.1}^2)
					// = 0
					// + lastVel * phase3T_1 - dir * LimitAcceleration/2 * phase3T_1^2
					// + (lastVel +dir * LimitAcceleration * phase3T_1 +lastVel -dir * LimitAcceleration/2 *2*phase3T_1) * phase1T_{2.1}
					// + (dir * LimitAcceleration / 2 +dir * LimitAcceleration - dir * LimitAcceleration/2) * phase1T_{2.1}^2
					// It follows:
					// 0 =
					// [-deltaPos +lastVel * phase3T_1 - dir * LimitAcceleration/2 * phase3T_1^2]
					// +[2 * lastVel] * phase1T_{2.1}
					// +[dir * LimitAcceleration] * phase1T_{2.1}^2
					// =
					// [-deltaPos +lastVel * phase3T_1 - dir * LimitAcceleration/2 * phase3T_1^2]/[dir * LimitAcceleration]
					// +[2 * lastVel]/[dir * LimitAcceleration] * phase1T_{2.1}
					// +phase1T_{2.1}^2
					// It follows:
					// phase1T_{2.1.1}
					// = 1/2 * (-[2 * lastVel]/[dir * LimitAcceleration]) +/- sqrt( ([2 * lastVel]/[dir * LimitAcceleration])^2 -4*([-deltaPos +lastVel * phase3T_1 - dir * LimitAcceleration/2 * phase3T_1^2]/[dir * LimitAcceleration]) )
					// We only need the positive solution.
					//
					// 2.2) Assume we can hit LimitVelocity:
					// -LimitVelocity <= lastVel + dir * LimitAcceleration * phase1T_{2.2} <= LimitVelocity	
					// phase1T_{2.2} = (s_{2.2} * LimitVelocity -lastVel) / (dir * LimitAcceleration)
					// s_{2.2} = dir
					//
					// phase1T_2 = min{phase1T_{2.1},phase1T_{2.2}}

					double temp1 =
						(2.0 * lastVel) / (dir * LimitAcceleration);

					double phase1T_2d1 =
						0.5 * ((-temp1)
							+ sqrt(temp1*temp1 - 4 * (-targetDeltaPos + lastVel * phase3T - dir * LimitAcceleration / 2.0 * phase3T*phase3T) / (dir * LimitAcceleration)));

					double phase1T_2d2 = (dir * LimitVelocity - lastVel) / (dir * LimitAcceleration);

					phase1T = std::min(phase1T_2d1, phase1T_2d2);
					phase3T += phase1T;

					resultDeltaPos = lastVel * phase1T + dir * LimitAcceleration / 2.0 * phase1T * phase1T
						+ (lastVel + dir * LimitAcceleration * phase1T) * phase3T - dir * LimitAcceleration / 2.0 * phase3T*phase3T;

					if (
						0 < dir && 0 < targetDeltaPos - resultDeltaPos
						|| 0 > dir && 0 > targetDeltaPos - resultDeltaPos
						)
					{
						// Solving Step 3 (only if we didn't (overshoot or hit) in Step 2):
						// 
						// phase2T is increased until deltaPos is hit.
						//
						// deltaPos =
						// + lastVel * phase1T_2 +dir * LimitAcceleration / 2 * phase1T_2^2
						// + (lastVel + dir * LimitAcceleration * phase1T_2) * phase2T_3
						// + (lastVel + dir * LimitAcceleration * phase1T_2) * phase3T_2 -dir * LimitAcceleration/2 * phase3T_2^2
						//
						// phase2T_3 = [deltaPos -lastVel * phase1T_2 -dir * LimitAcceleration / 2 * phase1T_2^2
						// -(lastVel + dir * LimitAcceleration * phase1T_2) * phase3T_2 +dir * LimitAcceleration/2 * phase3T_2^2]
						// / (lastVel + dir * LimitAcceleration * phase1T_2)

						double temp2 = lastVel + dir * LimitAcceleration * phase1T;

						if (temp2)
							phase2T = (targetDeltaPos - lastVel * phase1T - dir * LimitAcceleration / 2.0 * phase1T*phase1T
								- (temp2)* phase3T + dir * LimitAcceleration / 2.0 * phase3T*phase3T)
							/ (temp2);
					}
				}

				// Limit by deltaT:

				phase3T = std::max(std::min(phase1T + phase2T + phase3T, deltaT) - phase2T - phase1T, 0.0);
				phase2T = std::max(std::min(phase1T + phase2T, deltaT) - phase1T, 0.0);
				phase1T = std::min(phase1T, deltaT);

				resultDeltaPos = lastVel * phase1T + dir * LimitAcceleration / 2.0 * phase1T * phase1T
					+ (lastVel + dir * LimitAcceleration * phase1T) * phase2T
					+ (lastVel + dir * LimitAcceleration * phase1T) * phase3T - dir * LimitAcceleration / 2.0 * phase3T*phase3T;
				lastVel += +dir * LimitAcceleration * phase1T - dir * LimitAcceleration * phase3T;
				deltaT -= phase1T + phase2T + phase3T;

				// If we can consider to be finished, then we use-up the time completely:
				if (abs(targetDeltaPos - resultDeltaPos) <= AFX_MATH_EPS && abs(0 - lastVel) <= AFX_MATH_EPS)
				{
					deltaT = 0;
					resultDeltaPos = targetDeltaPos;
				}
			}
	}
}

void CalcSmooth(double deltaT, double targetPos, double & lastPos, double & lastVel, double LimitVelocity, double LimitAcceleration)
{
	double deltaPos;

	CalcDeltaSmooth(deltaT, targetPos - lastPos, deltaPos, lastVel, LimitVelocity, LimitAcceleration);

	lastPos += deltaPos;
}

////////////////////////////////////////////////////////////////////////////////

class CMirvCalc : public IMirvCalc
{
public:
	CMirvCalc(char const * name)
		: m_Name(name ? name : "")
		, m_HasName(nullptr != name)
	{

	}

	virtual void AddRef(void)
	{
		++m_RefCount;
	}

	virtual void Release(void)
	{
		int cnt = --m_RefCount;

		if (0 == cnt)
			delete this;
	}

	virtual int GetRefCount(void)
	{
		return m_RefCount;
	}

	virtual  char const * GetName(void)
	{
		return m_Name.c_str();
	}

	virtual void Console_PrintBegin(void)
	{
		if(m_HasName) Tier0_Msg("{ name: \"%s\"", m_Name.c_str());
		else Tier0_Msg("{ name: null", m_Name.c_str());
	}
	
	virtual void Console_PrintEnd(void)
	{
		Tier0_Msg(" }");
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		Tier0_Msg("No editable options.\n");
	}

protected:
	virtual ~CMirvCalc()
	{

	}

private:
	bool m_HasName;
	std::string m_Name;
	int m_RefCount = 0;
};

class CMirvHandleCalc : public CMirvCalc, public IMirvHandleCalc
{
public:
	CMirvHandleCalc(char const * name)
		: CMirvCalc(name)
	{

	}

	virtual void AddRef(void)
	{
		CMirvCalc::AddRef();
	}

	virtual void Release(void)
	{
		CMirvCalc::Release();
	}

	virtual int GetRefCount(void)
	{
		return CMirvCalc::GetRefCount();
	}

	virtual  char const * GetName(void)
	{
		return CMirvCalc::GetName();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvCalc::Console_PrintBegin();
		Tier0_Msg(", type: \"handle\"");
	}

	virtual void Console_PrintEnd(void)
	{
		CMirvCalc::Console_PrintEnd();
	}

	virtual bool CalcHandle(SOURCESDK::CSGO::CBaseHandle & outHandle)
	{
		return false;
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvCalc::Console_Edit(args);
	}
};


class CMirvHandleValueCalc : public CMirvHandleCalc
{
public:
	CMirvHandleValueCalc(char const * name, int handle)
		: CMirvHandleCalc(name)
		, m_Handle(handle)
	{

	}

	virtual bool CalcHandle(SOURCESDK::CSGO::CBaseHandle & outHandle)
	{
		outHandle = m_Handle;
		return true;
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvHandleCalc::Console_PrintBegin();
		Tier0_Msg(", fn: \"value\", handle: %i", m_Handle);
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		int argc = args->ArgC();
		char const * arg0 = args->ArgV(0);
		
		if (2 <= argc)
		{
			char const * arg1 = args->ArgV(1);

			if (0 == _stricmp("handle", arg1))
			{
				if (3 <= argc)
				{
					m_Handle = atoi(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s handle <iHandle> - Set new value.\n"
					"Current value: %i\n"
					, arg0
					, m_Handle.ToInt()
				);
				return;
			}
		}

		Tier0_Msg(
			"%s handle [...]\n"
			, arg0
		);
	}

private:
	SOURCESDK::CSGO::CBaseHandle m_Handle;
};

#ifndef _WIN64
class CMirvHandleIndexCalc : public CMirvHandleCalc
{
public:
	CMirvHandleIndexCalc(char const * name, int index)
		: CMirvHandleCalc(name)
		, m_Index(index)
	{

	}

	virtual bool CalcHandle(SOURCESDK::CSGO::CBaseHandle & outHandle)
	{
		// g_Entitylist_csgo is only set for CS:GO / CSCO, it is null for other games (e.g. TF2).
		if (!SOURCESDK::g_Entitylist_csgo)
			return false;

		SOURCESDK::IClientEntity_csgo * ce = Mirv_GetClientEntity(m_Index);

		if (ce)
		{
			outHandle = ce->GetRefEHandle();
			return true;
		}

		return false;
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvHandleCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"index\", index: %i", m_Index);
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		int argc = args->ArgC();
		char const * arg0 = args->ArgV(0);

		if (2 <= argc)
		{
			char const * arg1 = args->ArgV(1);

			if (0 == _stricmp("index", arg1))
			{
				if (3 <= argc)
				{
					m_Index = atoi(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s index <iIndex> - Set new value.\n"
					"Current value: %i\n"
					, arg0
					, m_Index
				);
				return;
			}
		}

		Tier0_Msg(
			"%s index [...]\n"
			, arg0
		);
	}

private:
	int m_Index;
};
#endif //#ifndef _WIN64

typedef bool(*csgon_fnPlayerSidesWappedOnScreen_t)(void);

#ifndef _WIN64
SOURCESDK::C_BaseEntity_csgo * GetSpectatorPlayerForKey(int key) {

	if(AFXADDR_GET(csgo_Unknown_GetTeamsSwappedOnScreen)
		&& g_VEngineClient->IsInGame() // will crash otherwise
	) {
		// Left screen side keys: 1, 2, 3, 4, 5
		// Right screen side keys: 6, 7, 8, 9, 0

		csgon_fnPlayerSidesWappedOnScreen_t fnPlayerSidesWappedOnScreen = (csgon_fnPlayerSidesWappedOnScreen_t)AFXADDR_GET(csgo_Unknown_GetTeamsSwappedOnScreen);

		bool swapPlayerSide = fnPlayerSidesWappedOnScreen && fnPlayerSidesWappedOnScreen();

		int nr = ((key +9) % 10);

		bool isOtherScreenSide = 0 != ((nr / 5) % 2);

		int slot = (nr % 5);

		int slotCT = 0;
		int slotT = 0;

		WrpGlobals * gl = g_Hook_VClient_RenderView.GetGlobals();
		int imax = gl ? gl->maxclients_get() : 0;

		for (int i = 1; i <= imax; ++i)
		{
			SOURCESDK::IClientEntity_csgo * ce = Mirv_GetClientEntity(i);
			SOURCESDK::C_BaseEntity_csgo * be = ce ? ce->GetBaseEntity() : 0;
			if (be && be->IsPlayer())
			{
				int be_team = be->GetTeamNumber();

				if (3 == be_team) // CT
				{
					if (isOtherScreenSide == swapPlayerSide && slotCT == slot)
					{
						return be;
					}

					++slotCT;
				}
				else if (2 == be_team) // T
				{
					if (isOtherScreenSide != swapPlayerSide && slotT == slot)
					{
						return be;
					}

					++slotT;
				}
			}
		}
	}

	return nullptr;	
}
#endif //#ifndef _WIN64

#ifndef _WIN64
class CMirvHandleKeyCalc : public CMirvHandleCalc
{
public:
	CMirvHandleKeyCalc(char const * name, int key)
		: CMirvHandleCalc(name)
		, m_Key(key)
		, m_ClSpecSwapPlayerSides("cl_spec_swapplayersides")
	{

	}

	virtual bool CalcHandle(SOURCESDK::CSGO::CBaseHandle & outHandle)
	{
		if(SOURCESDK::C_BaseEntity_csgo * be = GetSpectatorPlayerForKey(m_Key)) {

			outHandle = be->GetRefEHandle();
			return true;
		}

		return false;
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvHandleCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"key\", key: %i", m_Key);
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		int argc = args->ArgC();
		char const * arg0 = args->ArgV(0);

		if (2 <= argc)
		{
			char const * arg1 = args->ArgV(1);

			if (0 == _stricmp("key", arg1))
			{
				if (3 <= argc)
				{
					m_Key = atoi(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s key <iKey> - Set new value.\n"
					"Current value: %i\n"
					, arg0
					, m_Key
				);
				return;
			}
		}

		Tier0_Msg(
			"%s key [...]\n"
			, arg0
		);
	}

private:
	int m_Key;
	WrpConVarRef m_ClSpecSwapPlayerSides;
};
#endif //#ifndef _WIN64

#ifndef _WIN64
class CMirvHandleActiveWeaponCalc : public CMirvHandleCalc
{
public:
	CMirvHandleActiveWeaponCalc(char const * name, IMirvHandleCalc * parentCalc, bool world)
		: CMirvHandleCalc(name)
		, m_ParentCalc(parentCalc)
		, m_World(world)
	{
		m_ParentCalc->AddRef();

	}

	virtual bool CalcHandle(SOURCESDK::CSGO::CBaseHandle & outHandle)
	{
		SOURCESDK::CSGO::CBaseHandle parentHandle;

		if (m_ParentCalc->CalcHandle(parentHandle) && parentHandle.IsValid())
		{
			SOURCESDK::IClientEntity_csgo * clientEntity = Mirv_GetClientEntityFromHandle(parentHandle);
			SOURCESDK::C_BaseEntity_csgo * baseEntity = clientEntity ? clientEntity->GetBaseEntity() : 0;
			SOURCESDK::C_BaseCombatCharacter_csgo * combatCharacter = baseEntity ? baseEntity->MyCombatCharacterPointer() : 0;
			SOURCESDK::C_BaseCombatWeapon_csgo * activeWeapon = combatCharacter ? combatCharacter->GetActiveWeapon() : 0;

			if (activeWeapon)
			{
				SOURCESDK::IClientEntity_csgo * ce = 0;
				
				if(!m_World)
					ce = activeWeapon->GetIClientEntity();
				else if(-1 != AFXADDR_GET(csgo_C_BaseCombatWeapon_m_hWeaponWorldModel))
				{
					SOURCESDK::CSGO::CBaseHandle hWeaponWorldModel = *(SOURCESDK::CSGO::CBaseHandle *)((char const *)activeWeapon + AFXADDR_GET(csgo_C_BaseCombatWeapon_m_hWeaponWorldModel));
					ce = Mirv_GetClientEntityFromHandle(hWeaponWorldModel);
				}

				if (ce)
				{
					outHandle = ce->GetRefEHandle();
					return true;
				}
			}
		}

		return false;
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvHandleCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"activeWeapon\"");
		Tier0_Msg(", parent: "); m_ParentCalc->Console_PrintBegin(); m_ParentCalc->Console_PrintEnd();
		Tier0_Msg(", getWorld: %i", m_World ? 1 : 0);
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		int argc = args->ArgC();
		char const * arg0 = args->ArgV(0);

		if (2 <= argc)
		{
			char const * arg1 = args->ArgV(1);

			if (0 == _stricmp("getWorld", arg1))
			{
				if (3 <= argc)
				{
					m_World = 0 != atoi(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s getWorld <bGetWorld> - Set new value.\n"
					"Current value: %i\n"
					, arg0
					, m_World ? 1 : 0
				);
				return;
			}
		}

		Tier0_Msg(
			"%s getWorld [...]\n"
			, arg0
		);
	}

protected:
	virtual ~CMirvHandleActiveWeaponCalc()
	{
		m_ParentCalc->Release();
	}

private:
	IMirvHandleCalc * m_ParentCalc;
	bool m_World;
};
#endif //#ifndef _WIN64

#ifndef _WIN64
class CMirvHandleLocalPlayerCalc : public CMirvHandleCalc
{
public:
	CMirvHandleLocalPlayerCalc(char const * name)
		: CMirvHandleCalc(name)
	{
	}

	virtual bool CalcHandle(SOURCESDK::CSGO::CBaseHandle & outHandle)
	{
		SOURCESDK::IClientEntity_csgo * ce = Mirv_GetClientEntity(g_VEngineClient->GetLocalPlayer());

		if (ce)
		{
			outHandle = ce->GetRefEHandle();
			return true;
		}

		return false;
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvHandleCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"localPlayer\"");
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		return CMirvHandleCalc::Console_Edit(args);
	}

protected:
	virtual ~CMirvHandleLocalPlayerCalc()
	{
	}

private:

};
#endif //#ifndef _WIN64

#ifndef _WIN64
class CMirvHandleObserverTargetCalc : public CMirvHandleCalc
{
public:
	CMirvHandleObserverTargetCalc(char const * name, IMirvHandleCalc * parentCalc)
		: CMirvHandleCalc(name)
		, m_ParentCalc(parentCalc)
	{
		m_ParentCalc->AddRef();

	}

	virtual bool CalcHandle(SOURCESDK::CSGO::CBaseHandle & outHandle)
	{
		SOURCESDK::CSGO::CBaseHandle parentHandle;

		if (m_ParentCalc->CalcHandle(parentHandle) && parentHandle.IsValid())
		{
			SOURCESDK::IClientEntity_csgo * clientEntity = Mirv_GetClientEntityFromHandle(parentHandle);
			SOURCESDK::C_BaseEntity_csgo * baseEntity = clientEntity ? clientEntity->GetBaseEntity() : 0;

			if (baseEntity && baseEntity->IsPlayer())
			{
				SOURCESDK::C_BasePlayer_csgo * player = (SOURCESDK::C_BasePlayer_csgo *)baseEntity;

				if (SOURCESDK::C_BaseEntity_csgo * be = player->GetObserverTarget())
				{
					if (SOURCESDK::IClientEntity_csgo * ce = be->GetIClientEntity())
					{
						outHandle = ce->GetRefEHandle();
						return true;
					}
				}
			}
		}

		return false;
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvHandleCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"observerTarget\"");
		Tier0_Msg(", parent: "); m_ParentCalc->Console_PrintBegin(); m_ParentCalc->Console_PrintEnd();
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		return CMirvHandleCalc::Console_Edit(args);
	}

protected:
	virtual ~CMirvHandleObserverTargetCalc()
	{
		m_ParentCalc->Release();
	}

private:
	IMirvHandleCalc * m_ParentCalc;
};
#endif //#ifndef _WIN64

class CMirvHandleOrCalc : public CMirvHandleCalc
{
public:
	CMirvHandleOrCalc(char const * name, IMirvHandleCalc * a, IMirvHandleCalc * b)
		: CMirvHandleCalc(name)
		, m_A(a)
		, m_B(b)
	{
		m_A->AddRef();
		m_B->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvHandleCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"or\"");
		Tier0_Msg(", a: "); m_A->Console_PrintBegin(); m_A->Console_PrintEnd();
		Tier0_Msg(", b: "); m_B->Console_PrintBegin(); m_B->Console_PrintEnd();
	}

	virtual bool CalcHandle(SOURCESDK::CSGO::CBaseHandle & outHandle)
	{
		return m_A->CalcHandle(outHandle) || m_B->CalcHandle(outHandle);
	}

protected:
	virtual ~CMirvHandleOrCalc()
	{
		m_B->Release();
		m_A->Release();
	}

private:
	IMirvHandleCalc * m_A;
	IMirvHandleCalc * m_B;
};

#ifndef _WIN64
class CMirvHandleOwnerEntityCalc : public CMirvHandleCalc
{
public:
	CMirvHandleOwnerEntityCalc(char const * name, IMirvHandleCalc * calc, unsigned int maxDepth = 1)
		: CMirvHandleCalc(name)
		, m_ParentCalc(calc)
		, m_MaxDepth(maxDepth)
	{
		m_ParentCalc->AddRef();

	}

	virtual bool CalcHandle(SOURCESDK::CSGO::CBaseHandle & outHandle)
	{
		SOURCESDK::CSGO::CBaseHandle parentHandle;

		if (m_ParentCalc->CalcHandle(parentHandle) && parentHandle.IsValid())
		{
			if (CClientToolsCsgo::Instance())
			{
				if (SOURCESDK::CSGO::IClientTools * clientTools = CClientToolsCsgo::Instance()->GetClientToolsInterface())
				{
					if (SOURCESDK::IClientEntity_csgo * clientEntity = Mirv_GetClientEntityFromHandle(parentHandle))
					{
						SOURCESDK::C_BaseEntity_csgo * ent = clientEntity->GetBaseEntity();

						unsigned int maxDepth = m_MaxDepth;

						while (ent && 0 < maxDepth)
						{
							--maxDepth;

							if (SOURCESDK::C_BaseEntity_csgo * owningEnt = reinterpret_cast<SOURCESDK::C_BaseEntity_csgo *>(clientTools->GetOwnerEntity(reinterpret_cast<SOURCESDK::CSGO::EntitySearchResult>(ent))))
								ent = owningEnt;
							else break;
						}

						if (ent)
						{
							outHandle = ent->GetRefEHandle();
							return true;
						}
					}
				}
			}
		}

		return false;
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvHandleCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"ownerEntity\"");
		Tier0_Msg(", calc: "); m_ParentCalc->Console_PrintBegin(); m_ParentCalc->Console_PrintEnd();
		Tier0_Msg(", maxDepth: %u", m_MaxDepth);
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		return CMirvHandleCalc::Console_Edit(args);
	}

protected:
	virtual ~CMirvHandleOwnerEntityCalc()
	{
		m_ParentCalc->Release();
	}

private:
	IMirvHandleCalc * m_ParentCalc;
	unsigned int m_MaxDepth;
};
#endif //#ifndef _WIN64

class CMirvVecAngCalc : public CMirvCalc, public IMirvVecAngCalc
{
public:
	CMirvVecAngCalc(char const * name)
		: CMirvCalc(name)
	{

	}

	virtual void AddRef(void)
	{
		CMirvCalc::AddRef();
	}

	virtual void Release(void)
	{
		CMirvCalc::Release();
	}

	virtual int GetRefCount(void)
	{
		return CMirvCalc::GetRefCount();
	}

	virtual  char const * GetName(void)
	{
		return CMirvCalc::GetName();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvCalc::Console_PrintBegin();
		Tier0_Msg(", type: \"vecAng\"");
	}

	virtual void Console_PrintEnd(void)
	{
		CMirvCalc::Console_PrintEnd();
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		return false;
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvCalc::Console_Edit(args);
	}
};


class CMirvCamCalc : public CMirvCalc, public IMirvCamCalc
{
public:
	CMirvCamCalc(char const * name)
		: CMirvCalc(name)
	{

	}

	virtual void AddRef(void)
	{
		CMirvCalc::AddRef();
	}

	virtual void Release(void)
	{
		CMirvCalc::Release();
	}

	virtual int GetRefCount(void)
	{
		return CMirvCalc::GetRefCount();
	}

	virtual  char const * GetName(void)
	{
		return CMirvCalc::GetName();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvCalc::Console_PrintBegin();
		Tier0_Msg(", type: \"cam\"");
	}

	virtual void Console_PrintEnd(void)
	{
		CMirvCalc::Console_PrintEnd();
	}

	virtual bool CalcCam(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles, float & outFov)
	{
		return false;
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvCalc::Console_Edit(args);
	}
};

double CalcExpSmooth(double deltaT, double oldVal, double newVal)
{
	const double limitTime = 19.931568569324174087221916576936;

	if (deltaT < 0)
		return oldVal;
	else if (limitTime < deltaT)
		return newVal;

	const double halfTime = 0.69314718055994530941723212145818;

	double x = 1 / exp(deltaT * halfTime);

	return x * oldVal +  (1 - x) * newVal;
}

double CalcDeltaExpSmooth(double deltaT, double deltaVal)
{
	const double limitTime = 19.931568569324174087221916576936;

	if (deltaT < 0)
		return 0;
	else if (limitTime < deltaT)
		return deltaVal;

	const double halfTime = 0.69314718055994530941723212145818;

	double x = 1 / exp(deltaT * halfTime);

	return (1 - x) * deltaVal;
}

class CMirvCamSmoothCalc : public CMirvCamCalc
{
public:
	static void LevelInitPreEntity()
	{

	}

	CMirvCamSmoothCalc(char const * name, IMirvCamCalc * cam, IMirvHandleCalc * trackHandle)
		: CMirvCamCalc(name)
		, m_Parent(cam)
		, m_TrackHandle(trackHandle)
	{
		m_Parent->AddRef();
		m_TrackHandle->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvCamCalc::Console_PrintBegin();
		Tier0_Msg(", fn: \"smooth\"");
		Tier0_Msg(", parent: "); m_Parent->Console_PrintBegin(); m_Parent->Console_PrintEnd();
		Tier0_Msg(", trackHandle: "); m_TrackHandle->Console_PrintBegin(); m_TrackHandle->Console_PrintEnd();
		Tier0_Msg(", halfTimeVec: %f, halfTimeAng : %f, halfTimeFov : %f", (float)m_HalfTimeVec, (float)m_HalfTimeAng, (float)m_HalfTimeFov);
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		int argc = args->ArgC();
		char const * arg0 = args->ArgV(0);

		if (2 <= argc)
		{
			char const * arg1 = args->ArgV(1);

			if (0 == _stricmp("halfTime", arg1))
			{
				if (3 <= argc)
				{
					m_HalfTimeVec = m_HalfTimeAng = m_HalfTimeFov = atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s halfTime <fHalfTimeSecs> - Set new value for vec,ang,fov.\n"
					, arg0
				);
				return;
			}
			else if (0 == _stricmp("halfTimeVec", arg1))
			{
				if (3 <= argc)
				{
					m_HalfTimeVec = atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s halfTimeVec <fHalfTimeSecs> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_HalfTimeVec
				);
				return;
			}
			else if (0 == _stricmp("halfTimeAng", arg1))
			{
				if (3 <= argc)
				{
					m_HalfTimeAng = atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s halfTimeAng <fHalfTimeSecs> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_HalfTimeAng
				);
				return;
			}
			else if (0 == _stricmp("halfTimeFov", arg1))
			{
				if (3 <= argc)
				{
					m_HalfTimeFov = atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s halfTimeFov <fHalfTimeSecs> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_HalfTimeFov
				);
				return;
			}
			else if (0 == _stricmp("rotShortestPath", arg1))
			{
				if (3 <= argc)
				{
					m_RotShortestPath = 0 != atoi(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s rotShortestPath 0|1 - Set new value.\n"
					"Current value: %i\n"
					, arg0
					, m_RotShortestPath ? 1 : 0
				);
				return;
			}
		}

		Tier0_Msg(
			"%s halfTime [...]\n"
			"%s halfTimeVec [...]\n"
			"%s halfTimeAng [...]\n"
			"%s halfTimeFov [...]\n"
			"%s rotShortestPath [...] - If to rotate shortest path.\n"
			, arg0
			, arg0
			, arg0
			, arg0
			, arg0
		);
	}

	virtual bool CalcCam(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles, float & outFov) override
	{
		SOURCESDK::Vector parentVector;
		SOURCESDK::QAngle parentAngles;
		float parentFov;

		SOURCESDK::CSGO::CBaseHandle handle;

		bool calcedParent = m_Parent->CalcCam(parentVector, parentAngles, parentFov);
		bool calcedHandle = m_TrackHandle->CalcHandle(handle);

		if (m_LevelInitCount != g_LevelInitCount)
		{
			m_LevelInitCount = g_LevelInitCount;
			m_Reset = true;
		}

		m_Reset = m_Reset || !(calcedParent && calcedHandle && m_LastHandle == handle);

		if(calcedHandle) m_LastHandle = handle;

		if (calcedParent && calcedHandle)
		{
			if (m_Reset)
			{
				m_Reset = false;

				m_LastClientTime = g_MirvTime.GetTime();

				m_LastX = outVector.x = parentVector.x;
				m_LastY = outVector.y = parentVector.y;
				m_LastZ = outVector.z = parentVector.z;

				outAngles.x = parentAngles.x;
				outAngles.y = parentAngles.y;
				outAngles.z = parentAngles.z;
				m_LastOutQuat = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(outAngles.x, outAngles.y, outAngles.z)));

				m_LastFov = outFov = parentFov;
			}
			else
			{
				float clientTime = g_MirvTime.GetTime();
				double deltaT = clientTime - m_LastClientTime;
				m_LastClientTime = clientTime;

				if (m_HalfTimeAng)
				{
					double t = deltaT / m_HalfTimeAng;

					Quaternion targetQuat = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(parentAngles.x, parentAngles.y, parentAngles.z))).Normalized();

					if (m_RotShortestPath)
					{
						// Take shortest path:
						double dotProduct = DotProduct(targetQuat, m_LastOutQuat);
						if (dotProduct < 0)
						{
							targetQuat = -1.0 * targetQuat;
						}
					}

					double targetAngle = m_LastOutQuat.GetAng(targetQuat, Vector3()) * 180.0 / M_PI;
					double angle = CalcDeltaExpSmooth(t, targetAngle);

					if (abs(angle) > AFX_MATH_EPS)
					{
						m_LastOutQuat = m_LastOutQuat.Slerp(targetQuat, angle / targetAngle).Normalized();
					}

					QEulerAngles newAngles = m_LastOutQuat.ToQREulerAngles().ToQEulerAngles();

					outAngles.x = (float)newAngles.Pitch;
					outAngles.y = (float)newAngles.Yaw;
					outAngles.z = (float)newAngles.Roll;
				}
				else
				{
					outAngles.x = parentAngles.x;
					outAngles.y = parentAngles.y;
					outAngles.z = parentAngles.z;
					m_LastOutQuat = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(outAngles.x, outAngles.y, outAngles.z)));
				}

				if (m_HalfTimeVec)
				{
					double t = deltaT / m_HalfTimeVec;

					m_LastX = outVector.x = (float)CalcExpSmooth(t, m_LastX, parentVector.x);
					m_LastY = outVector.y = (float)CalcExpSmooth(t, m_LastY, parentVector.y);
					m_LastZ = outVector.z = (float)CalcExpSmooth(t, m_LastZ, parentVector.z);
				}
				else
				{
					m_LastX = outVector.x = parentVector.x;
					m_LastY = outVector.y = parentVector.y;
					m_LastZ = outVector.z = parentVector.z;
				}

				if (m_HalfTimeFov)
				{
					double t = deltaT / m_HalfTimeFov;

					double oldOpposite = 2 * tan(0.5 * m_LastFov * M_PI / 180.0);
					double targetOpposite = 2 * tan(0.5 * parentFov * M_PI / 180.0);

					double newOppposite = CalcExpSmooth(t, oldOpposite, targetOpposite);
					m_LastFov = outFov = (float)(2 * atan(0.5 * newOppposite) * 180.0 / M_PI);
				}
				else
				{
					m_LastFov = outFov = parentFov;
				}
			}

			return true;
		}

		return false;
	}

protected:
	virtual ~CMirvCamSmoothCalc()
	{
		m_TrackHandle->Release();
		m_Parent->Release();
	}

private:
	IMirvCamCalc * m_Parent;
	IMirvHandleCalc * m_TrackHandle;

	bool m_Reset = true;
	int m_LevelInitCount = 0;
	SOURCESDK::CSGO::CBaseHandle m_LastHandle;
	float m_LastClientTime = 0;
	bool m_RotShortestPath = true;

	double m_LastX = 0;
	double m_LastY = 0;
	double m_LastZ = 0;

	Quaternion m_LastOutQuat;

	double m_LastFov = 90.0;

	double m_HalfTimeVec = 0.5;
	double m_HalfTimeAng = 0.5;
	double m_HalfTimeFov = 0.5;
};


class CMirvCamVecAngFovCalc : public CMirvCamCalc
{
public:
	CMirvCamVecAngFovCalc(char const * name, IMirvVecAngCalc * vecAng, IMirvFovCalc * fov)
		: CMirvCamCalc(name)
		, m_VecAng(vecAng)
		, m_Fov(fov)
	{
		m_VecAng->AddRef();
		m_Fov->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvCamCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"vecAngFov\"");
		Tier0_Msg(", vecAng: "); m_VecAng->Console_PrintBegin(); m_VecAng->Console_PrintEnd();
		Tier0_Msg(", fov: "); m_Fov->Console_PrintBegin(); m_Fov->Console_PrintEnd();
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvCamCalc::Console_Edit(args);
	}

	virtual bool CalcCam(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles, float & outFov) override
	{
		SOURCESDK::Vector parentVector;
		SOURCESDK::QAngle parentAngles;
		float parentFov;

		SOURCESDK::CSGO::CBaseHandle handle;

		if (
			m_VecAng->CalcVecAng(parentVector, parentAngles)
			&& m_Fov->CalcFov(parentFov))
		{
			outVector.x = parentVector.x;
			outVector.y = parentVector.y;
			outVector.z = parentVector.z;
			outAngles.x = parentAngles.x;
			outAngles.y = parentAngles.y;
			outAngles.z = parentAngles.z;
			outFov = parentFov;

			return true;
		}

		return false;
	}

protected:
	virtual ~CMirvCamVecAngFovCalc()
	{
		m_Fov->Release();
		m_VecAng->Release();
	}

private:
	IMirvVecAngCalc * m_VecAng;
	IMirvFovCalc * m_Fov;
};

class CMirvFovCalc : public CMirvCalc, public IMirvFovCalc
{
public:
	CMirvFovCalc(char const * name)
		: CMirvCalc(name)
	{

	}

	virtual void AddRef(void)
	{
		CMirvCalc::AddRef();
	}

	virtual void Release(void)
	{
		CMirvCalc::Release();
	}

	virtual int GetRefCount(void)
	{
		return CMirvCalc::GetRefCount();
	}

	virtual  char const * GetName(void)
	{
		return CMirvCalc::GetName();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvCalc::Console_PrintBegin();
		Tier0_Msg(", type: \"fov\"");
	}

	virtual void Console_PrintEnd(void)
	{
		CMirvCalc::Console_PrintEnd();
	}

	virtual bool CalcFov(float & outFov)
	{
		return false;
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvCalc::Console_Edit(args);
	}
};

class CMirvVecAngValueCalc : public CMirvVecAngCalc
{
public:
	CMirvVecAngValueCalc(char const * name, float x, float y, float z, float rX, float rY, float rZ)
		: CMirvVecAngCalc(name)
	{
		m_Vec.x = x;
		m_Vec.y = y;
		m_Vec.z = z;
		m_Ang.z = rX;
		m_Ang.x = rY;
		m_Ang.y = rZ;
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"value\", x: %f, y: %f, z: %f, rX: %f, rY: %f, rZ: %f", m_Vec.x, m_Vec.y, m_Vec.z, m_Ang.z, m_Ang.x, m_Ang.y);
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		outVector = m_Vec;
		outAngles = m_Ang;

		return true;
	}


	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		int argc = args->ArgC();
		char const * arg0 = args->ArgV(0);

		if (2 <= argc)
		{
			char const * arg1 = args->ArgV(1);

			if (0 == _stricmp("x", arg1))
			{
				if (3 <= argc)
				{
					m_Vec.x = (float)atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s x <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_Vec.x
				);
				return;
			}
			else if (0 == _stricmp("y", arg1))
			{
				if (3 <= argc)
				{
					m_Vec.y = (float)atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s y <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_Vec.y
				);
				return;
			}
			else if (0 == _stricmp("z", arg1))
			{
				if (3 <= argc)
				{
					m_Vec.z = (float)atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s z <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_Vec.z
				);
				return;
			}
			else if (0 == _stricmp("rX", arg1))
			{
				if (3 <= argc)
				{
					m_Ang.z = (float)atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s rX <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_Ang.z
				);
				return;
			}
			else if (0 == _stricmp("rY", arg1))
			{
				if (3 <= argc)
				{
					m_Ang.x = (float)atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s rY <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_Ang.x
				);
				return;
			}
			else if (0 == _stricmp("rZ", arg1))
			{
				if (3 <= argc)
				{
					m_Ang.y = (float)atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s rZ <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_Ang.y
				);
				return;
			}
		}

		Tier0_Msg(
			"%s x [...]\n"
			"%s y [...]\n"
			"%s z [...]\n"
			"%s rX [...]\n"
			"%s rY [...]\n"
			"%s rZ [...]\n"
			, arg0
			, arg0
			, arg0
			, arg0
			, arg0
			, arg0
		);
	}

private:
	SOURCESDK::Vector m_Vec;
	SOURCESDK::QAngle m_Ang;
};


class CMirvVecAngOffsetCalc : public CMirvVecAngCalc
{
public:
	CMirvVecAngOffsetCalc(char const * name, IMirvVecAngCalc * parent, IMirvVecAngCalc * offset, bool order)
		: CMirvVecAngCalc(name)
		, m_Parent(parent)
		, m_Offset(offset)
		, m_Order(order)
	{
		m_Parent->AddRef();
		m_Offset->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"offset\"");
		Tier0_Msg(", parent: "); m_Parent->Console_PrintBegin(); m_Parent->Console_PrintEnd();
		Tier0_Msg(", offset: "); m_Offset->Console_PrintBegin(); m_Offset->Console_PrintEnd();
		Tier0_Msg(", order: %i", m_Order ? 1 : 0);
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		int argc = args->ArgC();
		char const * arg0 = args->ArgV(0);

		if (2 <= argc)
		{
			char const * arg1 = args->ArgV(1);

			if (0 == _stricmp("order", arg1))
			{
				if (3 <= argc)
				{
					m_Order = atoi(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s order <bValue> - Set new value.\n"
					"Current value: %i\n"
					, arg0
					, m_Order ? 1 : 0
				);
				return;
			}
		}

		Tier0_Msg(
			"%s order [...]\n"
			, arg0
		);
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		SOURCESDK::Vector parentVector;
		SOURCESDK::QAngle parentAngles;
		SOURCESDK::Vector offsetVector;
		SOURCESDK::QAngle offsetAngles;
		bool calcedParent = m_Parent->CalcVecAng(parentVector, parentAngles);
		bool calcedOffset = calcedParent && m_Offset->CalcVecAng(offsetVector, offsetAngles);

		if (calcedParent && calcedOffset)
		{
			if (m_Order)
			{
				if (offsetVector.x || offsetVector.y || offsetVector.z)
				{
					double forward[3], right[3], up[3];

					Afx::Math::MakeVectors(parentAngles.z, parentAngles.x, parentAngles.y, forward, right, up);

					outVector.x = (float)(parentVector.x + offsetVector.x*forward[0] - offsetVector.y*right[0] + offsetVector.z*up[0]);
					outVector.y = (float)(parentVector.y + offsetVector.x*forward[1] - offsetVector.y*right[1] + offsetVector.z*up[1]);
					outVector.z = (float)(parentVector.z + offsetVector.x*forward[2] - offsetVector.y*right[2] + offsetVector.z*up[2]);
				}
				else
				{
					outVector = parentVector;
				}

				if (offsetAngles.x || offsetAngles.y || offsetAngles.z)
				{
					Afx::Math::Quaternion q1 = Afx::Math::Quaternion::FromQREulerAngles(Afx::Math::QREulerAngles::FromQEulerAngles(Afx::Math::QEulerAngles(parentAngles.x, parentAngles.y, parentAngles.z)));
					Afx::Math::Quaternion q2 = Afx::Math::Quaternion::FromQREulerAngles(Afx::Math::QREulerAngles::FromQEulerAngles(Afx::Math::QEulerAngles(offsetAngles.x, offsetAngles.y, offsetAngles.z)));

					Afx::Math::QEulerAngles angs = (q1 * q2).ToQREulerAngles().ToQEulerAngles();

					outAngles.z = (float)angs.Roll;
					outAngles.x = (float)angs.Pitch;
					outAngles.y = (float)angs.Yaw;
				}
				else
				{
					outAngles = parentAngles;
				}
			}
			else
			{
				double forward[3], right[3], up[3];

				Afx::Math::Quaternion q1 = Afx::Math::Quaternion::FromQREulerAngles(Afx::Math::QREulerAngles::FromQEulerAngles(Afx::Math::QEulerAngles(parentAngles.x, parentAngles.y, parentAngles.z)));
				Afx::Math::Quaternion q2 = Afx::Math::Quaternion::FromQREulerAngles(Afx::Math::QREulerAngles::FromQEulerAngles(Afx::Math::QEulerAngles(offsetAngles.x, offsetAngles.y, offsetAngles.z)));

				Afx::Math::QEulerAngles angs = (q1 * q2).ToQREulerAngles().ToQEulerAngles();

				outAngles.z = (float)angs.Roll;
				outAngles.x = (float)angs.Pitch;
				outAngles.y = (float)angs.Yaw;

				Afx::Math::MakeVectors(outAngles.z, outAngles.x, outAngles.y, forward, right, up);

				outVector.x = (float)(parentVector.x + offsetVector.x*forward[0] - offsetVector.y*right[0] + offsetVector.z*up[0]);
				outVector.y = (float)(parentVector.y + offsetVector.x*forward[1] - offsetVector.y*right[1] + offsetVector.z*up[1]);
				outVector.z = (float)(parentVector.z + offsetVector.x*forward[2] - offsetVector.y*right[2] + offsetVector.z*up[2]);
			}
			return true;
		}

		return false;
	}

protected:
	virtual ~CMirvVecAngOffsetCalc()
	{
		m_Offset->Release();
		m_Parent->Release();
	}

private:
	IMirvVecAngCalc * m_Parent;
	IMirvVecAngCalc * m_Offset;
	bool m_Order;
};

#ifndef _WIN64
class CMirvVecAngHandleCalcEx : public CMirvVecAngCalc
{
public:
	CMirvVecAngHandleCalcEx(char const * name, IMirvHandleCalc * handle, bool eyeVec, bool eyeAng)
		: CMirvVecAngCalc(name)
		, m_Handle(handle)
		, m_EyeVec(eyeVec)
		, m_EyeAng(eyeAng)
	{
		m_Handle->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();
		Tier0_Msg(", fn: \"handleEx\"");
		Tier0_Msg(", handle: "); m_Handle->Console_PrintBegin(); m_Handle->Console_PrintEnd();
		Tier0_Msg(", eyeVec: %i, eyeAng: %i", m_EyeVec ? 1 : 0, m_EyeAng ? 1 : 0);
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		int argc = args->ArgC();
		char const * arg0 = args->ArgV(0);

		if (2 <= argc)
		{
			char const * arg1 = args->ArgV(1);

			if (0 == _stricmp("eyeVec", arg1))
			{
				if (3 <= argc)
				{
					m_EyeVec = atoi(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s eyeVec <bValue> - Set new value.\n"
					"Current value: %i\n"
					, arg0
					, m_EyeVec ? 1 : 0
				);
				return;
			}
			else if (0 == _stricmp("eyeAng", arg1))
			{
				if (3 <= argc)
				{
					m_EyeAng = atoi(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s eyeAng <bValue> - Set new value.\n"
					"Current value: %i\n"
					, arg0
					, m_EyeAng ? 1 : 0
				);
				return;
			}
		}

		Tier0_Msg(
			"%s eyeVec [...]\n"
			"%s eyeAng [...]\n"
			, arg0
			, arg0
		);
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		SOURCESDK::CSGO::CBaseHandle handle;
		bool calcedHandle = m_Handle->CalcHandle(handle);
		SOURCESDK::IClientEntity_csgo * ce = calcedHandle ? Mirv_GetClientEntityFromHandle(handle) : 0;
		SOURCESDK::C_BaseEntity_csgo * be = ce ? ce->GetBaseEntity() : 0;

		if (be || ce && !m_EyeVec && !m_EyeAng)
		{
			outVector = m_EyeVec ? be->EyePosition() : ce->GetAbsOrigin();
			outAngles = m_EyeAng ? be->EyeAngles() : ce->GetAbsAngles();
			return true;
		}

		return false;
	}

protected:
	virtual ~CMirvVecAngHandleCalcEx()
	{
		m_Handle->Release();
	}

private:
	IMirvHandleCalc * m_Handle;
	bool m_EyeVec;
	bool m_EyeAng;
};
#endif //#ifndef _WIN64

// TF2: The TF2 SDK subset in use has no IClientEntity / IClientEntityList definitions,
// so entities are accessed by entity index through IClientTools instead.
// Without a bone index only abs origin / abs angles are available (no eye position / eye angles).
// With a bone index the bone's world transform is used (IClientRenderable::SetupBones).

// Minimal IClientRenderable (public Source SDK interface, same vtable layout as in the game).
// The definition in the TF2 SDK subset is commented out, so it is declared here.
// Only SetupBones is used, the other entries are placeholders to keep the layout.
class CMirvTf2ClientRenderable
{
public:
	virtual void * GetIClientUnknown() = 0; // 0
	virtual const void * GetRenderOrigin(void) = 0; // 1
	virtual const void * GetRenderAngles(void) = 0; // 2
	virtual bool ShouldDraw(void) = 0; // 3
	virtual bool IsTransparent(void) = 0; // 4
	virtual bool UsesPowerOfTwoFrameBufferTexture() = 0; // 5
	virtual bool UsesFullFrameBufferTexture() = 0; // 6
	virtual unsigned short GetShadowHandle() const = 0; // 7
	virtual void * RenderHandle() = 0; // 8
	virtual const void * GetModel() const = 0; // 9
	virtual int DrawModel(int flags) = 0; // 10
	virtual int GetBody() = 0; // 11
	virtual void ComputeFxBlend() = 0; // 12
	virtual int GetFxBlend(void) = 0; // 13
	virtual void GetColorModulation(float * color) = 0; // 14
	virtual bool LODTest() = 0; // 15
	virtual bool SetupBones(SOURCESDK::matrix3x4_t * pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime) = 0; // 16
	virtual void SetupWeights(const SOURCESDK::matrix3x4_t * pBoneToWorld, int nFlexWeightCount, float * pFlexWeights, float * pFlexDelayedWeights) = 0; // 17
	virtual void DoAnimationEvents(void) = 0; // 18
	virtual void * GetPVSNotifyInterface() = 0; // 19
	virtual void GetRenderBounds(float * mins, float * maxs) = 0; // 20 (Vector references)
	virtual void GetRenderBoundsWorldspace(float * mins, float * maxs) = 0; // 21 (Vector references)
};

// Minimal IClientUnknown (public Source SDK interface, same vtable layout as in the game).
// Only GetClientRenderable is used, the other entries are placeholders to keep the layout.
class CMirvTf2ClientUnknown
{
public:
	virtual ~CMirvTf2ClientUnknown() {}
	virtual void SetRefEHandle(const void * handle) = 0;
	virtual const void * GetRefEHandle() const = 0;
	virtual void * GetCollideable() = 0;
	virtual void * GetClientNetworkable() = 0;
	virtual CMirvTf2ClientRenderable * GetClientRenderable() = 0;
};

// Orientation helpers for TF2 bone cameras. Same conventions as the Source SDK's mathlib
// (matrix columns are forward, left, up; angles are pitch, yaw, roll in degrees).

static void Mirv_AngleMatrix3(float pitch, float yaw, float roll, float m[3][3])
{
	const float deg2rad = 0.017453292519943295f;
	const float sp = sinf(pitch * deg2rad), cp = cosf(pitch * deg2rad);
	const float sy = sinf(yaw * deg2rad), cy = cosf(yaw * deg2rad);
	const float sr = sinf(roll * deg2rad), cr = cosf(roll * deg2rad);

	m[0][0] = cp * cy;
	m[1][0] = cp * sy;
	m[2][0] = -sp;

	const float crcy = cr * cy;
	const float crsy = cr * sy;
	const float srcy = sr * cy;
	const float srsy = sr * sy;

	m[0][1] = sp * srcy - crsy;
	m[1][1] = sp * srsy + crcy;
	m[2][1] = sr * cp;

	m[0][2] = sp * crcy + srsy;
	m[1][2] = sp * crsy - srcy;
	m[2][2] = cr * cp;
}

static void Mirv_MatrixAngles3(const float m[3][3], float & pitch, float & yaw, float & roll)
{
	const float rad2deg = 57.29577951308232f;
	const float xyDist = sqrtf(m[0][0] * m[0][0] + m[1][0] * m[1][0]);

	if (0.001f < xyDist)
	{
		yaw = rad2deg * atan2f(m[1][0], m[0][0]);
		pitch = rad2deg * atan2f(-m[2][0], xyDist);
		roll = rad2deg * atan2f(m[2][1], m[2][2]);
	}
	else
	{
		yaw = rad2deg * atan2f(-m[0][1], m[1][1]);
		pitch = rad2deg * atan2f(-m[2][0], xyDist);
		roll = 0.0f;
	}
}

// An unset bone matrix has no usable forward axis.
static bool Mirv_Tf2_IsValidBoneMatrix(const float m[3][4])
{
	return !(m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0] < 0.25f);
}

// Makes a right handed, orthonormal basis out of the rotation part of a bone matrix (removes scale).
// Returns false if the matrix is degenerate.
static bool Mirv_Basis3(const float (*m)[4], float b[3][3])
{
	float f[3] = { m[0][0], m[1][0], m[2][0] };
	float l[3] = { m[0][1], m[1][1], m[2][1] };

	float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
	if (fl < 1e-6f)
		return false;
	for (int i = 0; i < 3; ++i) f[i] /= fl;

	const float d = l[0] * f[0] + l[1] * f[1] + l[2] * f[2];
	for (int i = 0; i < 3; ++i) l[i] -= d * f[i];

	float ll = sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
	if (ll < 1e-6f)
		return false;
	for (int i = 0; i < 3; ++i) l[i] /= ll;

	const float u[3] = { f[1] * l[2] - f[2] * l[1], f[2] * l[0] - f[0] * l[2], f[0] * l[1] - f[1] * l[0] };

	for (int i = 0; i < 3; ++i)
	{
		b[i][0] = f[i];
		b[i][1] = l[i];
		b[i][2] = u[i];
	}

	return true;
}

// out = a * b
static void Mirv_Mul3(const float a[3][3], const float b[3][3], float out[3][3])
{
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			out[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
}

// out = transpose(a) * b
static void Mirv_MulTransposeA3(const float a[3][3], const float b[3][3], float out[3][3])
{
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			out[r][c] = a[0][r] * b[0][c] + a[1][r] * b[1][c] + a[2][r] * b[2][c];
}

class CMirvVecAngTf2EntityIndexCalc : public CMirvVecAngCalc
{
public:
	CMirvVecAngTf2EntityIndexCalc(char const * name, int entityIndex, int boneIndex, int mode)
		: CMirvVecAngCalc(name)
		, m_EntityIndex(entityIndex)
		, m_BoneIndex(boneIndex)
		, m_Mode(mode)
		, m_Calibrated(false)
		, m_Captured(false)
	{
		memset(m_Fix, 0, sizeof(m_Fix));
		memset(m_Offset, 0, sizeof(m_Offset));
		if (m_BoneIndex >= 0)
		{
			MirvTf2BoneName_t names[MIRV_TF2_MAX_BONES];
			const int count = Mirv_Tf2_GetBoneNames(m_EntityIndex, names);
			if (m_BoneIndex < count && names[m_BoneIndex].name[0])
				m_BoneName = names[m_BoneIndex].name;
		}
		if (m_Mode == MIRV_TF2_BONEMODE_WORLD)
		{
			const auto &angles = g_Hook_VClient_RenderView.LastCameraAngles;
			Mirv_AngleMatrix3(float(angles[0]), float(angles[1]), float(angles[2]), m_Fix);
			m_Calibrated = true;
		}
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();

		Tier0_Msg(
			", fn: \"tf2EntityIndex\", index: %i, boneIndex: %i, mode: %s, captured: %s"
			, m_EntityIndex, m_BoneIndex
			, MIRV_TF2_BONEMODE_WORLD == m_Mode ? "world" :
			  (MIRV_TF2_BONEMODE_ENTITY == m_Mode ? "entity" :
			  (MIRV_TF2_BONEMODE_ALIGNED == m_Mode ? "aligned" : "bone"))
			, m_Captured ? "yes" : "no"
		);
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		m_AnchorValid = false;
		SOURCESDK::TF2::IClientTools * iface = GetTools();

		if (!iface)
			return false;

		SOURCESDK::Vector pos;
		float base[3][3];
		SOURCESDK::QAngle rawAngles;
		SOURCESDK::QAngle facing;
		bool baseOk = false;

		if (!GetBase(iface, pos, base, rawAngles, facing, baseOk))
			return false;

		m_Anchor[0] = pos.x; m_Anchor[1] = pos.y; m_Anchor[2] = pos.z;
		m_AnchorValid = true;
		const bool bone = 0 <= m_BoneIndex;

		if (m_Mode == MIRV_TF2_BONEMODE_WORLD || m_Mode == MIRV_TF2_BONEMODE_ENTITY)
		{
			const bool world = m_Mode == MIRV_TF2_BONEMODE_WORLD;
			Mirv_AngleMatrix3(world ? 0 : facing.x, world ? 0 : facing.y, world ? 0 : facing.z, base);
			baseOk = true;
		}
		if (!m_Captured && m_Mode != MIRV_TF2_BONEMODE_WORLD)
		{
			if (!bone || MIRV_TF2_BONEMODE_ENTITY == m_Mode)
			{
				outVector = pos;
				outAngles = facing;
				return true;
			}

			if (MIRV_TF2_BONEMODE_BONE == m_Mode)
			{
				outVector = pos;
				outAngles = rawAngles;
				return true;
			}
		}

		// Aligned or captured: The base's rotation times a fixed rotation (in the base's space).

		if (!baseOk)
			return false;

		if (!m_Calibrated)
		{
			// Start out facing like the entity: fix = inverse(base) * entity, so that base * fix == entity right now.
			float entity[3][3];
			Mirv_AngleMatrix3(facing.x, facing.y, facing.z, entity);
			Mirv_MulTransposeA3(base, entity, m_Fix);
			m_Calibrated = true;
		}

		float result[3][3];
		Mirv_Mul3(base, m_Fix, result);
		Mirv_MatrixAngles3(result, outAngles.x, outAngles.y, outAngles.z);

		outVector = pos;

		if (m_Captured)
		{
			// m_Offset is in the camera's own axes (forward, left, up are the columns of the result).
			outVector.x += result[0][0] * m_Offset[0] + result[0][1] * m_Offset[1] + result[0][2] * m_Offset[2];
			outVector.y += result[1][0] * m_Offset[0] + result[1][1] * m_Offset[1] + result[1][2] * m_Offset[2];
			outVector.z += result[2][0] * m_Offset[0] + result[2][1] * m_Offset[1] + result[2][2] * m_Offset[2];
		}

		return true;
	}

	bool GetAttachAnchor(double origin[3], void *&entity, double entityOrigin[3], int *mode = nullptr) const
	{
		if (!m_AnchorValid || !m_AnchorEntity)
			return false;
		std::copy(m_Anchor, m_Anchor + 3, origin);
		std::copy(m_AnchorRoot, m_AnchorRoot + 3, entityOrigin);
		entity = m_AnchorEntity;
		if (mode) *mode = m_Mode;
		return true;
	}

	// See Mirv_Tf2_CaptureCamera.
	bool CaptureCamera(const double camOrigin[3], const double camAngles[3], float outLocalOffset[3])
	{
		SOURCESDK::TF2::IClientTools * iface = GetTools();

		if (!iface)
			return false;

		SOURCESDK::Vector pos;
		float base[3][3];
		SOURCESDK::QAngle rawAngles;
		SOURCESDK::QAngle facing;
		bool baseOk = false;

		if (!GetBase(iface, pos, base, rawAngles, facing, baseOk))
			return false;
		if (m_Mode == MIRV_TF2_BONEMODE_WORLD || m_Mode == MIRV_TF2_BONEMODE_ENTITY)
		{
			const bool world = m_Mode == MIRV_TF2_BONEMODE_WORLD;
			Mirv_AngleMatrix3(world ? 0 : facing.x, world ? 0 : facing.y, world ? 0 : facing.z, base);
			baseOk = true;
		}
		if (!baseOk)
			return false;
		float cam[3][3];
		Mirv_AngleMatrix3((float)camAngles[0], (float)camAngles[1], (float)camAngles[2], cam);

		// base * fix == cam
		Mirv_MulTransposeA3(base, cam, m_Fix);

		// Position relative to the target, in the camera's axes: transpose(cam) * delta.
		const float delta[3] = { (float)camOrigin[0] - pos.x, (float)camOrigin[1] - pos.y, (float)camOrigin[2] - pos.z };

		for (int i = 0; i < 3; ++i)
		{
			m_Offset[i] = cam[0][i] * delta[0] + cam[1][i] * delta[1] + cam[2][i] * delta[2];
			outLocalOffset[i] = m_Offset[i];
		}

		m_Calibrated = true;
		m_Captured = true;

		return true;
	}

	// See Mirv_Tf2_GetEntityAnglesInfo.
	bool GetAnglesInfo(float outAbsAngles[3], float outRenderAngles[3])
	{
		SOURCESDK::TF2::IClientTools * iface = GetTools();

		if (!iface)
			return false;

		for (auto ent = iface->FirstEntity(); ent; ent = iface->NextEntity(ent))
		{
			if (iface->GetEntIndex(ent) != m_EntityIndex)
				continue;

			SOURCESDK::Vector absOrigin;
			SOURCESDK::QAngle absAngles;
			if (!CalcEntity(iface, absOrigin, absAngles))
				return false;

			outAbsAngles[0] = absAngles.x; outAbsAngles[1] = absAngles.y; outAbsAngles[2] = absAngles.z;
			outRenderAngles[0] = outRenderAngles[1] = outRenderAngles[2] = 0.0f;

			CMirvTf2ClientUnknown * unknown = reinterpret_cast<CMirvTf2ClientUnknown *>(ent);
			CMirvTf2ClientRenderable * renderable = unknown ? unknown->GetClientRenderable() : 0;
			const float * ra = renderable ? reinterpret_cast<const float *>(renderable->GetRenderAngles()) : 0;

			if (ra)
			{
				outRenderAngles[0] = ra[0]; outRenderAngles[1] = ra[1]; outRenderAngles[2] = ra[2];
			}

			return true;
		}

		return false;
	}

private:
	bool m_AnchorValid = false;
	double m_Anchor[3] = {}, m_AnchorRoot[3] = {};
	void *m_AnchorEntity = nullptr;
	int m_EntityIndex;
	int m_BoneIndex;
	int m_Mode;

	// Rotation (in base space) from the base (bone or entity) to the camera. For MIRV_TF2_BONEMODE_ALIGNED
	// it is set on the first valid frame so that the camera starts out facing like the entity.
	bool m_Calibrated;
	float m_Fix[3][3];

	// Camera captured relative to the target (see CaptureCamera).
	bool m_Captured;
	float m_Offset[3];

	// Keep a selected bone's identity across class/model changes, including
	// numeric selections from the overlay. Never silently reuse another limb.
	const void *m_Model = nullptr;
	void *m_BoundEntity = nullptr;
	int m_LevelInitCount = -1;
	bool m_BindingValid = true;
	std::string m_BoneName;
	bool BindModel(void *entity, const void *model)
	{
		if (m_BoundEntity == entity && m_Model == model && m_LevelInitCount == g_LevelInitCount)
			return m_BindingValid;
		const bool changed = m_BoundEntity != nullptr;
		m_BoundEntity = entity;
		m_Model = model;
		m_LevelInitCount = g_LevelInitCount;
		m_BindingValid = true;
		if (changed)
		{
			MirvCameraAttachedReset();
			if (!m_Captured && m_Mode == MIRV_TF2_BONEMODE_ALIGNED)
				m_Calibrated = false;
		}
		if (m_BoneIndex < 0)
			return true;
		MirvTf2BoneName_t names[MIRV_TF2_MAX_BONES];
		const int count = Mirv_Tf2_GetBoneNames(m_EntityIndex, names);
		if (!changed && m_BoneName.empty() && m_BoneIndex < count && names[m_BoneIndex].name[0])
			m_BoneName = names[m_BoneIndex].name;
		else if (changed || !m_BoneName.empty())
		{
			m_BindingValid = false;
			for (int i = 0; i < count && !m_BoneName.empty(); ++i)
				if (!_stricmp(names[i].name, m_BoneName.c_str()))
				{
					m_BoneIndex = i;
					m_BindingValid = true;
					break;
				}
		}
		if (!m_BindingValid)
			Tier0_Warning("TF2 entity %i: selected bone is unavailable after a model change; "
			              "reselect the attachment.\n", m_EntityIndex);
		return m_BindingValid;
	}

	SOURCESDK::TF2::IClientTools * GetTools(void)
	{
		CClientToolsTf2 * tools = CClientToolsTf2::Instance();
		SOURCESDK::TF2::IClientTools * iface = tools ? tools->GetClientTools() : 0;

		// Only networked entities (index 0 .. 2047) can be looked up by index.
		if (!iface || m_EntityIndex < 0 || m_EntityIndex >= 2048)
			return 0;

		return iface;
	}

	bool CalcEntity(SOURCESDK::TF2::IClientTools * iface, SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		SOURCESDK::TF2::HTOOLHANDLE hHandle = iface->GetToolHandleForEntityByIndex(m_EntityIndex);
		SOURCESDK::TF2::EntitySearchResult attachedEntity = 0;

		if (SOURCESDK::TF2::HTOOLHANDLE_INVALID == hHandle)
		{
			// Attach temporarily if the game has not assigned a tool handle.
			for (auto ent = iface->FirstEntity(); ent; ent = iface->NextEntity(ent))
			{
				if (iface->GetEntIndex(ent) == m_EntityIndex)
				{
					hHandle = iface->AttachToEntity(ent);
					if (SOURCESDK::TF2::HTOOLHANDLE_INVALID != hHandle)
						attachedEntity = ent;
					break;
				}
			}
		}

		if (SOURCESDK::TF2::HTOOLHANDLE_INVALID == hHandle || !iface->IsValidHandle(hHandle))
		{
			if (attachedEntity)
				iface->DetachFromEntity(attachedEntity);
			return false;
		}

		outVector = iface->GetAbsOrigin(hHandle);
		outAngles = iface->GetAbsAngles(hHandle);

		if (attachedEntity)
			iface->DetachFromEntity(attachedEntity);

		return true;
	}

	// Gets the position and rotation the camera is based on.
	// pos: Position of the entity or bone.
	// base: Rotation of the base as orthonormal matrix (the bone's, or the entity's facing if there is no bone), valid if baseOk.
	// rawAngles: The angles to use without any calibration (the bone's own angles, or the entity's facing).
	// facing: The entity's facing, i.e. its render angles (for players this is the body direction, their abs angles are zero),
	//         or its abs angles if the render angles are all zero.
	bool GetBase(SOURCESDK::TF2::IClientTools * iface, SOURCESDK::Vector & pos, float base[3][3], SOURCESDK::QAngle & rawAngles, SOURCESDK::QAngle & facing, bool & baseOk)
	{
		baseOk = false;

		const int maxBones = MIRV_TF2_MAX_BONES;

		if (maxBones <= m_BoneIndex)
			return false;

		for (auto ent = iface->FirstEntity(); ent; ent = iface->NextEntity(ent))
		{
			if (iface->GetEntIndex(ent) != m_EntityIndex)
				continue;

			SOURCESDK::Vector absOrigin;
			SOURCESDK::QAngle absAngles;
			if (!CalcEntity(iface, absOrigin, absAngles))
				return false;

			m_AnchorRoot[0] = absOrigin.x;
			m_AnchorRoot[1] = absOrigin.y;
			m_AnchorRoot[2] = absOrigin.z;
			m_AnchorEntity = ent;
			// EntitySearchResult is a C_BaseEntity pointer, IClientUnknown is its first base.
			CMirvTf2ClientUnknown * unknown = reinterpret_cast<CMirvTf2ClientUnknown *>(ent);
			CMirvTf2ClientRenderable * renderable = unknown ? unknown->GetClientRenderable() : 0;

			if (!BindModel(ent, renderable ? renderable->GetModel() : nullptr))
				return false;

			facing = absAngles;

			if (renderable)
			{
				const float * ra = reinterpret_cast<const float *>(renderable->GetRenderAngles());

				if (ra && (0.0f != ra[0] || 0.0f != ra[1] || 0.0f != ra[2]))
				{
					facing.x = ra[0];
					facing.y = ra[1];
					facing.z = ra[2];
				}
			}

			if (m_BoneIndex < 0)
			{
				pos = absOrigin;
				rawAngles = facing;
				Mirv_AngleMatrix3(facing.x, facing.y, facing.z, base);
				baseOk = true;
				return true;
			}

			if (!renderable)
				return false;

			SOURCESDK::matrix3x4_t bones[maxBones];
			for (int b = 0; b < maxBones; ++b)
				for (int r = 0; r < 3; ++r)
					for (int c = 0; c < 4; ++c)
						bones[b].m_flMatVal[r][c] = 0.0f;

			WrpGlobals * gl = g_Hook_VClient_RenderView.GetGlobals();
			if (!gl)
				return false;

			// 0x0007FF00 = BONE_USED_BY_ANYTHING
			if (!renderable->SetupBones(bones, maxBones, 0x0007FF00, gl->curtime_get()))
				return false;

			const float (*m)[4] = bones[m_BoneIndex].m_flMatVal;

			if (!Mirv_Tf2_IsValidBoneMatrix(m))
				return false;

			pos.x = m[0][3];
			pos.y = m[1][3];
			pos.z = m[2][3];

			float raw[3][3] = {
				{ m[0][0], m[0][1], m[0][2] },
				{ m[1][0], m[1][1], m[1][2] },
				{ m[2][0], m[2][1], m[2][2] }
			};
			Mirv_MatrixAngles3(raw, rawAngles.x, rawAngles.y, rawAngles.z);

			baseOk = Mirv_Basis3(m, base);

			return true;
		}

		return false;
	}
};

#ifndef _WIN64
class CMirvVecAngHandleAttachmentCalc : public CMirvVecAngCalc
{
public:
	CMirvVecAngHandleAttachmentCalc(char const * name, IMirvHandleCalc * handle, char const * attachmentName)
		: CMirvVecAngCalc(name)
		, m_Handle(handle)
		, m_AttachmentName(attachmentName)
	{
		m_Handle->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"handleAttachment\"");
		Tier0_Msg(", handle: "); m_Handle->Console_PrintBegin(); m_Handle->Console_PrintEnd();
		Tier0_Msg(", attachmentName: \"%s\"", m_AttachmentName.c_str());
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		int argc = args->ArgC();
		char const * arg0 = args->ArgV(0);

		if (2 <= argc)
		{
			char const * arg1 = args->ArgV(1);

			if (0 == _stricmp("attachmentName", arg1))
			{
				if (3 <= argc)
				{
					m_AttachmentName = args->ArgV(2);
					return;
				}

				Tier0_Msg(
					"%s attachmentName <sValue> - Set new value.\n"
					"Current value: %s\n"
					, arg0
					, m_AttachmentName.c_str()
				);
				return;
			}
		}

		Tier0_Msg(
			"%s attachmentName [...]\n"
			, arg0
		);
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		SOURCESDK::CSGO::CBaseHandle handle;
		bool calcedHandle = m_Handle->CalcHandle(handle);
		SOURCESDK::IClientEntity_csgo * ce = calcedHandle ? Mirv_GetClientEntityFromHandle(handle) : 0;

		if (ce)
		{
			int idx = ce->LookupAttachment(m_AttachmentName.c_str());
			if (-1 != idx)
			{
				return ce->GetAttachment(idx, outVector, outAngles);
			}
		}

		return false;
	}

protected:
	virtual ~CMirvVecAngHandleAttachmentCalc()
	{
		m_Handle->Release();
	}

private:
	IMirvHandleCalc * m_Handle;
	std::string m_AttachmentName;
};
#endif //#ifndef _WIN64

class CMirvVecAngIfCalc : public CMirvVecAngCalc
{
public:
	CMirvVecAngIfCalc(char const * name, IMirvBoolCalc * condition, IMirvVecAngCalc * condTrue, IMirvVecAngCalc * condFalse)
		: CMirvVecAngCalc(name)
		, m_Condition(condition)
		, m_CondTrue(condTrue)
		, m_CondFalse(condFalse)
	{
		m_Condition->AddRef();
		m_CondTrue->AddRef();
		m_CondFalse->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"if\"");
		Tier0_Msg(", condition: "); m_Condition->Console_PrintBegin(); m_Condition->Console_PrintEnd();
		Tier0_Msg(", true: "); m_CondTrue->Console_PrintBegin(); m_CondTrue->Console_PrintEnd();
		Tier0_Msg(", false: "); m_CondFalse->Console_PrintBegin(); m_CondFalse->Console_PrintEnd();
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		bool condition;

		bool result = m_Condition->CalcBool(condition);
		
		condition = result && condition;

		return condition ? m_CondTrue->CalcVecAng(outVector, outAngles) : m_CondFalse->CalcVecAng(outVector, outAngles);
	}

protected:
	virtual ~CMirvVecAngIfCalc()
	{
		m_CondFalse->Release();
		m_CondTrue->Release();
		m_Condition->Release();
	}

private:
	IMirvBoolCalc * m_Condition;
	IMirvVecAngCalc * m_CondTrue;
	IMirvVecAngCalc * m_CondFalse;
};


class CMirvVecAngOrCalc : public CMirvVecAngCalc
{
public:
	CMirvVecAngOrCalc(char const * name, IMirvVecAngCalc * a, IMirvVecAngCalc * b)
		: CMirvVecAngCalc(name)
		, m_A(a)
		, m_B(b)
	{
		m_A->AddRef();
		m_B->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"or\"");
		Tier0_Msg(", a: "); m_A->Console_PrintBegin(); m_A->Console_PrintEnd();
		Tier0_Msg(", b: "); m_B->Console_PrintBegin(); m_B->Console_PrintEnd();
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		return m_A->CalcVecAng(outVector, outAngles) || m_B->CalcVecAng(outVector, outAngles);
	}

protected:
	virtual ~CMirvVecAngOrCalc()
	{
		m_B->Release();
		m_A->Release();
	}

private:
	IMirvVecAngCalc * m_A;
	IMirvVecAngCalc * m_B;
};


class CMirvVecAngMotionProfile2Calc : public CMirvVecAngCalc
{
public:
	CMirvVecAngMotionProfile2Calc(char const * name, IMirvVecAngCalc * parent, IMirvHandleCalc * trackHandle)
		: CMirvVecAngCalc(name)
		, m_Parent(parent)
		, m_TrackHandle(trackHandle)
	{
		m_Parent->AddRef();
		m_TrackHandle->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"motionProfile\"");
		Tier0_Msg(", parent: "); m_Parent->Console_PrintBegin(); m_Parent->Console_PrintEnd();
		Tier0_Msg(", trackHandle: "); m_TrackHandle->Console_PrintBegin(); m_TrackHandle->Console_PrintEnd();
		Tier0_Msg(
			", limXVelo: %f, limXAcel: %f, limYVelo: %f, limYAcel: %f, limZVelo: %f, limZAcel: %f, limAngVelo: %f, limAngAcel: %f\n"
			, m_LimitVelocityX, m_LimitAccelerationX		
			, m_LimitVelocityY, m_LimitAccelerationY
			, m_LimitVelocityZ, m_LimitAccelerationZ
			, m_LimitVelocityAng, m_LimitAccelerationAng
		);
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		int argc = args->ArgC();
		char const * arg0 = args->ArgV(0);

		if (2 <= argc)
		{
			char const * arg1 = args->ArgV(1);

			if (0 == _stricmp("limXVelo", arg1))
			{
				if (3 <= argc)
				{
					m_LimitVelocityX = atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s limXVelo <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_LimitVelocityX
				);
				return;
			}
			else if (0 == _stricmp("limXAcel", arg1))
			{
				if (3 <= argc)
				{
					m_LimitAccelerationX = atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s limXAcel <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_LimitAccelerationX
				);
				return;
			}
			else if (0 == _stricmp("limYVelo", arg1))
			{
				if (3 <= argc)
				{
					m_LimitVelocityY = atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s limYVelo <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_LimitVelocityY
				);
				return;
			}
			else if (0 == _stricmp("limYAcel", arg1))
			{
				if (3 <= argc)
				{
					m_LimitAccelerationY = atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s limYAcel <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_LimitAccelerationY
				);
				return;
			}
			else if (0 == _stricmp("limZVelo", arg1))
			{
				if (3 <= argc)
				{
					m_LimitVelocityZ = atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s limZVelo <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_LimitVelocityZ
				);
				return;
			}
			else if (0 == _stricmp("limZAcel", arg1))
			{
				if (3 <= argc)
				{
					m_LimitAccelerationZ = atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s limZAcel <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_LimitAccelerationZ
				);
				return;
			}
			else if (0 == _stricmp("limAngVelo", arg1))
			{
				if (3 <= argc)
				{
					m_LimitVelocityAng = atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s limAngVelo <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_LimitVelocityAng
				);
				return;
			}
			else if (0 == _stricmp("limAngAcel", arg1))
			{
				if (3 <= argc)
				{
					m_LimitAccelerationAng = atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s limAngAcel <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_LimitAccelerationAng
				);
				return;
			}
			else if (0 == _stricmp("rotShortestPath", arg1))
			{
				if (3 <= argc)
				{
					m_RotShortestPath = 0 != atoi(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s rotShortestPath 0|1 - Set new value.\n"
					"Current value: %i\n"
					, arg0
					, m_RotShortestPath ? 1: 0
				);
				return;
			}
		}

		Tier0_Msg(
			"%s limXVelo [...] - X-location velocity limit in inch.\n"
			"%s limXAcel [...] - X-location acceleration limit in inch.\n"
			"%s limYVelo [...] - y-location velocity limit in inch.\n"
			"%s limYAcel [...] - Y-location acceleration limit in inch.\n"
			"%s limZVelo [...] - Z-location velocity limit in inch.\n"
			"%s limZAcel [...] - Z-location acceleration limit in inch.\n"
			"%s limAngVelo [...] - Angular velocity limit in degrees.\n"
			"%s limAngAcel [...] - Angular acceleration limit in degrees.\n"
			"%s rotShortestPath [...] - If to rotate shortest path.\n"
			, arg0
			, arg0
			, arg0
			, arg0
			, arg0
			, arg0
			, arg0
			, arg0
			, arg0
		);
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		SOURCESDK::Vector parentVector;
		SOURCESDK::QAngle parentAngles;

		SOURCESDK::CSGO::CBaseHandle handle;

		bool calcedParent = m_Parent->CalcVecAng(parentVector, parentAngles);
		bool calcedHandle = m_TrackHandle->CalcHandle(handle);

		if (m_LevelInitCount != g_LevelInitCount)
		{
			m_LevelInitCount = g_LevelInitCount;
			m_Reset = true;
		}

		m_Reset = m_Reset || !(calcedParent && calcedHandle && m_LastHandle == handle);

		if (calcedHandle) m_LastHandle = handle;

		if (calcedParent && calcedHandle)
		{
			if (m_Reset)
			{
				m_Reset = false;

				m_LastClientTime = g_MirvTime.GetTime();

				m_LastX = parentVector.x;
				m_LastY = parentVector.y;
				m_LastZ = parentVector.z;

				m_XVelocity = 0;
				m_YVelocity = 0;
				m_ZVelocity = 0;

				outVector.x = (float)m_LastX;
				outVector.y = (float)m_LastY;
				outVector.z = (float)m_LastZ;

				outAngles.x = parentAngles.x;
				outAngles.y = parentAngles.y;
				outAngles.z = parentAngles.z;
				m_LastAngVelocity = 0;
				m_LastOutQuat = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(outAngles.x, outAngles.y, outAngles.z)));
			}
			else
			{
				float clientTime = g_MirvTime.GetTime();
				double deltaT = clientTime - m_LastClientTime;
				m_LastClientTime = clientTime;

				Quaternion targetQuat = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(parentAngles.x, parentAngles.y, parentAngles.z))).Normalized();

				if (m_RotShortestPath)
				{
					// Take shortest path:
					double dotProduct = DotProduct(targetQuat, m_LastOutQuat);
					if (dotProduct < 0)
					{
						targetQuat = -1.0 * targetQuat;
					}
				}

				double targetAngle = m_LastOutQuat.GetAng(targetQuat, Vector3()) * 180.0 / M_PI;
				double angle;

				CalcDeltaSmooth(deltaT, targetAngle, angle, m_LastAngVelocity, m_LimitVelocityAng, m_LimitAccelerationAng);

				if (abs(angle) > AFX_MATH_EPS)
				{
					m_LastOutQuat = m_LastOutQuat.Slerp(targetQuat, angle / targetAngle).Normalized();
				}

				QEulerAngles newAngles = m_LastOutQuat.ToQREulerAngles().ToQEulerAngles();

				outAngles.x = (float)newAngles.Pitch;
				outAngles.y = (float)newAngles.Yaw;
				outAngles.z = (float)newAngles.Roll;

				//

				CalcSmooth(deltaT, parentVector.x, m_LastX, m_XVelocity, m_LimitVelocityX, m_LimitAccelerationX);
				outVector.x = (float)m_LastX;

				CalcSmooth(deltaT, parentVector.y, m_LastY, m_YVelocity, m_LimitVelocityY, m_LimitAccelerationY);
				outVector.y = (float)m_LastY;

				CalcSmooth(deltaT, parentVector.z, m_LastZ, m_ZVelocity, m_LimitVelocityZ, m_LimitAccelerationZ);
				outVector.z = (float)m_LastZ;
			}

			return true;
		}

		return false;
	}

protected:
	virtual ~CMirvVecAngMotionProfile2Calc()
	{
		m_TrackHandle->Release();
		m_Parent->Release();
	}

private:
	IMirvVecAngCalc * m_Parent;
	IMirvHandleCalc * m_TrackHandle;

	bool m_Reset = true;
	int m_LevelInitCount = 0;
	SOURCESDK::CSGO::CBaseHandle m_LastHandle;
	float m_LastClientTime = 0;
	bool m_RotShortestPath = true;

	double m_LastX = 0;
	double m_LastY = 0;
	double m_LastZ = 0;
	double m_XVelocity = 0;
	double m_ZVelocity = 0;
	double m_YVelocity = 0;

	double m_LimitVelocityX = 600;
	double m_LimitAccelerationX = 60;
	double m_LimitVelocityY = 600;
	double m_LimitAccelerationY = 60;
	double m_LimitVelocityZ = 600;
	double m_LimitAccelerationZ = 60;

	Quaternion m_LastOutQuat;
	double m_LastAngVelocity = 0;

	double m_LimitVelocityAng = 360;
	double m_LimitAccelerationAng = 90;
};

class CMirvVecAngAddCalc : public CMirvVecAngCalc
{
public:
	CMirvVecAngAddCalc(char const * name, IMirvVecAngCalc * a, IMirvVecAngCalc * b)
		: CMirvVecAngCalc(name)
		, m_A(a)
		, m_B(b)
	{
		m_A->AddRef();
		m_B->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"add\"");
		Tier0_Msg(", a: "); m_A->Console_PrintBegin(); m_A->Console_PrintEnd();
		Tier0_Msg(", b: "); m_B->Console_PrintBegin(); m_B->Console_PrintEnd();
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvVecAngCalc::Console_Edit(args);
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		SOURCESDK::Vector aVector;
		SOURCESDK::QAngle aAngles;
		SOURCESDK::Vector bVector;
		SOURCESDK::QAngle bAngles;
		bool cacledA = m_A->CalcVecAng(aVector, aAngles);
		bool calcedB = cacledA && m_B->CalcVecAng(bVector, bAngles);

		if (cacledA && calcedB)
		{
			// Add location:

			outVector.x = aVector.x + bVector.x;
			outVector.y = aVector.y + bVector.y;
			outVector.z = aVector.z + bVector.z;

			// Add rotation:

			Quaternion quatA = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(aAngles.x, aAngles.y, aAngles.z)));
			Quaternion quatB = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(bAngles.x, bAngles.y, bAngles.z)));

			Quaternion quatR = quatB * quatA;

			QEulerAngles angles = quatR.ToQREulerAngles().ToQEulerAngles();

			outAngles.x = (float)angles.Pitch;
			outAngles.y = (float)angles.Yaw;
			outAngles.z = (float)angles.Roll;

			return true;
		}

		return false;
	}

protected:
	virtual ~CMirvVecAngAddCalc()
	{
		m_B->Release();
		m_A->Release();
	}

private:
	IMirvVecAngCalc * m_A;
	IMirvVecAngCalc * m_B;
};

class CMirvVecAngSubtractCalc : public CMirvVecAngCalc
{
public:
	CMirvVecAngSubtractCalc(char const * name, IMirvVecAngCalc * a, IMirvVecAngCalc * b)
		: CMirvVecAngCalc(name)
		, m_A(a)
		, m_B(b)
	{
		m_A->AddRef();
		m_B->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"subtract\"");
		Tier0_Msg(", a: "); m_A->Console_PrintBegin(); m_A->Console_PrintEnd();
		Tier0_Msg(", b: "); m_B->Console_PrintBegin(); m_B->Console_PrintEnd();
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvVecAngCalc::Console_Edit(args);
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		SOURCESDK::Vector sourceVector;
		SOURCESDK::QAngle sourceAngles;
		SOURCESDK::Vector targetVector;
		SOURCESDK::QAngle targetAngles;
		bool cacledSource = m_B->CalcVecAng(sourceVector, sourceAngles);
		bool calcedTarget = cacledSource && m_A->CalcVecAng(targetVector, targetAngles);

		if (cacledSource && calcedTarget)
		{
			// Delta location:

			outVector.x = targetVector.x - sourceVector.x;
			outVector.y = targetVector.y - sourceVector.y;
			outVector.z = targetVector.z - sourceVector.z;

			// Delta rotation:

			Quaternion quatSource = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(sourceAngles.x, sourceAngles.y, sourceAngles.z)));
			Quaternion quatDest = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(targetAngles.x, targetAngles.y, targetAngles.z)));

			// Make sure we take the shortest path:
			double dotProduct = DotProduct(quatDest, quatSource);
			if (dotProduct < 0.0)
			{
				quatSource = -1.0 * quatSource;
			}

			Quaternion quatR = quatDest * (/*(1.0 / quatSource.Norm()) * */quatSource.Conjugate());

			QEulerAngles angles = quatR.ToQREulerAngles().ToQEulerAngles();

			outAngles.x = (float)angles.Pitch;
			outAngles.y = (float)angles.Yaw;
			outAngles.z = (float)angles.Roll;


			return true;
		}

		return false;
	}

protected:
	virtual ~CMirvVecAngSubtractCalc()
	{
		m_B->Release();
		m_A->Release();
	}

private:
	IMirvVecAngCalc * m_A;
	IMirvVecAngCalc * m_B;
};

class CMirvVecAngSwitchInterpCalc : public CMirvVecAngCalc
{
public:
	CMirvVecAngSwitchInterpCalc(char const * name, IMirvVecAngCalc * source, IMirvHandleCalc * switchHandle, IMirvHandleCalc * resetHandle, float holdTime, float interpTime)
		: CMirvVecAngCalc(name)
		, m_Source(source)
		, m_SwitchHandle(switchHandle)
		, m_ResetHandle(resetHandle)
		, m_HoldTime(holdTime)
		, m_InterpTime(interpTime)
		, m_LastPos(0.0 , 0.0, 0.0)
		, m_LastQuat(1.0, 0.0, 0.0, 0.0)
	{
		m_Source->AddRef();
		m_SwitchHandle->AddRef();
		m_ResetHandle->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"switchInterp\"");
		Tier0_Msg(", source: "); m_Source->Console_PrintBegin(); m_Source->Console_PrintEnd();
		Tier0_Msg(", switchHandle: "); m_SwitchHandle->Console_PrintBegin(); m_SwitchHandle->Console_PrintEnd();
		Tier0_Msg(", resetHandle: "); m_ResetHandle->Console_PrintBegin(); m_ResetHandle->Console_PrintEnd();
		Tier0_Msg(", holdTime: %f, interpTime: %f", (float)m_HoldTime, (float)m_InterpTime);
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		int argc = args->ArgC();
		char const * arg0 = args->ArgV(0);

		if (2 <= argc)
		{
			char const * arg1 = args->ArgV(1);

			if (0 == _stricmp("holdTime", arg1))
			{
				if (3 <= argc)
				{
					m_HoldTime = atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s holdTime <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_HoldTime
				);
				return;
			}
			else if (0 == _stricmp("interpTime", arg1))
			{
				if (3 <= argc)
				{
					m_InterpTime = atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s interpTime <fValue> - Set new value.\n"
					"Current value: %f\n"
					, arg0
					, m_InterpTime
				);
				return;
			}
		}

		Tier0_Msg(
			"%s holdTime [...].\n"
			"%s interpTime [...]\n"
			, arg0
			, arg0
		);
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		SOURCESDK::CSGO::CBaseHandle handle;
		SOURCESDK::CSGO::CBaseHandle resetHandle;

		if (m_SwitchHandle->CalcHandle(handle) && m_ResetHandle->CalcHandle(resetHandle))
		{
			float curTime = g_MirvTime.GetTime();

			if (m_LastResetHandle != resetHandle)
			{
				m_LastResetHandle = resetHandle;
				m_Mode = Mode_Init;
			}

			if (Mode_Init == m_Mode)
			{
				m_LastHandle = handle;
				m_Mode = Mode_Pasthrough;
			}

			if (m_LastHandle != handle)
			{
				m_LastHandle = handle;
				m_Mode = Mode_Hold;
				m_SwitchedTime = curTime;
			}

			if (Mode_Hold == m_Mode)
			{
				if (m_HoldTime <= std::fabs(curTime - m_SwitchedTime))
					m_Mode = Mode_Interp;
			}

			if (Mode_Interp == m_Mode)
			{
				if ((m_InterpTime + m_HoldTime) <= std::fabs(curTime - m_SwitchedTime))
					m_Mode = Mode_Pasthrough;
			}

			SOURCESDK::Vector sourceVector;
			SOURCESDK::QAngle sourceAngles;

			if (m_Source->CalcVecAng(sourceVector, sourceAngles))
			{
				switch (m_Mode)
				{
				case Mode_Hold:
				{
					outVector.x = (float)m_LastPos.X;
					outVector.y = (float)m_LastPos.Y;
					outVector.z = (float)m_LastPos.Z;
					QEulerAngles angles = m_LastQuat.ToQREulerAngles().ToQEulerAngles();
					outAngles.x = (float)angles.Pitch;
					outAngles.y = (float)angles.Yaw;
					outAngles.z = (float)angles.Roll;
					return true;
				}
				case Mode_Interp:
				{
					double t = 0 != m_InterpTime ? (curTime - m_HoldTime - m_SwitchedTime) / m_InterpTime : 0;
	
					Vector3 pos = (1 - t) * m_LastPos + t * Vector3((double)sourceVector.x, (double)sourceVector.y, (double)sourceVector.z);
					
					outVector.x = (float)pos.X;
					outVector.y = (float)pos.Y;
					outVector.z = (float)pos.Z;

					Quaternion sourceQuat = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(sourceAngles.x, sourceAngles.y, sourceAngles.z))).Normalized();

					double dotProduct = DotProduct(sourceQuat, m_LastQuat);

					if (dotProduct < 0)
					{
						sourceQuat = -1.0 * sourceQuat;
					}
					
					QEulerAngles newAngles = m_LastQuat.Slerp(sourceQuat, t).Normalized().ToQREulerAngles().ToQEulerAngles();

					outAngles.x = (float)newAngles.Pitch;
					outAngles.y = (float)newAngles.Yaw;
					outAngles.z = (float)newAngles.Roll;

					return true;
				}
				default:
					outVector = sourceVector;
					outAngles = sourceAngles;
					m_LastPos.X = sourceVector.x;
					m_LastPos.Y = sourceVector.y;
					m_LastPos.Z = sourceVector.z;
					m_LastQuat = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(sourceAngles.x, sourceAngles.y, sourceAngles.z)));
					return true;
				}
			}
		}

		return false;
	}

protected:
	virtual ~CMirvVecAngSwitchInterpCalc()
	{
		m_ResetHandle->Release();
		m_SwitchHandle->Release();
		m_Source->Release();
	}

private:
	IMirvVecAngCalc * m_Source;
	IMirvHandleCalc * m_SwitchHandle;
	IMirvHandleCalc * m_ResetHandle;
	double m_HoldTime;
	double m_InterpTime;
	enum Mode_e {
		Mode_Init,
		Mode_Pasthrough,
		Mode_Hold,
		Mode_Interp
	} m_Mode = Mode_Init;
	float m_SwitchedTime = 0;
	SOURCESDK::CSGO::CBaseHandle m_LastHandle;
	SOURCESDK::CSGO::CBaseHandle m_LastResetHandle;
	Vector3 m_LastPos;
	Quaternion m_LastQuat;
};


#ifndef _WIN64
class CMirvVecAngLocalToGlobalCalc : public CMirvVecAngCalc
{
public:
	CMirvVecAngLocalToGlobalCalc(char const * name, IMirvVecAngCalc * source, IMirvHandleCalc * handle)
		: CMirvVecAngCalc(name)
		, m_Source(source)
		, m_Handle(handle)
	{
		m_Source->AddRef();
		m_Handle->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"localToGlobal\"");
		Tier0_Msg(", source: "); m_Source->Console_PrintBegin(); m_Source->Console_PrintEnd();
		Tier0_Msg(", handle: "); m_Handle->Console_PrintBegin(); m_Handle->Console_PrintEnd();
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvVecAngCalc::Console_Edit(args);
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		SOURCESDK::CSGO::CBaseHandle handle;
		SOURCESDK::Vector sourceVector;
		SOURCESDK::QAngle sourceAngles;

		if (m_Handle->CalcHandle(handle) && m_Source->CalcVecAng(sourceVector, sourceAngles))
		{
			SOURCESDK::IClientEntity_csgo * ce = Mirv_GetClientEntityFromHandle(handle);
			SOURCESDK::IClientRenderable_csgo * re = ce ? ce->GetClientRenderable() : 0;

			if (re)
			{
				const SOURCESDK::matrix3x4_t & matrix = re->RenderableToWorldTransform();
				
				SOURCESDK::VectorTransform(sourceVector, matrix, outVector);

				SOURCESDK::QAngle matrixAngles;

				SOURCESDK::MatrixAngles(matrix, matrixAngles);

				Quaternion sourceQuat = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(sourceAngles.x, sourceAngles.y, sourceAngles.z)));
				Quaternion matrixQuat = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(matrixAngles.x, matrixAngles.y, matrixAngles.z)));

				QEulerAngles angles = (matrixQuat * sourceQuat).ToQREulerAngles().ToQEulerAngles();
				outAngles.x = (float)angles.Pitch;
				outAngles.y = (float)angles.Yaw;
				outAngles.z = (float)angles.Roll;

				return true;
			}

			return false;

			return true;
		}

		return false;
	}

protected:
	virtual ~CMirvVecAngLocalToGlobalCalc()
	{
		m_Handle->Release();
		m_Source->Release();
	}

private:
	IMirvVecAngCalc * m_Source;
	IMirvHandleCalc * m_Handle;
};
#endif //#ifndef _WIN64

#ifndef _WIN64
class CMirvVecAngGlobalToLocalCalc : public CMirvVecAngCalc
{
public:
	CMirvVecAngGlobalToLocalCalc(char const * name, IMirvVecAngCalc * source, IMirvHandleCalc * handle)
		: CMirvVecAngCalc(name)
		, m_Source(source)
		, m_Handle(handle)
	{
		m_Source->AddRef();
		m_Handle->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"globalToLocal\"");
		Tier0_Msg(", source: "); m_Source->Console_PrintBegin(); m_Source->Console_PrintEnd();
		Tier0_Msg(", handle: "); m_Handle->Console_PrintBegin(); m_Handle->Console_PrintEnd();
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvVecAngCalc::Console_Edit(args);
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		SOURCESDK::CSGO::CBaseHandle handle;
		SOURCESDK::Vector sourceVector;
		SOURCESDK::QAngle sourceAngles;

		if (m_Handle->CalcHandle(handle) && m_Source->CalcVecAng(sourceVector, sourceAngles))
		{
			SOURCESDK::IClientEntity_csgo * ce = Mirv_GetClientEntityFromHandle(handle);
			SOURCESDK::IClientRenderable_csgo * re = ce ? ce->GetClientRenderable() : 0;

			if (re)
			{
				const SOURCESDK::matrix3x4_t & matrix = re->RenderableToWorldTransform();

				SOURCESDK::matrix3x4_t transposed;
				
				transposed[0][0] = matrix[0][0];
				transposed[0][1] = matrix[1][0];
				transposed[0][2] = matrix[2][0];

				transposed[1][0] = matrix[0][1];
				transposed[1][1] = matrix[1][1];
				transposed[1][2] = matrix[2][1];

				transposed[2][0] = matrix[0][2];
				transposed[2][1] = matrix[1][2];
				transposed[2][2] = matrix[2][2];

				float tmp[3];
				tmp[0] = matrix[0][3];
				tmp[1] = matrix[1][3];
				tmp[2] = matrix[2][3];

				transposed[0][3] = -SOURCESDK::DotProduct(tmp, transposed[0]);
				transposed[1][3] = -SOURCESDK::DotProduct(tmp, transposed[1]);
				transposed[2][3] = -SOURCESDK::DotProduct(tmp, transposed[2]);

				SOURCESDK::VectorTransform(sourceVector, transposed, outVector);

				SOURCESDK::QAngle matrixAngles;

				SOURCESDK::MatrixAngles(transposed, matrixAngles);

				Quaternion sourceQuat = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(sourceAngles.x, sourceAngles.y, sourceAngles.z)));
				Quaternion matrixQuat = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(matrixAngles.x, matrixAngles.y, matrixAngles.z)));

				QEulerAngles angles = (matrixQuat * sourceQuat).ToQREulerAngles().ToQEulerAngles();
				outAngles.x = (float)angles.Pitch;
				outAngles.y = (float)angles.Yaw;
				outAngles.z = (float)angles.Roll;

				return true;
			}

			return false;

			return true;
		}

		return false;
	}

protected:
	virtual ~CMirvVecAngGlobalToLocalCalc()
	{
		m_Handle->Release();
		m_Source->Release();
	}

private:
	IMirvVecAngCalc * m_Source;
	IMirvHandleCalc * m_Handle;
};
#endif //#ifndef _WIN64


#ifndef _WIN64
extern SOURCESDK::CSGO::IEngineTrace * g_pClientEngineTrace;
#endif //#ifndef _WIN64

#ifndef _WIN64
class CMirvVecAngCollisionClipCalc : public CMirvVecAngCalc
{
public:
	CMirvVecAngCollisionClipCalc(char const * name, IMirvVecAngCalc * source, IMirvVecAngCalc * clipTo, IMirvHandleCalc * ignoreHandle, float safeDist)
		: CMirvVecAngCalc(name)
		, m_Source(source)
		, m_ClipTo(clipTo)
		, m_Ignore(ignoreHandle)
		, m_SafeZoneDist(safeDist)
	{
		m_Source->AddRef();
		m_ClipTo->AddRef();
		m_Ignore->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"globalToLocal\"");
		Tier0_Msg(", source: "); m_Source->Console_PrintBegin(); m_Source->Console_PrintEnd();
		Tier0_Msg(", clipTo: "); m_ClipTo->Console_PrintBegin(); m_ClipTo->Console_PrintEnd();
		Tier0_Msg(", ignore: "); m_Ignore->Console_PrintBegin(); m_Ignore->Console_PrintEnd();
		Tier0_Msg(", safeZoneDist: %f", m_SafeZoneDist);
		Tier0_Msg(", smoothTime: %f", m_SmoothTime);
		Tier0_Msg(", sleepTime: %f", m_SleepTime);
		Tier0_Msg(", ignoreSome: %i", m_IgnoreSomeEntites ? 1 : 0);
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		int argc = args->ArgC();
		char const * arg0 = args->ArgV(0);

		if (2 <= argc)
		{
			char const * arg1 = args->ArgV(1);

			if (0 == _stricmp("safeZoneDist", arg1))
			{
				if (3 <= argc)
				{
					m_SafeZoneDist = (float)atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s safeZoneDist <fSaveZoneDist> - Set safe zone distance.\n"
					"Current value: %f\n"
					, arg0
					, m_SafeZoneDist
				);
				return;
			}
			else if (0 == _stricmp("smoothTime", arg1))
			{
				if (3 <= argc)
				{
					m_SmoothTime = (float)atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s smoothTime <fValue> - Set 0 to disable, greater than 0 for smoothing out time in seconds.\n"
					"Current value: %f\n"
					, arg0
					, m_SmoothTime
				);
				return;
			}
			else if (0 == _stricmp("sleepTime", arg1))
			{
				if (3 <= argc)
				{
					m_SleepTime = (float)atof(args->ArgV(2));
					return;
				}

				Tier0_Msg(
					"%s sleepTime <fValue> - Set time to hold before smoothing out.\n"
					"Current value: %f\n"
					, arg0
					, m_SleepTime
				);
				return;
			}
			else if (0 == _stricmp("ignoreSome", arg1))
			{
				if (3 <= argc)
				{
					m_IgnoreSomeEntites = (0 != atoi(args->ArgV(2)));
					return;
				}

				Tier0_Msg(
					"%s ignoreSome 0|1 - If to ignore some more entities.\n"
					"Current value: %i\n"
					, arg0
					, (m_IgnoreSomeEntites ? 1 : 0)
				);
				return;
			}
		}

		Tier0_Msg(
			"%s safeZoneDist [...]\n"
			"%s smoothTime [...]\n"
			"%s sleepTime [...]\n"
			"%s ignoreSome [...]\n"
			, arg0
			, arg0
			, arg0
			, arg0
		);
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		SOURCESDK::CSGO::CBaseHandle ignore;
		SOURCESDK::Vector sourceVector;
		SOURCESDK::QAngle sourceAngles;
		SOURCESDK::Vector clipToVector;
		SOURCESDK::QAngle clipToAngles;

		if (m_Ignore->CalcHandle(ignore) && m_Source->CalcVecAng(sourceVector, sourceAngles) && m_ClipTo->CalcVecAng(clipToVector, clipToAngles) )
		{
			SOURCESDK::IClientEntity_csgo* ignoreEnt = Mirv_GetClientEntityFromHandle(ignore);
			CTraceFilter * pTraceFilter = m_IgnoreSomeEntites ? new CTraceFilterSome() : new CTraceFilter();
			if(ignoreEnt) pTraceFilter->Add(ignoreEnt);

			SOURCESDK::C_BaseEntity_csgo * localPlayer = reinterpret_cast<SOURCESDK::C_BaseEntity_csgo *>(CClientToolsCsgo::Instance()->GetClientToolsInterface()->GetLocalPlayer());
			if(localPlayer && localPlayer->GetIClientEntity() != ignoreEnt) pTraceFilter->Add(localPlayer);

			SOURCESDK::Vector traceEnd = sourceVector;
			SOURCESDK::Vector vDirOrg = (sourceVector - clipToVector);
			SOURCESDK::Vector vTrace = vDirOrg;
			float sourceLenSqr = vTrace.LengthSqr();
			float bestLenSqr = sourceLenSqr;
			if(sourceLenSqr >= AFX_MATH_EPS) {
				float sourceLen = std::sqrtf(sourceLenSqr);
				vDirOrg *= 1 / sourceLen;
				vTrace = vDirOrg;
				vTrace *= SOURCESDK_CSGO_MAX_TRACE_LENGTH;
				traceEnd = clipToVector + vTrace;
			}

			// c^2 = d^2 + d^2
			// c = sqrt(2) * d
			const float ofsLen = 1.4142135623730950488016887242097f * m_SafeZoneDist;

			float ofsSourceVecs[5][3] = {
				{0,0,0},
				{0,ofsLen,ofsLen},
				{0,ofsLen,-ofsLen},
				{0,-ofsLen,-ofsLen},
				{0,-ofsLen,ofsLen},
			};

			SOURCESDK::QAngle traceAngs;
			SOURCESDK::VectorAngles(vDirOrg,traceAngs);
			SOURCESDK::matrix3x4_t traceRotMatrix;
			SOURCESDK::AngleMatrix(traceAngs,traceRotMatrix);

			for(int i =0; i < 5; ++i) {
				SOURCESDK::Vector vOffs;
				SOURCESDK::VectorTransform(SOURCESDK::Vector(ofsSourceVecs[i][0],ofsSourceVecs[i][1],ofsSourceVecs[i][2]), traceRotMatrix, vOffs);

				SOURCESDK::Vector srcVec = clipToVector + vOffs;
				SOURCESDK::Vector dstVec = traceEnd + vOffs;

				SOURCESDK::CSGO::Ray_t ray;
				ray.Init(srcVec, dstVec);

				SOURCESDK::CSGO::trace_t tr;
				g_pClientEngineTrace->TraceRay(ray, SOURCESDK_CSGO_MASK_ALL, pTraceFilter, &tr);

				if (tr.DidHit())
				{
					SOURCESDK::Vector vDir = tr.endpos - tr.startpos;
					float curLenSqr = vDir.LengthSqr();

					//Tier0_Msg("[%d]: %f\n", i, std::sqrtf(vDir.LengthSqr()));

					if(curLenSqr > bestLenSqr + m_SafeZoneDist) {
						continue;	
					}

					if(tr.allsolid || m_SafeZoneDist <= AFX_MATH_EPS) {
						// plane invalid
						// OR safezone is 0
						bestLenSqr = curLenSqr;
						continue;
					}

					// https://en.wikipedia.org/wiki/Intercept_theorem
					// https://math.stackexchange.com/a/100783 (Projecting tr.startpos onto the plane using the plane normal)

					float s = tr.plane.normal.LengthSqr();
					float c = AFX_MATH_EPS <= s ? -(vDir.x * tr.plane.normal.x + vDir.y * tr.plane.normal.y + vDir.z * tr.plane.normal.z) / tr.plane.normal.LengthSqr() : 0;
					SOURCESDK::Vector cV = tr.plane.normal;
					cV *= c;
					c = std::sqrtf(cV.LengthSqr());
					float t = 0 != c ? 1 - m_SafeZoneDist / c : 1;

					if(t < 0) t = 0;
					else if(t > 1) t = 1;

					vDir *= t;
					bestLenSqr = std::min(bestLenSqr, vDir.LengthSqr());
					continue;
				}
			}

			float clientTime = g_MirvTime.GetTime();

			float bestLen = std::sqrtf(bestLenSqr);

			if (m_LevelInitCount != g_LevelInitCount) {
				m_LevelInitCount = g_LevelInitCount;

				m_LastLen = bestLen;
				m_LastTime = clientTime;
				m_LastVelocity = 0;
				m_SleepLeft = m_SleepTime;
			}

			float newLen;
			if(m_LastLen <= bestLen) {
				if(0 < m_SleepTime) m_SleepLeft = std::min(m_SleepLeft,(double)m_SleepTime); else m_SleepLeft = 0;
				double deltaT = clientTime -m_LastTime - m_SleepLeft;
				if(0 < deltaT) {
					if(0 < m_SmoothTime) {
						double newDelta = 0;
						double deltaY = bestLen - m_LastLen;
						if(AFX_MATH_EPS < deltaY) {
							CalcDeltaSmooth(deltaT, deltaY, newDelta, m_LastVelocity, 2.0f * deltaY / m_SmoothTime, 2.0f * deltaY / m_SmoothTime / m_SmoothTime);
							newLen = m_LastLen + newDelta;
						} else {
							newLen = m_LastLen;
						}
					} else {
						newLen = bestLen;
						m_LastVelocity = 0;
						m_SleepLeft = m_SleepTime;
					}
				} else {
					newLen = m_LastLen;
					m_LastVelocity = 0;
					m_SleepLeft = -deltaT;
				}
			} else {
				newLen = bestLen;
				m_LastVelocity = 0;
				m_SleepLeft = m_SleepTime;
			}
			vDirOrg *= newLen;

			outVector = clipToVector + vDirOrg;
			outAngles = sourceAngles;

			delete pTraceFilter;

			m_LastLen = newLen;
			m_LastTime = clientTime;
			return true;
		}

		return false;
	}

protected:
	virtual ~CMirvVecAngCollisionClipCalc()
	{
		m_Ignore->Release();
		m_ClipTo->Release();
		m_Source->Release();
	}

private:
	IMirvVecAngCalc * m_Source;
	IMirvVecAngCalc * m_ClipTo;
	IMirvHandleCalc * m_Ignore;
	float m_SafeZoneDist;
	float m_SmoothTime = 0;
	float m_SleepTime = 0;
	bool m_IgnoreSomeEntites = false;
	int m_LevelInitCount = 0;
	float m_LastLen = 0;
	float m_LastTime = 0;
	double m_LastVelocity = 0;
	double m_SleepLeft = 0;

	class CTraceFilter : public SOURCESDK::CSGO::CTraceFilter
	{
	public:
		CTraceFilter() {

		}

		virtual ~CTraceFilter() {}

		void Add(SOURCESDK::CSGO::IHandleEntity *pHandleEntity) {
			m_PassEnts.emplace(pHandleEntity);
		}
		
		virtual bool ShouldHitEntity(SOURCESDK::CSGO::IHandleEntity *pHandleEntity, int contentsMask) {
			return pHandleEntity && m_PassEnts.end() == m_PassEnts.find(pHandleEntity);
		}

	private:
		std::set<const SOURCESDK::CSGO::IHandleEntity *> m_PassEnts;
	};

	class CTraceFilterSome : public CTraceFilter
	{
	public:
		CTraceFilterSome() {

		}

		virtual bool ShouldHitEntity(SOURCESDK::CSGO::IHandleEntity *pHandleEntity, int contentsMask) {
			if(!CTraceFilter::ShouldHitEntity(pHandleEntity,contentsMask))
				return false;

			if(SOURCESDK::IClientEntity_csgo* clientEntity = Mirv_GetClientEntityFromHandle(pHandleEntity->GetRefEHandle())) {
				if (SOURCESDK::C_BaseEntity_csgo* baseEntity = clientEntity->GetBaseEntity())
					return baseEntity->ShouldCollide(SOURCESDK::CSGO::COLLISION_GROUP_PLAYER_MOVEMENT, SOURCESDK_CSGO_CONTENTS_SOLID|SOURCESDK_CSGO_CONTENTS_WINDOW|SOURCESDK_CSGO_CONTENTS_MOVEABLE|SOURCESDK_CSGO_CONTENTS_GRATE)
						&& !StringBeginsWith(baseEntity->GetClassname(), "weapon_");
			}			

			return true;
		}
	};

};
#endif //#ifndef _WIN64

class CMirvCamCamCalc : public CMirvCamCalc
{
public:
	CMirvCamCamCalc(char const * name, const char * camFileName, const char * startClientTime)
		: CMirvCamCalc(name)
		, m_CamFileName(camFileName)
		, m_StartClientTime(StartClientTimeFromString(startClientTime))
	{
		m_CamImport = new CamImport(m_CamFileName.c_str(), m_StartClientTime);
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvCamCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"cam\", camFileName: \"%s\", startClientTime: %f", m_CamFileName.c_str(), m_StartClientTime);
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		int argc = args->ArgC();
		char const * arg0 = args->ArgV(0);

		if (2 <= argc)
		{
			char const * arg1 = args->ArgV(1);

			if (0 == _stricmp("filePath", arg1))
			{
				if (3 <= argc)
				{
					m_CamFileName = args->ArgV(2);
					delete m_CamImport;
					m_CamImport = new CamImport(m_CamFileName.c_str(), m_StartClientTime);
					if(m_CamImport->IsBad()) Tier0_Warning("Error importing CAM file \"%s\"\n", m_CamFileName.c_str());
					return;
				}

				Tier0_Msg(
					"%s filePath <sFilePath> - Set mirv_camio input file name / path.\n"
					"Current value: %s\n"
					, arg0
					, m_CamFileName.c_str()
				);
				return;
			}
			else if (0 == _stricmp("startTime", arg1))
			{
				if (3 <= argc)
				{
					m_StartClientTime = StartClientTimeFromString(args->ArgV(2));
					m_CamImport->SetStart(m_StartClientTime);
					return;
				}

				Tier0_Msg(
					"%s startTime <fStartTime>|current - Set mirv_camio input file name / path.\n"
					"Current value: %f\n"
					, arg0
					, m_StartClientTime
				);
				return;
			}
		}

		Tier0_Msg(
			"%s filePath [...]\n"
			"%s startTime [...]\n"
			, arg0
			, arg0
		);
	}

	virtual bool CalcCam(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles, float & outFov)
	{
		CamIO::CamData outCamData;

		if (m_CamImport->GetCamData(g_MirvTime.GetTime(), g_Hook_VClient_RenderView.LastWidth, g_Hook_VClient_RenderView.LastHeight, outCamData))
		{
			outVector.x = (float)outCamData.XPosition;
			outVector.y = (float)outCamData.YPosition;
			outVector.z = (float)outCamData.ZPosition;
			outAngles.x = (float)outCamData.YRotation;
			outAngles.y = (float)outCamData.ZRotation;
			outAngles.z = (float)outCamData.XRotation;
			outFov = (float)outCamData.Fov;

			return true;
		}

		return false;
	}

	virtual bool IsBad() {
		return m_CamImport->IsBad();
	}

protected:
	virtual ~CMirvCamCamCalc()
	{
		delete m_CamImport;
	}

private:
	CamImport * m_CamImport;
	std::string m_CamFileName;
	double m_StartClientTime;

	double StartClientTimeFromString(const char * startClientTime)
	{
		return 0 == _stricmp(startClientTime, "current") ? g_MirvTime.GetTime() : atof(startClientTime);
	}
};

class CMirvCamGameCalc : public CMirvCamCalc
{
public:
	CMirvCamGameCalc(char const * name)
		: CMirvCamCalc(name)
	{
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvCamCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"game\"");
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvCamCalc::Console_Edit(args);
	}

	virtual bool CalcCam(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles, float & outFov)
	{
		outVector.x = g_Hook_VClient_RenderView.GameCameraOrigin[0];
		outVector.y = g_Hook_VClient_RenderView.GameCameraOrigin[1];
		outVector.z = g_Hook_VClient_RenderView.GameCameraOrigin[2];
		outAngles.x = g_Hook_VClient_RenderView.GameCameraAngles[0];
		outAngles.y = g_Hook_VClient_RenderView.GameCameraAngles[1];
		outAngles.z = g_Hook_VClient_RenderView.GameCameraAngles[2];
		outFov = g_Hook_VClient_RenderView.GameCameraFov;

		return true;
	}

protected:
	virtual ~CMirvCamGameCalc()
	{
	}

private:

};


class CMirvCamCurrentCalc : public CMirvCamCalc
{
public:
	CMirvCamCurrentCalc(char const * name)
		: CMirvCamCalc(name)
	{
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvCamCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"current\"");
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvCamCalc::Console_Edit(args);
	}

	virtual bool CalcCam(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles, float & outFov)
	{
		outVector.x = (float)g_Hook_VClient_RenderView.CurrentCameraOrigin[0];
		outVector.y = (float)g_Hook_VClient_RenderView.CurrentCameraOrigin[1];
		outVector.z = (float)g_Hook_VClient_RenderView.CurrentCameraOrigin[2];
		outAngles.x = (float)g_Hook_VClient_RenderView.CurrentCameraAngles[0];
		outAngles.y = (float)g_Hook_VClient_RenderView.CurrentCameraAngles[1];
		outAngles.z = (float)g_Hook_VClient_RenderView.CurrentCameraAngles[2];
		outFov = (float)g_Hook_VClient_RenderView.CurrentCameraFov;

		return true;
	}

protected:
	virtual ~CMirvCamCurrentCalc()
	{
	}

private:

};


#ifndef _WIN64
class CMirvCamPlayerCalc : public CMirvCamCalc
{
public:
	CMirvCamPlayerCalc(char const* name, IMirvHandleCalc * handleCalc)
		: CMirvCamCalc(name)
		, m_HandleCalc(handleCalc)
	{
		m_HandleCalc->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvCamCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"player\"");
	}

	virtual void Console_Edit(IWrpCommandArgs* args)
	{
		CMirvCamCalc::Console_Edit(args);
	}

	virtual bool CalcCam(SOURCESDK::Vector& outVector, SOURCESDK::QAngle& outAngles, float& outFov)
	{
		SOURCESDK::CSGO::CBaseHandle handle;

		if (m_HandleCalc->CalcHandle(handle) && handle.IsValid())
		{
			SOURCESDK::IClientEntity_csgo* clientEntity = Mirv_GetClientEntityFromHandle(handle);
			SOURCESDK::C_BaseEntity_csgo* baseEntity = clientEntity ? clientEntity->GetBaseEntity() : nullptr;
			if (baseEntity && baseEntity->IsPlayer())
			{
				float dummy1 = 0;
				float dummy2 = 1;
				SOURCESDK::C_BasePlayer_csgo* player = (SOURCESDK::C_BasePlayer_csgo*)baseEntity;
				player->CalcView(outVector, outAngles, dummy1, dummy2, outFov);
				return true;
			}
		}

		return false;
	}

protected:
	virtual ~CMirvCamPlayerCalc()
	{
		m_HandleCalc->Release();
	}

private:
	IMirvHandleCalc * m_HandleCalc;
};
#endif //#ifndef _WIN64

class CMirvVecAngCamCalc : public CMirvVecAngCalc
{
public:
	CMirvVecAngCamCalc(char const * name, IMirvCamCalc * cam)
		: CMirvVecAngCalc(name)
		, m_Cam(cam)
	{
		m_Cam->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvVecAngCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"cam\"");
		Tier0_Msg(", cam: "); m_Cam->Console_PrintBegin(); m_Cam->Console_PrintEnd();
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvVecAngCalc::Console_Edit(args);
	}

	virtual bool CalcVecAng(SOURCESDK::Vector & outVector, SOURCESDK::QAngle & outAngles)
	{
		float dummyFov;

		return m_Cam->CalcCam(outVector, outAngles, dummyFov);
	}

protected:
	virtual ~CMirvVecAngCamCalc()
	{
		m_Cam->Release();
	}

private:
	IMirvCamCalc * m_Cam;
};

class CMirvFovCamCalc : public CMirvFovCalc
{
public:
	CMirvFovCamCalc(char const * name, IMirvCamCalc * cam)
		: CMirvFovCalc(name)
		, m_Cam(cam)
	{
		m_Cam->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvFovCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"cam\"");
		Tier0_Msg(", cam: "); m_Cam->Console_PrintBegin(); m_Cam->Console_PrintEnd();
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvFovCalc::Console_Edit(args);
	}

	virtual bool CalcFov(float & outFov)
	{
		SOURCESDK::Vector dummyVector;
		SOURCESDK::QAngle dummyAngles;

		return m_Cam->CalcCam(dummyVector, dummyAngles, outFov);
	}

protected:
	virtual ~CMirvFovCamCalc()
	{
		m_Cam->Release();
	}

private:
	IMirvCamCalc * m_Cam;
};

class CMirvBoolCalc : public CMirvCalc, public IMirvBoolCalc
{
public:
	CMirvBoolCalc(char const * name)
		: CMirvCalc(name)
	{

	}

	virtual void AddRef(void)
	{
		CMirvCalc::AddRef();
	}

	virtual void Release(void)
	{
		CMirvCalc::Release();
	}

	virtual int GetRefCount(void)
	{
		return CMirvCalc::GetRefCount();
	}

	virtual  char const * GetName(void)
	{
		return CMirvCalc::GetName();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvCalc::Console_PrintBegin();

		Tier0_Msg(", type: \"bool\"");
	}

	virtual void Console_PrintEnd(void)
	{
		CMirvCalc::Console_PrintEnd();
	}

	virtual bool CalcBool(void)
	{
		return false;
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvCalc::Console_Edit(args);
	}
};

class CMirvBoolAndCalc : public CMirvBoolCalc
{
public:
	CMirvBoolAndCalc(char const * name, IMirvBoolCalc * calcA, IMirvBoolCalc * calcB)
		: CMirvBoolCalc(name)
		, m_CalcA(calcA)
		, m_CalcB(calcB)
	{
		m_CalcA->AddRef();
		m_CalcB->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvBoolCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"and\"");
		Tier0_Msg(", calcA: "); m_CalcA->Console_PrintBegin(); m_CalcA->Console_PrintEnd();
		Tier0_Msg(", calcB: "); m_CalcB->Console_PrintBegin(); m_CalcB->Console_PrintEnd();
	}

	virtual bool CalcBool(bool & outResult)
	{
		bool calcValueA, calcValueB;

		if (m_CalcA->CalcBool(calcValueA) && m_CalcB->CalcBool(calcValueB))
		{
			outResult = calcValueA || calcValueB;
			return true;
		}
		
		return false;
	}

protected:
	virtual ~CMirvBoolAndCalc()
	{
		m_CalcB->Release();
		m_CalcA->Release();
	}

private:
	IMirvBoolCalc * m_CalcA;
	IMirvBoolCalc * m_CalcB;
};

class CMirvBoolOrCalc : public CMirvBoolCalc
{
public:
	CMirvBoolOrCalc(char const * name, IMirvBoolCalc * calcA, IMirvBoolCalc * calcB)
		: CMirvBoolCalc(name)
		, m_CalcA(calcA)
		, m_CalcB(calcB)
	{
		m_CalcA->AddRef();
		m_CalcB->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvBoolCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"or\"");
		Tier0_Msg(", calcA: "); m_CalcA->Console_PrintBegin(); m_CalcA->Console_PrintEnd();
		Tier0_Msg(", calcB: "); m_CalcB->Console_PrintBegin(); m_CalcB->Console_PrintEnd();
	}

	virtual bool CalcBool(bool & outResult)
	{
		bool calcValueA, calcValueB;

		if (m_CalcA->CalcBool(calcValueA))
		{
			outResult = calcValueA;
			if(calcValueA) return true;
		}

		if (m_CalcB->CalcBool(calcValueB))
		{
			outResult = calcValueB;
			return true;
		}

		return false;
	}

protected:
	virtual ~CMirvBoolOrCalc()
	{
		m_CalcB->Release();
		m_CalcA->Release();
	}

private:
	IMirvBoolCalc * m_CalcA;
	IMirvBoolCalc * m_CalcB;
};

class CMirvBoolNotCalc : public CMirvBoolCalc
{
public:
	CMirvBoolNotCalc(char const * name, IMirvBoolCalc * calc)
		: CMirvBoolCalc(name)
		, m_Calc(calc)
	{
		m_Calc->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvBoolCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"not\"");
		Tier0_Msg(", calc: "); m_Calc->Console_PrintBegin(); m_Calc->Console_PrintEnd();

	}

	virtual bool CalcBool(bool & outResult)
	{
		bool calcValue;

		if (m_Calc->CalcBool(calcValue))
		{
			outResult = !calcValue;
			return true;
		}

		return false;
	}

protected:
	virtual ~CMirvBoolNotCalc()
	{
		m_Calc->Release();
	}

private:
	IMirvBoolCalc * m_Calc;
};

class CMirvBoolHandleCalc : public CMirvBoolCalc
{
public:
	CMirvBoolHandleCalc(char const * name, IMirvHandleCalc * handle)
		: CMirvBoolCalc(name)
		, m_Handle(handle)
	{
		m_Handle->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvBoolCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"handle\"");
		Tier0_Msg(", handle: "); m_Handle->Console_PrintBegin(); m_Handle->Console_PrintEnd();
	}

	virtual bool CalcBool(bool & outResult)
	{
		SOURCESDK::CSGO::CBaseHandle dummy;

		outResult = m_Handle->CalcHandle(dummy);

		return true;
	}

protected:
	virtual ~CMirvBoolHandleCalc()
	{
		m_Handle->Release();
	}

private:
	IMirvHandleCalc * m_Handle;
};

class CMirvBoolVecAngCalc : public CMirvBoolCalc
{
public:
	CMirvBoolVecAngCalc(char const * name, IMirvVecAngCalc * vecAng)
		: CMirvBoolCalc(name)
		, m_VecAng(vecAng)
	{
		m_VecAng->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvBoolCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"vecAng\"");
		Tier0_Msg(", vecAng: "); m_VecAng->Console_PrintBegin(); m_VecAng->Console_PrintEnd();
	}

	virtual bool CalcBool(bool & outResult)
	{
		SOURCESDK::Vector dummyVec;
		SOURCESDK::QAngle dummyAng;

		outResult = m_VecAng->CalcVecAng(dummyVec, dummyAng);

		return true;
	}

protected:
	virtual ~CMirvBoolVecAngCalc()
	{
		m_VecAng->Release();
	}

private:
	IMirvVecAngCalc * m_VecAng;
};

#ifndef _WIN64
class CMirvBoolAliveCalc : public CMirvBoolCalc
{
public:
	CMirvBoolAliveCalc(char const * name, IMirvHandleCalc * handle)
		: CMirvBoolCalc(name)
		, m_Handle(handle)
	{
		m_Handle->AddRef();
	}

	virtual void Console_Print(void)
	{
		CMirvBoolCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"alive\"");
		Tier0_Msg(", handle: "); m_Handle->Console_PrintBegin(); m_Handle->Console_PrintEnd();
	}

	virtual bool CalcBool(bool & outResult)
	{
		SOURCESDK::CSGO::CBaseHandle parentHandle;

		if (m_Handle->CalcHandle(parentHandle) && parentHandle.IsValid())
		{
			SOURCESDK::IClientEntity_csgo * clientEntity = Mirv_GetClientEntityFromHandle(parentHandle);
			SOURCESDK::C_BaseEntity_csgo * baseEntity = clientEntity ? clientEntity->GetBaseEntity() : 0;

			if (baseEntity)
			{
				outResult = baseEntity->IsAlive();
				return true;
			}
		}

		return false;
	}

protected:
	virtual ~CMirvBoolAliveCalc()
	{
		m_Handle->Release();
	}

private:
	IMirvHandleCalc * m_Handle;
};
#endif //#ifndef _WIN64

class CMirvBoolHandlesEqualCalc : public CMirvBoolCalc
{
public:
	CMirvBoolHandlesEqualCalc(char const * name, IMirvHandleCalc * calcA, IMirvHandleCalc * calcB)
		: CMirvBoolCalc(name)
		, m_CalcA(calcA)
		, m_CalcB(calcB)
	{
		m_CalcA->AddRef();
		m_CalcB->AddRef();
	}

	virtual void Console_Print(void)
	{
		CMirvBoolCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"handlesEqual\"");
		Tier0_Msg(", calcA: "); m_CalcA->Console_PrintBegin(); m_CalcA->Console_PrintEnd();
		Tier0_Msg(", calcB: "); m_CalcB->Console_PrintBegin(); m_CalcB->Console_PrintEnd();
	}

	virtual bool CalcBool(bool & outResult)
	{
		SOURCESDK::CSGO::CBaseHandle calcValueA, calcValueB;

		if (m_CalcA->CalcHandle(calcValueA) && m_CalcB->CalcHandle(calcValueB))
		{
			outResult = calcValueA == calcValueB;
			return true;
		}

		return false;
	}

protected:
	virtual ~CMirvBoolHandlesEqualCalc()
	{
		m_CalcB->Release();
		m_CalcA->Release();
	}

private:
	IMirvHandleCalc * m_CalcA;
	IMirvHandleCalc * m_CalcB;
};

class CMirvBoolIntsEqualCalc : public CMirvBoolCalc
{
public:
	CMirvBoolIntsEqualCalc(char const * name, IMirvIntCalc * calcA, IMirvIntCalc * calcB)
		: CMirvBoolCalc(name)
		, m_CalcA(calcA)
		, m_CalcB(calcB)
	{
		m_CalcA->AddRef();
		m_CalcB->AddRef();
	}

	virtual void Console_Print(void)
	{
		CMirvBoolCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"intsEqual\"");
		Tier0_Msg(", calcA: "); m_CalcA->Console_PrintBegin(); m_CalcA->Console_PrintEnd();
		Tier0_Msg(", calcB: "); m_CalcB->Console_PrintBegin(); m_CalcB->Console_PrintEnd();
	}

	virtual bool CalcBool(bool & outResult)
	{
		int calcValueA, calcValueB;

		if (m_CalcA->CalcInt(calcValueA) && m_CalcB->CalcInt(calcValueB))
		{
			outResult = calcValueA == calcValueB;
			return true;
		}

		return false;
	}

protected:
	virtual ~CMirvBoolIntsEqualCalc()
	{
		m_CalcB->Release();
		m_CalcA->Release();
	}

private:
	IMirvIntCalc * m_CalcA;
	IMirvIntCalc * m_CalcB;
};


#ifndef _WIN64
class CMirvBoolClassNameMatchesCalc : public CMirvBoolCalc
{
public:
	CMirvBoolClassNameMatchesCalc(char const * name, IMirvHandleCalc * calc, const char * wildCardString)
		: CMirvBoolCalc(name)
		, m_Calc(calc)
		, m_WildCardString(wildCardString)
	{
		m_Calc->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvBoolCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"classnameMatches\"");
		Tier0_Msg(", calc: "); m_Calc->Console_PrintBegin(); m_Calc->Console_PrintEnd();
		Tier0_Msg(", wildCardString: \"%s\"", m_WildCardString.c_str());
	}

	virtual bool CalcBool(bool & outResult)
	{
		SOURCESDK::CSGO::CBaseHandle handle;

		if (m_Calc->CalcHandle(handle) && handle.IsValid())
		{
			SOURCESDK::IClientEntity_csgo * clientEntity = Mirv_GetClientEntityFromHandle(handle);
			SOURCESDK::C_BaseEntity_csgo * baseEntity = clientEntity ? clientEntity->GetBaseEntity() : 0;

			if (baseEntity)
			{
				outResult = StringWildCard1Matched(m_WildCardString.c_str(), baseEntity->GetClassname());
				return true;
			}
		}

		return false;
	}

protected:
	virtual ~CMirvBoolClassNameMatchesCalc()
	{
		m_Calc->Release();
	}

private:
	IMirvHandleCalc * m_Calc;
	std::string m_WildCardString;
};
#endif //#ifndef _WIN64


class CMirvIntCalc : public CMirvCalc, public IMirvIntCalc
{
public:
	CMirvIntCalc(char const * name)
		: CMirvCalc(name)
	{

	}

	virtual void AddRef(void)
	{
		CMirvCalc::AddRef();
	}

	virtual void Release(void)
	{
		CMirvCalc::Release();
	}

	virtual int GetRefCount(void)
	{
		return CMirvCalc::GetRefCount();
	}

	virtual  char const * GetName(void)
	{
		return CMirvCalc::GetName();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvCalc::Console_PrintBegin();

		Tier0_Msg(", type: \"int\"");
	}

	virtual void Console_PrintEnd(void)
	{
		CMirvCalc::Console_PrintEnd();
	}

	virtual bool CalcInt(void)
	{
		return false;
	}

	virtual void Console_Edit(IWrpCommandArgs * args)
	{
		CMirvCalc::Console_Edit(args);
	}
};


class CMirvIntValueCalc : public CMirvIntCalc
{
public:
	CMirvIntValueCalc(char const * name, int value)
		: CMirvIntCalc(name)
		, m_Value(value)
	{
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvIntCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"value\", value: %i", m_Value);
	}

	virtual bool CalcInt(int & outResult)
	{
		outResult = m_Value;

		return true;
	}

protected:
	virtual ~CMirvIntValueCalc()
	{
	}

private:
	int m_Value;
};

#ifndef _WIN64
class CMirvIntTeamNumberCalc : public CMirvIntCalc
{
public:
	CMirvIntTeamNumberCalc(char const * name, IMirvHandleCalc * handle)
		: CMirvIntCalc(name)
		, m_Handle(handle)
	{
		m_Handle->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvIntCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"handle\"");
		Tier0_Msg(", handle: "); m_Handle->Console_PrintBegin(); m_Handle->Console_PrintEnd();
	}

	virtual bool CalcInt(int & outResult)
	{
		SOURCESDK::CSGO::CBaseHandle parentHandle;

		if (m_Handle->CalcHandle(parentHandle) && parentHandle.IsValid())
		{
			SOURCESDK::IClientEntity_csgo * clientEntity = Mirv_GetClientEntityFromHandle(parentHandle);
			SOURCESDK::C_BaseEntity_csgo * baseEntity = clientEntity ? clientEntity->GetBaseEntity() : 0;

			if (baseEntity)
			{
				outResult = baseEntity->GetTeamNumber();
				return true;
			}
		}

		return false;
	}

protected:
	virtual ~CMirvIntTeamNumberCalc()
	{
		m_Handle->Release();
	}

private:
	IMirvHandleCalc * m_Handle;
};
#endif //#ifndef _WIN64


class CMirvFloatCalc : public CMirvCalc, public IMirvFloatCalc
{
public:
	CMirvFloatCalc(char const* name)
		: CMirvCalc(name)
	{

	}

	virtual void AddRef(void)
	{
		CMirvCalc::AddRef();
	}

	virtual void Release(void)
	{
		CMirvCalc::Release();
	}

	virtual int GetRefCount(void)
	{
		return CMirvCalc::GetRefCount();
	}

	virtual  char const* GetName(void)
	{
		return CMirvCalc::GetName();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvCalc::Console_PrintBegin();

		Tier0_Msg(", type: \"float\"");
	}

	virtual void Console_PrintEnd(void)
	{
		CMirvCalc::Console_PrintEnd();
	}

	virtual bool CalcFloat(void)
	{
		return false;
	}

	virtual void Console_Edit(IWrpCommandArgs* args)
	{
		CMirvCalc::Console_Edit(args);
	}
};


class CMirvFloatValueCalc : public CMirvFloatCalc
{
public:
	CMirvFloatValueCalc(char const* name, float value)
		: CMirvFloatCalc(name)
		, m_Value(value)
	{
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvFloatCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"value\", value: %f", m_Value);
	}

	virtual bool CalcFloat(float& outResult)
	{
		outResult = m_Value;

		return true;
	}

protected:
	virtual ~CMirvFloatValueCalc()
	{
	}

private:
	float m_Value;
};


class CMirvFloatIfCalc : public CMirvFloatCalc
{
public:
	CMirvFloatIfCalc(char const* name, IMirvBoolCalc* condition, IMirvFloatCalc* condTrue, IMirvFloatCalc* condFalse)
		: CMirvFloatCalc(name)
		, m_Condition(condition)
		, m_CondTrue(condTrue)
		, m_CondFalse(condFalse)
	{
		m_Condition->AddRef();
		m_CondTrue->AddRef();
		m_CondFalse->AddRef();
	}

	virtual void Console_PrintBegin(void)
	{
		CMirvFloatCalc::Console_PrintBegin();

		Tier0_Msg(", fn: \"if\"");
		Tier0_Msg(", condition: "); m_Condition->Console_PrintBegin(); m_Condition->Console_PrintEnd();
		Tier0_Msg(", true: "); m_CondTrue->Console_PrintBegin(); m_CondTrue->Console_PrintEnd();
		Tier0_Msg(", false: "); m_CondFalse->Console_PrintBegin(); m_CondFalse->Console_PrintEnd();
	}

	virtual bool CalcFloat(float& outResult)
	{
		bool condition;

		bool result = m_Condition->CalcBool(condition);

		condition = result && condition;

		return condition ? m_CondTrue->CalcFloat(outResult) : m_CondFalse->CalcFloat(outResult);
	}

protected:
	virtual ~CMirvFloatIfCalc()
	{
		m_CondFalse->Release();
		m_CondTrue->Release();
		m_Condition->Release();
	}

private:
	IMirvBoolCalc* m_Condition;
	IMirvFloatCalc* m_CondTrue;
	IMirvFloatCalc* m_CondFalse;
};

CMirvHandleCalcs::~CMirvHandleCalcs()
{
	for (std::list<IMirvHandleCalc *>::iterator it = m_Calcs.begin(); it != m_Calcs.end(); ++it)
	{
		(*it)->Release();
	}
}

IMirvHandleCalc * CMirvHandleCalcs::GetByName(char const * name)
{
	std::list<IMirvHandleCalc *>::iterator it;
	GetIteratorByName(name, it);
	if (it != m_Calcs.end())
	{
		return *it;
	}

	return 0;
}

IMirvHandleCalc * CMirvHandleCalcs::NewValueCalc(char const * name, int handle)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvHandleCalc * result = new CMirvHandleValueCalc(name, handle);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

#ifndef _WIN64
IMirvHandleCalc * CMirvHandleCalcs::NewIndexCalc(char const * name, int entityIndex)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvHandleCalc * result = new CMirvHandleIndexCalc(name, entityIndex);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

#ifndef _WIN64
IMirvHandleCalc * CMirvHandleCalcs::NewActiveWeaponCalc(char const * name, IMirvHandleCalc * parent, bool world)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvHandleCalc * result = new CMirvHandleActiveWeaponCalc(name, parent, world);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

#ifndef _WIN64
IMirvHandleCalc *  CMirvHandleCalcs::NewLocalPlayerCalc(char const * name)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvHandleCalc * result = new CMirvHandleLocalPlayerCalc(name);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

#ifndef _WIN64
IMirvHandleCalc *  CMirvHandleCalcs::NewObserverTargetCalc(char const * name, IMirvHandleCalc * parent)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvHandleCalc * result = new CMirvHandleObserverTargetCalc(name, parent);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

IMirvHandleCalc * CMirvHandleCalcs::NewOrCalc(char const * name, IMirvHandleCalc * a, IMirvHandleCalc * b)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvHandleCalc * result = new CMirvHandleOrCalc(name, a, b);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

#ifndef _WIN64
IMirvHandleCalc *  CMirvHandleCalcs::NewOwnerEntityCalc(char const * name, IMirvHandleCalc * calc, unsigned int maxDepth)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvHandleCalc * result = new CMirvHandleOwnerEntityCalc(name, calc, maxDepth);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64



#ifndef _WIN64
IMirvHandleCalc * CMirvHandleCalcs::NewKeyCalc(char const * name, int key)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvHandleCalc * result = new CMirvHandleKeyCalc(name, key);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

void CMirvHandleCalcs::Console_Remove(char const * name)
{
	std::list<IMirvHandleCalc *>::iterator it;
	GetIteratorByName(name, it);
	if (it != m_Calcs.end())
	{
		if (1 == (*it)->GetRefCount())
		{
			(*it)->Release();
			m_Calcs.erase(it);
		}
		else
			Tier0_Warning("Error: Cannot remove %s: Still in use.\n", (*it)->GetName());
	}
	else
	{
		Tier0_Warning("Error: No Calc named \"%s\" found.\n", name);
	}
}

bool CMirvHandleCalcs::Console_CheckName(char const * name)
{
	if (!name)
	{
		Tier0_Warning("Error: Name cannot be null pointer.\n");
		return false;
	}

	if (!isalpha(*name))
	{
		Tier0_Warning("Error: Name has to begin with an alphabet letter.\n");
		return false;
	}

	if (!StringIsAlNum(name))
	{
		Tier0_Warning("Error: Name has to be alpha-numeric (letters and digits).\n");
		return false;
	}

	if (GetByName(name))
	{
		Tier0_Warning("Error: Name is already in use.\n");
		return false;
	}

	return true;
}

void CMirvHandleCalcs::Console_Print(void)
{
	for (std::list<IMirvHandleCalc *>::iterator it = m_Calcs.begin(); it != m_Calcs.end(); ++it)
	{
		(*it)->Console_PrintBegin(); (*it)->Console_PrintEnd(); Tier0_Msg(";\n");
	}
}

void CMirvHandleCalcs::GetIteratorByName(char const * name, std::list<IMirvHandleCalc *>::iterator & outIt)
{
	for (outIt = m_Calcs.begin(); outIt != m_Calcs.end(); ++outIt)
	{
		if (0 == _stricmp(name, (*outIt)->GetName()))
			break;
	}
}


CMirvVecAngCalcs::~CMirvVecAngCalcs()
{
	for (std::list<IMirvVecAngCalc *>::iterator it = m_Calcs.begin(); it != m_Calcs.end(); ++it)
	{
		(*it)->Release();
	}
}

IMirvVecAngCalc * CMirvVecAngCalcs::GetByName(char const * name)
{
	std::list<IMirvVecAngCalc *>::iterator it;
	GetIteratorByName(name, it);
	if (it != m_Calcs.end())
	{
		return *it;
	}

	return 0;
}

IMirvVecAngCalc * CMirvVecAngCalcs::NewValueCalc(char const * name, float x, float y, float z, float rX, float rY, float rZ)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngValueCalc(name, x, y, z, rX, rY, rZ);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

IMirvVecAngCalc * CMirvVecAngCalcs::NewAddCalc(char const * name, IMirvVecAngCalc * parent, IMirvVecAngCalc * offset)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngAddCalc(name, parent, offset);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

IMirvVecAngCalc * CMirvVecAngCalcs::NewSubtractCalc(char const * name, IMirvVecAngCalc * parent, IMirvVecAngCalc * offset)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngSubtractCalc(name, parent, offset);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}


IMirvVecAngCalc * CMirvVecAngCalcs::NewOffsetCalc(char const * name, IMirvVecAngCalc * parent, IMirvVecAngCalc * offset, bool order)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngOffsetCalc(name, parent, offset, order);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

#ifndef _WIN64
IMirvVecAngCalc * CMirvVecAngCalcs::NewHandleCalc(char const * name, IMirvHandleCalc * handle)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngHandleCalcEx(name, handle, false, false);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

#ifndef _WIN64
IMirvVecAngCalc * CMirvVecAngCalcs::NewHandleEyeCalc(char const * name, IMirvHandleCalc * handle)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngHandleCalcEx(name, handle, true, true);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

#ifndef _WIN64
IMirvVecAngCalc * CMirvVecAngCalcs::NewHandleCalcEx(char const * name, IMirvHandleCalc * handle, bool eyeVec, bool eyeAng)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngHandleCalcEx(name, handle, eyeVec, eyeAng);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

// ---- TF2 helpers for mirv_listplayers / mirv_listbones / mirv_cam attach ----

// Minimal IVEngineClient (public Source SDK interface, same vtable layout as IVEngineClient_013).
// Only GetPlayerInfo is used, the other entries are placeholders to keep the layout.
class CMirvTf2EngineClient
{
public:
	virtual void _Unused0(void) = 0;
	virtual void _Unused1(void) = 0;
	virtual void _Unused2(void) = 0;
	virtual void _Unused3(void) = 0;
	virtual void _Unused4(void) = 0;
	virtual void _Unused5(void) = 0;
	virtual void _Unused6(void) = 0;
	virtual void _Unused7(void) = 0;
	virtual bool GetPlayerInfo(int ent_num, void * pinfo) = 0; // slot 8
};

int Mirv_Tf2_GetMaxPlayers(void)
{
	if (!g_VEngineClient)
		return 0;

	int max = g_VEngineClient->GetMaxClients();

	return max < 0 ? 0 : (128 < max ? 128 : max);
}

bool Mirv_Tf2_GetPlayerInfo(int entityIndex, char * outName, int outNameSize, int * outUserId)
{
	if (entityIndex < 1 || Mirv_Tf2_GetMaxPlayers() < entityIndex)
		return false;

	SOURCESDK::IVEngineClient_013 * raw = g_VEngineClient->GetVEngineClient_css();
	if (!raw)
		return false;

	// player_info_t is roughly 140 bytes in size, use a generous zeroed buffer.
	// Layout of the start: char name[32]; int userID;
	unsigned char info[512];
	memset(info, 0, sizeof(info));

	if (!reinterpret_cast<CMirvTf2EngineClient *>(raw)->GetPlayerInfo(entityIndex, info))
		return false;

	if (outName && 0 < outNameSize)
	{
		int n = 0;
		while (n < 31 && n < outNameSize - 1 && info[n])
		{
			outName[n] = (char)info[n];
			++n;
		}
		outName[n] = 0;
	}

	if (outUserId)
		memcpy(outUserId, info + 32, sizeof(int));

	return true;
}

bool Mirv_Tf2_GetEntityOriginAngles(int entityIndex, float outOrigin[3], float outAngles[3])
{
	IMirvVecAngCalc * calc = g_MirvVecAngCalcs.NewTf2EntityIndexCalc(0, entityIndex, -1);
	if (!calc)
		return false;

	calc->AddRef();

	SOURCESDK::Vector o;
	SOURCESDK::QAngle a;
	bool ok = calc->CalcVecAng(o, a);

	calc->Release();

	if (ok)
	{
		outOrigin[0] = o.x; outOrigin[1] = o.y; outOrigin[2] = o.z;
		outAngles[0] = a.x; outAngles[1] = a.y; outAngles[2] = a.z;
	}

	return ok;
}

bool Mirv_Tf2_GetBones(int entityIndex, MirvTf2Bone_t * outBones, int & outCount)
{
	outCount = 0;

	CClientToolsTf2 * tools = CClientToolsTf2::Instance();
	SOURCESDK::TF2::IClientTools * iface = tools ? tools->GetClientTools() : 0;

	if (!iface || entityIndex < 0 || 2048 <= entityIndex)
		return false;

	WrpGlobals * gl = g_Hook_VClient_RenderView.GetGlobals();
	if (!gl)
		return false;

	for (auto ent = iface->FirstEntity(); ent; ent = iface->NextEntity(ent))
	{
		if (iface->GetEntIndex(ent) != entityIndex)
			continue;

		CMirvTf2ClientUnknown * unknown = reinterpret_cast<CMirvTf2ClientUnknown *>(ent);
		CMirvTf2ClientRenderable * renderable = unknown ? unknown->GetClientRenderable() : 0;

		if (!renderable)
			return false;

		SOURCESDK::matrix3x4_t bones[MIRV_TF2_MAX_BONES];
		for (int b = 0; b < MIRV_TF2_MAX_BONES; ++b)
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 4; ++c)
					bones[b].m_flMatVal[r][c] = 0.0f;

		// 0x0007FF00 = BONE_USED_BY_ANYTHING
		if (!renderable->SetupBones(bones, MIRV_TF2_MAX_BONES, 0x0007FF00, gl->curtime_get()))
			return false;

		for (int b = 0; b < MIRV_TF2_MAX_BONES; ++b)
		{
			const float (*m)[4] = bones[b].m_flMatVal;
			MirvTf2Bone_t & out = outBones[b];

			out.valid = false;

			if (!Mirv_Tf2_IsValidBoneMatrix(m))
				continue;

			out.valid = true;
			out.origin[0] = m[0][3];
			out.origin[1] = m[1][3];
			out.origin[2] = m[2][3];

			const float rotation[3][3] = {
				{ m[0][0], m[0][1], m[0][2] },
				{ m[1][0], m[1][1], m[1][2] },
				{ m[2][0], m[2][1], m[2][2] }
			};
			Mirv_MatrixAngles3(rotation, out.angles[0], out.angles[1], out.angles[2]);

			outCount = b + 1;
		}

		return true;
	}

	return false;
}

// ---- TF2 bone names (read from the model's studio header) ----

extern SOURCESDK::CreateInterfaceFn g_AppSystemFactory;

// Minimal Source SDK 2013 / TF2 EngineTraceClient003 ABI (not CS:GO's 004).
// Ray_t has no world-axis pointer in TF2; TraceRay is virtual slot 4.
// Keep these adapters beside the existing TF2 entity/model interface adapters.
struct alignas(16) MirvTf2Ray_t
{
	float start[4], delta[4], startOffset[4], extents[4];
	bool isRay, isSwept;
};
struct MirvTf2Trace_t
{
	float start[3], end[3];
	struct { float normal[3], distance; unsigned char type, signBits, padding[2]; } plane;
	float fraction;
	int contents;
	unsigned short dispFlags;
	bool allSolid, startSolid;
	float fractionLeftSolid;
	struct { const char *name; short surfaceProps; unsigned short flags; } surface;
	int hitGroup;
	short physicsBone;
	void *entity;
	int hitbox;
};
static_assert(sizeof(MirvTf2Ray_t) == 80, "TF2 Ray_t ABI");
static_assert(sizeof(MirvTf2Trace_t) == (sizeof(void *) == 8 ? 104 : 84), "TF2 trace_t size");
static_assert(offsetof(MirvTf2Trace_t, entity) == (sizeof(void *) == 8 ? 88 : 76), "TF2 trace_t ABI");
class CMirvTf2TraceFilter
{
public:
	virtual bool ShouldHitEntity(void *entity, int)
	{
		if (entity == m_Skip || entity == m_Attached)
			return false;
		if (!m_Tools)
			return true;
		if (m_Tools->IsPlayer(entity))
			return entity == m_Local || m_Players;
		if (m_Camera)
		{
			// TF2's C_FuncRespawnRoomVisualizer::ShouldCollide rejects non-player
			// movement. Engine TraceRay does not invoke that game-side predicate.
			// Ignore only the team barrier, not the physical moving door.
			auto handle = m_Tools->GetToolHandleForEntityByIndex(m_Tools->GetEntIndex(entity));
			const bool temporary = handle == SOURCESDK::TF2::HTOOLHANDLE_INVALID;
			if (temporary)
				handle = m_Tools->AttachToEntity(entity);
			bool barrier = false;
			if (handle != SOURCESDK::TF2::HTOOLHANDLE_INVALID)
			{
				const char *name = m_Tools->GetClassname(handle);
				barrier = name && (!std::strcmp(name, "func_respawnroomvisualizer") ||
				                   std::strstr(name, "C_FuncRespawnRoomVisualizer"));
			}
			if (temporary && handle != SOURCESDK::TF2::HTOOLHANDLE_INVALID)
				m_Tools->DetachFromEntity(entity);
			if (barrier)
				return false;
		}
		return true;
	}
	virtual int GetTraceType() const
	{
		return 0;
	} // TRACE_EVERYTHING: walls occlude selection.
	explicit CMirvTf2TraceFilter(void *skip, SOURCESDK::TF2::IClientTools *tools = nullptr,
	                             bool players = true, void *attached = nullptr, void *local = nullptr,
	                             bool camera = false)
	    : m_Skip(skip), m_Tools(tools), m_Players(players), m_Attached(attached), m_Local(local),
	      m_Camera(camera)
	{
	}

private:
	void *m_Skip;
	SOURCESDK::TF2::IClientTools *m_Tools;
	bool m_Players;
	void *m_Attached, *m_Local;
	bool m_Camera;
};
class CMirvTf2EngineTrace
{
public:
	virtual void _UNUSED_GetPointContents() = 0;
	virtual void _UNUSED_GetPointContents_Collideable() = 0;
	virtual void ClipRayToEntity(const MirvTf2Ray_t &, unsigned int, void *, MirvTf2Trace_t *) = 0;
	virtual void _UNUSED_ClipRayToCollideable() = 0;
	virtual void TraceRay(const MirvTf2Ray_t &, unsigned int, CMirvTf2TraceFilter *, MirvTf2Trace_t *) = 0;
};

bool Mirv_Tf2_TraceEntity(const double origin[3], const double angles[3], int &outIndex)
{
	outIndex = -1;
	if (g_SourceSdkVer != SourceSdkVer_TF2 && g_SourceSdkVer != SourceSdkVer_TF2Classified)
		return false;
	auto *tools = CClientToolsTf2::Instance();
	auto *iface = tools ? tools->GetClientTools() : nullptr;
	static CMirvTf2EngineTrace *trace = nullptr;
	if (!trace && g_AppSystemFactory)
		trace = reinterpret_cast<CMirvTf2EngineTrace *>(g_AppSystemFactory("EngineTraceClient003", nullptr));
	if (!trace || !iface)
		return false;
	double forward[3], right[3], up[3];
	MakeVectors(angles[2], angles[0], angles[1], forward, right, up);
	MirvTf2Ray_t ray = {};
	for (int axis = 0; axis < 3; ++axis)
	{
		if (!std::isfinite(origin[axis]) || !std::isfinite(forward[axis]))
			return false;
		ray.start[axis] = float(origin[axis]);
		ray.delta[axis] = float(56756.0 * forward[axis]); // World diagonal, MAX_TRACE_LENGTH.
	}
	ray.isRay = ray.isSwept = true;
	MirvTf2Trace_t hit = {};
	CMirvTf2TraceFilter filter(iface->GetLocalPlayer());
	trace->TraceRay(ray, 0x4600400B, &filter, &hit); // MASK_SHOT, including hitboxes.
	if (hit.entity && (hit.fraction < 1.0f || hit.startSolid || hit.allSolid))
	{
		// Resolve through tool entities rather than dereferencing static-prop hits.
		for (auto ent = iface->FirstEntity(); ent; ent = iface->NextEntity(ent))
			if (ent == hit.entity)
			{
				const int index = iface->GetEntIndex(ent);
				if (0 < index && index < 2048)
					outIndex = index;
				break;
			}
	}
	return true;
}


// Optional camera collision implementation.
namespace
{
std::mutex g_CameraCollisionMutex;
MirvCameraCollisionSettings g_CameraCollisionSettings;
std::atomic<unsigned long long> g_CameraCollisionGeneration{0}, g_AttachedCameraGeneration{0};
struct CameraCollisionHistory
{
	bool Valid = false, Free = false, Recovering = false;
	const void *Path = nullptr;
	unsigned long long Revision = 0, Generation = 0;
	double SceneTime = 0, PathTime = 0, Position[3] = {};
} g_CameraCollisionHistory;

CMirvTf2EngineTrace *CameraCollisionTraceInterface()
{
	if (g_SourceSdkVer != SourceSdkVer_TF2 && g_SourceSdkVer != SourceSdkVer_TF2Classified)
		return nullptr;
	static CMirvTf2EngineTrace *trace = nullptr;
	if (!trace && g_AppSystemFactory)
		trace = reinterpret_cast<CMirvTf2EngineTrace *>(g_AppSystemFactory("EngineTraceClient003", nullptr));
	return trace;
}

bool CameraCollisionTrace(const double start[3], const double end[3],
                          const MirvCameraCollisionSettings &settings, MirvTf2Trace_t &hit,
                          void *attached = nullptr, void *onlyEntity = nullptr)
{
	auto *trace = CameraCollisionTraceInterface();
	auto *tools = CClientToolsTf2::Instance();
	auto *iface = tools ? tools->GetClientTools() : nullptr;
	if (!trace || !iface)
		return false;
	MirvTf2Ray_t ray = {};
	double length2 = 0;
	for (int axis = 0; axis < 3; ++axis)
	{
		if (!std::isfinite(start[axis]) || !std::isfinite(end[axis]) || std::abs(start[axis]) > 1e7 ||
		    std::abs(end[axis]) > 1e7)
			return false;
		ray.start[axis] = float(start[axis]);
		ray.delta[axis] = float(end[axis] - start[axis]);
		ray.extents[axis] = float(settings.Radius);
		length2 += double(ray.delta[axis]) * ray.delta[axis];
	}
	if (length2 > 56756.0 * 56756.0)
		return false;
	ray.isRay = false;
	ray.isSwept = length2 > 0;
	hit = {};
	hit.fraction = 1;
	CMirvTf2TraceFilter filter(settings.IgnoreLocal ? iface->GetLocalPlayer() : nullptr, iface,
	                           settings.Players, attached, iface->GetLocalPlayer(), true);
	if (onlyEntity)
		trace->ClipRayToEntity(ray, 0x200400B, onlyEntity, &hit);
	else
		trace->TraceRay(ray, 0x200400B, &filter, &hit); // SDK MASK_SOLID; ignore non-solid decoration.
	return std::isfinite(hit.fraction) && hit.fraction >= 0 && hit.fraction <= 1;
}

bool CameraCollisionClear(const double point[3], const MirvCameraCollisionSettings &settings,
                          void *attached = nullptr)
{
	MirvTf2Trace_t hit;
	return CameraCollisionTrace(point, point, settings, hit, attached) && !hit.startSolid && !hit.allSolid;
}

bool CameraCollisionMove(const double from[3], const double target[3],
                         const MirvCameraCollisionSettings &settings, double output[3],
                         void *attached = nullptr)
{
	double position[3] = {from[0], from[1], from[2]};
	if (!CameraCollisionClear(position, settings, attached))
	{
		// A moving solid can engulf a stationary camera. Bounded recovery only;
		// do not run an unbounded search or promise to escape deeply embedded starts.
		bool clear = false;
		double away[3], awayLength2 = 0;
		for (int a = 0; a < 3; ++a)
		{
			away[a] = from[a] - target[a];
			awayLength2 += away[a] * away[a];
		}
		int directions[6] = {0, 1, 2, 3, 4, 5};
		std::sort(directions, directions + 6, [&](int a, int b) {
			const double sa = (a % 2 ? 1 : -1) * away[a / 2], sb = (b % 2 ? 1 : -1) * away[b / 2];
			return sa == sb ? a < b : sa > sb;
		});
		for (int ring = 0; ring < 4 && !clear; ++ring)
		{
			const double distance = (std::min)(64.0, (settings.Radius + 1) * (1 << ring));
			if (awayLength2 > 1e-8)
			{
				double candidate[3];
				for (int axis = 0; axis < 3; ++axis)
					candidate[axis] = from[axis] + away[axis] * distance / std::sqrt(awayLength2);
				if (CameraCollisionClear(candidate, settings, attached))
				{
					std::copy(candidate, candidate + 3, position);
					clear = true;
				}
			}
			for (int d = 0; d < 6 && !clear; ++d)
			{
				const int direction = directions[d];
				double candidate[3] = {from[0], from[1], from[2]};
				candidate[direction / 2] += direction % 2 ? distance : -distance;
				if (CameraCollisionClear(candidate, settings, attached))
				{
					std::copy(candidate, candidate + 3, position);
					clear = true;
				}
			}
		}
		if (!clear)
		{
			std::copy(from, from + 3, output);
			return true;
		}
	}
	double safe[3] = {position[0], position[1], position[2]};
	double remaining[3] = {target[0] - position[0], target[1] - position[1], target[2] - position[2]};
	double length2 = 0;
	for (double v : remaining)
		length2 += v * v;
	if (length2 > 56755.0 * 56755.0)
		for (auto &v : remaining)
			v *= 56755.0 / std::sqrt(length2);
	double planes[3][3] = {};
	for (int pass = 0; pass < 3; ++pass)
	{
		double end[3];
		for (int axis = 0; axis < 3; ++axis)
			end[axis] = position[axis] + remaining[axis];
		MirvTf2Trace_t hit;
		if (!CameraCollisionTrace(position, end, settings, hit, attached))
			return false;
		if (hit.startSolid || hit.allSolid)
			break;
		for (int axis = 0; axis < 3; ++axis)
			position[axis] += remaining[axis] * hit.fraction;
		if (hit.fraction == 1)
			break;
		double normal2 = 0;
		for (int axis = 0; axis < 3; ++axis)
		{
			planes[pass][axis] = hit.plane.normal[axis];
			normal2 += planes[pass][axis] * planes[pass][axis];
		}
		if (!std::isfinite(normal2) || normal2 < .5 || normal2 > 1.5)
			break;
		if (!settings.Slide)
		{
			double length2 = 0;
			for (double v : remaining)
				length2 += v * v;
			const double length = std::sqrt(length2);
			// Back off along the sweep, not the plane normal. Repeating a fixed
			// paused target then stays at the same contact instead of creeping.
			if (length > 0)
				for (int axis = 0; axis < 3; ++axis)
					position[axis] -= remaining[axis] * (std::min)(.03125 / length, double(hit.fraction));
			break;
		}
		for (int axis = 0; axis < 3; ++axis)
		{
			position[axis] += planes[pass][axis] * .03125;
			remaining[axis] *= 1 - hit.fraction;
		}
		for (int plane = 0; plane <= pass; ++plane)
		{
			double into = 0;
			for (int axis = 0; axis < 3; ++axis)
				into += remaining[axis] * planes[plane][axis];
			if (into < 0)
				for (int axis = 0; axis < 3; ++axis)
					remaining[axis] -= planes[plane][axis] * into;
		}
	}
	// Multiple planes can constrain the final clearance nudge. Retain a verified
	// clear point if that happens rather than feed an embedded position to the view.
	const double *result = CameraCollisionClear(position, settings, attached) ? position : safe;
	std::copy(result, result + 3, output);
	return true;
}
void CameraReturnTarget(const double from[3], const double desired[3], double dt, double halfLife,
                        double speed, double out[3])
{
	const double alpha = halfLife > 0 ? -std::expm1(-std::log(2.0) * dt / halfLife) : (dt > 0 ? 1 : 0);
	double length2 = 0;
	for (int a = 0; a < 3; ++a)
	{
		out[a] = (desired[a] - from[a]) * alpha;
		length2 += out[a] * out[a];
	}
	const double scale = speed > 0 && length2 > speed * dt * speed * dt ? speed * dt / std::sqrt(length2) : 1;
	for (int a = 0; a < 3; ++a)
		out[a] = from[a] + out[a] * scale;
}
} // namespace

MirvCameraCollisionSettings MirvCameraCollisionGet()
{
	std::lock_guard<std::mutex> lock(g_CameraCollisionMutex);
	return g_CameraCollisionSettings;
}
bool MirvCameraCollisionSet(const MirvCameraCollisionSettings &settings)
{
	if (!std::isfinite(settings.Radius) || settings.Radius < 1 || settings.Radius > 64 ||
	    !std::isfinite(settings.ReturnHalfLife) || settings.ReturnHalfLife < 0 ||
	    settings.ReturnHalfLife > 10 || !std::isfinite(settings.ReturnSpeed) || settings.ReturnSpeed < 0 ||
	    settings.ReturnSpeed > 1e6 || !std::isfinite(settings.FollowHalfLife) ||
	    settings.FollowHalfLife < 0 || settings.FollowHalfLife > 10 ||
	    !std::isfinite(settings.FollowMaxDistance) || settings.FollowMaxDistance < 1 ||
	    settings.FollowMaxDistance > 10000 || !std::isfinite(settings.BoneHalfLife) ||
	    settings.BoneHalfLife < 0 || settings.BoneHalfLife > 10 ||
	    !std::isfinite(settings.FollowTeleportDistance) || settings.FollowTeleportDistance < 0 ||
	    settings.FollowTeleportDistance > 1e6 || !std::isfinite(settings.OrbitHalfLife) ||
	    settings.OrbitHalfLife < 0 || settings.OrbitHalfLife > 10 || !std::isfinite(settings.OrbitSpeed) ||
	    settings.OrbitSpeed < 0 || settings.OrbitSpeed > 1e6 ||
	    !std::isfinite(settings.FollowRecoveryDelay) || settings.FollowRecoveryDelay < 0 ||
	    settings.FollowRecoveryDelay > 60 || !std::isfinite(settings.AttachedSoftDistance) ||
	    settings.AttachedSoftDistance < 0 || settings.AttachedSoftDistance > 256 ||
	    ((settings.Free || settings.Campath || settings.Attached) && !CameraCollisionTraceInterface()) ||
	    ((settings.FollowHalfLife > 0 || settings.BoneHalfLife > 0) && g_SourceSdkVer != SourceSdkVer_TF2 &&
	     g_SourceSdkVer != SourceSdkVer_TF2Classified))
		return false;
	{
		std::lock_guard<std::mutex> lock(g_CameraCollisionMutex);
		g_CameraCollisionSettings = settings;
	}
	return true;
}
void MirvCameraCollisionReset()
{
	++g_CameraCollisionGeneration;
	++g_AttachedCameraGeneration;
}

bool MirvCameraCollisionApply(bool freeCamera, const void *path, unsigned long long revision,
                              double sceneTime, double pathTime, const double seed[3], float &x, float &y,
                              float &z)
{
	auto &history = g_CameraCollisionHistory;
	const auto settings = MirvCameraCollisionGet();
	if ((freeCamera ? !settings.Free : !settings.Campath || !path) || !std::isfinite(sceneTime) ||
	    !std::isfinite(pathTime))
	{
		history.Valid = false;
		return false;
	}
	auto *tools = CClientToolsTf2::Instance();
	if (!CameraCollisionTraceInterface() || !g_VEngineClient || !g_VEngineClient->IsInGame() ||
	    g_VEngineClient->IsDrawingLoadingImage() || !tools || !tools->GetClientTools())
	{
		history.Valid = false;
		return false;
	}
	const double desired[3] = {x, y, z};
	for (int axis = 0; axis < 3; ++axis)
		if (!std::isfinite(desired[axis]) || !std::isfinite(seed[axis]) || std::abs(desired[axis]) > 1e7 ||
		    std::abs(seed[axis]) > 1e7)
		{
			history.Valid = false;
			return false;
		}
	const auto generation = g_CameraCollisionGeneration.load();
	const bool continuous =
	    history.Valid && history.Free == freeCamera && history.Path == path && history.Revision == revision &&
	    history.Generation == generation && sceneTime >= history.SceneTime &&
	    (freeCamera ||
		 (pathTime >= history.PathTime && (history.Recovering || (sceneTime - history.SceneTime <= .25 &&
		                                                          pathTime - history.PathTime <= .25))));
	double from[3], corrected[3];
	if (continuous)
		std::copy(history.Position, history.Position + 3, from);
	else if (!freeCamera && CameraCollisionClear(desired, settings))
		std::copy(desired, desired + 3, from);
	else
		std::copy(seed, seed + 3, from);
	double target[3] = {desired[0], desired[1], desired[2]};
	if (continuous && !freeCamera && history.Recovering)
	{
		const double dt =
		    std::clamp((std::max)(sceneTime - history.SceneTime, pathTime - history.PathTime), 0.0, .1);
		CameraReturnTarget(from, desired, dt, settings.ReturnHalfLife, settings.ReturnSpeed, target);
	}
	if (!CameraCollisionMove(from, target, settings, corrected))
	{
		history.Valid = false;
		return false;
	}
	x = float(corrected[0]);
	y = float(corrected[1]);
	z = float(corrected[2]);
	double error2 = 0;
	for (int a = 0; a < 3; ++a)
		error2 += (corrected[a] - desired[a]) * (corrected[a] - desired[a]);
	history.Recovering = !freeCamera && error2 > 1e-6;
	history.Valid = true;
	history.Free = freeCamera;
	history.Path = path;
	history.Revision = revision;
	history.Generation = generation;
	history.SceneTime = sceneTime;
	history.PathTime = pathTime;
	std::copy(corrected, corrected + 3, history.Position);
	return true;
}
void MirvCameraAttachedReset()
{
	++g_AttachedCameraGeneration;
}
namespace
{
struct AttachedCameraHistory
{
	bool Valid = false, Retracted = false, Contracting = false;
	double LastAllowed = 0;
	const void *Identity = nullptr;
	void *Entity = nullptr;
	unsigned long long Generation = 0;
	double Time = 0, ArmLength = 0, Position[3] = {}, FollowPosition[3] = {};
	double Desired[3] = {}, AuthoredAngles[3] = {}, DisplayAngles[3] = {};
	bool HasAngles = false;
	double Root[3] = {}, FollowVelocity[3] = {}, BonePosition[3] = {}, BoneVelocity[3] = {};
	double FollowHalfLife = 0, BoneHalfLife = 0;
	// Orbit, composition and focus are one shot state. Source poses remain inputs,
	// rather than repeatedly replacing the camera with an instantly rotating boom.
	Afx::Math::Quaternion Orbit, Composition, View;
	double OrbitLength = 0, OrbitYaw = 0, BlockedTime = 0;
	bool CompositionValid = false;
} g_AttachedCameraHistory;
} // namespace
// Capture the authored pose when recapturing the displayed, collision-corrected
// attachment. Otherwise the soft clearance and framing correction apply twice.
bool MirvCameraAttachedCapturePose(const void *identity, double origin[3], double angles[3])
{
	const auto &history = g_AttachedCameraHistory;
	if (!history.Valid || identity != history.Identity ||
	    history.Generation != g_AttachedCameraGeneration.load())
		return false;
	for (int a = 0; a < 3; ++a)
		if (std::abs(origin[a] - history.Position[a]) > .001)
			return false;
	std::copy(history.Desired, history.Desired + 3, origin);
	bool sameAngles = history.HasAngles;
	for (int a = 0; a < 3; ++a)
		sameAngles =
		    sameAngles && std::abs(std::remainder(angles[a] - history.DisplayAngles[a], 360.0)) < .001;
	if (sameAngles)
		std::copy(history.AuthoredAngles, history.AuthoredAngles + 3, angles);
	return true;
}
bool MirvCameraAttachedReframe(const double from[3], const double to[3], const double target[3],
                               float angles[3])
{
	using namespace Afx::Math;
	for (int a = 0; a < 3; ++a)
		if (!std::isfinite(from[a]) || !std::isfinite(to[a]) || !std::isfinite(target[a]) ||
		    !std::isfinite(angles[a]))
			return false;
	double before[3], after[3], before2 = 0, after2 = 0;
	for (int a = 0; a < 3; ++a)
	{
		before[a] = target[a] - from[a];
		after[a] = target[a] - to[a];
		before2 += before[a] * before[a];
		after2 += after[a] * after[a];
	}
	if (before2 < 1e-6 || after2 < 1e-6)
		return false;
	// Solve yaw/pitch with the authored roll held fixed. Transporting a complete
	// look frame instead introduces horizon roll for an off-centre subject.
	const double degrees = 57.29577951308232;
	double forward[3], right[3], up[3], local[3] = {};
	MakeVectors(angles[2], angles[0], angles[1], forward, right, up);
	for (int a = 0; a < 3; ++a)
	{
		before[a] /= std::sqrt(before2);
		after[a] /= std::sqrt(after2);
		local[0] += forward[a] * before[a];
		local[1] += right[a] * before[a];
		local[2] += up[a] * before[a];
	}
	const double roll = angles[2] / degrees;
	const double lateral = std::cos(roll) * local[1] + std::sin(roll) * local[2];
	const double vertical = -std::sin(roll) * local[1] + std::cos(roll) * local[2];
	const double flat = std::hypot(after[0], after[1]);
	const double bearing = flat > 1e-9 ? std::atan2(after[1], after[0]) : angles[1] / degrees;
	// Close to a vertical sight line, exact off-centre framing may be impossible
	// with fixed roll. Clamp to the nearest attainable direction instead.
	const double side = flat > 1e-9 ? std::asin((std::max)(-1.0, (std::min)(1.0, lateral / flat))) : 0;
	double best = (std::numeric_limits<double>::max)(), bestPitch = angles[0], bestYaw = angles[1];
	for (int branch = 0; branch < 2; ++branch)
	{
		const double yaw = bearing + (branch ? 3.141592653589793 - side : side);
		const double horizontal = std::cos(yaw) * after[0] + std::sin(yaw) * after[1];
		const double pitch = std::atan2(vertical, local[0]) - std::atan2(after[2], horizontal);
		if (std::abs(std::remainder(angles[0], 360.0)) <= 90 &&
		    std::abs(std::remainder(pitch * degrees, 360.0)) > 90.00001)
			continue;
		const double dp = std::remainder(pitch * degrees - angles[0], 360.0);
		const double dy = std::remainder(yaw * degrees - angles[1], 360.0);
		const double cost = dp * dp + dy * dy;
		if (cost < best)
		{
			best = cost;
			bestPitch = angles[0] + dp;
			bestYaw = angles[1] + dy;
		}
	}
	angles[0] = float(bestPitch);
	angles[1] = float(bestYaw);
	return true;
}
bool MirvCameraAttachedApply(const void *identity, void *entity, const double pivot[3], double sceneTime,
                             float &x, float &y, float &z, const double *entityOrigin, float *angles)
{
	auto &history = g_AttachedCameraHistory;
	const auto settings = MirvCameraCollisionGet();
	if (!identity || !entity ||
	    (!settings.Attached && settings.FollowHalfLife == 0 && settings.BoneHalfLife == 0) ||
	    !std::isfinite(sceneTime) || !g_VEngineClient || !g_VEngineClient->IsInGame() ||
	    g_VEngineClient->IsDrawingLoadingImage())
	{
		history.Valid = false;
		return false;
	}
	const double desired[3] = {x, y, z};
	const double *root = entityOrigin ? entityOrigin : pivot;
	double rootJump2 = 0;
	for (int a = 0; a < 3; ++a)
	{
		if ((angles && !std::isfinite(angles[a])) || !std::isfinite(desired[a]) || !std::isfinite(pivot[a]) ||
		    !std::isfinite(root[a]) || std::abs(desired[a]) > 1e7 || std::abs(pivot[a]) > 1e7 ||
		    std::abs(root[a]) > 1e7)
		{
			history.Valid = false;
			return false;
		}
		rootJump2 += (root[a] - history.Root[a]) * (root[a] - history.Root[a]);
	}
	const auto generation = g_AttachedCameraGeneration.load();
	const bool continuous = history.Valid && history.Identity == identity && history.Entity == entity &&
	                        history.Generation == generation && sceneTime >= history.Time &&
	                        sceneTime - history.Time <= 10 &&
	                        (settings.FollowTeleportDistance == 0 ||
	                         rootJump2 <= settings.FollowTeleportDistance * settings.FollowTeleportDistance);
	const double dt = continuous ? sceneTime - history.Time : 0;
	const bool following = settings.FollowHalfLife > 1e-6;
	const bool shotContinuous = continuous && (history.FollowHalfLife > 1e-6) == following;
	const double orbitAlpha = following && settings.OrbitHalfLife > 1e-6
	                              ? -std::expm1(-std::log(2.0) * dt / settings.OrbitHalfLife)
	                              : 1;
	if (!shotContinuous)
		history.OrbitYaw = angles ? angles[1] : 0;
	if (!continuous)
	{
		history.Contracting = false;
		history.BlockedTime = 0;
	}
	using namespace Afx::Math;
	auto directionFrame = [](const double v[3], double fallbackYaw) {
		const double flat = std::hypot(v[0], v[1]);
		return Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(
		    QEulerAngles(std::atan2(-v[2], flat) * 57.29577951308232,
			             flat > 1e-9 ? std::atan2(v[1], v[0]) * 57.29577951308232 : fallbackYaw, 0)));
	};
	auto forward = [](const Quaternion &q, double v[3]) {
		const auto rotated = q * Quaternion(0, 1, 0, 0) * q.Conjugate();
		v[0] = rotated.X;
		v[1] = rotated.Y;
		v[2] = rotated.Z;
	};
	auto blend = [](const Quaternion &from, Quaternion to, double alpha) {
		if (DotProduct(from, to) < 0)
			to = -1.0 * to;
		return from.Slerp(to, alpha).Normalized();
	};
	double candidate[3];
	for (int a = 0; a < 3; ++a)
	{
		const double boneOffset = pivot[a] - root[a];
		if (!continuous || settings.FollowHalfLife <= 1e-6 || history.FollowHalfLife == 0)
		{
			history.FollowPosition[a] = root[a];
			history.FollowVelocity[a] = 0;
		}
		else
			Afx::Math::SpringStep(history.FollowPosition[a], history.FollowVelocity[a], root[a], dt,
			                      1.67834699001666 / (6.283185307179586 * settings.FollowHalfLife), 1);
		if (!continuous || settings.BoneHalfLife <= 1e-6 || history.BoneHalfLife == 0)
		{
			history.BonePosition[a] = boneOffset;
			history.BoneVelocity[a] = 0;
		}
		else
			Afx::Math::SpringStep(history.BonePosition[a], history.BoneVelocity[a], boneOffset, dt,
			                      1.67834699001666 / (6.283185307179586 * settings.BoneHalfLife), 1);
	}
	// A lagged world position must not let the subject walk through its own camera.
	// Limit the steady lag estimated from the filtered velocity. Clamping the
	// raw root-to-camera difference feeds snapshot jitter straight back into
	// the camera whenever that clamp saturates.
	double offset[3], rawOffset[3], offset2 = 0, lag[3], rawLag[3], lag2 = 0, anchor[3];
	for (int a = 0; a < 3; ++a)
	{
		offset[a] = rawOffset[a] = desired[a] - pivot[a];
		offset2 += offset[a] * offset[a];
		lag[a] = settings.FollowHalfLife > 1e-6
		             ? -history.FollowVelocity[a] * (2 * settings.FollowHalfLife / 1.67834699001666)
		             : 0;
		rawLag[a] = lag[a];
		lag2 += lag[a] * lag[a];
		anchor[a] = root[a] + history.BonePosition[a];
	}
	const double rawLength = std::sqrt(offset2), lagLength = std::sqrt(lag2);
	if (rawLength > 1e-6)
	{
		if (std::hypot(rawOffset[0], rawOffset[1]) > 1e-6)
			history.OrbitYaw = std::atan2(rawOffset[1], rawOffset[0]) * 57.29577951308232;
		auto target = directionFrame(rawOffset, history.OrbitYaw);
		if (!shotContinuous || history.OrbitLength < 1e-6 || orbitAlpha == 1)
			history.Orbit = target;
		else
		{
			Vector3 axis;
			if (DotProduct(history.Orbit, target) < 0)
				target = -1.0 * target;
			const double angle = std::abs(history.Orbit.GetAng(target, axis));
			const double alpha =
			    settings.OrbitSpeed > 0 && angle > 1e-9
			        ? (std::min)(orbitAlpha, settings.OrbitSpeed / 57.29577951308232 * dt / angle)
			        : orbitAlpha;
			history.Orbit = blend(history.Orbit, target, alpha);
		}
	}
	if (!shotContinuous || orbitAlpha == 1)
		history.OrbitLength = rawLength;
	else
		history.OrbitLength += (rawLength - history.OrbitLength) * orbitAlpha;
	if (history.OrbitLength > 1e-6)
	{
		forward(history.Orbit, offset);
		for (int a = 0; a < 3; ++a)
			offset[a] *= history.OrbitLength;
	}
	else
		std::fill(offset, offset + 3, 0.0);
	const double offsetLength = history.OrbitLength;
	const double lagScale =
	    lagLength > 1e-9
	        ? settings.FollowMaxDistance * std::tanh(lagLength / settings.FollowMaxDistance) / lagLength
	        : 1;
	for (int a = 0; a < 3; ++a)
		lag[a] *= lagScale;
	if (offsetLength > 1e-3)
	{
		double parallel = 0, transverse[3], transverse2 = 0;
		for (int a = 0; a < 3; ++a)
			parallel += lag[a] * offset[a] / offsetLength;
		for (int a = 0; a < 3; ++a)
		{
			transverse[a] = lag[a] - parallel * offset[a] / offsetLength;
			transverse2 += transverse[a] * transverse[a];
		}
		const double alongLimit = .25 * offsetLength, acrossLimit = .5 * offsetLength;
		const double along = alongLimit * std::tanh(parallel / alongLimit);
		const double acrossLength = std::sqrt(transverse2);
		const double acrossScale =
		    acrossLength > 1e-9 ? acrossLimit * std::tanh(acrossLength / acrossLimit) / acrossLength : 1;
		for (int a = 0; a < 3; ++a)
			lag[a] = along * offset[a] / offsetLength + transverse[a] * acrossScale;
	}
	for (int a = 0; a < 3; ++a)
		candidate[a] = history.FollowPosition[a] + history.BonePosition[a] + offset[a] + lag[a] - rawLag[a];
	// The explicit user limit remains a hard bound, including sudden corrections.
	double actualLag2 = 0;
	for (int a = 0; a < 3; ++a)
		actualLag2 += std::pow(candidate[a] - anchor[a] - offset[a], 2);
	if (actualLag2 > settings.FollowMaxDistance * settings.FollowMaxDistance)
		for (int a = 0; a < 3; ++a)
			candidate[a] =
			    anchor[a] + offset[a] +
			    (candidate[a] - anchor[a] - offset[a]) * settings.FollowMaxDistance / std::sqrt(actualLag2);
	// The smooth estimate can briefly lag further during acceleration. Keep a
	// hard safety envelope as a fallback, rather than using it for every frame.
	if (offsetLength > 1e-3)
	{
		double along = 0;
		for (int a = 0; a < 3; ++a)
			along += (candidate[a] - anchor[a] - offset[a]) * offset[a] / offsetLength;
		const double bounded = (std::max)(-.5 * offsetLength, (std::min)(.5 * offsetLength, along));
		for (int a = 0; a < 3; ++a)
			candidate[a] += (bounded - along) * offset[a] / offsetLength;
	}
	double focus[3];
	for (int a = 0; a < 3; ++a)
		focus[a] = candidate[a] - offset[a];
	const double followed[3] = {candidate[0], candidate[1], candidate[2]};
	if (settings.Attached)
	{
		double recoveryGoal[3] = {candidate[0], candidate[1], candidate[2]};
		double direction[3], length2 = 0;
		for (int a = 0; a < 3; ++a)
		{
			direction[a] = candidate[a] - anchor[a];
			length2 += direction[a] * direction[a];
		}
		double length = std::sqrt(length2);
		if (length < 1e-6)
		{
			MirvTf2Trace_t subject;
			const bool embedded =
			    !CameraCollisionClear(candidate, settings, entity) ||
			    (CameraCollisionTrace(candidate, candidate, settings, subject, nullptr, entity) &&
				 (subject.startSolid || subject.allSolid));
			if (embedded)
			{
				double forward[3] = {1, 0, 0}, right[3], up[3];
				if (angles)
					Afx::Math::MakeVectors(angles[2], angles[0], angles[1], forward, right, up);
				for (int a = 0; a < 3; ++a)
					direction[a] = -forward[a];
				length = 1;
			}
		}
		// An anticipatory soft zone makes continuous approaches retract before
		// contact. Sudden overlaps still require an immediate safety correction.
		const double soft = (std::min)(settings.AttachedSoftDistance, .5 * length);
		double rayEnd[3];
		for (int a = 0; a < 3; ++a)
		{
			direction[a] = length > 1e-9 ? direction[a] / length : 0;
			rayEnd[a] = anchor[a] + direction[a] * (length + soft);
		}
		MirvTf2Trace_t hit;
		if (!CameraCollisionTrace(anchor, rayEnd, settings, hit, entity))
		{
			history.Valid = false;
			return false;
		}
		double allowed = length, hardLimit = length + soft;
		if (!hit.startSolid && !hit.allSolid)
		{
			if (hit.fraction < 1)
			{
				hardLimit = (std::max)(0.0, (length + soft) * hit.fraction - .03125);
				allowed = (std::min)(length, hardLimit);
				if (soft > 0)
				{
					const double h = (std::max)(0.0, 1 - std::abs(length - hardLimit) / soft);
					allowed = (std::max)(0.0, allowed - .25 * soft * h * h);
				}
			}
			// A recovery cut uses the verified boom target, independently of
			// gradual retraction, which can still be on the obstructed side.
			for (int a = 0; a < 3; ++a)
				recoveryGoal[a] = anchor[a] + direction[a] * allowed;
			const bool abrupt =
			    continuous && allowed < history.ArmLength &&
			    (history.Contracting || history.LastAllowed - allowed > (std::max)(1.0, soft * .25));
			history.LastAllowed = allowed;
			history.Contracting = abrupt && CameraCollisionClear(history.Position, settings, entity);
			if (continuous && (history.Contracting || (history.Retracted && allowed > history.ArmLength)) &&
			    CameraCollisionClear(history.Position, settings, entity))
			{
				const double from[3] = {history.ArmLength, 0, 0}, to[3] = {allowed, 0, 0};
				double limited[3];
				CameraReturnTarget(from, to, dt, settings.ReturnHalfLife, settings.ReturnSpeed, limited);
				allowed = limited[0];
				if (std::abs(allowed - history.LastAllowed) < .001)
					history.Contracting = false;
			}
			for (int a = 0; a < 3; ++a)
				candidate[a] = anchor[a] + direction[a] * allowed;
		}
		// Test the final hull against the subject even if other player collisions
		// are disabled. Find its exit along the boom, not an arbitrary axis jump.
		auto clear = [&](const double point[3]) {
			MirvTf2Trace_t subject;
			return CameraCollisionClear(point, settings, entity) &&
			       CameraCollisionTrace(point, point, settings, subject, nullptr, entity) &&
			       !subject.startSolid && !subject.allSolid;
		};
		MirvTf2Trace_t overlap;
		const bool insideSubject =
		    CameraCollisionTrace(candidate, candidate, settings, overlap, nullptr, entity) &&
		    (overlap.startSolid || overlap.allSolid);
		if (insideSubject && length > 1e-6 && !hit.startSolid && !hit.allSolid)
		{
			const double outerLength = length + 64 + settings.Radius * 2;
			double outer[3];
			for (int a = 0; a < 3; ++a)
				outer[a] = anchor[a] + direction[a] * outerLength;
			MirvTf2Trace_t subject;
			if (CameraCollisionTrace(outer, anchor, settings, subject, nullptr, entity) &&
			    !subject.startSolid && !subject.allSolid && subject.fraction < 1)
			{
				const double exit = outerLength * (1 - subject.fraction) + .03125;
				double trial[3];
				for (int a = 0; a < 3; ++a)
					trial[a] = anchor[a] + direction[a] * exit;
				MirvTf2Trace_t tether;
				if (clear(trial) && CameraCollisionTrace(anchor, trial, settings, tether, entity) &&
				    !tether.startSolid && !tether.allSolid && tether.fraction == 1)
					std::copy(trial, trial + 3, candidate);
			}
		}
		const double movementGoal[3] = {candidate[0], candidate[1], candidate[2]};
		if (continuous && clear(history.Position))
		{
			double swept[3];
			if (!CameraCollisionMove(history.Position, candidate, settings, swept, entity))
			{
				history.Valid = false;
				return false;
			}
			// The same sweep must not pass through the attached subject itself.
			MirvTf2Trace_t subject;
			if (CameraCollisionTrace(history.Position, swept, settings, subject, nullptr, entity) &&
			    !subject.startSolid && !subject.allSolid && subject.fraction < 1)
				for (int a = 0; a < 3; ++a)
					swept[a] = history.Position[a] + (swept[a] - history.Position[a]) *
					                                     (std::max)(0.0, double(subject.fraction) - .001);
			std::copy(swept, swept + 3, candidate);
		}
		if (!clear(candidate) && continuous && clear(history.Position))
			std::copy(history.Position, history.Position + 3, candidate);
		// A collision sweep cannot find a route through a closed door. Bound the
		// time spent stranded, and perform an explicit recovery cut only to a
		// verified clear endpoint reachable from the subject's physical pivot.
		double blocked2 = 0;
		for (int a = 0; a < 3; ++a)
			blocked2 += std::pow(candidate[a] - movementGoal[a], 2);
		const double tolerance = (std::max)(settings.Radius * 2, offsetLength * .25);
		bool blockedView = false;
		if (following && continuous && settings.FollowRecoveryDelay > 0 &&
		    (hit.startSolid || hit.allSolid || hit.fraction < 1 || blocked2 > 1e-6))
		{
			// A clear boom and unchanged movement endpoint already prove visibility.
			MirvTf2Trace_t visibility;
			auto sight = settings;
			sight.Radius = 0; // Visibility uses a point ray, rather than the camera clearance hull.
			blockedView = CameraCollisionTrace(anchor, candidate, sight, visibility, entity) &&
			              !visibility.startSolid && !visibility.allSolid && visibility.fraction < 1;
		}
		// Opposite sides of a thin closed door can be only a few units apart.
		// Distance alone must not leave the camera stranded indefinitely.
		if (following && continuous && settings.FollowRecoveryDelay > 0 &&
		    (blocked2 > tolerance * tolerance || blockedView))
		{
			if (!clear(recoveryGoal))
				std::copy(movementGoal, movementGoal + 3, recoveryGoal);
			MirvTf2Trace_t tether;
			if (clear(recoveryGoal) && CameraCollisionTrace(anchor, recoveryGoal, settings, tether, entity) &&
			    !tether.startSolid && !tether.allSolid && tether.fraction == 1)
			{
				history.BlockedTime += dt;
				if (history.BlockedTime >= settings.FollowRecoveryDelay)
				{
					std::copy(recoveryGoal, recoveryGoal + 3, candidate);
					std::copy(root, root + 3, history.FollowPosition);
					std::fill(history.FollowVelocity, history.FollowVelocity + 3, 0.0);
					std::copy(anchor, anchor + 3, focus);
					// Rebase the whole shot to the recovered pose. Resetting only
					// root lag would cause a second jump on the following frame.
					double recoveredLength2 = 0;
					for (int a = 0; a < 3; ++a)
					{
						offset[a] = candidate[a] - anchor[a];
						recoveredLength2 += offset[a] * offset[a];
					}
					history.OrbitLength = std::sqrt(recoveredLength2);
					if (history.OrbitLength > 1e-6)
						history.Orbit = directionFrame(offset, history.OrbitYaw);
					history.BlockedTime = 0;
					history.Contracting = false;
				}
			}
			else
				history.BlockedTime = 0;
		}
		else
			history.BlockedTime = 0;
		// There may be no camera-sized gap between the subject and geometry.
		// Keep the swept result in that case; do not teleport to another side.
	}
	if (!settings.Attached)
		history.BlockedTime = 0;
	std::copy(desired, desired + 3, history.Desired);
	if (angles)
	{
		std::copy(angles, angles + 3, history.AuthoredAngles);
		if (rawLength > 1e-3)
		{
			double f[3], r[3], u[3], local[3] = {};
			MakeVectors(angles[2], angles[0], angles[1], f, r, u);
			for (int a = 0; a < 3; ++a)
			{
				local[0] -= f[a] * rawOffset[a] / rawLength;
				local[1] -= r[a] * rawOffset[a] / rawLength;
				local[2] -= u[a] * rawOffset[a] / rawLength;
			}
			const auto target = directionFrame(local, 0);
			const bool previous = shotContinuous && history.HasAngles && history.CompositionValid;
			history.Composition = previous ? blend(history.Composition, target, orbitAlpha) : target;
			const float roll =
			    previous ? float(history.DisplayAngles[2] +
				                 std::remainder(angles[2] - history.DisplayAngles[2], 360.0) * orbitAlpha)
				         : angles[2];
			if (previous)
				std::copy(history.DisplayAngles, history.DisplayAngles + 3, angles);
			angles[2] = roll;
			forward(history.Composition, local);
			MakeVectors(angles[2], angles[0], angles[1], f, r, u);
			double reference[3], distance2 = 0;
			// A filtered focus avoids turning snapshot/bone noise into view jitter.
			// With rigid follow, use the physical pivot as before.
			const double *targetPoint = following ? focus : anchor;
			for (int a = 0; a < 3; ++a)
			{
				reference[a] = targetPoint[a] - (f[a] * local[0] + r[a] * local[1] + u[a] * local[2]);
				distance2 += (candidate[a] - targetPoint[a]) * (candidate[a] - targetPoint[a]);
			}
			if (distance2 > 1)
				MirvCameraAttachedReframe(reference, candidate, targetPoint, angles);
		}
		else if (following && shotContinuous && history.HasAngles)
		{
			// At zero boom length the target direction is undefined. Smooth the
			// authored view directly instead of solving a coincident look-at.
			auto target = Quaternion::FromQREulerAngles(
			    QREulerAngles::FromQEulerAngles(QEulerAngles(angles[0], angles[1], angles[2])));
			if (DotProduct(history.View, target) < 0)
				target = -1.0 * target;
			Vector3 axis;
			const double angle = std::abs(history.View.GetAng(target, axis));
			const double alpha =
			    orbitAlpha < 1 && settings.OrbitSpeed > 0 && angle > 1e-9
			        ? (std::min)(orbitAlpha, settings.OrbitSpeed / 57.29577951308232 * dt / angle)
			        : orbitAlpha;
			const auto value = NearestEquivalentEuler(
			    blend(history.View, target, alpha).ToQREulerAngles().ToQEulerAngles(),
			    QEulerAngles(history.DisplayAngles[0], history.DisplayAngles[1], history.DisplayAngles[2]));
			angles[0] = float(value.Pitch);
			angles[1] = float(value.Yaw);
			angles[2] = float(value.Roll);
		}
		history.View = Quaternion::FromQREulerAngles(
		    QREulerAngles::FromQEulerAngles(QEulerAngles(angles[0], angles[1], angles[2])));
		std::copy(angles, angles + 3, history.DisplayAngles);
	}
	history.CompositionValid = angles && rawLength > 1e-3;
	history.HasAngles = angles != nullptr;
	x = float(candidate[0]);
	y = float(candidate[1]);
	z = float(candidate[2]);
	double error2 = 0;
	for (int a = 0; a < 3; ++a)
		error2 += (candidate[a] - followed[a]) * (candidate[a] - followed[a]);
	history.Retracted = settings.Attached && error2 > 1e-6;
	history.Valid = true;
	history.Identity = identity;
	history.Entity = entity;
	history.Generation = generation;
	history.Time = sceneTime;
	std::copy(candidate, candidate + 3, history.Position);
	std::copy(root, root + 3, history.Root);
	history.FollowHalfLife = settings.FollowHalfLife;
	history.BoneHalfLife = settings.BoneHalfLife;
	history.ArmLength = 0;
	for (int a = 0; a < 3; ++a)
		history.ArmLength += (candidate[a] - anchor[a]) * (candidate[a] - anchor[a]);
	history.ArmLength = std::sqrt(history.ArmLength);
	return true;
}
// End optional camera collision implementation.

// Minimal IVModelInfo (public Source SDK interface "VModelInfoClient006") up to GetStudiomodel.
// The declarations mirror the SDK's IVModelInfo in order (including the overloads), so the compiler
// lays out the vtable like the game's. GetModelName, GetModelType and GetStudiomodel are used.
class CMirvTf2ModelInfo
{
public:
	virtual ~CMirvTf2ModelInfo() {}
	virtual const void * GetModel(int modelindex) = 0;
	virtual int GetModelIndex(const char * name) const = 0;
	virtual const char * GetModelName(const void * model) const = 0;
	virtual void * GetVCollide(const void * model) = 0;
	virtual void * GetVCollide(int modelindex) = 0;
	virtual void GetModelBounds(const void * model, void * mins, void * maxs) const = 0;
	virtual void GetModelRenderBounds(const void * model, void * mins, void * maxs) const = 0;
	virtual int GetModelFrameCount(const void * model) const = 0;
	virtual int GetModelType(const void * model) const = 0;
	virtual void * GetModelExtraData(const void * model) = 0;
	virtual bool ModelHasMaterialProxy(const void * model) const = 0;
	virtual bool IsTranslucent(const void * model) const = 0;
	virtual bool IsTranslucentTwoPass(const void * model) const = 0;
	virtual void RecomputeTranslucency(const void * model, int nSkin, int nBody, void * pClientRenderable, float fInstanceAlphaModulation) = 0;
	virtual int GetModelMaterialCount(const void * model) const = 0;
	virtual void GetModelMaterials(const void * model, int count, void ** ppMaterial) = 0;
	virtual bool IsModelVertexLit(const void * model) const = 0;
	virtual const char * GetModelKeyValueText(const void * model) = 0;
	virtual bool GetModelKeyValue(const void * model, void * buf) = 0;
	virtual float GetModelRadius(const void * model) = 0;
	virtual const void * FindModel(const void * pStudioHdr, void ** cache, const char * modelname) const = 0;
	virtual const void * FindModel(void * cache) const = 0;
	virtual void * GetVirtualModel(const void * pStudioHdr) const = 0;
	virtual unsigned char * GetAnimBlock(const void * pStudioHdr, int iBlock) const = 0;
	virtual void GetModelMaterialColorAndLighting(const void * model, void const * origin, void const * angles, void * pTrace, void * lighting, void * matColor) = 0;
	virtual void GetIlluminationPoint(const void * model, void * pRenderable, void const * origin, void const * angles, void * pLightingCenter) = 0;
	virtual int GetModelContents(int modelIndex) = 0;
	virtual void * GetStudiomodel(const void * mod) = 0;
};

static CMirvTf2ModelInfo * Mirv_Tf2_GetModelInfo(void)
{
	static CMirvTf2ModelInfo * cached = 0;

	if (!cached && g_AppSystemFactory)
		cached = reinterpret_cast<CMirvTf2ModelInfo *>(g_AppSystemFactory("VModelInfoClient006", 0));

	return cached;
}

// Reads the bone names out of a studio header (studiohdr_t, MDL versions 44 to 49).
// Layout: int id (0), int version (4), int length (76), int numbones (156), int boneindex (160).
// mstudiobone_t is 216 bytes: int sznameindex (0), int parent (4), ...
// Every offset is checked against the header's length. Returns the number of bones read, 0 if the header looks invalid.
static int Mirv_Tf2_ReadBoneNames(const unsigned char * hdr, MirvTf2BoneName_t * outNames)
{
	const int hdrSize = 164;
	const int boneSize = 216;

	int id, version, length, numBones, boneIndex;
	memcpy(&id, hdr + 0, sizeof(int));
	memcpy(&version, hdr + 4, sizeof(int));
	memcpy(&length, hdr + 76, sizeof(int));
	memcpy(&numBones, hdr + 156, sizeof(int));
	memcpy(&boneIndex, hdr + 160, sizeof(int));

	if (0x54534449 /* IDST */ != id || version < 44 || 49 < version)
		return 0;

	if (length < hdrSize || numBones < 1 || 1024 < numBones || boneIndex < hdrSize)
		return 0;

	if ((long long)boneIndex + (long long)numBones * boneSize > (long long)length)
		return 0;

	const int count = numBones < MIRV_TF2_MAX_BONES ? numBones : MIRV_TF2_MAX_BONES;

	for (int i = 0; i < count; ++i)
	{
		const long long boneOffset = (long long)boneIndex + (long long)i * boneSize;

		int nameIndex, parent;
		memcpy(&nameIndex, hdr + boneOffset + 0, sizeof(int));
		memcpy(&parent, hdr + boneOffset + 4, sizeof(int));

		MirvTf2BoneName_t & out = outNames[i];
		out.name[0] = 0;
		out.parent = parent;

		const long long nameOffset = boneOffset + nameIndex;

		if (nameIndex <= 0 || nameOffset < 0 || nameOffset >= length)
			continue;

		int n = 0;
		while (n < (int)sizeof(out.name) - 1 && nameOffset + n < length && hdr[nameOffset + n])
		{
			out.name[n] = (char)hdr[nameOffset + n];
			++n;
		}
		out.name[n] = 0;
	}

	return count;
}

int Mirv_Tf2_GetBoneNames(int entityIndex, MirvTf2BoneName_t * outNames)
{
	CMirvTf2ModelInfo * modelInfo = Mirv_Tf2_GetModelInfo();

	CClientToolsTf2 * tools = CClientToolsTf2::Instance();
	SOURCESDK::TF2::IClientTools * iface = tools ? tools->GetClientTools() : 0;

	if (!modelInfo || !iface || entityIndex < 0 || 2048 <= entityIndex)
		return 0;

	for (auto ent = iface->FirstEntity(); ent; ent = iface->NextEntity(ent))
	{
		if (iface->GetEntIndex(ent) != entityIndex)
			continue;

		CMirvTf2ClientUnknown * unknown = reinterpret_cast<CMirvTf2ClientUnknown *>(ent);
		CMirvTf2ClientRenderable * renderable = unknown ? unknown->GetClientRenderable() : 0;

		if (!renderable)
			return 0;

		const void * model = renderable->GetModel();
		if (!model)
			return 0;

		// Sanity check of the interface: model names end in ".mdl".
		const char * modelName = modelInfo->GetModelName(model);
		if (!modelName)
			return 0;

		const size_t nameLen = strlen(modelName);
		if (nameLen < 4 || 0 != _stricmp(modelName + nameLen - 4, ".mdl"))
			return 0;

		const unsigned char * hdr = reinterpret_cast<const unsigned char *>(modelInfo->GetStudiomodel(model));
		if (!hdr)
			return 0;

		return Mirv_Tf2_ReadBoneNames(hdr, outNames);
	}

	return 0;
}

static bool Mirv_ContainsNoCase(const char * hay, const char * needle)
{
	const size_t n = strlen(needle);

	if (0 == n)
		return true;

	for (const char * p = hay; *p; ++p)
	{
		if (0 == _strnicmp(p, needle, n))
			return true;
	}

	return false;
}

// Result: Bone index, -1 not found, -2 more than one partial match.
static int Mirv_FindBoneInNames(const MirvTf2BoneName_t * names, int count, const char * name)
{
	for (int i = 0; i < count; ++i)
	{
		if (0 == _stricmp(names[i].name, name))
			return i;
	}

	int found = -1;

	for (int i = 0; i < count; ++i)
	{
		if (names[i].name[0] && Mirv_ContainsNoCase(names[i].name, name))
		{
			if (-1 != found)
				return -2;

			found = i;
		}
	}

	return found;
}

bool Mirv_Tf2_ParseBoneArg(int entityIndex, const char * arg, int & outBone)
{
	bool numeric = '\0' != arg[0];
	for (const char * c = ('-' == arg[0] ? arg + 1 : arg); *c; ++c)
	{
		if (*c < '0' || '9' < *c) { numeric = false; break; }
	}
	if ('-' == arg[0] && '\0' == arg[1]) numeric = false;

	if (numeric)
	{
		outBone = atoi(arg);
		return true;
	}

	MirvTf2BoneName_t names[MIRV_TF2_MAX_BONES];
	const int count = Mirv_Tf2_GetBoneNames(entityIndex, names);

	if (count < 1)
	{
		Tier0_Warning("Bone names are not available for entity %i, use the bone index instead (see mirv_listbones).\n", entityIndex);
		return false;
	}

	const int bone = Mirv_FindBoneInNames(names, count, arg);

	if (-1 == bone)
	{
		Tier0_Warning("Entity %i has no bone with \"%s\" in its name (see mirv_listbones).\n", entityIndex, arg);
		return false;
	}

	if (-2 == bone)
	{
		Tier0_Warning("More than one bone of entity %i matches \"%s\", use a longer part of the name or the index:\n", entityIndex, arg);

		for (int i = 0; i < count; ++i)
		{
			if (names[i].name[0] && Mirv_ContainsNoCase(names[i].name, arg))
				Tier0_Warning("  %i: %s\n", i, names[i].name);
		}

		return false;
	}

	outBone = bone;
	return true;
}

bool Mirv_Tf2_GetAttachAnchor(IMirvVecAngCalc *calc, double origin[3], void *&entity, double entityOrigin[3], int *mode)
{
	return calc && static_cast<CMirvVecAngTf2EntityIndexCalc *>(calc)->GetAttachAnchor(origin, entity, entityOrigin, mode);
}

bool Mirv_Tf2_CaptureCamera(IMirvVecAngCalc * calc, const double camOrigin[3], const double camAngles[3], float outLocalOffset[3])
{
	if (!calc)
		return false;

	// Only valid for calcs created by NewTf2EntityIndexCalc.
	return static_cast<CMirvVecAngTf2EntityIndexCalc *>(calc)->CaptureCamera(camOrigin, camAngles, outLocalOffset);
}

bool Mirv_Tf2_GetEntityAnglesInfo(int entityIndex, float outAbsAngles[3], float outRenderAngles[3])
{
	IMirvVecAngCalc * calc = g_MirvVecAngCalcs.NewTf2EntityIndexCalc(0, entityIndex, -1, MIRV_TF2_BONEMODE_BONE);
	if (!calc)
		return false;

	calc->AddRef();

	bool ok = static_cast<CMirvVecAngTf2EntityIndexCalc *>(calc)->GetAnglesInfo(outAbsAngles, outRenderAngles);

	calc->Release();

	return ok;
}

void Mirv_Tf2_ReadDiagnosticFields(int entity, const void *object, const int *offsets, float *values,
                                   int count, float renderAngles[3])
{
	if (!object || !offsets || !values || count < 0 || count > 32)
		return;
	auto tools = CClientToolsTf2::Instance();
	auto iface = tools ? tools->GetClientTools() : nullptr;
	if (!iface)
		return;
	for (auto ent = iface->FirstEntity(); ent; ent = iface->NextEntity(ent))
	{
		if (iface->GetEntIndex(ent) != entity)
			continue;
		if (static_cast<const void *>(ent) != object)
			return;
		for (int i = 0; i < count; ++i)
			if (offsets[i] >= 0 && offsets[i] <= 65536 - int(sizeof(float)))
				std::memcpy(values + i, static_cast<const char *>(object) + offsets[i], sizeof(float));
		auto renderable = reinterpret_cast<CMirvTf2ClientUnknown *>(ent)->GetClientRenderable();
		auto angles = renderable ? reinterpret_cast<const float *>(renderable->GetRenderAngles()) : nullptr;
		if (angles && renderAngles)
			std::copy(angles, angles + 3, renderAngles);
		return;
	}
}

// Animation diagnostic access stays next to the existing TF2 entity helpers.
void *Mirv_Tf2_DiagnosticEntity(int entity)
{
	auto tools = CClientToolsTf2::Instance();
	auto iface = tools ? tools->GetClientTools() : nullptr;
	if (iface)
		for (auto ent = iface->FirstEntity(); ent; ent = iface->NextEntity(ent))
			if (iface->GetEntIndex(ent) == entity)
				return ent;
	return nullptr;
}
void Mirv_Tf2_ReadAnimationDiagnostic(int entity, const void *object, const MirvTf2DiagnosticField *fields,
                                      int count, double *values, int *status, float origin[3])
{
	if (!fields || !values || !status || count < 0 || count > 512)
		return;
	void *live = Mirv_Tf2_DiagnosticEntity(entity);
	if (!live || live != object)
		return;
	struct VectorCopy
	{
		int Length = 0, Stride = 0, Maximum = 0, Status = 3;
		std::vector<char> Data;
	};
	std::map<int, VectorCopy> vectors;
	for (int i = 0; i < count; ++i)
	{
		const auto &f = fields[i];
		int size = f.Kind == 2 ? 1 : f.Kind == 3 ? 2 : 4;
		if (f.Kind < 0 || f.Kind > 3 || f.Offset < 0 || f.Offset > 65536 - size)
			continue;
		const char *base = static_cast<const char *>(object);
		if (f.VectorOffset >= 0)
		{
			// Public Source SDK CUtlVector/CUtlMemory layout. The location, element
			// stride and maximum are supplied by validated receive-proxy extra data.
			if (f.VectorOffset > 65536 - 32 || f.Maximum < 1 || f.Maximum > 64 || f.Element < 0 ||
			    f.Element >= f.Maximum || f.Stride < size || f.Stride > 512 || f.Offset > f.Stride - size)
			{
				status[i] = 3;
				continue;
			}
			const char *data = nullptr;
			int allocation = 0, length = 0;
			std::memcpy(&data, base + f.VectorOffset, sizeof(data));
			std::memcpy(&allocation, base + f.VectorOffset + sizeof(void *), sizeof(int));
			std::memcpy(&length, base + f.VectorOffset + sizeof(void *) + 2 * sizeof(int), sizeof(int));
			if (length < 0 || length > f.Maximum || allocation < length || allocation > 65536 ||
			    (length && !data))
			{
				status[i] = 3;
				continue;
			}
			if (f.Element >= length)
			{
				status[i] = 4;
				continue;
			}
			auto found = vectors.find(f.VectorOffset);
			if (found == vectors.end())
			{
				VectorCopy copy;
				copy.Length = length;
				copy.Stride = f.Stride;
				copy.Maximum = f.Maximum;
				copy.Data.resize(size_t(length) * f.Stride);
				SIZE_T read = 0;
				if (ReadProcessMemory(GetCurrentProcess(), data, copy.Data.data(), copy.Data.size(), &read) &&
				    read == copy.Data.size())
					copy.Status = 0;
				found = vectors.emplace(f.VectorOffset, std::move(copy)).first;
			}
			const auto &copy = found->second;
			if (copy.Status || copy.Length != length || copy.Stride != f.Stride || copy.Maximum != f.Maximum)
			{
				status[i] = 3;
				continue;
			}
			base = copy.Data.data() + f.Element * f.Stride;
		}
		if (f.Kind == 0)
		{
			float v;
			std::memcpy(&v, base + f.Offset, 4);
			values[i] = v;
		}
		else if (f.Kind == 1)
		{
			int v;
			std::memcpy(&v, base + f.Offset, 4);
			values[i] = v;
		}
		else if (f.Kind == 2)
		{
			unsigned char v;
			std::memcpy(&v, base + f.Offset, 1);
			values[i] = v;
		}
		else
		{
			short v;
			std::memcpy(&v, base + f.Offset, 2);
			values[i] = v;
		}
		status[i] = std::isfinite(values[i]) ? 0 : 5;
	}
	auto renderable = reinterpret_cast<CMirvTf2ClientUnknown *>(live)->GetClientRenderable();
	auto renderOrigin = renderable ? reinterpret_cast<const float *>(renderable->GetRenderOrigin()) : nullptr;
	if (origin && renderOrigin)
		std::copy(renderOrigin, renderOrigin + 3, origin);
}
bool Mirv_Tf2_DiagnosticModel(int entity, MirvTf2DiagnosticModel &out)
{
	out = MirvTf2DiagnosticModel{};
	auto live = Mirv_Tf2_DiagnosticEntity(entity);
	auto renderable = live ? reinterpret_cast<CMirvTf2ClientUnknown *>(live)->GetClientRenderable() : nullptr;
	auto info = Mirv_Tf2_GetModelInfo();
	auto model = renderable ? renderable->GetModel() : nullptr;
	if (!info || !model)
		return false;
	const char *name = info->GetModelName(model);
	if (name)
	{
		std::strncpy(out.Name, name, sizeof(out.Name) - 1);
	}
	auto hdr = static_cast<const unsigned char *>(info->GetStudiomodel(model));
	if (!hdr)
		return false;
	MirvTf2BoneName_t names[MIRV_TF2_MAX_BONES];
	out.Bones = Mirv_Tf2_ReadBoneNames(hdr, names);
	if (!out.Bones)
		return false;
	int length, boneOffset;
	std::memcpy(&length, hdr + 76, 4);
	std::memcpy(&boneOffset, hdr + 160, 4);
	for (int i = 0; i < out.Bones; ++i)
		std::memcpy(out.BoneFlags + i, hdr + boneOffset + i * 216 + 160, 4);
	if (length < 308)
		return true;
	int count, offset;
	std::memcpy(&count, hdr + 300, 4);
	std::memcpy(&offset, hdr + 304, 4);
	if (count < 0 || count > 24 || offset < 308 || (long long)offset + count * 20 > length)
		return true;
	out.Poses = count;
	for (int i = 0; i < count; ++i)
	{
		int at = offset + i * 20, nameOffset;
		std::memcpy(&nameOffset, hdr + at, 4);
		if (nameOffset > 0 && nameOffset <= length - at - 1)
			for (int j = 0; j < 63 && at + nameOffset + j < length && hdr[at + nameOffset + j]; ++j)
				out.PoseNames[i][j] = char(hdr[at + nameOffset + j]);
		std::memcpy(out.PoseStart + i, hdr + at + 8, 4);
		std::memcpy(out.PoseEnd + i, hdr + at + 12, 4);
		std::memcpy(out.PoseLoop + i, hdr + at + 16, 4);
	}
	return true;
}
namespace
{
struct DiagnosticBoneObserver
{
	std::mutex Mutex;
	std::atomic<void *> Target{nullptr};
	const void *Model = nullptr;
	int Count = 0, Generation = 0;
	unsigned long long Call = 0, Dropped = 0;
	MirvTf2DiagnosticBones Queue[16];
	int Read = 0, Size = 0;
} g_DiagnosticBones;
#ifdef _WIN64
using DiagnosticSetupBones = bool (*)(void *, SOURCESDK::matrix3x4_t *, int, int, float);
#else
using DiagnosticSetupBones = bool(__fastcall *)(void *, void *, SOURCESDK::matrix3x4_t *, int, int, float);
#endif
DiagnosticSetupBones g_DiagnosticSetupBones = nullptr;
void *g_DiagnosticSetupBonesAddress = nullptr;
bool __fastcall DiagnosticObserveSetupBones(void *self,
#ifndef _WIN64
                                            void *edx,
#endif
                                            SOURCESDK::matrix3x4_t *matrix, int maximum, int mask, float time)
{
	bool result = g_DiagnosticSetupBones(self,
#ifndef _WIN64
	                                     edx,
#endif
	                                     matrix, maximum, mask, time);
	auto &o = g_DiagnosticBones;
	if (self != o.Target.load())
		return result;
	const void *model = static_cast<CMirvTf2ClientRenderable *>(self)->GetModel();
	std::lock_guard<std::mutex> lock(o.Mutex);
	if (self != o.Target.load())
		return result;
	if (model != o.Model)
	{
		++o.Dropped;
		return result;
	}
	if (o.Size == 16)
	{
		++o.Dropped;
		return result;
	}
	auto &row = o.Queue[(o.Read + o.Size++) % 16];
	row.Call = ++o.Call;
	row.Success = result;
	row.Output = matrix != nullptr;
	row.Time = time;
	row.Mask = mask;
	row.Generation = o.Generation;
	auto gl = g_Hook_VClient_RenderView.GetGlobals();
	row.Frame = gl ? gl->framecount_get() : -1;
	row.ClientTime = gl ? gl->curtime_get() : time;
	row.Count = result && matrix && maximum > 0 ? (std::min)(maximum, o.Count) : 0;
	for (int b = 0; b < row.Count; ++b)
		std::memcpy(row.Matrix[b], matrix[b].m_flMatVal, sizeof(row.Matrix[b]));
	return result;
}
} // namespace
int Mirv_Tf2_DiagnosticBoneConfigure(int entity, int &generation)
{
	auto &o = g_DiagnosticBones;
	void *live = Mirv_Tf2_DiagnosticEntity(entity);
	auto renderable = live ? reinterpret_cast<CMirvTf2ClientUnknown *>(live)->GetClientRenderable() : nullptr;
	if (!renderable)
	{
		Mirv_Tf2_DiagnosticBoneStop();
		return 0;
	}
	void *address = (*reinterpret_cast<void ***>(renderable))[16];
	if (!g_DiagnosticSetupBones)
	{
		g_DiagnosticSetupBones = reinterpret_cast<DiagnosticSetupBones>(address);
		LONG error = DetourTransactionBegin();
		if (error == NO_ERROR)
		{
			error = DetourUpdateThread(GetCurrentThread());
			if (error == NO_ERROR)
				error = DetourAttach(reinterpret_cast<PVOID *>(&g_DiagnosticSetupBones),
				                     reinterpret_cast<PVOID>(DiagnosticObserveSetupBones));
			if (error == NO_ERROR)
				error = DetourTransactionCommit();
			else
				DetourTransactionAbort();
		}
		if (error != NO_ERROR)
		{
			g_DiagnosticSetupBones = nullptr;
			return 0;
		}
		g_DiagnosticSetupBonesAddress = address;
	}
	if (address != g_DiagnosticSetupBonesAddress)
	{
		Mirv_Tf2_DiagnosticBoneStop();
		return 0;
	}
	const void *model = renderable->GetModel();
	{
		std::lock_guard<std::mutex> lock(o.Mutex);
		if (o.Model == model && o.Target.load() == renderable && o.Count > 0)
		{
			generation = o.Generation;
			return o.Count;
		}
	}
	// Model-cache calls must stay outside the queue mutex (bone jobs can hold the model-cache lock).
	MirvTf2BoneName_t names[MIRV_TF2_MAX_BONES];
	int count = Mirv_Tf2_GetBoneNames(entity, names);
	std::lock_guard<std::mutex> lock(o.Mutex);
	if (o.Model != model || o.Target.load() != renderable || o.Count != count)
	{
		o.Target.store(nullptr);
		o.Dropped += o.Size;
		o.Read = o.Size = 0;
		o.Count = count;
		o.Model = model;
		++o.Generation;
		o.Target.store(renderable);
	}
	generation = o.Generation;
	return o.Count;
}
bool Mirv_Tf2_DiagnosticBonePop(MirvTf2DiagnosticBones &out)
{
	auto &o = g_DiagnosticBones;
	std::lock_guard<std::mutex> lock(o.Mutex);
	if (!o.Size)
		return false;
	out = o.Queue[o.Read];
	o.Read = (o.Read + 1) % 16;
	--o.Size;
	return true;
}
unsigned long long Mirv_Tf2_DiagnosticBoneDropped()
{
	std::lock_guard<std::mutex> lock(g_DiagnosticBones.Mutex);
	return g_DiagnosticBones.Dropped;
}
void Mirv_Tf2_DiagnosticBoneStop()
{
	auto &o = g_DiagnosticBones;
	std::lock_guard<std::mutex> lock(o.Mutex);
	o.Target.store(nullptr);
	o.Model = nullptr;
	o.Count = 0;
	o.Dropped += o.Size;
	o.Read = o.Size = 0;
}
static bool Mirv_IsUsableBound(float v)
{
	// Also false for NaN.
	return -1.0e6f < v && v < 1.0e6f;
}

// TF2 client class names can be C++-style (CTFProjectile_Rocket) or
// entity-style (tf_projectile_rocket). Match case-insensitively and give
// weapon / wearable names priority over shared words such as "rocket".
static int Mirv_Tf2_LabelCategory(const char *name, bool isPlayer, bool scene = false)
{
    if (isPlayer) return MIRV_TF2_ENTITY_PLAYER;
    if (!name || !name[0]) return MIRV_TF2_ENTITY_OTHER;

    // Class names are short; cap the copy to avoid a pathological input.
    char lower[256];
    size_t n = 0;
    for (; name[n] && n + 1 < sizeof(lower); ++n)
        lower[n] = (char)tolower((unsigned char)name[n]);
    lower[n] = '\0';

	// Keep existing mirv_drawentities categories unchanged; extended categories are cache-only.
	if (scene && (strstr(lower, "viewmodel") || strstr(lower, "view_model") || strstr(lower, "wearablevm")))
		return -1; // Never capture first-person arms/weapons as ordinary props.
	if (scene &&
	    (strstr(lower, "objectsentrygun") || strstr(lower, "objectdispenser") ||
	     strstr(lower, "objectteleporter") || strstr(lower, "objectsapper") ||
	     strstr(lower, "objectcartdispenser") || strstr(lower, "baseobject") || !strncmp(lower, "obj_", 4)))
		return MIRV_TF2_ENTITY_BUILDING;

	if (scene && strstr(lower, "ragdoll"))
		return MIRV_TF2_ENTITY_RAGDOLL;
	if (scene && (strstr(lower, "c_gib") || strstr(lower, "c_tfgib") || strstr(lower, "ctfgib") ||
	              !strcmp(lower, "gib") || !strcmp(lower, "tf_gib")))
		return MIRV_TF2_ENTITY_GIB;

	if (strstr(lower, "wearable") || strstr(lower, "cosmetic") || strstr(lower, "econ_wear") || strstr(lower, "econwear"))
        return MIRV_TF2_ENTITY_COSMETIC;

    // Network class names lack the tools API's tf_weapon_ prefix. Explicit
    // projectile families win; launcher/flare-gun weapon names stay weapons.
    if (strstr(lower, "projectile") || strstr(lower, "grenadeproj") || strstr(lower, "merasmusgrenade"))
        return MIRV_TF2_ENTITY_PROJECTILE;

    // A rocket launcher and grenade launcher are weapons, not projectiles.
    if (strstr(lower, "weapon") || strstr(lower, "tf_weapon") || strstr(lower, "combatweapon") ||
        strstr(lower, "launcher") || strstr(lower, "flaregun") || strstr(lower, "crossbow") ||
        strstr(lower, "compoundbow") || strstr(lower, "rocketpack") || strstr(lower, "ctfcleaver"))
        return MIRV_TF2_ENTITY_WEAPON;

    // Explicit projectile families and common TF2 network / entity names.
    if (strstr(lower, "projectile") || strstr(lower, "pipebomb") || strstr(lower, "grenade")
        || strstr(lower, "rocket") || strstr(lower, "arrow") || strstr(lower, "flare")
        || strstr(lower, "cleaver") || strstr(lower, "stunball") || strstr(lower, "stun_ball")
        || strstr(lower, "baseball") || strstr(lower, "tf_flame") || strstr(lower, "flame_entity")
        || strstr(lower, "tf_spell") || strstr(lower, "spellbook") || strstr(lower, "pumpkin_bomb"))
        return MIRV_TF2_ENTITY_PROJECTILE;

    return MIRV_TF2_ENTITY_OTHER;
}

// Share the tools-classname fallback with entity labels. In particular,
// CTFSniperRifle / CTFMinigun lack "weapon" in their network class name.
// A temporary tools handle does not enable recording or change AGR mode.
static int Mirv_Tf2_ToolCategory(SOURCESDK::TF2::IClientTools *iface,
                                 SOURCESDK::TF2::EntitySearchResult entity, int index, bool isPlayer,
                                 bool scene = false)
{
	auto handle = iface->GetToolHandleForEntityByIndex(index);
	bool attachedHere = false;
	if (handle == SOURCESDK::TF2::HTOOLHANDLE_INVALID)
	{
		handle = iface->AttachToEntity(entity);
		attachedHere = handle != SOURCESDK::TF2::HTOOLHANDLE_INVALID;
	}
	int category = Mirv_Tf2_LabelCategory(
	    handle != SOURCESDK::TF2::HTOOLHANDLE_INVALID ? iface->GetClassname(handle) : nullptr, isPlayer,
	    scene);
	if (attachedHere)
		iface->DetachFromEntity(entity);
	return category;
}
int Mirv_Tf2_CollectEntityLabels(bool playersOnly, MirvTf2EntityLabel_t * outLabels, int maxCount)
{
	CClientToolsTf2 * tools = CClientToolsTf2::Instance();
	SOURCESDK::TF2::IClientTools * iface = tools ? tools->GetClientTools() : 0;

	if (!iface)
		return 0;

	int count = 0;

	for (auto ent = iface->FirstEntity(); ent && count < maxCount; ent = iface->NextEntity(ent))
	{
		const int index = iface->GetEntIndex(ent);

		// Index 0 is the world, only networked entities have an index that can be used elsewhere.
		if (index < 1 || 2048 <= index)
			continue;

		if (playersOnly && !iface->IsPlayer(ent))
			continue;

		// EntitySearchResult is a C_BaseEntity pointer, IClientUnknown is its first base.
		CMirvTf2ClientUnknown * unknown = reinterpret_cast<CMirvTf2ClientUnknown *>(ent);
		CMirvTf2ClientRenderable * renderable = unknown ? unknown->GetClientRenderable() : 0;

		if (!renderable)
			continue;

		// When showing all entities, leave out those that are not drawn (hidden, not in view and so on) to reduce the clutter.
		if (!playersOnly && !renderable->ShouldDraw())
			continue;

		const float * origin = reinterpret_cast<const float *>(renderable->GetRenderOrigin());

		if (!origin)
			continue;

		MirvTf2EntityLabel_t & label = outLabels[count++];
		label.category = Mirv_Tf2_ToolCategory(iface, ent, index, iface->IsPlayer(ent));
		label.index = index;
		label.origin[0] = origin[0];
		label.origin[1] = origin[1];
		label.origin[2] = origin[2];

		// Many entities (i.e. brush entities) have their origin at the world's origin or somewhere that is not
		// where they are, so use the top center of the world space bounds if they are usable.
		float mins[3] = { 0, 0, 0 };
		float maxs[3] = { 0, 0, 0 };
		renderable->GetRenderBoundsWorldspace(mins, maxs);

		bool usable = true;
		bool hasSize = false;

		for (int i = 0; i < 3; ++i)
		{
			if (!Mirv_IsUsableBound(mins[i]) || !Mirv_IsUsableBound(maxs[i]) || maxs[i] < mins[i] || 65536.0f < maxs[i] - mins[i])
				usable = false;
			else if (0.01f < maxs[i] - mins[i])
				hasSize = true;
		}

		if (usable && hasSize)
		{
			// Prefer the entity's own render origin for compact models (weapons,
			// wearables and other attached entities). Their world bounds can move
			// with the parent or be much larger than the model, causing labels to
			// jump to the player's bounds. Use bounds only when the render origin
			// is clearly outside them, which is common for brush/map entities.
			const bool originInsideBounds =
				mins[0] - 32.0f <= origin[0] && origin[0] <= maxs[0] + 32.0f &&
				mins[1] - 32.0f <= origin[1] && origin[1] <= maxs[1] + 32.0f &&
				mins[2] - 32.0f <= origin[2] && origin[2] <= maxs[2] + 32.0f;

			if (!originInsideBounds)
			{
				label.origin[0] = 0.5f * (mins[0] + maxs[0]);
				label.origin[1] = 0.5f * (mins[1] + maxs[1]);
				label.origin[2] = maxs[2];
			}
		}
	}

	return count;
}

IMirvVecAngCalc * CMirvVecAngCalcs::NewTf2EntityIndexCalc(char const * name, int entityIndex, int boneIndex, int mode)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngTf2EntityIndexCalc(name, entityIndex, boneIndex, mode);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

#ifndef _WIN64
IMirvVecAngCalc * CMirvVecAngCalcs::NewHandleAttachmentCalc(char const * name, IMirvHandleCalc * handle, char const * attachmentName)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngHandleAttachmentCalc(name, handle, attachmentName);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

IMirvVecAngCalc * CMirvVecAngCalcs::NewIfCalc(char const * name, IMirvBoolCalc * condition, IMirvVecAngCalc * condTrue, IMirvVecAngCalc * condFalse)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngIfCalc(name, condition, condTrue, condFalse);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

IMirvVecAngCalc * CMirvVecAngCalcs::NewOrCalc(char const * name, IMirvVecAngCalc * a, IMirvVecAngCalc * b)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngOrCalc(name, a, b);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

IMirvVecAngCalc * CMirvVecAngCalcs::NewCamCalc(char const * name, IMirvCamCalc * src)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngCamCalc(name, src);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

IMirvVecAngCalc * CMirvVecAngCalcs::NewMotionProfile2Calc(char const * name, IMirvVecAngCalc * parent, IMirvHandleCalc * trackHandle)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngMotionProfile2Calc(name, parent, trackHandle);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

IMirvVecAngCalc * CMirvVecAngCalcs::NewSwitchInterpCalc(char const * name, IMirvVecAngCalc * source, IMirvHandleCalc * switchHandle, IMirvHandleCalc * resetHandle, float holdTime, float interpTime)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngSwitchInterpCalc(name, source, switchHandle, resetHandle, holdTime, interpTime);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

#ifndef _WIN64
IMirvVecAngCalc * CMirvVecAngCalcs::NewLocalToGlobalCalc(char const * name, IMirvVecAngCalc * source, IMirvHandleCalc * handle)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngLocalToGlobalCalc(name, source, handle);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

#ifndef _WIN64
IMirvVecAngCalc * CMirvVecAngCalcs::NewGlobalToLocalCalc(char const * name, IMirvVecAngCalc * source, IMirvHandleCalc * handle)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngGlobalToLocalCalc(name, source, handle);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

#ifndef _WIN64
IMirvVecAngCalc * CMirvVecAngCalcs::NewCollisionClipCalc(char const * name, IMirvVecAngCalc * source, IMirvVecAngCalc * clipTo, IMirvHandleCalc * ignoreHandle, float safeZoneDist)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvVecAngCalc * result = new CMirvVecAngCollisionClipCalc(name, source, clipTo, ignoreHandle, safeZoneDist);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64


void CMirvVecAngCalcs::Console_Remove(char const * name)
{
	std::list<IMirvVecAngCalc *>::iterator it;
	GetIteratorByName(name, it);
	if (it != m_Calcs.end())
	{
		if (1 == (*it)->GetRefCount())
		{
			(*it)->Release();
			m_Calcs.erase(it);
		}
		else
			Tier0_Warning("Error: Cannot remove %s: Still in use.\n", (*it)->GetName());
	}
	else
	{
		Tier0_Warning("Error: No Calc named \"%s\" found.\n", name);
	}
}

bool CMirvVecAngCalcs::Console_CheckName(char const * name)
{
	if (!name)
	{
		Tier0_Warning("Error: Name cannot be null pointer.\n");
		return false;
	}

	if (!isalpha(*name))
	{
		Tier0_Warning("Error: Name has to begin with an alphabet letter.\n");
		return false;
	}

	if (!StringIsAlNum(name))
	{
		Tier0_Warning("Error: Name has to be alpha-numeric (letters and digits).\n");
		return false;
	}

	if (GetByName(name))
	{
		Tier0_Warning("Error: Name is already in use.\n");
		return false;
	}

	return true;
}

void CMirvVecAngCalcs::Console_Print(void)
{
	for (std::list<IMirvVecAngCalc *>::iterator it = m_Calcs.begin(); it != m_Calcs.end(); ++it)
	{
		(*it)->Console_PrintBegin(); (*it)->Console_PrintEnd(); Tier0_Msg(";\n");
	}
}

void CMirvVecAngCalcs::GetIteratorByName(char const * name, std::list<IMirvVecAngCalc *>::iterator & outIt)
{
	for (outIt = m_Calcs.begin(); outIt != m_Calcs.end(); ++outIt)
	{
		if (0 == _stricmp(name, (*outIt)->GetName()))
			break;
	}
}



CMirvCamCalcs::~CMirvCamCalcs()
{
	for (std::list<IMirvCamCalc *>::iterator it = m_Calcs.begin(); it != m_Calcs.end(); ++it)
	{
		(*it)->Release();
	}
}

IMirvCamCalc * CMirvCamCalcs::GetByName(char const * name)
{
	std::list<IMirvCamCalc *>::iterator it;
	GetIteratorByName(name, it);
	if (it != m_Calcs.end())
	{
		return *it;
	}

	return 0;
}

IMirvCamCalc * CMirvCamCalcs::NewCamCalc(char const * name, const char * camFileName, const char * startClientTime)
{
	if (name && !Console_CheckName(name))
		return 0;

	CMirvCamCamCalc * result = new CMirvCamCamCalc(name, camFileName, startClientTime);

	if(result->IsBad()) Tier0_Warning("Error importing CAM file \"%s\"\n", name);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

IMirvCamCalc * CMirvCamCalcs::NewGameCalc(char const * name)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvCamCalc * result = new CMirvCamGameCalc(name);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}


IMirvCamCalc * CMirvCamCalcs::NewCurrentCalc(char const * name)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvCamCalc * result = new CMirvCamCurrentCalc(name);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

#ifndef _WIN64
IMirvCamCalc* CMirvCamCalcs::NewPlayerCalc(char const* name, IMirvHandleCalc* handleCalc)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvCamCalc* result = new CMirvCamPlayerCalc(name, handleCalc);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

IMirvCamCalc * CMirvCamCalcs::NewSmoothCalc(char const * name, IMirvCamCalc * parent, IMirvHandleCalc * trackHandle)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvCamCalc * result = new CMirvCamSmoothCalc(name, parent, trackHandle);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

IMirvCamCalc * CMirvCamCalcs::NewVecAngFovCalc(char const * name, IMirvVecAngCalc * vecAng, IMirvFovCalc * fov)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvCamCalc * result = new CMirvCamVecAngFovCalc(name, vecAng, fov);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}


void CMirvCamCalcs::Console_Remove(char const * name)
{
	std::list<IMirvCamCalc *>::iterator it;
	GetIteratorByName(name, it);
	if (it != m_Calcs.end())
	{
		if (1 == (*it)->GetRefCount())
		{
			(*it)->Release();
			m_Calcs.erase(it);
		}
		else
			Tier0_Warning("Error: Cannot remove %s: Still in use.\n", (*it)->GetName());
	}
	else
	{
		Tier0_Warning("Error: No Calc named \"%s\" found.\n", name);
	}
}

bool CMirvCamCalcs::Console_CheckName(char const * name)
{
	if (!name)
	{
		Tier0_Warning("Error: Name cannot be null pointer.\n");
		return false;
	}

	if (!isalpha(*name))
	{
		Tier0_Warning("Error: Name has to begin with an alphabet letter.\n");
		return false;
	}

	if (!StringIsAlNum(name))
	{
		Tier0_Warning("Error: Name has to be alpha-numeric (letters and digits).\n");
		return false;
	}

	if (GetByName(name))
	{
		Tier0_Warning("Error: Name is already in use.\n");
		return false;
	}

	return true;
}

void CMirvCamCalcs::Console_Print(void)
{
	for (std::list<IMirvCamCalc *>::iterator it = m_Calcs.begin(); it != m_Calcs.end(); ++it)
	{
		(*it)->Console_PrintBegin(); (*it)->Console_PrintEnd(); Tier0_Msg(";\n");
	}
}

void CMirvCamCalcs::GetIteratorByName(char const * name, std::list<IMirvCamCalc *>::iterator & outIt)
{
	for (outIt = m_Calcs.begin(); outIt != m_Calcs.end(); ++outIt)
	{
		if (0 == _stricmp(name, (*outIt)->GetName()))
			break;
	}
}


CMirvFovCalcs::~CMirvFovCalcs()
{
	for (std::list<IMirvFovCalc *>::iterator it = m_Calcs.begin(); it != m_Calcs.end(); ++it)
	{
		(*it)->Release();
	}
}

IMirvFovCalc * CMirvFovCalcs::GetByName(char const * name)
{
	std::list<IMirvFovCalc *>::iterator it;
	GetIteratorByName(name, it);
	if (it != m_Calcs.end())
	{
		return *it;
	}

	return 0;
}

IMirvFovCalc * CMirvFovCalcs::NewCamCalc(char const * name, IMirvCamCalc * src)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvFovCalc * result = new CMirvFovCamCalc(name, src);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

void CMirvFovCalcs::Console_Remove(char const * name)
{
	std::list<IMirvFovCalc *>::iterator it;
	GetIteratorByName(name, it);
	if (it != m_Calcs.end())
	{
		if (1 == (*it)->GetRefCount())
		{
			(*it)->Release();
			m_Calcs.erase(it);
		}
		else
			Tier0_Warning("Error: Cannot remove %s: Still in use.\n", (*it)->GetName());
	}
	else
	{
		Tier0_Warning("Error: No Calc named \"%s\" found.\n", name);
	}
}

bool CMirvFovCalcs::Console_CheckName(char const * name)
{
	if (!name)
	{
		Tier0_Warning("Error: Name cannot be null pointer.\n");
		return false;
	}

	if (!isalpha(*name))
	{
		Tier0_Warning("Error: Name has to begin with an alphabet letter.\n");
		return false;
	}

	if (!StringIsAlNum(name))
	{
		Tier0_Warning("Error: Name has to be alpha-numeric (letters and digits).\n");
		return false;
	}

	if (GetByName(name))
	{
		Tier0_Warning("Error: Name is already in use.\n");
		return false;
	}

	return true;
}

void CMirvFovCalcs::Console_Print(void)
{
	for (std::list<IMirvFovCalc *>::iterator it = m_Calcs.begin(); it != m_Calcs.end(); ++it)
	{
		(*it)->Console_PrintBegin(); (*it)->Console_PrintEnd(); Tier0_Msg(";\n");
	}
}

void CMirvFovCalcs::GetIteratorByName(char const * name, std::list<IMirvFovCalc *>::iterator & outIt)
{
	for (outIt = m_Calcs.begin(); outIt != m_Calcs.end(); ++outIt)
	{
		if (0 == _stricmp(name, (*outIt)->GetName()))
			break;
	}
}


CMirvBoolCalcs::~CMirvBoolCalcs()
{
	for (std::list<IMirvBoolCalc *>::iterator it = m_Calcs.begin(); it != m_Calcs.end(); ++it)
	{
		(*it)->Release();
	}
}

IMirvBoolCalc * CMirvBoolCalcs::GetByName(char const * name)
{
	std::list<IMirvBoolCalc *>::iterator it;
	GetIteratorByName(name, it);
	if (it != m_Calcs.end())
	{
		return *it;
	}

	return 0;
}

IMirvBoolCalc * CMirvBoolCalcs::NewHandleCalc(char const * name, IMirvHandleCalc * handle)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvBoolCalc * result = new CMirvBoolHandleCalc(name, handle);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

IMirvBoolCalc * CMirvBoolCalcs::NewVecAngCalc(char const * name, IMirvVecAngCalc * vecAng)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvBoolCalc * result = new CMirvBoolVecAngCalc(name, vecAng);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

#ifndef _WIN64
IMirvBoolCalc * CMirvBoolCalcs::NewAliveCalc(char const * name, IMirvHandleCalc * handle)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvBoolCalc * result = new CMirvBoolAliveCalc(name, handle);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

IMirvBoolCalc * CMirvBoolCalcs::NewAndCalc(char const * name, IMirvBoolCalc * a, IMirvBoolCalc * b)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvBoolCalc * result = new CMirvBoolAndCalc(name, a, b);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

IMirvBoolCalc * CMirvBoolCalcs::NewOrCalc(char const * name, IMirvBoolCalc * a, IMirvBoolCalc * b)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvBoolCalc * result = new CMirvBoolOrCalc(name, a, b);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

IMirvBoolCalc * CMirvBoolCalcs::NewNotCalc(char const * name, IMirvBoolCalc * a)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvBoolCalc * result = new CMirvBoolNotCalc(name, a);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

IMirvBoolCalc * CMirvBoolCalcs::NewHandlesEqualCalc(char const * name, IMirvHandleCalc * a, IMirvHandleCalc * b)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvBoolCalc * result = new CMirvBoolHandlesEqualCalc(name, a, b);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

IMirvBoolCalc * CMirvBoolCalcs::NewIntsEqualCalc(char const * name, IMirvIntCalc * a, IMirvIntCalc * b)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvBoolCalc * result = new CMirvBoolIntsEqualCalc(name, a, b);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

#ifndef _WIN64
IMirvBoolCalc * CMirvBoolCalcs::NewClassnameMatchesCalc(char const * name, IMirvHandleCalc * handle, const char * wildCardString)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvBoolCalc * result = new CMirvBoolClassNameMatchesCalc(name, handle, wildCardString);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

void CMirvBoolCalcs::Console_Remove(char const * name)
{
	std::list<IMirvBoolCalc *>::iterator it;
	GetIteratorByName(name, it);
	if (it != m_Calcs.end())
	{
		if (1 == (*it)->GetRefCount())
		{
			(*it)->Release();
			m_Calcs.erase(it);
		}
		else
			Tier0_Warning("Error: Cannot remove %s: Still in use.\n", (*it)->GetName());
	}
	else
	{
		Tier0_Warning("Error: No Calc named \"%s\" found.\n", name);
	}
}

bool CMirvBoolCalcs::Console_CheckName(char const * name)
{
	if (!name)
	{
		Tier0_Warning("Error: Name cannot be null pointer.\n");
		return false;
	}

	if (!isalpha(*name))
	{
		Tier0_Warning("Error: Name has to begin with an alphabet letter.\n");
		return false;
	}

	if (!StringIsAlNum(name))
	{
		Tier0_Warning("Error: Name has to be alpha-numeric (letters and digits).\n");
		return false;
	}

	if (GetByName(name))
	{
		Tier0_Warning("Error: Name is already in use.\n");
		return false;
	}

	return true;
}

void CMirvBoolCalcs::Console_Print(void)
{
	for (std::list<IMirvBoolCalc *>::iterator it = m_Calcs.begin(); it != m_Calcs.end(); ++it)
	{
		(*it)->Console_PrintBegin(); (*it)->Console_PrintEnd(); Tier0_Msg(";\n");
	}
}

void CMirvBoolCalcs::GetIteratorByName(char const * name, std::list<IMirvBoolCalc *>::iterator & outIt)
{
	for (outIt = m_Calcs.begin(); outIt != m_Calcs.end(); ++outIt)
	{
		if (0 == _stricmp(name, (*outIt)->GetName()))
			break;
	}
}


CMirvIntCalcs::~CMirvIntCalcs()
{
	for (std::list<IMirvIntCalc *>::iterator it = m_Calcs.begin(); it != m_Calcs.end(); ++it)
	{
		(*it)->Release();
	}
}

IMirvIntCalc * CMirvIntCalcs::GetByName(char const * name)
{
	std::list<IMirvIntCalc *>::iterator it;
	GetIteratorByName(name, it);
	if (it != m_Calcs.end())
	{
		return *it;
	}

	return 0;
}


IMirvIntCalc * CMirvIntCalcs::NewValueCalc(char const * name, int value)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvIntCalc * result = new CMirvIntValueCalc(name, value);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}

#ifndef _WIN64
IMirvIntCalc * CMirvIntCalcs::NewTeamNumberCalc(char const * name, IMirvHandleCalc * handle)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvIntCalc * result = new CMirvIntTeamNumberCalc(name, handle);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}
#endif //#ifndef _WIN64

void CMirvIntCalcs::Console_Remove(char const * name)
{
	std::list<IMirvIntCalc *>::iterator it;
	GetIteratorByName(name, it);
	if (it != m_Calcs.end())
	{
		if (1 == (*it)->GetRefCount())
		{
			(*it)->Release();
			m_Calcs.erase(it);
		}
		else
			Tier0_Warning("Error: Cannot remove %s: Still in use.\n", (*it)->GetName());
	}
	else
	{
		Tier0_Warning("Error: No Calc named \"%s\" found.\n", name);
	}
}

bool CMirvIntCalcs::Console_CheckName(char const * name)
{
	if (!name)
	{
		Tier0_Warning("Error: Name cannot be null pointer.\n");
		return false;
	}

	if (!isalpha(*name))
	{
		Tier0_Warning("Error: Name has to begin with an alphabet letter.\n");
		return false;
	}

	if (!StringIsAlNum(name))
	{
		Tier0_Warning("Error: Name has to be alpha-numeric (letters and digits).\n");
		return false;
	}

	if (GetByName(name))
	{
		Tier0_Warning("Error: Name is already in use.\n");
		return false;
	}

	return true;
}

void CMirvIntCalcs::Console_Print(void)
{
	for (std::list<IMirvIntCalc *>::iterator it = m_Calcs.begin(); it != m_Calcs.end(); ++it)
	{
		(*it)->Console_PrintBegin(); (*it)->Console_PrintEnd(); Tier0_Msg(";\n");
	}
}

void CMirvIntCalcs::GetIteratorByName(char const * name, std::list<IMirvIntCalc *>::iterator & outIt)
{
	for (outIt = m_Calcs.begin(); outIt != m_Calcs.end(); ++outIt)
	{
		if (0 == _stricmp(name, (*outIt)->GetName()))
			break;
	}
}



CMirvFloatCalcs::~CMirvFloatCalcs()
{
	for (std::list<IMirvFloatCalc*>::iterator it = m_Calcs.begin(); it != m_Calcs.end(); ++it)
	{
		(*it)->Release();
	}
}

IMirvFloatCalc* CMirvFloatCalcs::GetByName(char const* name)
{
	std::list<IMirvFloatCalc*>::iterator it;
	GetIteratorByName(name, it);
	if (it != m_Calcs.end())
	{
		return *it;
	}

	return 0;
}


IMirvFloatCalc* CMirvFloatCalcs::NewValueCalc(char const* name, float value)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvFloatCalc* result = new CMirvFloatValueCalc(name, value);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}


IMirvFloatCalc* CMirvFloatCalcs::NewIfCalc(char const* name, IMirvBoolCalc* condition, IMirvFloatCalc* condTrue, IMirvFloatCalc* condFalse)
{
	if (name && !Console_CheckName(name))
		return 0;

	IMirvFloatCalc* result = new CMirvFloatIfCalc(name, condition, condTrue, condFalse);

	if (name)
	{
		result->AddRef();

		m_Calcs.push_back(result);
	}

	return result;
}


void CMirvFloatCalcs::Console_Remove(char const* name)
{
	std::list<IMirvFloatCalc*>::iterator it;
	GetIteratorByName(name, it);
	if (it != m_Calcs.end())
	{
		if (1 == (*it)->GetRefCount())
		{
			(*it)->Release();
			m_Calcs.erase(it);
		}
		else
			Tier0_Warning("Error: Cannot remove %s: Still in use.\n", (*it)->GetName());
	}
	else
	{
		Tier0_Warning("Error: No Calc named \"%s\" found.\n", name);
	}
}

bool CMirvFloatCalcs::Console_CheckName(char const* name)
{
	if (!name)
	{
		Tier0_Warning("Error: Name cannot be null pointer.\n");
		return false;
	}

	if (!isalpha(*name))
	{
		Tier0_Warning("Error: Name has to begin with an alphabet letter.\n");
		return false;
	}

	if (!StringIsAlNum(name))
	{
		Tier0_Warning("Error: Name has to be alpha-numeric (letters and digits).\n");
		return false;
	}

	if (GetByName(name))
	{
		Tier0_Warning("Error: Name is already in use.\n");
		return false;
	}

	return true;
}

void CMirvFloatCalcs::Console_Print(void)
{
	for (std::list<IMirvFloatCalc*>::iterator it = m_Calcs.begin(); it != m_Calcs.end(); ++it)
	{
		(*it)->Console_PrintBegin(); (*it)->Console_PrintEnd(); Tier0_Msg(";\n");
	}
}

void CMirvFloatCalcs::GetIteratorByName(char const* name, std::list<IMirvFloatCalc*>::iterator& outIt)
{
	for (outIt = m_Calcs.begin(); outIt != m_Calcs.end(); ++outIt)
	{
		if (0 == _stricmp(name, (*outIt)->GetName()))
			break;
	}
}


void mirv_calcs_handle(IWrpCommandArgs * args)
{
	int argc = args->ArgC();

	char const * arg0 = args->ArgV(0);

	if (2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if (0 == _stricmp("add", arg1))
		{
			if (3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if (0 == _stricmp("value", arg2) && 5 <= argc)
				{
					g_MirvHandleCalcs.NewValueCalc(args->ArgV(3), atoi(args->ArgV(4)));
					return;
				}
#ifndef _WIN64
				else if (0 == _stricmp("index", arg2) && 5 <= argc)
				{
					g_MirvHandleCalcs.NewIndexCalc(args->ArgV(3), atoi(args->ArgV(4)));
					return;
				}
#endif //#ifndef _WIN64
#ifndef _WIN64
				else if (0 == _stricmp("key", arg2) && 5 <= argc)
				{
					g_MirvHandleCalcs.NewKeyCalc(args->ArgV(3), atoi(args->ArgV(4)));
					return;
				}
#endif //#ifndef _WIN64
#ifndef _WIN64
				else if (0 == _stricmp("activeWeapon", arg2) && 6 <= argc)
				{
					char const * parentCalcName = args->ArgV(4);

					IMirvHandleCalc * parentCalc = g_MirvHandleCalcs.GetByName(parentCalcName);

					if (parentCalc)
						g_MirvHandleCalcs.NewActiveWeaponCalc(args->ArgV(3), parentCalc, 0 != atoi(args->ArgV(5)));
					else
						Tier0_Warning("Error: No handle calc with name \"%s\" found.\n", parentCalcName);

					return;
				}
#endif //#ifndef _WIN64
#ifndef _WIN64
				else if (0 == _stricmp("localPlayer", arg2) && 4 <= argc)
				{
					g_MirvHandleCalcs.NewLocalPlayerCalc(args->ArgV(3));

					return;
				}
#endif //#ifndef _WIN64
#ifndef _WIN64
				else if (0 == _stricmp("observerTarget", arg2) && 5 <= argc)
				{
					char const * parentCalcName = args->ArgV(4);

					IMirvHandleCalc * parentCalc = g_MirvHandleCalcs.GetByName(parentCalcName);

					if (parentCalc)
						g_MirvHandleCalcs.NewObserverTargetCalc(args->ArgV(3), parentCalc);
					else
						Tier0_Warning("Error: No handle calc with name \"%s\" found.\n", parentCalcName);

					return;
				}
#endif //#ifndef _WIN64
				else if (0 == _stricmp("or", arg2) && 6 <= argc)
				{
					char const * calcAName = args->ArgV(4);
					IMirvHandleCalc * calcA = g_MirvHandleCalcs.GetByName(calcAName);

					if (calcA)
					{
						char const * calcBName = args->ArgV(5);
						IMirvHandleCalc * calcB = g_MirvHandleCalcs.GetByName(calcBName);

						if (calcB)
						{
							g_MirvHandleCalcs.NewOrCalc(args->ArgV(3), calcA, calcB);
						}
						else
							Tier0_Warning("Error: No handle calcB with name \"%s\" found.\n", calcBName);
					}
					else
						Tier0_Warning("Error: No handle calcA with name \"%s\" found.\n", calcAName);

					return;
				}
#ifndef _WIN64
				else if (0 == _stricmp("ownerEntity", arg2) && 6 <= argc)
				{
					char const * parentCalcName = args->ArgV(4);
					unsigned int maxDepth = strtoul(args->ArgV(5), nullptr, 10);

					IMirvHandleCalc * parentCalc = g_MirvHandleCalcs.GetByName(parentCalcName);

					if (parentCalc)
						g_MirvHandleCalcs.NewOwnerEntityCalc(args->ArgV(3), parentCalc, maxDepth);
					else
						Tier0_Warning("Error: No handle calc with name \"%s\" found.\n", parentCalcName);

					return;
				}
#endif //#ifndef _WIN64
			}

			Tier0_Msg(
				"%s add value <sName> <iHandle> - Add a new calc with a constant value.\n"
				"%s add index <sName> <iIndex> - Add a new index calc.\n"
				"%s add key <sName> <iKeyNumber> - Add a new key calc (like spectator HUD).\n"
				"%s add activeWeapon <sName> <sParentCalcHandleName> <bGetWorld> - Add an active weapon calc, <bGetWorld> is 0 or 1.\n"
				"%s add localPlayer <sName> - Add localPlayer calc.\n"
				"%s add observerTarget <sName> <sParentCalcHandleName> - Add observer target calc (use e.g. localPlayer calc as parent name).\n"
				"%s add or <sName> <sAName> <sBName> - Add an OR calc.\n"
				"%s add ownerEntity <sName> <sCalcHandleName> <iMaxDepth>\n"
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
			);
			return;
		}
		else if (0 == _stricmp("remove", arg1) && 3 <= argc)
		{
			g_MirvHandleCalcs.Console_Remove(args->ArgV(2));
			return;
		}
		else if (0 == _stricmp("print", arg1))
		{
			g_MirvHandleCalcs.Console_Print();
			return;
		}
		else if (0 == _stricmp("test", arg1) && 3 <= argc)
		{
			char const * parentCalcName = args->ArgV(2);

			IMirvHandleCalc * parentCalc = g_MirvHandleCalcs.GetByName(parentCalcName);

			if (parentCalc)
			{
				SOURCESDK::CSGO::CBaseHandle handle;
				bool calced = parentCalc->CalcHandle(handle);

				if(calced)
					Tier0_Msg("\nResult: true, handle=%i\n", handle);
				else
					Tier0_Msg("\nResult: false\n");
			}
			else
				Tier0_Warning("Error: No calc with name \"%s\" found.\n", parentCalcName);

			return;
		}
		else if (0 == _stricmp("edit", arg1) && 3 <= argc)
		{
			char const * parentCalcName = args->ArgV(2);

			IMirvHandleCalc * parentCalc = g_MirvHandleCalcs.GetByName(parentCalcName);

			if (parentCalc)
			{
				CSubWrpCommandArgs sub(args, 3);
				parentCalc->Console_Edit(&sub);
				return;
			}
			else
				Tier0_Warning("Error: No calc with name \"%s\" found.\n", parentCalcName);

			return;
		}
	}

	Tier0_Msg(
		"%s add [...] - Add a new handle calc.\n"
		"%s remove <sCalcName> - Remove calc with name <sCalcName>.\n"
		"%s print - Print calcs.\n"
		"%s test <sCalcName> - Test a calc.\n"
		"%s edit <sCalcName> [...] - Edit a calc.\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
}

void mirv_calcs_vecang(IWrpCommandArgs * args)
{
	int argc = args->ArgC();

	char const * arg0 = args->ArgV(0);

	if (2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if (0 == _stricmp("add", arg1))
		{
			if (3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if (0 == _stricmp("value", arg2) && 10 <= argc)
				{
					g_MirvVecAngCalcs.NewValueCalc(args->ArgV(3), (float)atof(args->ArgV(4)), (float)atof(args->ArgV(5)), (float)atof(args->ArgV(6)), (float)atof(args->ArgV(7)), (float)atof(args->ArgV(8)), (float)atof(args->ArgV(9)));
					return;
				}
				else if (0 == _stricmp("add", arg2) && 6 <= argc)
				{
					char const * calcAName = args->ArgV(4);
					IMirvVecAngCalc * calcA = g_MirvVecAngCalcs.GetByName(calcAName);

					if (calcA)
					{
						char const * calcBName = args->ArgV(5);
						IMirvVecAngCalc * calcB = g_MirvVecAngCalcs.GetByName(calcBName);

						if (calcB)
						{
							g_MirvVecAngCalcs.NewAddCalc(args->ArgV(3), calcA, calcB);
						}
						else
							Tier0_Warning("Error: No vecAng A with name \"%s\" found.\n", calcBName);
					}
					else
						Tier0_Warning("Error: No vecAng B with name \"%s\" found.\n", calcAName);

					return;
				}
				else if (0 == _stricmp("subtract", arg2) && 6 <= argc)
				{
					char const * calcAName = args->ArgV(4);
					IMirvVecAngCalc * calcA = g_MirvVecAngCalcs.GetByName(calcAName);

					if (calcA)
					{
						char const * calcBName = args->ArgV(5);
						IMirvVecAngCalc * calcB = g_MirvVecAngCalcs.GetByName(calcBName);

						if (calcB)
						{
							g_MirvVecAngCalcs.NewSubtractCalc(args->ArgV(3), calcA, calcB);
						}
						else
							Tier0_Warning("Error: No vecAng A with name \"%s\" found.\n", calcBName);
					}
					else
						Tier0_Warning("Error: No vecAng B with name \"%s\" found.\n", calcAName);

					return;
				}
				else if (0 == _stricmp("offset", arg2) && 7 <= argc)
				{
					char const * calcAName = args->ArgV(4);
					IMirvVecAngCalc * calcA = g_MirvVecAngCalcs.GetByName(calcAName);

					if (calcA)
					{
						char const * calcBName = args->ArgV(5);
						IMirvVecAngCalc * calcB = g_MirvVecAngCalcs.GetByName(calcBName);

						if (calcB)
						{
							g_MirvVecAngCalcs.NewOffsetCalc(args->ArgV(3), calcA, calcB, 0 != atoi(args->ArgV(6)));
						}
						else
							Tier0_Warning("Error: No vecAng parent with name \"%s\" found.\n", calcBName);
					}
					else
						Tier0_Warning("Error: No vecAng offset with name \"%s\" found.\n", calcAName);

					return;
				}
#ifndef _WIN64
				else if (0 == _stricmp("handle", arg2) && 5 <= argc)
				{
					char const * parentCalcName = args->ArgV(4);

					IMirvHandleCalc * parentCalc = g_MirvHandleCalcs.GetByName(parentCalcName);

					if (parentCalc)
						g_MirvVecAngCalcs.NewHandleCalc(args->ArgV(3), parentCalc);
					else
						Tier0_Warning("Error: No handle calc with name \"%s\" found.\n", parentCalcName);

					return;
				}
#endif //#ifndef _WIN64
#ifndef _WIN64
				else if (0 == _stricmp("handleEye", arg2) && 5 <= argc)
				{
					char const * parentCalcName = args->ArgV(4);

					IMirvHandleCalc * parentCalc = g_MirvHandleCalcs.GetByName(parentCalcName);

					if (parentCalc)
						g_MirvVecAngCalcs.NewHandleEyeCalc(args->ArgV(3), parentCalc);
					else
						Tier0_Warning("Error: No handle calc with name \"%s\" found.\n", parentCalcName);

					return;
				}
#endif //#ifndef _WIN64
#ifndef _WIN64
				else if (0 == _stricmp("handleAttachment", arg2) && 6 <= argc)
				{
					char const * parentCalcName = args->ArgV(4);

					IMirvHandleCalc * parentCalc = g_MirvHandleCalcs.GetByName(parentCalcName);

					if (parentCalc)
						g_MirvVecAngCalcs.NewHandleAttachmentCalc(args->ArgV(3), parentCalc, args->ArgV(5));
					else
						Tier0_Warning("Error: No handle calc with name \"%s\" found.\n", parentCalcName);

					return;
				}
#endif //#ifndef _WIN64
				else if (0 == _stricmp("tf2Entity", arg2) && 5 <= argc)
				{
					int boneIndex = -1;
					int mode = MIRV_TF2_BONEMODE_BONE;

					if (6 <= argc && !Mirv_Tf2_ParseBoneArg(atoi(args->ArgV(4)), args->ArgV(5), boneIndex))
						return;

					if (7 <= argc)
					{
						char const * modeName = args->ArgV(6);

						if (0 == _stricmp("bone", modeName)) mode = MIRV_TF2_BONEMODE_BONE;
						else if (0 == _stricmp("entity", modeName)) mode = MIRV_TF2_BONEMODE_ENTITY;
						else if (0 == _stricmp("aligned", modeName)) mode = MIRV_TF2_BONEMODE_ALIGNED;
						else if (0 == _stricmp("world", modeName)) mode = MIRV_TF2_BONEMODE_WORLD;
						else
						{
							Tier0_Warning("Error: Unknown mode \"%s\", use bone, entity, aligned or world.\n", modeName);
							return;
						}
					}

					g_MirvVecAngCalcs.NewTf2EntityIndexCalc(args->ArgV(3), atoi(args->ArgV(4)), boneIndex, mode);

					return;
				}
				else if (0 == _stricmp("or", arg2) && 6 <= argc)
				{
					char const * calcAName = args->ArgV(4);
					IMirvVecAngCalc * calcA = g_MirvVecAngCalcs.GetByName(calcAName);

					if (calcA)
					{
						char const * calcBName = args->ArgV(5);
						IMirvVecAngCalc * calcB = g_MirvVecAngCalcs.GetByName(calcBName);

						if (calcB)
						{
							g_MirvVecAngCalcs.NewOrCalc(args->ArgV(3), calcA, calcB);
						}
						else
							Tier0_Warning("Error: No vecAng calcB with name \"%s\" found.\n", calcBName);
					}
					else
						Tier0_Warning("Error: No vecAng calcA with name \"%s\" found.\n", calcAName);

					return;
				}
				else if (0 == _stricmp("cam", arg2) && 5 <= argc)
				{
					char const * parentCalcName = args->ArgV(4);

					IMirvCamCalc * parentCalc = g_MirvCamCalcs.GetByName(parentCalcName);

					if (parentCalc)
						g_MirvVecAngCalcs.NewCamCalc(args->ArgV(3), parentCalc);
					else
						Tier0_Warning("Error: No cam calc with name \"%s\" found.\n", parentCalcName);

					return;
				}
				else if (0 == _stricmp("motionProfile2", arg2) && 6 <= argc)
				{
					char const * calcAName = args->ArgV(4);
					IMirvVecAngCalc * calcA = g_MirvVecAngCalcs.GetByName(calcAName);

					if (calcA)
					{
						char const * calcBName = args->ArgV(5);
						IMirvHandleCalc * calcB = g_MirvHandleCalcs.GetByName(calcBName);

						if (calcB)
						{
							g_MirvVecAngCalcs.NewMotionProfile2Calc(args->ArgV(3), calcA, calcB);
						}
						else
							Tier0_Warning("Error: No handle calc with name \"%s\" found.\n", calcBName);
					}
					else
						Tier0_Warning("Error: No vecAng parent with name \"%s\" found.\n", calcAName);

					return;
				}
				else if (0 == _stricmp("switchInterp", arg2) && 9 <= argc)
				{
					char const * calcAName = args->ArgV(4);
					
					if (IMirvVecAngCalc * calcA = g_MirvVecAngCalcs.GetByName(calcAName))
					{
						char const * calcBName = args->ArgV(5);
						
						if (IMirvHandleCalc * calcB = g_MirvHandleCalcs.GetByName(calcBName))
						{
							char const * calcCName = args->ArgV(6);

							if (IMirvHandleCalc * calcC = g_MirvHandleCalcs.GetByName(calcCName))
							{
								g_MirvVecAngCalcs.NewSwitchInterpCalc(args->ArgV(3), calcA, calcB, calcC, (float)atof(args->ArgV(7)), (float)atof(args->ArgV(8)));
							}
							else
								Tier0_Warning("Error: No handle resetHandle calc with name \"%s\" found.\n", calcCName);
						}
						else
							Tier0_Warning("Error: No handle switchHandle calc with name \"%s\" found.\n", calcBName);
					}
					else
						Tier0_Warning("Error: No vecAng source calc with name \"%s\" found.\n", calcAName);
						

					return;
				}
#ifndef _WIN64
				else if (0 == _stricmp("localToGlobal", arg2) &&6 <= argc)
				{
					char const * calcAName = args->ArgV(4);
					IMirvVecAngCalc * calcA = g_MirvVecAngCalcs.GetByName(calcAName);

					if (calcA)
					{
						char const * calcBName = args->ArgV(5);
						IMirvHandleCalc * calcB = g_MirvHandleCalcs.GetByName(calcBName);

						if (calcB)
						{
							g_MirvVecAngCalcs.NewLocalToGlobalCalc(args->ArgV(3), calcA, calcB);
						}
						else
							Tier0_Warning("Error: No handle handle calc with name \"%s\" found.\n", calcBName);							
					}
					else
						Tier0_Warning("Error: No vecAng source calc with name \"%s\" found.\n", calcAName);

					return;
				}
#endif //#ifndef _WIN64
#ifndef _WIN64
				else if (0 == _stricmp("globalToLocal", arg2) && 6 <= argc)
				{
					char const * calcAName = args->ArgV(4);
					IMirvVecAngCalc * calcA = g_MirvVecAngCalcs.GetByName(calcAName);

					if (calcA)
					{
						char const * calcBName = args->ArgV(5);
						IMirvHandleCalc * calcB = g_MirvHandleCalcs.GetByName(calcBName);

						if (calcB)
						{
							g_MirvVecAngCalcs.NewGlobalToLocalCalc(args->ArgV(3), calcA, calcB);
						}
						else
							Tier0_Warning("Error: No handle handle calc with name \"%s\" found.\n", calcBName);
					}
					else
						Tier0_Warning("Error: No vecAng source calc with name \"%s\" found.\n", calcAName);
					
					return;
				}
#endif //#ifndef _WIN64
#ifndef _WIN64
				else if (0 == _stricmp("collisionClip", arg2) && 8 <= argc)
				{
					char const * calcSourceName = args->ArgV(4);
					IMirvVecAngCalc * calcSource = g_MirvVecAngCalcs.GetByName(calcSourceName);

					if (calcSource)
					{
						char const * calcClipToName = args->ArgV(5);
						IMirvVecAngCalc * calcClipTo = g_MirvVecAngCalcs.GetByName(calcClipToName);

						if (calcClipTo)
						{
							char const * calcIngoreName = args->ArgV(6); 
							IMirvHandleCalc * calcIgnore = g_MirvHandleCalcs.GetByName(calcIngoreName);

							if(calcIgnore) {
								g_MirvVecAngCalcs.NewCollisionClipCalc(args->ArgV(3), calcSource, calcClipTo, calcIgnore, (float)atof(args->ArgV(7)));
							}
							else Tier0_Warning("Error: No handle ugnore with name \"%s\" found.\n", calcIngoreName);
						}
						else
							Tier0_Warning("Error: No vecAng clipTo with name \"%s\" found.\n", calcClipToName);
					}
					else
						Tier0_Warning("Error: No vecAng source with name \"%s\" found.\n", calcSourceName);

					return;
				}
#endif //#ifndef _WIN64
			}

			Tier0_Msg(
				"%s add value <sName> <fX> <fY> <fZ> <rX> <rY> <rZ> - Add a new calc with a constant value.\n"
				"%s add offset <sName> <sParentName> <sOffSetName> <bOrder> - Add a new offset calc, <bOrder>: 0 rotate first then offset, 1: offset first then rotate.\n"
				"%s add add <sName> <sAName> <sBName> - A + B.\n"
				"%s add subtract <sName> <sAName> <sBName> - A - B.\n"
				"%s add handle <sName> <sHandleCalcName> - Add a calc that gets its values from an entity using a handle calc named <sHandleCalcName>.\n"
				"%s add handleEye <sName> <sHandleCalcName> - Add a calc that gets its values from an entity's eye point using a handle calc named <sHandleCalcName>.\n"
				"%s add handleAttachment <sName> <sHandleCalcName> <sAttachMentName> - Add a calc that gets its values from an entity's attachment.\n"
				"%s add or <sName> <sAName> <sBName> - Add an OR calc.\n"
				"%s add cam <sName> <sCamCalName> - Adds a calc that gets its values from a cam calc named <sCamCalName>.\n"
				"%s add motionProfile2 <sName> <sParentName> <sTrackHandleName> - Add a velocity motion profile calc, <sParentName> is the motion target, <sTrackHandleName> is used to detect target changes (reset).\n"
				"%s add switchInterp <sName> <sSourceVecAngName> <sSwitchHandleName> <sResetHandleName> <fHoldTime> <fInterpTime> - Add a calc that after <sSwitchHandleName> value changes, holds the last value <sSourceVecAngName> for <fHoldTime> seconds and then interpolates between last value and current value for <fInterpTime>, if <sResetHandleName> changes the change is instant (reset like initial).\n"
				"%s add localToGlobal <sName> <sSourceVecAngName> <sHandleName> - Add a calc that transforms from local <sSwitchHandleName> space <sSourceVecAngName> to global space.\n"
				"%s add globalToLocal <sName> <sSourceVecAngName> <sHandleName> - Add a calc that transforms to local <sSwitchHandleName> space <sSourceVecAngName> from global space.\n"
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
			);
			Tier0_Msg(
				"%s add collisionClip <sName> <sSourceVecAngName> <sClipToVecAngName> <sIgnoreHandleName> <fSafeZoneDist> - Checks for collisions between source and clipTo and if there are any moves source nearer to clip accordingly.\n"
				, arg0
			);
			Tier0_Msg(
				"%s add tf2Entity <sName> <iEntityIndex> [<iBoneIndex or sBoneName> [<sMode>]] - (TF2 only) Add a calc that gets its values from the entity with index <iEntityIndex> (see mirv_listentities), or from its bone (index or part of the name, see mirv_listbones) if given. <sMode> (for bones): bone (default) - angles of the bone, entity - angles of the entity, aligned - starts like the entity and then follows the bone's rotation; world - keeps the camera's current heading while following translation.\n"
				, arg0
			);
			return;
		}
		else if (0 == _stricmp("remove", arg1) && 3 <= argc)
		{
			g_MirvVecAngCalcs.Console_Remove(args->ArgV(2));
			return;
		}
		else if (0 == _stricmp("print", arg1))
		{
			g_MirvVecAngCalcs.Console_Print();
			return;
		}
		else if (0 == _stricmp("test", arg1) && 3 <= argc)
		{
			char const * parentCalcName = args->ArgV(2);

			IMirvVecAngCalc * parentCalc = g_MirvVecAngCalcs.GetByName(parentCalcName);

			if (parentCalc)
			{
				SOURCESDK::Vector vec;
				SOURCESDK::QAngle ang;
				bool calced = parentCalc->CalcVecAng(vec, ang);

				if(calced)
					Tier0_Msg("\nResult: true, vec=(%f, %f, %f), ang=(%f, %f, %f)\n", vec.x, vec.y, vec.z, ang.z, ang.x, ang.y);
				else
					Tier0_Msg("\nResult: false\n");
			}
			else
				Tier0_Warning("Error: No calc with name \"%s\" found.\n", parentCalcName);

			return;
		}
		else if (0 == _stricmp("edit", arg1) && 3 <= argc)
		{
			char const * parentCalcName = args->ArgV(2);

			IMirvVecAngCalc * parentCalc = g_MirvVecAngCalcs.GetByName(parentCalcName);

			if (parentCalc)
			{
				CSubWrpCommandArgs sub(args, 3);
				parentCalc->Console_Edit(&sub);
				return;
			}
			else
				Tier0_Warning("Error: No calc with name \"%s\" found.\n", parentCalcName);

			return;
		}
	}

	Tier0_Msg(
		"%s add [...] - Add a new calc.\n"
		"%s remove <sCalcName> - Remove calc with name <sCalcName>.\n"
		"%s print - Print calcs.\n"
		"%s test <sCalcName> - Test a calc.\n"
		"%s edit <sCalcName> [...] - Edit a calc.\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
}


void mirv_calcs_cam(IWrpCommandArgs * args)
{
	int argc = args->ArgC();

	char const * arg0 = args->ArgV(0);

	if (2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if (0 == _stricmp("add", arg1))
		{
			if (3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if (0 == _stricmp("cam", arg2) && 6 <= argc)
				{
					char const * camFileName = args->ArgV(4);
					char const * szStartClientTime = args->ArgV(5);

					g_MirvCamCalcs.NewCamCalc(args->ArgV(3), camFileName, szStartClientTime);

					return;
				}
				else if (0 == _stricmp("game", arg2))
				{
					g_MirvCamCalcs.NewGameCalc(args->ArgV(3));

					return;
				}
				else if (0 == _stricmp("current", arg2))
				{
					g_MirvCamCalcs.NewCurrentCalc(args->ArgV(3));

					return;
				}
#ifndef _WIN64
				else if (0 == _stricmp("player", arg2) && 5 <= argc)
				{
					char const * handleCalcName = args->ArgV(4);

					if (IMirvHandleCalc * handleCalc = g_MirvHandleCalcs.GetByName(handleCalcName))
					{
						g_MirvCamCalcs.NewPlayerCalc(args->ArgV(3), handleCalc);
					}
					else Tier0_Warning("Error: No handle calc with name \"%s\" found.\n", handleCalcName);
					
					return;
				}
#endif //#ifndef _WIN64
				else if (0 == _stricmp("smooth", arg2) && 6 <= argc)
				{
					char const* parentCalcName = args->ArgV(4);
					char const* trackHandleCalcName = args->ArgV(5);

					if (IMirvCamCalc* parentCalc = g_MirvCamCalcs.GetByName(parentCalcName))
					{
						if (IMirvHandleCalc* trackHandleCalc = g_MirvHandleCalcs.GetByName(trackHandleCalcName))
						{
							g_MirvCamCalcs.NewSmoothCalc(args->ArgV(3), parentCalc, trackHandleCalc);
						}
						else Tier0_Warning("Error: No handle calc with name \"%s\" found.\n", trackHandleCalcName);
					}
					else Tier0_Warning("Error: No cam calc with name \"%s\" found.\n", parentCalcName);

					return;
				}
				else if (0 == _stricmp("vecAngFov", arg2) && 6 <= argc)
				{
					char const * vecAngCalcName = args->ArgV(4);
					char const * fovCalcName = args->ArgV(5);

					if (IMirvVecAngCalc * vecAngCalc = g_MirvVecAngCalcs.GetByName(vecAngCalcName))
					{
						if (IMirvFovCalc * fovCalc = g_MirvFovCalcs.GetByName(fovCalcName))
						{
							g_MirvCamCalcs.NewVecAngFovCalc(args->ArgV(3), vecAngCalc, fovCalc);
						}
						else Tier0_Warning("Error: No fov calc with name \"%s\" found.\n", fovCalcName);
					}
					else Tier0_Warning("Error: No vecAng calc with name \"%s\" found.\n", vecAngCalcName);

					return;
				}
			}

			Tier0_Msg(
				"%s add cam <sName> <sfilePath> <fStartTime>|current - Adds a mirv_camio file as calc.\n"
				"%s add game <sName> - Current game camera.\n"
				"%s add current <sName> - Current camera (depends on mirv_cam order and overrides).\n"
				"%s add player <sName> <sPlayerHandleCalcName> - Camera of player.\n"
				"%s add smooth <sName> <sParentCamCalcName> <sTrackHandleCalcName>- Smooth calc.\n"
				"%s add vecAngFov <sName> <sVecAngCalcName> <sFovCalcName>- Combine a vecAng and a fov calc into a cam calc.\n"
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
			);
			return;
		}
		else if (0 == _stricmp("remove", arg1) && 3 <= argc)
		{
			g_MirvCamCalcs.Console_Remove(args->ArgV(2));
			return;
		}
		else if (0 == _stricmp("print", arg1))
		{
			g_MirvCamCalcs.Console_Print();
			return;
		}
		else if (0 == _stricmp("test", arg1) && 3 <= argc)
		{
			char const * parentCalcName = args->ArgV(2);

			IMirvCamCalc * parentCalc = g_MirvCamCalcs.GetByName(parentCalcName);

			if (parentCalc)
			{
				SOURCESDK::Vector vec;
				SOURCESDK::QAngle ang;
				float fov;
				bool calced = parentCalc->CalcCam(vec, ang, fov);

				if (calced)
					Tier0_Msg("\nResult: true, vec=(%f, %f, %f), ang=(%f, %f, %f), fov=%f\n", vec.x, vec.y, vec.z, ang.z, ang.x, ang.y, fov);
				else
					Tier0_Msg("\nResult: false\n");
			}
			else
				Tier0_Warning("Error: No calc with name \"%s\" found.\n", parentCalcName);

			return;
		}
		else if (0 == _stricmp("edit", arg1) && 3 <= argc)
		{
			char const * parentCalcName = args->ArgV(2);

			IMirvCamCalc * parentCalc = g_MirvCamCalcs.GetByName(parentCalcName);

			if (parentCalc)
			{
				CSubWrpCommandArgs sub(args, 3);
				parentCalc->Console_Edit(&sub);
				return;
			}
			else
				Tier0_Warning("Error: No calc with name \"%s\" found.\n", parentCalcName);

			return;
		}
	}

	Tier0_Msg(
		"%s add [...] - Add a new calc.\n"
		"%s remove <sCalcName> - Remove calc with name <sCalcName>.\n"
		"%s print - Print calcs.\n"
		"%s test <sCalcName> - Test a calc.\n"
		"%s edit <sCalcName> [...] - Edit a calc.\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
}



void mirv_calcs_fov(IWrpCommandArgs * args)
{
	int argc = args->ArgC();

	char const * arg0 = args->ArgV(0);

	if (2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if (0 == _stricmp("add", arg1))
		{
			if (3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if (0 == _stricmp("cam", arg2) && 5 <= argc)
				{
					char const * parentCalcName = args->ArgV(4);

					IMirvCamCalc * parentCalc = g_MirvCamCalcs.GetByName(parentCalcName);

					if (parentCalc)
						g_MirvFovCalcs.NewCamCalc(args->ArgV(3), parentCalc);
					else
						Tier0_Warning("Error: No cam calc with name \"%s\" found.\n", parentCalcName);

					return;
				}
			}

			Tier0_Msg(
				"%s add cam <sName> <sCamCalName> - Adds a calc that gets its values from a cam calc named <sCamCalName>.\n"
				, arg0
			);
			return;
		}
		else if (0 == _stricmp("remove", arg1) && 3 <= argc)
		{
			g_MirvFovCalcs.Console_Remove(args->ArgV(2));
			return;
		}
		else if (0 == _stricmp("print", arg1))
		{
			g_MirvFovCalcs.Console_Print();
			return;
		}
		else if (0 == _stricmp("test", arg1) && 3 <= argc)
		{
			char const * parentCalcName = args->ArgV(2);

			IMirvFovCalc * parentCalc = g_MirvFovCalcs.GetByName(parentCalcName);

			if (parentCalc)
			{
				float fov;
				bool calced = parentCalc->CalcFov(fov);

				if (calced)
					Tier0_Msg("\nResult: true, fov=%f\n", fov);
				else
					Tier0_Msg("\nResult: false\n");
			}
			else
				Tier0_Warning("Error: No calc with name \"%s\" found.\n", parentCalcName);

			return;
		}
		else if (0 == _stricmp("edit", arg1) && 3 <= argc)
		{
			char const * parentCalcName = args->ArgV(2);

			IMirvFovCalc * parentCalc = g_MirvFovCalcs.GetByName(parentCalcName);

			if (parentCalc)
			{
				CSubWrpCommandArgs sub(args, 3);
				parentCalc->Console_Edit(&sub);
				return;
			}
			else
				Tier0_Warning("Error: No calc with name \"%s\" found.\n", parentCalcName);

			return;
		}
	}

	Tier0_Msg(
		"%s add [...] - Add a new calc.\n"
		"%s remove <sCalcName> - Remove calc with name <sCalcName>.\n"
		"%s print - Print calcs.\n"
		"%s test <sCalcName> - Test a calc.\n"
		"%s edit <sCalcName> [...] - Edit a calc.\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
}


void mirv_calcs_bool(IWrpCommandArgs * args)
{
	int argc = args->ArgC();

	char const * arg0 = args->ArgV(0);

	if (2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if (0 == _stricmp("add", arg1))
		{
			if (3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if (0 == _stricmp("and", arg2) && 6 <= argc)
				{
					char const * parentCalcNameA = args->ArgV(4);
					char const * parentCalcNameB = args->ArgV(5);

					IMirvBoolCalc * parentCalcA = g_MirvBoolCalcs.GetByName(parentCalcNameA);
					IMirvBoolCalc * parentCalcB = g_MirvBoolCalcs.GetByName(parentCalcNameB);

					if (parentCalcA && parentCalcB)
						g_MirvBoolCalcs.NewAndCalc(args->ArgV(3), parentCalcA, parentCalcB);
					else
					{
						if(!parentCalcA) Tier0_Warning("Error: No bool calc with name \"%s\" found.\n", parentCalcA);
						if (!parentCalcB) Tier0_Warning("Error: No bool calc with name \"%s\" found.\n", parentCalcB);
					}

					return;
				}
				else if (0 == _stricmp("or", arg2) && 6 <= argc)
				{
					char const * parentCalcNameA = args->ArgV(4);
					char const * parentCalcNameB = args->ArgV(5);

					IMirvBoolCalc * parentCalcA = g_MirvBoolCalcs.GetByName(parentCalcNameA);
					IMirvBoolCalc * parentCalcB = g_MirvBoolCalcs.GetByName(parentCalcNameB);

					if (parentCalcA && parentCalcB)
						g_MirvBoolCalcs.NewOrCalc(args->ArgV(3), parentCalcA, parentCalcB);
					else
					{
						if(!parentCalcA) Tier0_Warning("Error: No bool calc with name \"%s\" found.\n", parentCalcA);
						if (!parentCalcB) Tier0_Warning("Error: No bool calc with name \"%s\" found.\n", parentCalcB);
					}

					return;
				}
				else if (0 == _stricmp("not", arg2) && 5 <= argc)
				{
					char const * parentCalcName = args->ArgV(4);

					IMirvBoolCalc * parentCalc = g_MirvBoolCalcs.GetByName(parentCalcName);

					if (parentCalc)
						g_MirvBoolCalcs.NewNotCalc(args->ArgV(3), parentCalc);
					else
						Tier0_Warning("Error: No bool calc with name \"%s\" found.\n", parentCalcName);

					return;
				}
				else if (0 == _stricmp("handle", arg2) && 5 <= argc)
				{
					char const * parentCalcName = args->ArgV(4);

					IMirvHandleCalc * parentCalc = g_MirvHandleCalcs.GetByName(parentCalcName);

					if (parentCalc)
						g_MirvBoolCalcs.NewHandleCalc(args->ArgV(3), parentCalc);
					else
						Tier0_Warning("Error: No handle calc with name \"%s\" found.\n", parentCalcName);

					return;
				}
				else if (0 == _stricmp("vecAng", arg2) && 5 <= argc)
				{
					char const * parentCalcName = args->ArgV(4);

					IMirvVecAngCalc * parentCalc = g_MirvVecAngCalcs.GetByName(parentCalcName);

					if (parentCalc)
						g_MirvBoolCalcs.NewVecAngCalc(args->ArgV(3), parentCalc);
					else
						Tier0_Warning("Error: No vecAng calc with name \"%s\" found.\n", parentCalcName);

					return;
				}
#ifndef _WIN64
				else if (0 == _stricmp("alive", arg2) && 5 <= argc)
				{
					char const * parentCalcName = args->ArgV(4);

					IMirvHandleCalc * parentCalc = g_MirvHandleCalcs.GetByName(parentCalcName);

					if (parentCalc)
						g_MirvBoolCalcs.NewAliveCalc(args->ArgV(3), parentCalc);
					else
						Tier0_Warning("Error: No handle calc with name \"%s\" found.\n", parentCalcName);

					return;
				}
#endif //#ifndef _WIN64
				else if (0 == _stricmp("handlesEqual", arg2) && 6 <= argc)
				{
					char const * parentCalcNameA = args->ArgV(4);
					char const * parentCalcNameB = args->ArgV(5);

					IMirvHandleCalc * parentCalcA = g_MirvHandleCalcs.GetByName(parentCalcNameA);
					IMirvHandleCalc * parentCalcB = g_MirvHandleCalcs.GetByName(parentCalcNameB);

					if (parentCalcA && parentCalcB)
						g_MirvBoolCalcs.NewHandlesEqualCalc(args->ArgV(3), parentCalcA, parentCalcB);
					else
					{
						if (!parentCalcA) Tier0_Warning("Error: No handle calc with name \"%s\" found.\n", parentCalcA);
						if (!parentCalcB) Tier0_Warning("Error: No handle calc with name \"%s\" found.\n", parentCalcB);
					}

					return;
				}
				else if (0 == _stricmp("intsEqual", arg2) && 6 <= argc)
				{
					char const * parentCalcNameA = args->ArgV(4);
					char const * parentCalcNameB = args->ArgV(5);

					IMirvIntCalc * parentCalcA = g_MirvIntCalcs.GetByName(parentCalcNameA);
					IMirvIntCalc * parentCalcB = g_MirvIntCalcs.GetByName(parentCalcNameB);

					if (parentCalcA && parentCalcB)
						g_MirvBoolCalcs.NewIntsEqualCalc(args->ArgV(3), parentCalcA, parentCalcB);
					else
					{
						if (!parentCalcA) Tier0_Warning("Error: No int calc with name \"%s\" found.\n", parentCalcA);
						if (!parentCalcB) Tier0_Warning("Error: No int calc with name \"%s\" found.\n", parentCalcB);
					}

					return;
				}
#ifndef _WIN64
				else if (0 == _stricmp("classnameMatches", arg2) && 6 <= argc)
				{
				char const * parentCalcNameA = args->ArgV(4);
				char const * wildCardString = args->ArgV(5);

				IMirvHandleCalc * parentCalc = g_MirvHandleCalcs.GetByName(parentCalcNameA);

				if (parentCalc)
					g_MirvBoolCalcs.NewClassnameMatchesCalc(args->ArgV(3), parentCalc, wildCardString);
				else
					Tier0_Warning("Error: No int calc with name \"%s\" found.\n", parentCalc);

				return;
				}
#endif //#ifndef _WIN64
			}

			Tier0_Msg(
				"%s add and <sName> <boolCalcNameA> <boolCalcNameB> - Returns AND of the bool calcs if valid.\n"
				"%s add or <sName> <boolCalcNameA> <boolCalcNameB> - Returns OR of the bool calcs if valid.\n"
				"%s add not <sName> <boolCalcName> - Returns NOT of bool calc if valid.\n"
				"%s add handle <sName> <handleCalcName> - Returns true if handleCalc is valid.\n"
				"%s add vecAng <sName> <vecAngCalcNae> - Returns true if vecAngCalc is valid.\n"
				"%s add alive <sName> <handleCalcName> - Returns true if alive.\n"
				"%s add handlesEqual <sName> <handleCalcNameA> <handleCalcNameB>\n"
				"%s add intsEqual <sName> <intCalcNameA> <intCalcNameB>\n"
				"%s add classnameMatches <sName> <handleCalcname> <sWildCardString>\n"
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
			);
			return;
		}
		else if (0 == _stricmp("remove", arg1) && 3 <= argc)
		{
			g_MirvBoolCalcs.Console_Remove(args->ArgV(2));
			return;
		}
		else if (0 == _stricmp("print", arg1))
		{
			g_MirvBoolCalcs.Console_Print();
			return;
		}
		else if (0 == _stricmp("test", arg1) && 3 <= argc)
		{
			char const * parentCalcName = args->ArgV(2);

			IMirvBoolCalc * parentCalc = g_MirvBoolCalcs.GetByName(parentCalcName);

			if (parentCalc)
			{
				bool value;
				bool calced = parentCalc->CalcBool(value);

				if (calced)
					Tier0_Msg("\nResult: true, bool=%i\n", value ? 1 : 0);
				else
					Tier0_Msg("\nResult: false\n");
			}
			else
				Tier0_Warning("Error: No calc with name \"%s\" found.\n", parentCalcName);

			return;
		}
		else if (0 == _stricmp("edit", arg1) && 3 <= argc)
		{
			char const * parentCalcName = args->ArgV(2);

			IMirvHandleCalc * parentCalc = g_MirvHandleCalcs.GetByName(parentCalcName);

			if (parentCalc)
			{
				CSubWrpCommandArgs sub(args, 3);
				parentCalc->Console_Edit(&sub);
				return;
			}
			else
				Tier0_Warning("Error: No calc with name \"%s\" found.\n", parentCalcName);

			return;
		}
	}

	Tier0_Msg(
		"%s add [...] - Add a new bool calc.\n"
		"%s remove <sCalcName> - Remove calc with name <sCalcName>.\n"
		"%s print - Print calcs.\n"
		"%s test <sCalcName> - Test a calc.\n"
		"%s edit <sCalcName> [...] - Edit a calc.\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
}



void mirv_calcs_int(IWrpCommandArgs * args)
{
	int argc = args->ArgC();

	char const * arg0 = args->ArgV(0);

	if (2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if (0 == _stricmp("add", arg1))
		{
			if (3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if (0 == _stricmp("value", arg2) && 5 <= argc)
				{
					int value = atoi(args->ArgV(4));

					g_MirvIntCalcs.NewValueCalc(args->ArgV(3), value);
					return;
				}
#ifndef _WIN64
				else if (0 == _stricmp("teamNumber", arg2) && 5 <= argc)
				{
					char const * parentCalcName = args->ArgV(4);

					IMirvHandleCalc * parentCalc = g_MirvHandleCalcs.GetByName(parentCalcName);

					if (parentCalc)
						g_MirvIntCalcs.NewTeamNumberCalc(args->ArgV(3), parentCalc);
					else
						Tier0_Warning("Error: No handle calc with name \"%s\" found.\n", parentCalcName);

					return;
				}
#endif //#ifndef _WIN64
			}

			Tier0_Msg(
				"%s add value <sName> <iValue>.\n"
				"%s add teamNumber <sName> <handleCalcName> - Returns team number (1 = spec, 2 = T, 3 = CT).\n"
				, arg0
				, arg0
			);
			return;
		}
		else if (0 == _stricmp("remove", arg1) && 3 <= argc)
		{
			g_MirvIntCalcs.Console_Remove(args->ArgV(2));
			return;
		}
		else if (0 == _stricmp("print", arg1))
		{
			g_MirvIntCalcs.Console_Print();
			return;
		}
		else if (0 == _stricmp("test", arg1) && 3 <= argc)
		{
			char const * parentCalcName = args->ArgV(2);

			IMirvIntCalc * parentCalc = g_MirvIntCalcs.GetByName(parentCalcName);

			if (parentCalc)
			{
				int value;
				bool calced = parentCalc->CalcInt(value);

				if (calced)
					Tier0_Msg("\nResult: true, int=%i\n", value);
				else
					Tier0_Msg("\nResult: false\n");
			}
			else
				Tier0_Warning("Error: No calc with name \"%s\" found.\n", parentCalcName);

			return;
		}
		else if (0 == _stricmp("edit", arg1) && 3 <= argc)
		{
			char const * parentCalcName = args->ArgV(2);

			IMirvHandleCalc * parentCalc = g_MirvHandleCalcs.GetByName(parentCalcName);

			if (parentCalc)
			{
				CSubWrpCommandArgs sub(args, 3);
				parentCalc->Console_Edit(&sub);
				return;
			}
			else
				Tier0_Warning("Error: No calc with name \"%s\" found.\n", parentCalcName);

			return;
		}
	}

	Tier0_Msg(
		"%s add [...] - Add a new bool calc.\n"
		"%s remove <sCalcName> - Remove calc with name <sCalcName>.\n"
		"%s print - Print calcs.\n"
		"%s test <sCalcName> - Test a calc.\n"
		"%s edit <sCalcName> [...] - Edit a calc.\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
}


void mirv_calcs_float(IWrpCommandArgs* args)
{
	int argc = args->ArgC();

	char const* arg0 = args->ArgV(0);

	if (2 <= argc)
	{
		char const* arg1 = args->ArgV(1);

		if (0 == _stricmp("add", arg1))
		{
			if (3 <= argc)
			{
				char const* arg2 = args->ArgV(2);

				if (0 == _stricmp("value", arg2) && 5 <= argc)
				{
					float value = (float)(atof(args->ArgV(4)));

					g_MirvFloatCalcs.NewValueCalc(args->ArgV(3), value);
					return;
				}
				else if (0 == _stricmp("if", arg2) && 7 <= argc)
				{
					const char* boolCalcNameCondition = args->ArgV(4);
					const char* floatCalcNameTrue = args->ArgV(5);
					const char* floatCalcNameFalse = args->ArgV(6);

					if (IMirvBoolCalc* boolCalcCondition = g_MirvBoolCalcs.GetByName(boolCalcNameCondition))
					{
						if (IMirvFloatCalc* floatCalcTrue = g_MirvFloatCalcs.GetByName(floatCalcNameTrue))
						{
							if (IMirvFloatCalc* floatCalcFalse = g_MirvFloatCalcs.GetByName(floatCalcNameFalse))
							{
								g_MirvFloatCalcs.NewIfCalc(args->ArgV(3), boolCalcCondition, floatCalcTrue, floatCalcFalse);
							}
							else Tier0_Warning("Error: No float calc with name \"%s\" found.\n", floatCalcNameFalse);
						}
						else Tier0_Warning("Error: No float calc with name \"%s\" found.\n", floatCalcNameTrue);
					}
					else Tier0_Warning("Error: No bool calc with name \"%s\" found.\n", boolCalcNameCondition);

					return;
				}
			}

			Tier0_Msg(
				"%s add value <sName> <iValue>.\n"
				"%s add if <sName> <boolCalcNameCondition> <floatCalcNameTrue> <floatCalcNameFalse>.\n"
				, arg0
				, arg0
			);
			return;
		}
		else if (0 == _stricmp("remove", arg1) && 3 <= argc)
		{
			g_MirvIntCalcs.Console_Remove(args->ArgV(2));
			return;
		}
		else if (0 == _stricmp("print", arg1))
		{
			g_MirvIntCalcs.Console_Print();
			return;
		}
		else if (0 == _stricmp("test", arg1) && 3 <= argc)
		{
			char const* parentCalcName = args->ArgV(2);

			IMirvIntCalc* parentCalc = g_MirvIntCalcs.GetByName(parentCalcName);

			if (parentCalc)
			{
				int value;
				bool calced = parentCalc->CalcInt(value);

				if (calced)
					Tier0_Msg("\nResult: true, int=%i\n", value);
				else
					Tier0_Msg("\nResult: false\n");
			}
			else
				Tier0_Warning("Error: No calc with name \"%s\" found.\n", parentCalcName);

			return;
		}
		else if (0 == _stricmp("edit", arg1) && 3 <= argc)
		{
			char const* parentCalcName = args->ArgV(2);

			IMirvHandleCalc* parentCalc = g_MirvHandleCalcs.GetByName(parentCalcName);

			if (parentCalc)
			{
				CSubWrpCommandArgs sub(args, 3);
				parentCalc->Console_Edit(&sub);
				return;
			}
			else
				Tier0_Warning("Error: No calc with name \"%s\" found.\n", parentCalcName);

			return;
		}
	}

	Tier0_Msg(
		"%s add [...] - Add a new bool calc.\n"
		"%s remove <sCalcName> - Remove calc with name <sCalcName>.\n"
		"%s print - Print calcs.\n"
		"%s test <sCalcName> - Test a calc.\n"
		"%s edit <sCalcName> [...] - Edit a calc.\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
}



CON_COMMAND(mirv_calcs, "Expressions, currently mainly for usage mirv_calcs, mirv_cam, mirv_aim")
{
	int argc = args->ArgC();
	char const * arg0 = args->ArgV(0);

	if (2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if (0 == _stricmp("handle", arg1))
		{
			CSubWrpCommandArgs sub(args, 2);
			mirv_calcs_handle(&sub);
			return;
		}
		else if (0 == _stricmp("vecAng", arg1))
		{
			CSubWrpCommandArgs sub(args, 2);
			mirv_calcs_vecang(&sub);
			return;
		}
		else if (0 == _stricmp("fov", arg1))
		{
			CSubWrpCommandArgs sub(args, 2);
			mirv_calcs_fov(&sub);
			return;
		}
		else if (0 == _stricmp("cam", arg1))
		{
			CSubWrpCommandArgs sub(args, 2);
			mirv_calcs_cam(&sub);
			return;
		}
		else if (0 == _stricmp("bool", arg1))
		{
			CSubWrpCommandArgs sub(args, 2);
			mirv_calcs_bool(&sub);
			return;
		}
		else if (0 == _stricmp("int", arg1))
		{
			CSubWrpCommandArgs sub(args, 2);
			mirv_calcs_int(&sub);
			return;
		}
		else if (0 == _stricmp("float", arg1))
		{
			CSubWrpCommandArgs sub(args, 2);
			mirv_calcs_float(&sub);
			return;
		}
	}

	Tier0_Msg(
		"%s handle [...] - Calcs that return an entity handle.\n"
		"%s vecAng [...] - Calcs that return VecAng (location and rotation).\n"
		"%s fov [...] - Calcs that return FOV (field of view).\n"
		"%s cam [...] - Calcs that return a view (location, rotation and FOV).\n"
		"%s bool [...] - Calc that returns true or false (if it could be evaluated that is).\n"
		"%s int [...] - Calc that returns an integer or nothing.\n"
		"%s float [...] - Calc that returns an integer or nothing.\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
}

void MirvCalcs_LevelInitPreEntity()
{
	MirvCameraCollisionReset();
	++g_LevelInitCount;
}

bool Mirv_Tf2_GetSceneEntity(int index, float origin[3], float angles[3], float mins[3], float maxs[3])
{
	auto entity = Mirv_Tf2_DiagnosticEntity(index);
	auto renderable = entity ? reinterpret_cast<CMirvTf2ClientUnknown *>(entity)->GetClientRenderable() : nullptr;
	if (!renderable || !renderable->GetModel()) return false;
	auto o = renderable->GetRenderOrigin();
	auto a = renderable->GetRenderAngles();
	if (!o || !a) return false;
	std::memcpy(origin, o, 3*sizeof(float));
	std::memcpy(angles, a, 3*sizeof(float));
	renderable->GetRenderBounds(mins, maxs);
	return true;
}

// Selected-player scene-pose prototype. The layout below is the public Source
// SDK 2013 ABI, not the CS:GO VStudioRender026 layout. No live entity pointers,
// studio headers, hardware-data pointers or cache handles enter scene history.
namespace
{
template <typename R, typename... Args> R SceneInterfaceCall(void *object, size_t slot, Args... args)
{
#ifdef _WIN64
	using Fn = R (*)(void *, Args...);
#else
	using Fn = R(__thiscall *)(void *, Args...);
#endif
	return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot])(object, args...);
}
struct SceneStudioInfo
{
	void *Header = nullptr, *Hardware = nullptr, *Decals = nullptr;
	int Skin = 0, Body = 0, Hitbox = 0;
	void *ClientEntity = nullptr;
	int Lod = 0;
	void *ColorMeshes = nullptr;
	bool StaticLighting = false;
	float AmbientCube[6][3]{};
	int LightCount = 0;
	float LightDescriptors[4][22]{}; // Source LightDesc_t: 88 bytes, 4-byte alignment.
};
static_assert(sizeof(SOURCESDK::matrix3x4_t) == sizeof(advancedfx::SceneCache::Matrix),
              "TF2 bone matrix ABI");
static_assert(sizeof(SceneStudioInfo) == (sizeof(void *) == 8 ? 496 : 468), "TF2 DrawModelInfo ABI");
static_assert(offsetof(SceneStudioInfo, Lod) == (sizeof(void *) == 8 ? 48 : 28), "TF2 model LOD ABI");
static_assert(offsetof(SceneStudioInfo, LightCount) == (sizeof(void *) == 8 ? 140 : 112), "TF2 light ABI");
// Private, immutable materials avoid mutating the engine's shared debug
// wireframe material or its color modulation. 082 is the verified TF2 ABI;
// older engines retain the default studio wireframe and colored proxy bounds.
static void *g_SceneMaterials[advancedfx::SceneCache::CategoryCount]{};
// MaterialSystem082 uses Source 2013's KeyValues layout. The older TF2
// read-only tool-message adapter contains extra local-table fields, so it
// cannot construct these shader definitions. Source owns the tree after
// CreateMaterial; every allocation uses Source's allocators.
struct SceneMaterialKeyValues
{
	int m_iKeyName;
	char *m_sValue;
	wchar_t *m_wsValue;
	union {
		int Integer;
		float Float;
		void *Pointer;
	} Value;
	char m_iDataType, EscapeSequences, m_bEvaluateConditionals, Unused;
	SceneMaterialKeyValues *m_pPeer, *m_pSub, *Chain;
};
static_assert(sizeof(SceneMaterialKeyValues) == (sizeof(void *) == 8 ? 64 : 32),
              "MaterialSystem082 KeyValues ABI");
static SceneMaterialKeyValues *SceneMaterialKey(const char *name)
{
	using KV = SceneMaterialKeyValues;
	auto system = SOURCESDK::TF2::KeyValuesSystem();
	if (!system)
		return nullptr;
	auto key = static_cast<KV *>(system->AllocKeyValuesMemory(sizeof(KV)));
	if (!key)
		return nullptr;
	std::memset(key, 0, sizeof(KV));
	key->m_iKeyName = system->GetSymbolForString(name, true);
	key->m_bEvaluateConditionals = true;
	return key;
}
static void SceneDeleteMaterialKeys(SceneMaterialKeyValues *key)
{
	while (key)
	{
		auto next = key->m_pPeer;
		SceneDeleteMaterialKeys(key->m_pSub);
		if (key->m_sValue)
			SOURCESDK::L4D2::g_pMemAlloc->Free(key->m_sValue);
		SOURCESDK::TF2::KeyValuesSystem()->FreeKeyValuesMemory(key);
		key = next;
	}
}
static bool SceneMaterialString(SceneMaterialKeyValues *root, const char *name, const char *value)
{
	auto key = SceneMaterialKey(name);
	if (!key)
		return false;
	const size_t bytes = std::strlen(value) + 1;
	key->m_sValue = static_cast<char *>(SOURCESDK::L4D2::g_pMemAlloc->IndirectAlloc(bytes));
	if (!key->m_sValue)
	{
		SceneDeleteMaterialKeys(key);
		return false;
	}
	std::memcpy(key->m_sValue, value, bytes);
	key->m_iDataType = 1; // TYPE_STRING
	key->m_pPeer = root->m_pSub;
	root->m_pSub = key;
	return true;
}
static void *SceneCategoryMaterial(int category)
{
	if (category < 0 || category >= advancedfx::SceneCache::CategoryCount || !g_AppSystemFactory ||
	    !SOURCESDK::L4D2::g_pMemAlloc)
		return nullptr;
	if (g_SceneMaterials[category])
		return g_SceneMaterials[category];
	void *system = g_AppSystemFactory("VMaterialSystem082", nullptr);
	if (!system)
		return nullptr;
	auto values = SceneMaterialKey("UnlitGeneric");
	if (!values)
		return nullptr;
	bool valid =
	    SceneMaterialString(values, "$basetexture", "vgui/white_additive") &&
	    SceneMaterialString(values, "$model", "1") && SceneMaterialString(values, "$wireframe", "1") &&
	    SceneMaterialString(values, "$vertexcolor", "0") &&
	    SceneMaterialString(values, "$vertexalpha", "0") && SceneMaterialString(values, "$ignorez", "0");
	const auto rgb = advancedfx::SceneCache::CategoryColor(category);
	char color[96], name[96];
	std::snprintf(color, sizeof(color), "[%.6f %.6f %.6f]", rgb[0] / 255.f, rgb[1] / 255.f, rgb[2] / 255.f);
	std::snprintf(name, sizeof(name), "__hlae_scene_cache_category_%d_v2", category);
	// Studio overwrites IMaterial's $color with its per-draw modulation
	// (normally white). $color2 is the shader's independent model tint and
	// survives that update, including when Source executes the draw later.
	valid = valid && SceneMaterialString(values, "$color2", color);
	if (!valid)
	{
		SceneDeleteMaterialKeys(values);
		return nullptr;
	}
	// IMaterialSystem082::CreateMaterial consumes KeyValues and returns one
	// caller reference (verified against TF2). Keep it until an idle demo frame.
	void *material =
	    SceneInterfaceCall<void *, const char *, SceneMaterialKeyValues *>(system, 70, name, values);
	if (!material)
		return nullptr;
	if (SceneInterfaceCall<bool>(material, 42))
	{
		SceneInterfaceCall<void>(material, 13);
		return nullptr;
	}
	g_SceneMaterials[category] = material;
	return material;
}
// The render queue only signals completion. MDL/material APIs are retired
// on the engine thread, including when Source discards callbacks on reset.
struct SceneRetirement
{
	void *Cache;
	unsigned short Handle;
	bool HeaderLocked = false;
	void *Material = nullptr;
	SceneRetirement *Next = nullptr;
};
static std::atomic<SceneRetirement *> g_SceneCompleted{nullptr};
static std::atomic<unsigned> g_SceneOutstanding{0}, g_SceneOutstandingPeak{0};
static constexpr unsigned SceneMaxOutstanding = 512;
static void SceneReleaseResources(SceneRetirement *data)
{
	if (data->HeaderLocked)
		SceneInterfaceCall<void, unsigned short>(data->Cache, 41, data->Handle);
	SceneInterfaceCall<int, unsigned short>(data->Cache, 8, data->Handle);
	if (data->Material)
		SceneInterfaceCall<void>(data->Material, 13);
	delete data;
}
class SceneModelResources : public CMaterialSystemFunctor
{
public:
	SceneModelResources(void *cache, unsigned short handle) : Data(new SceneRetirement{cache, handle})
	{
	}
	~SceneModelResources() override
	{
		Complete();
	}
	SceneRetirement &State()
	{
		return *Data.load();
	}
	void Submit()
	{
		Submitted = true;
		unsigned count = ++g_SceneOutstanding, peak = g_SceneOutstandingPeak.load();
		while (count > peak && !g_SceneOutstandingPeak.compare_exchange_weak(peak, count))
		{
		}
	}
	void operator()() override
	{
		Complete();
	}

private:
	void Complete()
	{
		auto data = Data.exchange(nullptr);
		if (!data)
			return;
		if (!Submitted)
		{
			SceneReleaseResources(data);
			return;
		} // Pre-draw errors, on engine thread.
		auto head = g_SceneCompleted.load(std::memory_order_relaxed);
		do
		{
			data->Next = head;
		} while (!g_SceneCompleted.compare_exchange_weak(head, data, std::memory_order_release,
		                                                 std::memory_order_relaxed));
	}
	std::atomic<SceneRetirement *> Data;
	bool Submitted = false;
};
// Convert once per observed pose, using the same inverse/concatenation as AGR.
// Unsupported bones retain their own verbatim matrices; the remaining bones can still blend.
static void ScenePrepareLocalPose(advancedfx::SceneCache::Pose &pose)
{
	using Cache = advancedfx::SceneCache;
	std::vector<Cache::Transform> local(pose.Bones.size());
	std::vector<Cache::FixedBone> fixed;
	for (size_t i = 0; i < local.size(); ++i)
	{
		if (!(pose.ModelInfo->Flags[i] & SOURCESDK_BONE_USED_BY_ANYTHING))
			continue;
		const int parent = pose.ModelInfo->Parents[i];
		Cache::FixedBone fallback;
		fallback.Index = int(i);
		fallback.Value = pose.Bones[i];
		fallback.World = true;
		if (parent >= 0 && !(pose.ModelInfo->Flags[parent] & SOURCESDK_BONE_USED_BY_ANYTHING))
		{
			fallback.Reason = Cache::BoneFallback::MissingParent;
			fixed.push_back(fallback);
			continue;
		}
		SOURCESDK::matrix3x4_t parentMatrix, inverse, world, relative;
		const auto &source = parent < 0 ? pose.Root : pose.Bones[parent];
		std::memcpy(parentMatrix.m_flMatVal, source.data(), sizeof(parentMatrix));
		std::memcpy(world.m_flMatVal, pose.Bones[i].data(), sizeof(world));
		if (!CClientTools::InvertMatrix(parentMatrix, inverse))
		{
			fallback.Reason = Cache::BoneFallback::SingularParent;
			fixed.push_back(fallback);
			continue;
		}
		SOURCESDK::R_ConcatTransforms(inverse, world, relative);
		Cache::Matrix matrix;
		std::memcpy(matrix.data(), relative.m_flMatVal, sizeof(relative));
		if (!std::all_of(matrix.begin(), matrix.end(), [](float value) { return std::isfinite(value); }))
		{
			fallback.Reason = Cache::BoneFallback::SingularParent;
			fixed.push_back(fallback);
			continue;
		}
		if (!Cache::Transform::Decompose(matrix, local[i]))
		{
			fallback.Value = matrix;
			fallback.World = false;
			fixed.push_back(fallback);
			local[i] = Cache::Transform{};
		}
	}
	pose.Local = std::move(local);
	pose.Fixed = std::move(fixed);
	std::vector<Cache::Matrix>().swap(pose.Bones);
}
// No live entity or bone-cache pointers are needed to rebuild a sampled pose.
static bool SceneBuildMatrices(const advancedfx::SceneCache::Entity &entity,
                               const advancedfx::SceneCache::Entity *owner, SOURCESDK::matrix3x4_t *out)
{
	using Cache = advancedfx::SceneCache;
	const auto &pose = *entity.ModelPose;
	SOURCESDK::matrix3x4_t root;
	SOURCESDK::AngleMatrix(SOURCESDK::QAngle(entity.Angles[0], entity.Angles[1], entity.Angles[2]),
	                       SOURCESDK::Vector(entity.Origin[0], entity.Origin[1], entity.Origin[2]), root);
	const size_t count = pose.ModelInfo->Parents.size();
	if (pose.Local.empty())
	{
		SOURCESDK::matrix3x4_t captured, inverse, delta;
		std::memcpy(captured.m_flMatVal, pose.Root.data(), sizeof(captured));
		if (!CClientTools::InvertMatrix(captured, inverse))
			return false;
		SOURCESDK::R_ConcatTransforms(root, inverse, delta);
		for (size_t i = 0; i < count; ++i)
		{
			SOURCESDK::matrix3x4_t bone;
			std::memcpy(bone.m_flMatVal, pose.Bones[i].data(), sizeof(bone));
			SOURCESDK::R_ConcatTransforms(delta, bone, out[i]);
		}
	}
	else
	{
		SOURCESDK::matrix3x4_t ownerBones[Cache::MaxBones];
		const bool merge =
		    owner &&
		    (owner->Category == MIRV_TF2_ENTITY_PLAYER || owner->Category == MIRV_TF2_ENTITY_RAGDOLL ||
			 owner->Category == MIRV_TF2_ENTITY_BUILDING || owner->Category == MIRV_TF2_ENTITY_PROP) &&
		    (entity.Category == MIRV_TF2_ENTITY_WEAPON || entity.Category == MIRV_TF2_ENTITY_COSMETIC) &&
		    entity.OwnerIdentity == owner->Identity && owner->ModelPose &&
		    Cache::ValidPose(*owner->ModelPose) && !pose.ModelInfo->BoneNames.empty() &&
		    !owner->ModelPose->ModelInfo->BoneNames.empty() &&
		    !owner->ModelPose->ModelInfo->MergeBones.empty() &&
		    SceneBuildMatrices(*owner, nullptr, ownerBones);
		SOURCESDK::matrix3x4_t delta;
		if (std::any_of(pose.Fixed.begin(), pose.Fixed.end(), [](const auto &bone) { return bone.World; }))
		{
			SOURCESDK::matrix3x4_t captured, inverse;
			std::memcpy(captured.m_flMatVal, pose.Root.data(), sizeof(captured));
			if (!CClientTools::InvertMatrix(captured, inverse))
				return false;
			SOURCESDK::R_ConcatTransforms(root, inverse, delta);
		}
		size_t fixedIndex = 0;
		for (size_t i = 0; i < count; ++i)
		{
			const auto *fixed = fixedIndex < pose.Fixed.size() && pose.Fixed[fixedIndex].Index == int(i)
			                        ? &pose.Fixed[fixedIndex++]
			                        : nullptr;
			bool merged = false;
			if (merge && (pose.ModelInfo->Flags[i] & SOURCESDK_BONE_USED_BY_ANYTHING))
			{
				const auto &model = *owner->ModelPose->ModelInfo;
				const auto &name = pose.ModelInfo->BoneNames[i];
				const auto found =
				    std::lower_bound(model.MergeBones.begin(), model.MergeBones.end(), name,
					                 [&](int index, const std::string &value) {
					                     return _stricmp(model.BoneNames[index].c_str(), value.c_str()) < 0;
				                     });
				if (!name.empty() && name.size() < 63 && found != model.MergeBones.end() &&
				    !_stricmp(name.c_str(), model.BoneNames[*found].c_str()))
				{
					out[i] = ownerBones[*found];
					merged = true;
				}
			}
			if (merged)
				continue;
			const auto matrix = fixed ? fixed->Value : pose.Local[i].Compose();
			SOURCESDK::matrix3x4_t relative;
			std::memcpy(relative.m_flMatVal, matrix.data(), sizeof(relative));
			const int parent = pose.ModelInfo->Parents[i];
			SOURCESDK::R_ConcatTransforms(fixed && fixed->World ? delta : (parent < 0 ? root : out[parent]),
			                              relative, out[i]);
		}
	}
	for (size_t i = 0; i < count; ++i)
		for (const auto &row : out[i].m_flMatVal)
			for (float v : row)
				if (!std::isfinite(v))
					return false;
	return true;
}
// IClientNetworkable::IsDormant is slot 8 in TF2 (CS:GO inserts another method).
static bool SceneRenderable(int index, CMirvTf2ClientRenderable *&renderable, void *observedObject)
{
	auto entity = observedObject ? observedObject : (index > 0 ? Mirv_Tf2_DiagnosticEntity(index) : nullptr);
	auto unknown = reinterpret_cast<CMirvTf2ClientUnknown *>(entity);
	void *networkable = unknown ? unknown->GetClientNetworkable() : nullptr;
	renderable = unknown ? unknown->GetClientRenderable() : nullptr;
	return renderable && renderable->ShouldDraw() &&
	       ((observedObject && (index < 0 || index >= 2048)) ||
	        (networkable && !SceneInterfaceCall<bool>(networkable, 8)));
}
// Validate the same on-disk studio header used by AGR/bone-name inspection.
static bool SceneStudioHeader(const unsigned char *header, int &count, unsigned int &checksum)
{
	if (!header)
		return false;
	int id, version, length, boneOffset;
	std::memcpy(&id, header, 4);
	std::memcpy(&version, header + 4, 4);
	std::memcpy(&checksum, header + 8, 4);
	std::memcpy(&length, header + 76, 4);
	std::memcpy(&count, header + 156, 4);
	std::memcpy(&boneOffset, header + 160, 4);
	return id == 0x54534449 && version >= 44 && version <= 49 && length >= 164 && count > 0 &&
	       count <= int(advancedfx::SceneCache::MaxBones) && boneOffset >= 164 &&
	       int64_t(boneOffset) + int64_t(count) * 216 <= length;
}
} // namespace
bool Mirv_Tf2_CaptureScenePose(int index, advancedfx::SceneCache::Entity &entity, std::string &status,
                               void *observedObject)
{
	using Cache = advancedfx::SceneCache;
	entity.ModelPose.reset();
	CMirvTf2ClientRenderable *renderable = nullptr;
	if (observedObject && (entity.Identity >> 63))
	{
		const void *reference = reinterpret_cast<CMirvTf2ClientUnknown *>(observedObject)->GetRefEHandle();
		std::uint32_t handle = 0xffffffff;
		if (reference)
			std::memcpy(&handle, reference, sizeof(handle));
		if (handle == 0xffffffff || handle != std::uint32_t(entity.Identity))
		{
			status = "Observed entity identity changed; no pose captured.";
			return false;
		}
	}
	if (!SceneRenderable(index, renderable, observedObject))
	{
		status = "Entity is dormant, hidden or unavailable; no pose captured.";
		return false;
	}
	auto info = Mirv_Tf2_GetModelInfo();
	const void *model = renderable->GetModel();
	const char *name = info && model ? info->GetModelName(model) : nullptr;
	if (info && model && info->GetModelType(model) == 1) // mod_brush (doors / moving BSP entities).
	{
		status = "Brush model uses recorded, rotated proxy bounds; studio mesh capture is unavailable.";
		return false;
	}
	auto header = info && model ? static_cast<const unsigned char *>(info->GetStudiomodel(model)) : nullptr;
	int count;
	unsigned int checksum;
	if (!name || std::strlen(name) >= 260 || !SceneStudioHeader(header, count, checksum))
	{
		status = "Selected model has no supported studio header (maximum 128 bones).";
		return false;
	}
	// Metadata is shared across frames of this model; all data is copied out of
	// the engine. Weak lookup entries do not keep evicted descriptors alive.
	using ModelKey = std::pair<std::string, unsigned int>;
	static std::map<ModelKey, std::weak_ptr<const Cache::Model>> models;
	const ModelKey key{name, checksum};
	auto found = models.find(key);
	auto metadata = found == models.end() ? std::shared_ptr<const Cache::Model>{} : found->second.lock();
	if (!metadata || metadata->Parents.size() != size_t(count))
	{
		for (auto it = models.begin(); it != models.end();)
			if (it->second.expired()) it = models.erase(it); else ++it;
		if (models.size() >= 256 && models.find(key) == models.end())
		{
			status = "Scene model metadata limit reached; proxy retained.";
			return false;
		}
		auto next = std::make_shared<Cache::Model>();
		next->Name = name;
		next->Checksum = checksum;
		MirvTf2BoneName_t names[MIRV_TF2_MAX_BONES];
		if (Mirv_Tf2_ReadBoneNames(header, names) != count)
		{
			status = "Selected model has an invalid bone hierarchy.";
			return false;
		}
		int boneOffset;
		std::memcpy(&boneOffset, header + 160, 4);
		for (int i = 0; i < count; ++i)
		{
			next->Parents.push_back(names[i].parent);
			next->BoneNames.emplace_back(names[i].name);
			int flags;
			std::memcpy(&flags, header + boneOffset + i * 216 + 160, 4);
			next->Flags.push_back(flags);
		}
		for (int i = 0; i < count; ++i)
			if ((next->Flags[i] & 0x40000) && !next->BoneNames[i].empty() && next->BoneNames[i].size() < 63)
				next->MergeBones.push_back(i);
		std::sort(next->MergeBones.begin(), next->MergeBones.end(), [&](int a, int b) {
			return _stricmp(next->BoneNames[a].c_str(), next->BoneNames[b].c_str()) < 0;
		});
		metadata = std::move(next);
		models[key] = metadata;
	}
	auto pose = std::make_shared<Cache::Pose>();
	pose->ModelInfo = metadata;
	pose->Body = renderable->GetBody();
	// Use the existing Source/AGR transform helpers. Unused bones are identity,
	// exactly as in AGR; SetupBones fills the bones used by the studio renderer.
	alignas(16) SOURCESDK::matrix3x4_t bones[MIRV_TF2_MAX_BONES]{};
	auto globals = g_Hook_VClient_RenderView.GetGlobals();
	if (!globals || !renderable->SetupBones(bones, MIRV_TF2_MAX_BONES, SOURCESDK_BONE_USED_BY_ANYTHING,
	                                        globals->curtime_get()))
	{
		status = "SetupBones did not provide a pose; proxy bounds retained.";
		return false;
	}
	SOURCESDK::matrix3x4_t root;
	SOURCESDK::AngleMatrix(SOURCESDK::QAngle(entity.Angles[0], entity.Angles[1], entity.Angles[2]),
	                       SOURCESDK::Vector(entity.Origin[0], entity.Origin[1], entity.Origin[2]), root);
	std::memcpy(pose->Root.data(), root.m_flMatVal, sizeof(root));
	pose->Bones.resize(count);
	for (int i = 0; i < count; ++i)
	{
		if (!(metadata->Flags[i] & SOURCESDK_BONE_USED_BY_ANYTHING))
		{
			std::memset(bones[i].m_flMatVal, 0, sizeof(bones[i]));
			for (int axis = 0; axis < 3; ++axis)
				bones[i][axis][axis] = 1;
		}
		const auto &matrix = bones[i].m_flMatVal;
		const double axisLength2 = double(matrix[0][0]) * matrix[0][0] + double(matrix[1][0]) * matrix[1][0] +
		                           double(matrix[2][0]) * matrix[2][0];
		if (axisLength2 < 1e-12)
		{
			status = "Required bone matrix is unavailable; proxy bounds retained.";
			return false;
		}
		std::memcpy(pose->Bones[i].data(), bones[i].m_flMatVal, sizeof(bones[i]));
	}
	if (!Cache::ValidPose(*pose))
	{
		status = "Nonfinite or invalid pose rejected; proxy bounds retained.";
		return false;
	}
	ScenePrepareLocalPose(*pose);
	entity.ModelPose = std::move(pose);
	status = "Capturing " + metadata->Name + ": " + std::to_string(count) + " bones per observed tick.";
	return true;
}
bool Mirv_Tf2_DrawScenePose(const advancedfx::SceneCache::Entity &entity, std::string &status,
                            const advancedfx::SceneCache::Entity *owner)
{
	Mirv_Tf2_SceneRenderFrame();
	if (g_SceneOutstanding.load() >= SceneMaxOutstanding)
	{
		status = "Cached model render queue is behind; submission deferred.";
		return false;
	}

	const auto pose = entity.ModelPose;
	if (!pose || !advancedfx::SceneCache::ValidPose(*pose))
	{
		status = "Cached model pose is unavailable or invalid.";
		return false;
	}
	if (!g_AppSystemFactory)
	{
		status = "Source render interfaces unavailable.";
		return false;
	}
	// This function runs on the engine thread, just like Source's own model
	// submission. IStudioRender snapshots its state for queued drawing. HLAE's
	// render queue is used only to retire pinned resources after that draw.
	if (!MaterialSystem_CanExecuteOnRenderThread())
	{
		status = "Material-system render queue unavailable; cached model submission skipped.";
		return false;
	}
	void *studio = g_AppSystemFactory("VStudioRender025", nullptr);
	void *cache = g_AppSystemFactory("MDLCache004", nullptr);
	if (!studio || !cache)
	{
		status = "VStudioRender025 / MDLCache004 unavailable; cached boxes remain usable.";
		return false;
	}
	void *overrideMaterial = nullptr;
	int overrideType = 0;
	SceneInterfaceCall<void, void **, int *>(studio, 47, &overrideMaterial, &overrideType);
	if (overrideMaterial || overrideType != 0)
	{
		status = "Cached model deferred during an engine material override.";
		return false;
	}
	SceneInterfaceCall<void>(cache, 25); // IMDLCache::BeginLock
	struct Unlock
	{
		void *Cache;
		~Unlock()
		{
			SceneInterfaceCall<void>(Cache, 26);
		}
	} unlock{cache};
	const auto handle =
	    SceneInterfaceCall<unsigned short, const char *>(cache, 6, pose->ModelInfo->Name.c_str());
	if (handle == 0xffff)
	{
		status = "Cached model asset could not be resolved.";
		return false;
	}
	auto resources = std::make_unique<SceneModelResources>(cache, handle);
	SceneStudioInfo info;
	info.Header = SceneInterfaceCall<void *, unsigned short>(cache, 40, handle); // LockStudioHdr
	resources->State().HeaderLocked = info.Header != nullptr;
	info.Hardware = SceneInterfaceCall<void *, unsigned short>(cache, 11, handle);
	int count;
	unsigned int checksum;
	if (SceneInterfaceCall<bool, unsigned short>(cache, 20, handle) || !info.Hardware ||
	    !SceneStudioHeader(static_cast<const unsigned char *>(info.Header), count, checksum) ||
	    checksum != pose->ModelInfo->Checksum || size_t(count) != pose->ModelInfo->Parents.size())
	{
		status = "Cached pose does not match the loaded model asset, or hardware data is unavailable.";
		return false;
	}
	// Allocate through studio's render context so queued rendering owns the
	// matrix lifetime. Stack matrices and live renderable pointers are not passed.
	auto matrices = SceneInterfaceCall<SOURCESDK::matrix3x4_t *, int>(studio, 22, count);
	if (!matrices)
	{
		status = "Studio bone allocation unavailable.";
		return false;
	}
	const bool built = SceneBuildMatrices(entity, owner, matrices);
	SceneInterfaceCall<void>(studio, 23); // Source owns the matrices until its queued render finishes.
	if (!built)
	{
		status = "Cached bone reconstruction failed; model drawing skipped.";
		return false;
	}
	info.Body = pose->Body;
	void *material = SceneCategoryMaterial(entity.Category);
	if (material)
	{
		SceneInterfaceCall<void>(material, 12);
		resources->State().Material = material;
		SceneInterfaceCall<void, void *, int>(studio, 33, material, 0);
	}
	struct RestoreMaterial
	{
		void *Studio;
		void *Material;
		int Type;
		bool Changed;
		~RestoreMaterial()
		{
			if (Changed)
				SceneInterfaceCall<void, void *, int>(Studio, 33, Material, Type);
		}
	} restore{studio, overrideMaterial, overrideType, material != nullptr};
	const int flags = 0x04 | 0x20 | (material ? 0 : 0x80); // NO_FLEXES | NO_SHADOWS, fallback WIREFRAME
	const SOURCESDK::Vector origin(entity.Origin[0], entity.Origin[1], entity.Origin[2]);
	resources->Submit();
	SceneInterfaceCall<void, void *, const SceneStudioInfo &, SOURCESDK::matrix3x4_t *, float *, float *,
	                   const SOURCESDK::Vector &, int>(studio, 29, nullptr, info, matrices, nullptr, nullptr,
	                                                   origin, flags);
	// The capability check above and this enqueue run on the same engine thread;
	// the material-system hook remains stable for this submission. The helper
	// owns/deletes the callback on both success and failure.
	if (!MaterialSystem_ExecuteOnRenderThread(resources.release()))
	{
		status = "Cached model cleanup callback could not be submitted.";
		return false;
	}
	status = material ? "Drawing category-colored cached models."
	                  : "Drawing default cached wireframe; category material requires VMaterialSystem082.";
	return true;
}
void Mirv_Tf2_SceneRenderFrame()
{
	auto data = g_SceneCompleted.exchange(nullptr, std::memory_order_acquire);
	while (data)
	{
		auto next = data->Next;
		SceneReleaseResources(data);
		--g_SceneOutstanding;
		data = next;
	}
}
void Mirv_Tf2_SceneRenderStatus(unsigned &outstanding, unsigned &peak)
{
	outstanding = g_SceneOutstanding.load();
	peak = g_SceneOutstandingPeak.load();
}
void Mirv_Tf2_ReleaseSceneMaterials()
{
	for (auto &material : g_SceneMaterials)
		if (void *value = std::exchange(material, nullptr))
			SceneInterfaceCall<void>(value, 13);
}
// End selected-player scene-pose prototype.

// Scene entity enumeration shares the label classifier, without attaching tool
// recorders to every projectile. Network class metadata is read through the
// public IClientNetworkable::GetClientClass slot (2).
void Mirv_Tf2_CollectSceneEntities(bool projectiles, bool weapons, bool cosmetics, bool ragdolls, bool gibs,
                                   bool buildings, bool props, std::vector<MirvTf2SceneEntity> &out)
{
	out.clear();
	auto tools = CClientToolsTf2::Instance();
	auto iface = tools ? tools->GetClientTools() : nullptr;
	if (!iface)
		return;
	const int players = Mirv_Tf2_GetMaxPlayers();
	size_t scanned = 0;
	for (auto object = iface->FirstEntity(); object && scanned++ < 4096; object = iface->NextEntity(object))
	{
		const int index = iface->GetEntIndex(object);
		if (index == 0 || ((index < 0 || index >= 2048) && !ragdolls && !gibs && !weapons && !cosmetics &&
		                   !buildings && !props))
			continue;
		auto unknown = reinterpret_cast<CMirvTf2ClientUnknown *>(object);
		auto networkable = unknown->GetClientNetworkable();
		auto renderable = unknown->GetClientRenderable();
		const bool networked = index > 0 && index < 2048;
		if (!renderable || (networked && (!networkable || SceneInterfaceCall<bool>(networkable, 8))))
			continue;
		MirvTf2SceneEntity row;
		row.Index = index;
		row.ObservedObject = object;
		if (networked && index <= players && iface->IsPlayer(object))
		{
			const auto clientClass = SceneInterfaceCall<const void *>(networkable, 2);
			if (!renderable->ShouldDraw() || !Mirv_Tf2_ScenePlayerVisible(object, clientClass))
				continue; // Death leaves a valid player entity behind its separate ragdoll/gibs.

			int user = 0;
			if (!Mirv_Tf2_GetPlayerInfo(index, nullptr, 0, &user) || user <= 0)
				continue;
			row.Snapshot.Identity = static_cast<unsigned int>(user);
			row.Snapshot.Category = MIRV_TF2_ENTITY_PLAYER;
		}
		else
		{
			if ((!projectiles && !weapons && !cosmetics && !ragdolls && !gibs && !buildings && !props) ||
			    !renderable->ShouldDraw())
				continue;
			if (networked && iface->GetOwningWeaponEntIndex(index) > 0)
				continue;
			std::uint32_t handle = 0xffffffff;
			const void *reference = unknown->GetRefEHandle();
			if (reference)
				std::memcpy(&handle, reference, sizeof(handle));
			if (handle == 0xffffffff || !(handle & SOURCESDK_TF2_ENT_ENTRY_MASK) ||
			    (networked && (handle & SOURCESDK_TF2_ENT_ENTRY_MASK) != static_cast<unsigned>(index)))
				continue;
			const void *clientClass =
			    networkable ? SceneInterfaceCall<const void *>(networkable, 2) : nullptr;
			if (!Mirv_Tf2_SceneEntityVisible(object, clientClass))
				continue;
			const char *name = nullptr;
			if (clientClass)
				std::memcpy(&name, static_cast<const char *>(clientClass) + 2 * sizeof(void *), sizeof(name));
			if (name && !std::strcmp(name, "CTFWearableVM"))
				continue; // First-person arms/wearables.
			int category = Mirv_Tf2_LabelCategory(name, false, true);
			if (networked && category == MIRV_TF2_ENTITY_OTHER && name &&
			    (!strncmp(name, "CTF", 3) || !strncmp(name, "C_TF", 4)))
				category = Mirv_Tf2_ToolCategory(iface, object, index, false, true);

			if (!networked)
			{
				// A client-only entity has entindex -1, but its serial-bearing
				// client handle still indexes the tools lookup. Preserve AGR attachments.
				category = Mirv_Tf2_ToolCategory(iface, object, int(handle & SOURCESDK_TF2_ENT_ENTRY_MASK),
				                                 false, true);
			}
			bool looseModel = false;
			if (category < 0)
				continue;
			if ((gibs || weapons || cosmetics || buildings || props) && category == MIRV_TF2_ENTITY_OTHER)
			{
				// Generic dynamic/physics classes need a model-based category fallback.
				auto info = Mirv_Tf2_GetModelInfo();
				const void *model = renderable->GetModel();
				const char *modelName = info && model ? info->GetModelName(model) : nullptr;
				if (modelName)
				{
					char path[260];
					size_t i = 0;
					for (; modelName[i] && i + 1 < sizeof(path); ++i)
						path[i] = modelName[i] == '\\'
						              ? '/'
						              : char(tolower(static_cast<unsigned char>(modelName[i])));
					path[i] = 0;
					if (strstr(path, "/gibs/"))
						category = MIRV_TF2_ENTITY_GIB;
					else if (strstr(path, "/weapons/"))
						category = MIRV_TF2_ENTITY_WEAPON;
					else if (strstr(path, "/player/items/") || strstr(path, "/bots/items/"))
						category = MIRV_TF2_ENTITY_COSMETIC;
					else if (strstr(path, "/buildables/"))
						category = MIRV_TF2_ENTITY_BUILDING;
					else if (props && (info->GetModelType(model) == 1 || info->GetModelType(model) == 3))
						category = MIRV_TF2_ENTITY_PROP; // Brush or studio model; no sprites/particles/world.
					looseModel = !networked &&
					             (category == MIRV_TF2_ENTITY_WEAPON || category == MIRV_TF2_ENTITY_COSMETIC);
				}
			}
			if (!networked && category != MIRV_TF2_ENTITY_RAGDOLL && category != MIRV_TF2_ENTITY_GIB &&
			    category != MIRV_TF2_ENTITY_WEAPON && category != MIRV_TF2_ENTITY_COSMETIC &&
			    category != MIRV_TF2_ENTITY_BUILDING && category != MIRV_TF2_ENTITY_PROP)
				continue; // Unrecognized client-only props stay outside this pass.
			if (category == MIRV_TF2_ENTITY_BUILDING || category == MIRV_TF2_ENTITY_PROP)
			{
				if (!(category == MIRV_TF2_ENTITY_BUILDING ? buildings : props))
					continue;
				row.HasMoveParent = Mirv_Tf2_SceneMoveParent(object, clientClass, row.MoveParent);
			}
			else if (category == MIRV_TF2_ENTITY_RAGDOLL || category == MIRV_TF2_ENTITY_GIB)
			{
				if (!(category == MIRV_TF2_ENTITY_RAGDOLL ? ragdolls : gibs))
					continue;
			}
			else if (category == MIRV_TF2_ENTITY_PROJECTILE)
			{
				if (!projectiles)
					continue;
			}
			else
			{
				if ((!weapons && !cosmetics) || iface->GetOwningWeaponEntIndex(index) > 0)
					continue;
				// Visible third-person gear may be held, corpse-attached or loose.
				// Inventory weapons fail ShouldDraw; bounded traversal tolerates
				// nested ownership and malformed/cyclic owner chains.
				row.HasMoveParent = Mirv_Tf2_SceneMoveParent(object, clientClass, row.MoveParent);
				bool resolved = looseModel;
				auto owner = object;
				for (int depth = 0; !resolved && depth < 8; ++depth)
				{
					owner = iface->GetOwnerEntity(owner);
					if (!owner)
					{
						resolved = true;
						break;
					}
					if (owner == object)
						break;
					if (!iface->IsPlayer(owner))
						continue;
					int user = 0;
					resolved =
					    Mirv_Tf2_GetPlayerInfo(iface->GetEntIndex(owner), nullptr, 0, &user) && user > 0;
					if (resolved)
						row.Snapshot.OwnerIdentity = static_cast<unsigned int>(user);
					break;
				}
				if (!resolved)
					continue; // Cyclic/unbounded owner chain.
				if (row.HasMoveParent &&
				    (row.MoveParent == 0xffffffff || !(row.MoveParent & SOURCESDK_TF2_ENT_ENTRY_MASK)))
					row.Snapshot.OwnerIdentity = 0; // Detached even if gameplay ownership is retained.
				if (category == MIRV_TF2_ENTITY_OTHER)
					category = Mirv_Tf2_ToolCategory(iface, object, index, false);
				if (!((category == MIRV_TF2_ENTITY_WEAPON && weapons) ||
				      (category == MIRV_TF2_ENTITY_COSMETIC && cosmetics)))
					continue;
			}
			row.Snapshot.Identity = (std::uint64_t(1) << 63) | handle;
			row.Snapshot.Category = category;
		}
		const auto origin = static_cast<const float *>(renderable->GetRenderOrigin());
		const auto angles = static_cast<const float *>(renderable->GetRenderAngles());
		if (!origin || !angles)
			continue;
		bool valid = true;
		renderable->GetRenderBounds(row.Snapshot.Mins, row.Snapshot.Maxs);
		for (int axis = 0; axis < 3; ++axis)
		{
			row.Snapshot.Origin[axis] = origin[axis];
			row.Snapshot.Angles[axis] = angles[axis];
			valid &= std::isfinite(origin[axis]) && std::isfinite(angles[axis]) &&
			         std::isfinite(row.Snapshot.Mins[axis]) && std::isfinite(row.Snapshot.Maxs[axis]) &&
			         row.Snapshot.Mins[axis] <= row.Snapshot.Maxs[axis];
		}
		if (valid)
			out.push_back(std::move(row));
	}
	std::map<std::uint32_t, std::uint64_t> attachmentHosts;
	std::vector<std::uint64_t> owners;
	for (const auto &row : out)
		if (row.Snapshot.Category == MIRV_TF2_ENTITY_PLAYER ||
		    row.Snapshot.Category == MIRV_TF2_ENTITY_RAGDOLL ||
		    row.Snapshot.Category == MIRV_TF2_ENTITY_BUILDING ||
		    row.Snapshot.Category == MIRV_TF2_ENTITY_PROP)
		{
			owners.push_back(row.Snapshot.Identity);
			std::uint32_t handle = 0xffffffff;
			const void *reference =
			    reinterpret_cast<CMirvTf2ClientUnknown *>(row.ObservedObject)->GetRefEHandle();
			if (reference)
				std::memcpy(&handle, reference, sizeof(handle));
			if (handle != 0xffffffff)
				attachmentHosts.emplace(handle, row.Snapshot.Identity);
		}
	for (auto &row : out)
		if (row.HasMoveParent)
			if (auto parent = attachmentHosts.find(row.MoveParent); parent != attachmentHosts.end())
				row.Snapshot.OwnerIdentity = parent->second;
			else if (!row.Snapshot.OwnerIdentity && row.MoveParent != 0xffffffff &&
			         (row.MoveParent & SOURCESDK_TF2_ENT_ENTRY_MASK))
				row.Snapshot.OwnerIdentity =
				    (std::uint64_t(1) << 63) |
				    row.MoveParent; // Unretained attachment host: orphan filter suppresses it.
	std::sort(owners.begin(), owners.end());
	const auto orphan = [&](const MirvTf2SceneEntity &row) {
		return row.Snapshot.OwnerIdentity &&
		       !std::binary_search(owners.begin(), owners.end(), row.Snapshot.OwnerIdentity);
	};
	out.erase(std::remove_if(out.begin(), out.end(), orphan), out.end());
}
// End scene entity enumeration.
