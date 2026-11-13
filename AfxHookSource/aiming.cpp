#include "stdafx.h"

#include "aiming.h"

#include "SourceInterfaces.h"
#include "addresses.h"
#include "WrpConsole.h"

#include <algorithm>

#define _USE_MATH_DEFINES
#include <math.h>

Aiming g_Aiming;

void LookAnglesFromTo(Vector3 const &from, Vector3 const &to, double fallBackPitch, double fallBackYaw, double & outPitch, double & outYaw)
{
	Vector3 dir = to - from;

	// Store then zero height
	double dz = dir.Z;
	
	dir.Z = 0;

	// Need this for later
	double length = dir.Length();

	if(length <= AFX_MATH_EPS)
	{
		outPitch = fabs(dz) > AFX_MATH_EPS ? (dz > 0 ? -90 : 90) : fallBackPitch;
		outYaw = fallBackYaw;
		return;
	}

	dir = dir.Normalize();

	// This is our forward angle
	Vector3 vForward(1.0, 0.0, 0.0);

	double dot_product = (dir.X * vForward.X) + (dir.Y * vForward.Y) + (dir.Z * vForward.Z);

	double angle = acos((std::max)(-1.0, (std::min)(1.0, dot_product))) * 180.0f / (float)M_PI;

	if (dir.Y < 0)
		angle = 360.0 - angle;

	double pitch = atan2(dz, length) * 180.0f / (float)M_PI;

	outYaw = angle;
	outPitch = -pitch;
}

Aiming::Aiming()
: Active(false)
, SoftDeactivate(false)
, OffSet(0.0,0.0,0.0)
, SnapTo(true)
, LimitVelocity(360)
, LimitAcceleration(90)
//, LimitJerk(0.01)
, Origin(O_View)
, Angles(A_Net)
, Up(U_World)
, m_YPitchVelocity(0)
, m_ZYawVelocity(0)
, m_XRollVelocity(0)
, LastTargetOrigin(0, 0, 0)
, LastYPitch(0)
, LastZYaw(0)
, LastXRoll(0)
, m_Deactivating(false)
{

}

std::shared_ptr<Aiming> Aiming::CloneSettings() const
{
	auto result = std::make_shared<Aiming>();
	result->Active = Active;
	result->SoftDeactivate = SoftDeactivate;
	result->OffSet = OffSet;
	result->SnapTo = SnapTo;
	result->LimitVelocity = LimitVelocity;
	result->LimitAcceleration = LimitAcceleration;
	result->Origin = Origin;
	result->Angles = Angles;
	result->Up = Up;
	result->EntityIndex = EntityIndex;
	result->LastTargetOrigin = m_TargetPoint;
	result->m_TargetPoint = m_TargetPoint;
	result->Source_set(m_Source);
	result->m_Tf2EntitySource = m_Tf2EntitySource;
	return result;
}
std::shared_ptr<Aiming> Aiming::CloneState() const
{
	auto result = CloneSettings();
	result->LastTargetOrigin = LastTargetOrigin;
	result->LastYPitch = LastYPitch;
	result->LastZYaw = LastZYaw;
	result->LastXRoll = LastXRoll;
	result->m_YPitchVelocity = m_YPitchVelocity;
	result->m_ZYawVelocity = m_ZYawVelocity;
	result->m_XRollVelocity = m_XRollVelocity;
	result->m_Deactivating = m_Deactivating;
	return result;
}
bool Aiming::SameSettings(const Aiming &other) const
{
	return Active == other.Active && SoftDeactivate == other.SoftDeactivate && SnapTo == other.SnapTo &&
		   OffSet.X == other.OffSet.X && OffSet.Y == other.OffSet.Y && OffSet.Z == other.OffSet.Z &&
		   LimitVelocity == other.LimitVelocity && LimitAcceleration == other.LimitAcceleration &&
		   Origin == other.Origin && Angles == other.Angles && Up == other.Up && EntityIndex == other.EntityIndex &&
		   m_Source == other.m_Source &&
		   (m_Source || (m_TargetPoint.X == other.m_TargetPoint.X && m_TargetPoint.Y == other.m_TargetPoint.Y &&
						 m_TargetPoint.Z == other.m_TargetPoint.Z));
}
void Aiming::Reset(double pitch, double yaw, double roll)
{
	LastYPitch = pitch;
	LastZYaw = yaw;
	LastXRoll = roll;
	m_YPitchVelocity = m_ZYawVelocity = m_XRollVelocity = 0;
	m_Deactivating = false;
}

void Aiming::RebuildCalc(void)
{
	if (-1 != EntityIndex)
	{
		if (SourceSdkVer_TF2 == g_SourceSdkVer || SourceSdkVer_TF2Classified == g_SourceSdkVer)
		{
			// TF2: use IClientTools (abs origin / abs angles only).
			Source_set(g_MirvVecAngCalcs.NewTf2EntityIndexCalc(0, EntityIndex));
			m_Tf2EntitySource = m_Source != nullptr;
			return;
		}

#ifndef _WIN64
		if (!SOURCESDK::g_Entitylist_csgo)
		{
			Tier0_Warning("mirv_aim entityIndex: Not supported for your engine!\n");
			Source_set(0);
			return;
		}

		IMirvHandleCalc * handleCalc = g_MirvHandleCalcs.NewIndexCalc(0, EntityIndex);
		if (handleCalc)
		{
			handleCalc->AddRef();

			Source_set(
				g_MirvVecAngCalcs.NewHandleCalcEx(0, handleCalc, O_View == Origin, A_View == Angles)
			);

			handleCalc->Release();
		}
#else
		Tier0_Warning("mirv_aim entityIndex: Not supported for your engine!\n");
		Source_set(0);
#endif //#ifndef _WIN64
	}
	else
		Source_set(0);
}

bool Aiming::Aim(double deltaT, Vector3 const camOrigin, double &yPitch, double &zYaw, double &xRoll,
				 const Vector3 *targetOverride)
{
	if(deltaT <= 0)
	{
		return false;
	}

	bool camRotationChanged = false;

	m_Deactivating = Active || m_Deactivating && SoftDeactivate;

	double targetYPitch = yPitch;
	double targetZYaw = zYaw;
	double targetXRoll = xRoll;

	if(Active)
	{
		if (targetOverride)
			LastTargetOrigin = *targetOverride;
		else if (m_Source)
		{

			SOURCESDK::Vector o;
			SOURCESDK::QAngle a;

			if (m_Source->CalcVecAng(o, a))
			{
				double forward[3], right[3], up[3];

				MakeVectors(a.z, a.x, a.y, forward, right, up);

				LastTargetOrigin = Vector3(
					o.x + OffSet.X * forward[0] + OffSet.Y * right[0] + OffSet.Z * up[0],
					o.y + OffSet.X * forward[1] + OffSet.Y * right[1] + OffSet.Z * up[1],
					o.z + OffSet.X * forward[2] + OffSet.Y * right[2] + OffSet.Z * up[2]
				);
			}
		}
		else
			LastTargetOrigin = m_TargetPoint;
	}
	else
	{
		LastTargetOrigin = camOrigin;
	}

	if(Active || m_Deactivating)
	{
		if(Active)
		{
			LookAnglesFromTo(camOrigin, LastTargetOrigin, LastYPitch, LastZYaw, targetYPitch, targetZYaw);
			targetXRoll = U_World == Up ? 0 : targetXRoll;
		}

		if(SnapTo)
		{
			yPitch = targetYPitch;
			zYaw = targetZYaw;
			xRoll = targetXRoll;
			camRotationChanged = true;
			m_Deactivating = false;
		}
		else
		{
			double reaimYPitch = targetYPitch -LastYPitch;
			double reaimZYaw = targetZYaw -LastZYaw;
			double reaimXRoll = targetXRoll -LastXRoll;

			// Force reaim angles to be in [-180°, 180°)

			reaimYPitch = AngleModDeg(reaimYPitch);
			reaimZYaw = AngleModDeg(reaimZYaw);
			reaimXRoll = AngleModDeg(reaimXRoll);

			m_Deactivating = Active || abs(reaimYPitch) > AFX_MATH_EPS || abs(reaimZYaw) > AFX_MATH_EPS || abs(reaimXRoll) > AFX_MATH_EPS;

			// apply re-aiming:

			//Tier0_Msg("+++pitch+++\n");
			CalcSmooth(deltaT, LastYPitch +reaimYPitch, LastYPitch, m_YPitchVelocity);
			yPitch = LastYPitch;

			//Tier0_Msg("+++yaw+++\n");
			CalcSmooth(deltaT, LastZYaw +reaimZYaw, LastZYaw, m_ZYawVelocity);
			zYaw = LastZYaw;

			//Tier0_Msg("+++roll+++\n");
			CalcSmooth(deltaT, LastXRoll +reaimXRoll, LastXRoll, m_XRollVelocity);
			xRoll = LastXRoll;

			camRotationChanged = true;
		}
	}
	else
	{
		m_YPitchVelocity = 0;
		m_ZYawVelocity = 0;
		m_XRollVelocity = 0;
	}

	// Force remembered angels to be in [-180°, 180°)

	LastYPitch = AngleModDeg(yPitch);
	LastZYaw = AngleModDeg(zYaw);
	LastXRoll = AngleModDeg(xRoll);

	return camRotationChanged;
}

void Aiming::TargetPoint(Vector3 origin)
{
	Source_set(0);
	m_TargetPoint = origin;
	LastTargetOrigin = origin;
}

void Aiming::TargetPointFromLast(Vector3 offset)
{
	TargetPoint(LastTargetOrigin +offset);
}

void Aiming::CalcSmooth(double deltaT, double targetPos, double & lastPos, double & lastVel)
{
	::CalcSmooth(deltaT, targetPos, lastPos, lastVel, LimitVelocity, LimitAcceleration);
}
