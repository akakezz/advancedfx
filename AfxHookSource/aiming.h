#pragma once

#include <shared/AfxMath.h>

#include "MirvCalcs.h"
#include <memory>

using namespace Afx::Math;

class Aiming
{
public:
  Aiming();
  Aiming(const Aiming &) = delete;
  Aiming &operator=(const Aiming &) = delete;
  std::shared_ptr<Aiming> CloneSettings() const;
  std::shared_ptr<Aiming> CloneState() const;
  bool SameSettings(const Aiming &other) const;
  void Reset(double pitch, double yaw, double roll);
  const Vector3 &GetTargetPoint() const { return m_TargetPoint; }
  bool HasTf2EntitySource() const { return m_Tf2EntitySource; }

  bool Aim(double deltaT, Vector3 const camOrigin, double &yPitch, double &zYaw, double &xRoll,
		   const Vector3 *targetOverride = nullptr);

  const Vector3 &GetLastTargetOrigin() const { return LastTargetOrigin; }

  void TargetPoint(Vector3 origin);
  void TargetPointFromLast(Vector3 offset);

  bool Active;
  bool SoftDeactivate;

  Vector3 OffSet;

  bool SnapTo;
  double LimitVelocity;
  double LimitAcceleration;
  // double LimitJerk;

  enum Origin_e
  {
	  O_Net,
	  O_View
  } Origin;

	enum Angles_e {
		A_Net,
		A_View
	} Angles;

	enum Up_e {
		U_Input,
		U_World
	} Up;

	int EntityIndex = -1;

	void RebuildCalc(void);

	~Aiming()
	{
		Source_set(0);
	}

	IMirvVecAngCalc * Source_get(void)
	{
		return m_Source;
	}

	void Source_set(IMirvVecAngCalc * value)
	{
		m_Tf2EntitySource = false;
		if (value)
			value->AddRef();
		if (m_Source) m_Source->Release();
		m_Source = value;
	}

private:
  IMirvVecAngCalc *m_Source = 0;
  bool m_Tf2EntitySource = false;
  Vector3 LastTargetOrigin;
  Vector3 m_TargetPoint{0, 0, 0};
  double m_YPitchVelocity;
  double m_ZYawVelocity;
  double m_XRollVelocity;
  double LastYPitch;
  double LastZYaw;
  double LastXRoll;
  bool m_Deactivating;

  void CalcSmooth(double deltaT, double targetPos, double &lastPos, double &lastVel);
};

extern Aiming g_Aiming;
