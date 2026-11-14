#include "shared/CameraRig.h"
#include <cassert>
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
	// A captured calculator can contain the whole offset, leaving the local rig at zero.
	target.Position[0] = 30;
	world = target;
	const double physicalPivot[3] = {0, 0, 0};
	assert(rig.Attach(world, target, true, 100, physicalPivot));
	assert(std::abs(rig.Pose.Position[0]) < 1e-7);
	assert(rig.Evaluate(&target, out, 101, physicalPivot));
	assert(near(out.Position[0], 0) && near(out.Position[1], 30));
	std::cout << "Camera orbit, physical pivot and recapture checks passed\n";
}
