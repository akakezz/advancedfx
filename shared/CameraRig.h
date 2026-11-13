#pragma once
#include "AfxMath.h"
#include <cmath>

namespace advancedfx
{
// Common output, independent of Source interfaces, keyframes and UI state.
struct CameraPose
{
	double Position[3]{};
	Afx::Math::Quaternion Rotation{1, 0, 0, 0};
	double Fov = 90;
	bool Valid() const
	{
		return std::isfinite(Position[0]) && std::isfinite(Position[1]) && std::isfinite(Position[2]) &&
		       std::abs(Position[0]) <= 1048576 && std::abs(Position[1]) <= 1048576 &&
		       std::abs(Position[2]) <= 1048576 && std::isfinite(Rotation.W) && std::isfinite(Rotation.X) &&
		       std::isfinite(Rotation.Y) && std::isfinite(Rotation.Z) && std::isfinite(Rotation.Norm()) &&
		       Rotation.Norm() > 1e-12 && std::isfinite(Fov) && Fov >= 1 && Fov <= 179;
	}
};
struct CameraRig
{
	CameraPose Pose;
	bool Attached = false;
	bool InheritRotation = true;
	bool Evaluate(const CameraPose *target, CameraPose &out) const
	{
		if (!Pose.Valid() || (Attached && (!target || !target->Valid())))
			return false;
		auto result = Pose;
		if (Attached)
		{
			double rotation[3][3];
			target->Rotation.Normalized().ToRotationMatrix(rotation);
			for (int r = 0; r < 3; ++r)
			{
				result.Position[r] = target->Position[r];
				for (int c = 0; c < 3; ++c)
					result.Position[r] += rotation[r][c] * Pose.Position[c];
			}
			if (InheritRotation)
				result.Rotation = (target->Rotation.Normalized() * Pose.Rotation).Normalized();
		}
		result.Rotation = result.Rotation.Normalized();
		if (!result.Valid())
			return false;
		out = result;
		return true;
	}
	// Capture the current world pose relative to a target without a visual jump.
	bool Attach(const CameraPose &world, const CameraPose &target, bool inheritRotation)
	{
		if (!world.Valid() || !target.Valid())
			return false;
		auto local = world;
		double rotation[3][3];
		target.Rotation.Normalized().ToRotationMatrix(rotation);
		for (int r = 0; r < 3; ++r)
		{
			local.Position[r] = 0;
			for (int c = 0; c < 3; ++c)
				local.Position[r] += rotation[c][r] * (world.Position[c] - target.Position[c]);
		}
		if (inheritRotation)
			local.Rotation = (target.Rotation.Normalized().Conjugate() * world.Rotation).Normalized();
		if (!local.Valid())
			return false;
		Pose = local;
		Attached = true;
		InheritRotation = inheritRotation;
		return true;
	}
};
} // namespace advancedfx
