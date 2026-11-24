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
	CameraInfo camera;
	assert(CameraCollisionGuideLines(camera).empty());
	camera.CollisionDebug = true;
	camera.CollisionRadius = 6;
	camera.CollisionPosition = {1, 2, 3};
	camera.CollisionDesired = {10, 20, 30};
	auto lines = CameraCollisionGuideLines(camera);
	assert(lines.size() == 14);
	assert(lines[0].A == camera.CollisionDesired && lines[0].B == camera.CollisionPosition);
	for (size_t i = 2; i < lines.size(); ++i)
		for (int a = 0; a < 3; ++a)
			assert(std::abs(lines[i].A[a] - camera.CollisionPosition[a]) == 6 &&
				   std::abs(lines[i].B[a] - camera.CollisionPosition[a]) == 6);
	camera.CollisionContact = true;
	camera.CollisionHit = {4, 5, 6};
	camera.CollisionNormal = {0, 0, 1};
	lines = CameraCollisionGuideLines(camera);
	assert(lines.size() == 15 && near(lines.back().B[2], 18));
	std::cout << "Camera orbit, physical pivot and recapture checks passed\n";
}
