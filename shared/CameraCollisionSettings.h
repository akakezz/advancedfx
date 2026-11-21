#pragma once
#include <cmath>
#include <tuple>

// One settings value shared by legacy, free/path collision and saved-camera overrides.
struct MirvCameraCollisionSettings
{
	bool Free = false, Campath = false, Attached = false, Players = true, Slide = true, IgnoreLocal = true;
	double SubjectClearance = 0, NearPlane = 0; // Attached anchor distance and optional frustum protection.
	bool Debug = false;
	double Radius = 6, ReturnHalfLife = .2, ReturnSpeed = 300, FollowHalfLife = 0, FollowMaxDistance = 128;
	double BoneHalfLife = 0, FollowTeleportDistance = 1024, AttachedSoftDistance = 24;
	double OrbitHalfLife = .15, OrbitSpeed = 360, FollowRecoveryDelay = 1;
	bool PreserveFraming = false, AimOwnsAngles = false;
	double FramingMargin = .1, FrameFov = 90, FrameAspect = 16.0 / 9;
	double BonePositionInfluence = 1, BoneRotationInfluence = 1, BonePositionLimit = 0, BoneRotationLimit = 0;

	MirvCameraCollisionSettings ForAttachment() const
	{
		MirvCameraCollisionSettings v;
		v.Attached = Attached;
		v.Players = Players;
		v.Slide = Slide;
		v.IgnoreLocal = IgnoreLocal;
		v.Debug = Debug;
		v.Radius = Radius;
		v.ReturnHalfLife = ReturnHalfLife;
		v.ReturnSpeed = ReturnSpeed;
		v.AttachedSoftDistance = AttachedSoftDistance;
		v.SubjectClearance = SubjectClearance;
		v.NearPlane = NearPlane;
		v.FollowRecoveryDelay = FollowRecoveryDelay;
		return v;
	}
	bool Valid() const
	{
		const auto &settings = *this;
		return !(!std::isfinite(settings.SubjectClearance) || settings.SubjectClearance < 0 ||
				 settings.SubjectClearance > 256 || !std::isfinite(settings.NearPlane) ||
				 settings.NearPlane < 0 || settings.NearPlane > 32 || !std::isfinite(settings.Radius) ||
				 settings.Radius < 1 || settings.Radius > 64 || !std::isfinite(settings.ReturnHalfLife) ||
				 settings.ReturnHalfLife < 0 || settings.ReturnHalfLife > 10 ||
				 !std::isfinite(settings.ReturnSpeed) || settings.ReturnSpeed < 0 ||
				 settings.ReturnSpeed > 1e6 || !std::isfinite(settings.FollowHalfLife) ||
				 settings.FollowHalfLife < 0 || settings.FollowHalfLife > 10 ||
				 !std::isfinite(settings.FollowMaxDistance) || settings.FollowMaxDistance < 1 ||
				 settings.FollowMaxDistance > 10000 || !std::isfinite(settings.BoneHalfLife) ||
				 settings.BoneHalfLife < 0 || settings.BoneHalfLife > 10 ||
				 !std::isfinite(settings.FollowTeleportDistance) || settings.FollowTeleportDistance < 0 ||
				 settings.FollowTeleportDistance > 1e6 || !std::isfinite(settings.OrbitHalfLife) ||
				 settings.OrbitHalfLife < 0 || settings.OrbitHalfLife > 10 ||
				 !std::isfinite(settings.OrbitSpeed) || settings.OrbitSpeed < 0 ||
				 settings.OrbitSpeed > 1e6 || !std::isfinite(settings.FollowRecoveryDelay) ||
				 settings.FollowRecoveryDelay < 0 || settings.FollowRecoveryDelay > 60 ||
				 !std::isfinite(settings.AttachedSoftDistance) || settings.AttachedSoftDistance < 0 ||
				 settings.AttachedSoftDistance > 256);
	}
	bool operator==(const MirvCameraCollisionSettings &v) const
	{
		return std::tie(Free, Campath, Attached, Players, Slide, IgnoreLocal, SubjectClearance, NearPlane,
						Debug, Radius, ReturnHalfLife, ReturnSpeed, FollowHalfLife, FollowMaxDistance,
						BoneHalfLife, FollowTeleportDistance, AttachedSoftDistance, OrbitHalfLife, OrbitSpeed,
						FollowRecoveryDelay, PreserveFraming, AimOwnsAngles, FramingMargin, FrameFov,
						FrameAspect, BonePositionInfluence, BoneRotationInfluence, BonePositionLimit,
						BoneRotationLimit) ==
			   std::tie(v.Free, v.Campath, v.Attached, v.Players, v.Slide, v.IgnoreLocal, v.SubjectClearance,
						v.NearPlane, v.Debug, v.Radius, v.ReturnHalfLife, v.ReturnSpeed, v.FollowHalfLife,
						v.FollowMaxDistance, v.BoneHalfLife, v.FollowTeleportDistance, v.AttachedSoftDistance,
						v.OrbitHalfLife, v.OrbitSpeed, v.FollowRecoveryDelay, v.PreserveFraming,
						v.AimOwnsAngles, v.FramingMargin, v.FrameFov, v.FrameAspect, v.BonePositionInfluence,
						v.BoneRotationInfluence, v.BonePositionLimit, v.BoneRotationLimit);
	}
	bool operator!=(const MirvCameraCollisionSettings &v) const
	{
		return !(*this == v);
	}
};
