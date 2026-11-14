#include "shared/CameraRig.h"
#include <cassert>
#include <limits>
#include <iostream>
using namespace advancedfx;
static bool near(double a, double b)
{
	return std::abs(a - b) < 1e-7;
}
int main()
{
	CameraRig rig;
	CameraPose target, world, out;
	world.Position[0] = 10;
	rig.Motion.OrbitSpeed = 90;
	rig.Motion.OrbitEpoch = 100;
	assert(rig.Attach(world, target, true, 100));
	assert(rig.Evaluate(&target, out, 101));
	assert(near(out.Position[0], 0) && near(out.Position[1], 10));
	world.Position[0] = 20;
	assert(rig.Attach(world, target, true, 101));
	assert(rig.Evaluate(&target, out, 101) && near(out.Position[0], 20) && near(out.Position[1], 0));
	assert(rig.Attach(world, target, false, 101));
	assert(rig.Evaluate(&target, out, 102));
	assert(near(out.Rotation.W, 1) && near(out.Position[0], 0) && near(out.Position[1], 20));
	CameraMotion settings;
	settings.FollowHalfLife = .1;
	settings.MaxDistance = 0;
	settings.TeleportDistance = 0;
	CameraFollowState follow;
	target.Position[0] = 0;
	assert(follow.Sample(settings, 0, target, out));
	target.Position[0] = 100;
	assert(follow.Sample(settings, .1, target, out) && near(out.Position[0], 50));
	assert(follow.Sample(settings, .1, target, out) && near(out.Position[0], 50));
	assert(follow.Sample(settings, .2, target, out) && near(out.Position[0], 75));
	settings.MaxDistance = 20;
	follow = {};
	target.Position[0] = 0;
	follow.Sample(settings, 0, target, out);
	target.Position[0] = 100;
	follow.Sample(settings, .1, target, out);
	assert(near(out.Position[0], 80));
	settings.TeleportDistance = 200;
	target.Position[0] = 1000;
	follow.Sample(settings, .2, target, out);
	assert(near(out.Position[0], 1000));
	target.Position[0] = 5;
	follow.Sample(settings, .05, target, out);
	assert(near(out.Position[0], 5));
	settings.RotationHalfLife = .1;
	follow = {};
	target.Rotation =
		Afx::Math::Quaternion::FromQREulerAngles(Afx::Math::QREulerAngles::FromQEulerAngles({0, 179, 0}));
	follow.Sample(settings, 0, target, out);
	target.Rotation =
		Afx::Math::Quaternion::FromQREulerAngles(Afx::Math::QREulerAngles::FromQEulerAngles({0, -179, 0}));
	follow.Sample(settings, .1, target, out);
	assert(near(std::abs(out.Rotation.ToQREulerAngles().ToQEulerAngles().Yaw), 180));
	settings.FollowHalfLife = std::numeric_limits<double>::quiet_NaN();
	assert(!settings.Valid() && !follow.Sample(settings, .2, target, out));
	std::cout << "Camera orbit, recapture, half-life, lag, teleport and shortest-angle checks passed\n";
}
