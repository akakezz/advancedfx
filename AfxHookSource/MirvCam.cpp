#include "stdafx.h"

#include "MirvCam.h"

#include "WrpVEngineClient.h"
#include "WrpConsole.h"

#include "RenderView.h"
#include "addresses.h"

#include <shared/AfxMath.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <ctype.h>
#include <stdlib.h>
#include <string>
#include <map>
#include <memory>
#include "CameraOverlay.h"
#include <shared/MirvInput.h>

// How mirv_cam attach gets the angles when a bone is used.
static int g_MirvCamAttachMode = MIRV_TF2_BONEMODE_ALIGNED;

// The calc made by the last mirv_cam attach (used by mirv_cam attach capture).
static IMirvVecAngCalc * g_MirvCamAttachCalc = 0;

static void MirvCam_SetAttachCalc(IMirvVecAngCalc * calc)
{
	if (calc) calc->AddRef();
	if (g_MirvCamAttachCalc) g_MirvCamAttachCalc->Release();
	g_MirvCamAttachCalc = calc;
	MirvCameraAttachedReset();
}

static bool MirvCam_ReleaseLiveAttachment()
{
	const bool active = g_MirvCamAttachCalc && g_MirvCam.Source_get() == g_MirvCamAttachCalc;
	if (active) g_MirvCam.Source_set(nullptr);
	MirvCam_SetAttachCalc(nullptr);
	return active;
}

static char const * MirvCam_AttachModeName(int mode)
{
	return MIRV_TF2_BONEMODE_WORLD == mode ? "world" :
		(MIRV_TF2_BONEMODE_ENTITY == mode ? "entity" :
		(MIRV_TF2_BONEMODE_ALIGNED == mode ? "aligned" : "bone"));
}


bool CMirvCam::ApplySource(float & x, float & y, float & z, float & xRotation, float & yRotation, float & zRotation)
{
	bool overriden = false;

	if (m_Source)
	{
		SOURCESDK::Vector o;
		SOURCESDK::QAngle a;

		if (m_Source->CalcVecAng(o, a))
		{
			if (m_SourceUseX)
			{
				overriden = true;
				x = o.x;
			}
			if (m_SourceUseY)
			{
				overriden = true;
				y = o.y;
			}
			if (m_SourceUseZ)
			{
				overriden = true;
				z = o.z;
			}

			if (m_SourceUseXRotation)
			{
				overriden = true;
				xRotation = a.z;
			}
			if (m_SourceUseYRotation)
			{
				overriden = true;
				yRotation = a.x;
			}
			if (m_SourceUseZRotation)
			{
				overriden = true;
				zRotation = a.y;
			}
		}
	}

	return overriden;
}

bool CMirvCam::ApplyOffset(float & x, float & y, float & z, float & xRotation, float & yRotation, float & zRotation)
{
	bool overriden = false;

	if (m_OffsetForwad || m_OffsetLeft || m_OffsetUp)
	{
		overriden = true;

		double forward[3], right[3], up[3];

		Afx::Math::MakeVectors(xRotation, yRotation, zRotation, forward, right, up);

		x = (float)(x + m_OffsetForwad*forward[0] - m_OffsetLeft*right[0] + m_OffsetUp*up[0]);
		y = (float)(y + m_OffsetForwad*forward[1] - m_OffsetLeft*right[1] + m_OffsetUp*up[1]);
		z = (float)(z + m_OffsetForwad*forward[2] - m_OffsetLeft*right[2] + m_OffsetUp*up[2]);
	}

	if (m_OffsetForwardRot || m_OffseLeftRot || m_OffsetUpRot)
	{
		overriden = true;

		Afx::Math::Quaternion q1 = Afx::Math::Quaternion::FromQREulerAngles(Afx::Math::QREulerAngles::FromQEulerAngles(Afx::Math::QEulerAngles(yRotation, zRotation, xRotation)));
		Afx::Math::Quaternion q2 = Afx::Math::Quaternion::FromQREulerAngles(Afx::Math::QREulerAngles::FromQEulerAngles(Afx::Math::QEulerAngles(m_OffseLeftRot, m_OffsetUpRot, m_OffsetForwardRot)));

		Afx::Math::QEulerAngles angs = (q1 * q2).ToQREulerAngles().ToQEulerAngles();

		xRotation = (float)angs.Roll;
		yRotation = (float)angs.Pitch;
		zRotation = (float)angs.Yaw;
	}

	return overriden;
}

bool CMirvCam::ApplyFov(float & fov)
{
	bool overriden = false;

	if (m_Fov)
	{
		float tFov;

		if (m_Fov->CalcFov(tFov))
		{
			overriden = true;
			fov = tFov;
		}
	}

	return overriden;
}

void CMirvCam::RebuildCalc(void)
{
#ifndef _WIN64
	IMirvHandleCalc * handleCalc = g_MirvHandleCalcs.NewValueCalc(0, m_SourceHandle.ToInt());
	if (handleCalc)
	{
		handleCalc->AddRef();

		if (!m_SourceAttachment.empty())
		{
			Source_set(
				g_MirvVecAngCalcs.NewHandleAttachmentCalc(0, handleCalc, m_SourceAttachment.c_str())
			);
		}
		else
		{
			Source_set(
				g_MirvVecAngCalcs.NewHandleCalcEx(0, handleCalc, O_View == m_SourceOrigin, A_View == m_SourceAngles)
			);
		}

		handleCalc->Release();
	}
#else
	Tier0_Warning("mirv_cam source handle is not supported for 64-bit.\n");
#endif //#ifndef _WIN64
}

CMirvCam g_MirvCam;

int MirvCam_GetAttachMode()
{
	return g_MirvCamAttachMode;
}

bool MirvCam_GetAttachAnchor(double origin[3], void *&entity, const void *&identity, double entityOrigin[3], int *mode)
{
	if (!g_MirvCamAttachCalc || g_MirvCam.Source_get() != g_MirvCamAttachCalc ||
		!g_MirvCam.m_SourceUseX || !g_MirvCam.m_SourceUseY || !g_MirvCam.m_SourceUseZ)
		return false;
	identity = g_MirvCamAttachCalc;
	return Mirv_Tf2_GetAttachAnchor(g_MirvCamAttachCalc, origin, entity, entityOrigin, mode);
}

// Camera collision console command.
static void MirvCameraCollisionCommand(IWrpCommandArgs *args)
{
	auto settings = MirvCameraCollisionGet();
	const int argc = args->ArgC();
	bool changed = false, valid = true;
	if (argc == 3 && !_stricmp(args->ArgV(2), "reset"))
	{
		MirvCameraCollisionReset();
		return;
	}
	if (argc == 4)
	{
		const char *field = args->ArgV(2), *value = args->ArgV(3);
		const bool boolean = !std::strcmp(value, "0") || !std::strcmp(value, "1");
		if (!_stricmp(field, "free") || !_stricmp(field, "campath") || !_stricmp(field, "players") ||
		    !_stricmp(field, "attached") || !_stricmp(field, "ignoreLocal"))
		{
			valid = boolean;
			if (!_stricmp(field, "free"))
				settings.Free = !std::strcmp(value, "1");
			else if (!_stricmp(field, "campath"))
				settings.Campath = !std::strcmp(value, "1");
			else if (!_stricmp(field, "attached"))
				settings.Attached = !std::strcmp(value, "1");
			else if (!_stricmp(field, "ignoreLocal"))
				settings.IgnoreLocal = !std::strcmp(value, "1");
			else
				settings.Players = !std::strcmp(value, "1");
		}
		else if (!_stricmp(field, "radius") || !_stricmp(field, "returnHalfLife") ||
		         !_stricmp(field, "returnSpeed") || !_stricmp(field, "attachedSoftDistance"))
		{
			char *end = nullptr;
			double number = std::strtod(value, &end);
			if (!_stricmp(field, "radius"))
				settings.Radius = number;
			else if (!_stricmp(field, "returnHalfLife"))
				settings.ReturnHalfLife = number;
			else if (!_stricmp(field, "attachedSoftDistance"))
				settings.AttachedSoftDistance = number;
			else
				settings.ReturnSpeed = number;
			valid = end != value && !*end;
		}
		else if (!_stricmp(field, "response"))
		{
			valid = !_stricmp(value, "stop") || !_stricmp(value, "slide");
			settings.Slide = !_stricmp(value, "slide");
		}
		else
			valid = false;
		changed = valid;
	}
	else if (argc != 2 && !(argc == 3 && !_stricmp(args->ArgV(2), "status")))
		valid = false;
	if (!valid || (changed && !MirvCameraCollisionSet(settings)))
		Tier0_Warning("Camera collision edit rejected. TF2/TF2C EngineTraceClient003 is required when "
		              "enabled; radius 1..64, return half-life 0..10 s, speed 0..1000000; all values must be "
		              "finite; attachedSoftDistance 0..256 units.\n");
	settings = MirvCameraCollisionGet();
	Tier0_Msg("Camera collision: free %d, campath %d, radius %.2f, players %d, response %s.\n", settings.Free,
	          settings.Campath, settings.Radius, settings.Players, settings.Slide ? "slide" : "stop");
	Tier0_Msg(
	    "Attachment collision %d; ignore local %d; return half-life %.3f s, maximum speed %.1f units/s.\n",
	    settings.Attached, settings.IgnoreLocal, settings.ReturnHalfLife, settings.ReturnSpeed);
	Tier0_Msg("Attached collision soft zone %.1f units (0 = disabled).\n", settings.AttachedSoftDistance);
	if (argc == 2 || !valid)
		Tier0_Msg("mirv_cam collision free|campath|attached|players|ignoreLocal 0|1\n"
		          "mirv_cam collision radius <1..64> | response stop|slide | returnHalfLife <0..10> | "
		          "returnSpeed <0..1000000> | attachedSoftDistance <0..256> | reset | status\n"
		          "All collision switches default off. Local player is ignored by default; other "
		          "client-solid player "
		          "hulls are optional.\n"
		          "Correction changes only the live view. Deeply embedded starts may require disabling "
		          "collision and moving clear.\n");
}
static void MirvCameraAttachFollowCommand(IWrpCommandArgs *args)
{
	auto settings = MirvCameraCollisionGet();
	const int argc = args->ArgC();
	bool changed = false, valid = true;
	if (argc == 4 && !_stricmp(args->ArgV(3), "reset"))
	{
		MirvCameraAttachedReset();
		return;
	}
	if ((argc == 4 && _stricmp(args->ArgV(3), "status")) ||
	    (argc == 5 &&
	     (!_stricmp(args->ArgV(3), "maxDistance") || !_stricmp(args->ArgV(3), "boneHalfLife") ||
	      !_stricmp(args->ArgV(3), "teleportDistance") || !_stricmp(args->ArgV(3), "orbitHalfLife") ||
	      !_stricmp(args->ArgV(3), "orbitSpeed") || !_stricmp(args->ArgV(3), "recoveryDelay"))))
	{
		const char *value = args->ArgV(argc - 1);
		char *end = nullptr;
		double number = std::strtod(value, &end);
		valid = end != value && !*end;
		if (argc == 4)
			settings.FollowHalfLife = number;
		else if (!_stricmp(args->ArgV(3), "boneHalfLife"))
			settings.BoneHalfLife = number;
		else if (!_stricmp(args->ArgV(3), "teleportDistance"))
			settings.FollowTeleportDistance = number;
		else if (!_stricmp(args->ArgV(3), "orbitHalfLife"))
			settings.OrbitHalfLife = number;
		else if (!_stricmp(args->ArgV(3), "orbitSpeed"))
			settings.OrbitSpeed = number;
		else if (!_stricmp(args->ArgV(3), "recoveryDelay"))
			settings.FollowRecoveryDelay = number;
		else
			settings.FollowMaxDistance = number;
		changed = valid;
	}
	else if (argc != 3 && !(argc == 4 && !_stricmp(args->ArgV(3), "status")))
		valid = false;
	if (!valid || (changed && !MirvCameraCollisionSet(settings)))
		Tier0_Warning("Attachment follow edit rejected: requires TF2/TF2C, finite half-life 0..10 s and "
		              "maxDistance 1..10000 units, boneHalfLife/orbitHalfLife 0..10 s, teleportDistance "
		              "0..1000000 units, "
		              "orbitSpeed 0..1000000 deg/s, recoveryDelay 0..60 s.\n");
	settings = MirvCameraCollisionGet();
	Tier0_Msg("Attachment smooth follow: entity half-life %.3f s, bone half-life %.3f s (0 = rigid), "
	          "maximum entity lag %.1f units, teleport reset %.1f units (0 = disabled).\n",
	          settings.FollowHalfLife, settings.BoneHalfLife, settings.FollowMaxDistance,
	          settings.FollowTeleportDistance);
	Tier0_Msg("Orbit half-life %.3f s, maximum turn rate %.1f deg/s; blocked recovery cut after %.3f demo "
	          "seconds (0 = disabled).\n",
	          settings.OrbitHalfLife, settings.OrbitSpeed, settings.FollowRecoveryDelay);
	if (argc == 3 || !valid)
		Tier0_Msg("mirv_cam attach follow <seconds> - Root movement smoothing; 0 restores rigid orbit and "
		          "framing.\n"
		          "mirv_cam attach follow boneHalfLife <seconds> - Bone motion damping; 0 disables.\n"
		          "mirv_cam attach follow maxDistance <units> - Maximum entity translation lag; also bounded "
		          "by the offset length.\n"
		          "mirv_cam attach follow teleportDistance <units> - Large movement reset; 0 disables.\n"
		          "mirv_cam attach follow orbitHalfLife <seconds> - Coordinated orbit/framing response; 0 is "
		          "rigid.\n"
		          "mirv_cam attach follow orbitSpeed <degrees/second> - Orbit turn cap; 0 is unlimited.\n"
		          "mirv_cam attach follow recoveryDelay <seconds> - Cut to a clear subject-side pose after "
		          "blocking; 0 disables.\n"
		          "mirv_cam attach follow reset - Reset the follow shot.\n"
		          "mirv_cam attach follow status - Show settings.\n"
		          "Damping uses demo time. Framing follows the corrected position; world mode retains its "
		          "heading. Seeks/cuts reset history.\n");
}
// End camera collision console command.

extern bool MirvCampathPreview_SetActive(bool active);
extern int g_LevelInitCount;
namespace
{
struct NamedCameraRig
{
	unsigned long long Id = 0;
	int AttachMode = MIRV_TF2_BONEMODE_BONE;
	advancedfx::CameraRig Rig;
	advancedfx::CameraPose LastWorld;
	std::shared_ptr<IMirvVecAngCalc> Target;
	int EntityAddress = -1, TargetLevel = -1;
	unsigned int TargetHandle = 0xffffffffu;
	std::uint64_t CacheIdentity = 0;
};
std::map<std::string, NamedCameraRig> g_CameraRigs;
enum class CameraSource
{
	Legacy,
	Rig
};
CameraSource g_CameraSource = CameraSource::Legacy;
std::string g_CameraRigName;
std::string g_EditingCameraName, g_LegacyCameraName;
advancedfx::CameraPose g_LastRigPose;
bool g_RigUnavailable = false;
bool g_RigPreview = false;
bool g_RigNavigation = false;
unsigned long long g_NextCameraId = 0;
auto FindRig(const std::string &reference)
{
	auto named = g_CameraRigs.find(reference);
	if (named != g_CameraRigs.end())
		return named;
	for (auto it = g_CameraRigs.begin(); it != g_CameraRigs.end(); ++it)
		if (std::to_string(it->second.Id) == reference)
			return it;
	return g_CameraRigs.end();
}
void SyncRigLevel()
{
	static int level = g_LevelInitCount;
	if (level != g_LevelInitCount)
	{
		level = g_LevelInitCount;
		g_CameraSource = CameraSource::Legacy;
		g_RigPreview = false;
		g_EditingCameraName.clear();
		g_LegacyCameraName.clear();
		g_RigUnavailable = false;
	}
}
advancedfx::CameraPose RigCapture()
{
	advancedfx::CameraPose pose;
	double p, y, r, fov;
	for (int i = 0; i < 3; ++i)
		pose.Position[i] = g_Hook_VClient_RenderView.LastCameraOrigin[i];
	p = g_Hook_VClient_RenderView.LastCameraAngles[0];
	y = g_Hook_VClient_RenderView.LastCameraAngles[1];
	r = g_Hook_VClient_RenderView.LastCameraAngles[2];
	fov = g_Hook_VClient_RenderView.LastCameraFov;
	pose.Rotation = Afx::Math::Quaternion::FromQREulerAngles(Afx::Math::QREulerAngles::FromQEulerAngles({p, y, r}));
	pose.Fov = fov;
	return pose;
}
unsigned int RigEntityHandle(int address)
{
	if (address >= 2048)
		return Mirv_Tf2_HandleForAddress(address);
	auto entity = Mirv_Tf2_DiagnosticEntity(address);
	return entity ? Mirv_Tf2_EntityHandle(entity) : 0xffffffffu;
}
bool RigTarget(const NamedCameraRig &rig, advancedfx::CameraPose &pose)
{
	if (!rig.Target || rig.TargetLevel != g_LevelInitCount || rig.TargetHandle == 0xffffffffu)
		return false;
	SOURCESDK::Vector origin;
	SOURCESDK::QAngle angles;
	if (AfxHookSource::CameraOverlay::SceneCachePreviewActive())
	{
		auto frame = AfxHookSource::CameraOverlay::SceneCacheView();
		if (!frame || !rig.CacheIdentity)
			return false;
		const advancedfx::SceneCache::Entity *entity = nullptr, *owner = nullptr;
		for (const auto &candidate : frame->Entities)
			if (candidate.Identity == rig.CacheIdentity)
			{
				entity = &candidate;
				break;
			}
		if (!entity)
			return false;
		if (entity->OwnerIdentity)
			for (const auto &candidate : frame->Entities)
				if (candidate.Identity == entity->OwnerIdentity)
				{
					owner = &candidate;
					break;
				}
		if (!Mirv_Tf2_CalcCachedCamera(rig.Target.get(), *entity, owner, origin, angles))
			return false;
	}
	else if (RigEntityHandle(rig.EntityAddress) != rig.TargetHandle || !rig.Target->CalcVecAng(origin, angles))
		return false;
	pose.Position[0] = origin.x;
	pose.Position[1] = origin.y;
	pose.Position[2] = origin.z;
	pose.Rotation = Afx::Math::Quaternion::FromQREulerAngles(
		Afx::Math::QREulerAngles::FromQEulerAngles({angles.x, angles.y, angles.z}));
	return pose.Valid();
}
bool EvaluateNamedRig(NamedCameraRig &rig, advancedfx::CameraPose &pose)
{
	advancedfx::CameraPose target;
	if (rig.Rig.Attached && !RigTarget(rig, target))
		return false;
	if (!rig.Rig.Evaluate(rig.Rig.Attached ? &target : nullptr, pose))
		return false;
	rig.LastWorld = pose;
	return true;
}
void CameraStorageCommand(advancedfx::ICommandArgs *args)
{
	SyncRigLevel();
	int argc = args->ArgC();
	if (argc == 2 && !_stricmp(args->ArgV(1), "list"))
	{
		for (const auto &entry : g_CameraRigs)
			Tier0_Msg("%llu: %s (%s)%s\n", entry.second.Id, entry.first.c_str(),
					  entry.second.Rig.Attached ? "attached" : "fixed",
					  g_CameraSource == CameraSource::Rig && g_CameraRigName == entry.first ? " [viewing]" : "");
		Tier0_Msg("Camera controller: %s%s\n", g_CameraSource == CameraSource::Legacy ? "legacy" : "camera",
				  g_RigUnavailable ? "; source unavailable (holding last valid pose)" : "");
		return;
	}
	if (argc == 3)
	{
		const std::string name = args->ArgV(2);
		const char *action = args->ArgV(1);
		if (!_stricmp(action, "add"))
		{
			if (name.empty() || name.size() > 128 || g_CameraRigs.count(name) || g_CameraRigs.size() >= 128 ||
				std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }) ||
				std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
			{
				Tier0_Warning("Use a unique camera name of 1..128 bytes (maximum 128 rigs).\n");
				return;
			}
			NamedCameraRig rig;
			rig.Id = g_NextCameraId++;
			rig.AttachMode = g_MirvCamAttachMode;
			rig.Rig.Pose = RigCapture();
			if (!rig.Rig.Pose.Valid())
				return;
			rig.LastWorld = rig.Rig.Pose;
			g_CameraRigs.emplace(name, std::move(rig));
			g_LastRigPose = g_CameraRigs.at(name).Rig.Pose;
			g_CameraRigName = name;
			g_CameraSource = CameraSource::Rig;
			g_RigPreview = true;
			g_RigNavigation = true;
			g_RigUnavailable = false;
			return;
		}
		auto found = FindRig(name);
		if (found == g_CameraRigs.end())
		{
			Tier0_Warning("Unknown camera.\n");
			return;
		}
		if (!_stricmp(action, "remove"))
		{
			if (g_CameraSource == CameraSource::Rig && g_CameraRigName == found->first)
				MirvCam_EndRigPreview();
			if (g_EditingCameraName == found->first)
				g_EditingCameraName.clear();
			if (g_LegacyCameraName == found->first)
			{
				if (MirvCam_ReleaseLiveAttachment() && g_CameraSource == CameraSource::Legacy && !MirvCampathPreview_IsActive())
				{
					g_LastRigPose = RigCapture();
					g_CameraSource = CameraSource::Rig;
					MirvCam_EndRigPreview();
				}
				g_LegacyCameraName.clear();
			}
			g_CameraRigs.erase(found);
			return;
		}
		if (!_stricmp(action, "capture"))
		{
			auto pose = RigCapture();
			if (!pose.Valid())
				return;
			if (found->second.Rig.Attached)
			{
				advancedfx::CameraPose target;
				if (!RigTarget(found->second, target) ||
					!found->second.Rig.Attach(pose, target, found->second.Rig.InheritRotation))
				{
					Tier0_Warning("Camera target unavailable; pose retained.\n");
					return;
				}
			}
			else
				found->second.Rig.Pose = pose;
			found->second.LastWorld = pose;
			if (g_EditingCameraName == found->first) g_EditingCameraName.clear();
			return;
		}
	}
	if (argc == 10 && !_stricmp(args->ArgV(1), "pose"))
	{
		auto found = FindRig(args->ArgV(2));
		if (found == g_CameraRigs.end())
		{
			Tier0_Warning("Unknown camera.\n");
			return;
		}
		double values[7];
		for (int i = 0; i < 7; ++i)
		{
			char *end = nullptr;
			values[i] = std::strtod(args->ArgV(i + 3), &end);
			if (end == args->ArgV(i + 3) || *end || !std::isfinite(values[i]))
			{
				Tier0_Warning("Pose values must be finite numbers.\n");
				return;
			}
		}
		advancedfx::CameraPose pose;
		for (int i = 0; i < 3; ++i)
			pose.Position[i] = values[i];
		pose.Rotation = Afx::Math::Quaternion::FromQREulerAngles(
			Afx::Math::QREulerAngles::FromQEulerAngles({values[3], values[4], values[5]}));
		pose.Fov = values[6];
		if (!pose.Valid())
		{
			Tier0_Warning("FOV must be 1..179 degrees.\n");
			return;
		}
		if (found->second.Rig.Attached)
		{
			advancedfx::CameraPose target;
			if (!RigTarget(found->second, target) ||
				!found->second.Rig.Attach(pose, target, found->second.Rig.InheritRotation))
				return;
		}
		else
			found->second.Rig.Pose = pose;
		found->second.LastWorld = pose;
		return;
	}
	Tier0_Msg("mirv_cam add [name] | list | capture <ID/name> | remove <ID/name> | pose <ID/name> <x> <y> <z> <pitch> "
			  "<yaw> <roll> <fov>\n");
}
// Legacy attach remains the live evaluator (including follow/collision). Keep an
// independent camera definition so the same attachment is available in preview.
bool RegisterAttachedCamera(IMirvVecAngCalc *calc, int entity, int mode, const advancedfx::CameraPose &world)
{
	SyncRigLevel();
	auto edited = g_CameraRigs.find(g_EditingCameraName);
	if (!calc || !world.Valid() || (edited == g_CameraRigs.end() && g_CameraRigs.size() >= 128))
		return false;
	NamedCameraRig attached;
	calc->AddRef();
	attached.Target = std::shared_ptr<IMirvVecAngCalc>(calc, [](IMirvVecAngCalc *p) { p->Release(); });
	attached.EntityAddress = entity;
	attached.AttachMode = mode;
	attached.TargetLevel = g_LevelInitCount;
	attached.TargetHandle = RigEntityHandle(entity);
	int user = 0;
	attached.CacheIdentity = Mirv_Tf2_GetPlayerInfo(entity, nullptr, 0, &user) && user > 0
								 ? std::uint64_t(user)
								 : (std::uint64_t(1) << 63) | attached.TargetHandle;
	advancedfx::CameraPose target;
	if (!RigTarget(attached, target) || !attached.Rig.Attach(world, target, true))
		return false;
	attached.LastWorld = world;
	std::string name;
	if (edited != g_CameraRigs.end())
	{
		name = edited->first;
		attached.Id = edited->second.Id;
	}
	else
	{
		while (g_CameraRigs.count("Camera " + std::to_string(g_NextCameraId)))
			++g_NextCameraId;
		attached.Id = g_NextCameraId++;
		name = "Camera " + std::to_string(attached.Id);
	}
	g_CameraRigs.insert_or_assign(name, std::move(attached));
	g_CameraRigName = g_LegacyCameraName = name;
	g_EditingCameraName.clear();
	g_RigNavigation = true;
	Tier0_Msg("Attached camera %llu: %s.\n", g_CameraRigs.at(name).Id, name.c_str());
	return true;
}
void CaptureLegacyCamera(const advancedfx::CameraPose &world)
{
	auto found = g_CameraRigs.find(g_LegacyCameraName);
	advancedfx::CameraPose target;
	if (found != g_CameraRigs.end() && RigTarget(found->second, target) &&
		found->second.Rig.Attach(world, target, true))
		found->second.LastWorld = world;
}
void ClearCameraAttachment(bool liveSource)
{
	const auto name = (g_RigPreview || !g_EditingCameraName.empty()) ? g_CameraRigName : g_LegacyCameraName;
	auto found = g_CameraRigs.find(name);
	if (found != g_CameraRigs.end() && found->second.Rig.Attached)
	{
		advancedfx::CameraPose pose;
		const bool live = liveSource && !MirvCampathPreview_IsActive() && g_EditingCameraName.empty() && name == g_LegacyCameraName;
		if (live)
			pose = RigCapture();
		else if (!EvaluateNamedRig(found->second, pose))
			pose = found->second.LastWorld;
		found->second.Rig.Pose = pose;
		found->second.Rig.Attached = false;
		found->second.Target.reset();
		if (live)
		{
			g_LastRigPose = pose;
			g_CameraSource = CameraSource::Rig;
			MirvCam_EndRigPreview();
		}
	}
	g_LegacyCameraName.clear();
}
} // namespace
bool MirvCam_EvaluateRig(double clientTime, advancedfx::CameraPose &pose)
{
	SyncRigLevel();
	if (g_CameraSource == CameraSource::Legacy)
	{
		g_RigUnavailable = false;
		return false;
	}
	advancedfx::CameraPose evaluated;
	auto found = g_CameraRigs.find(g_CameraRigName);
	const bool valid = found != g_CameraRigs.end() && EvaluateNamedRig(found->second, evaluated);
	if (valid)
		g_LastRigPose = evaluated;
	else if (!g_RigUnavailable)
		Tier0_Warning("Selected camera source unavailable; holding its last valid pose. Target or cached pose is unavailable.\n");
	g_RigUnavailable = !valid;
	pose = g_LastRigPose;
	return true;
}

bool MirvCam_RigPreviewActive() { return g_RigPreview; }
void MirvCam_EndRigPreview()
{
	const bool adopt = g_CameraSource == CameraSource::Rig;
	auto pose = g_LastRigPose;
	g_RigPreview = false;
	g_CameraSource = CameraSource::Legacy;
	g_RigUnavailable = false;
	// Clear native preview too, before restoring the displayed rig pose.
	MirvCampathPreview_SetActive(false);
	auto input = g_Hook_VClient_RenderView.m_MirvInput;
	if (adopt && input && pose.Valid())
	{
		auto a = pose.Rotation.ToQREulerAngles().ToQEulerAngles();
		input->SetTx(float(pose.Position[0]));
		input->SetTy(float(pose.Position[1]));
		input->SetTz(float(pose.Position[2]));
		input->SetRx(float(a.Pitch));
		input->SetRy(float(a.Yaw));
		input->SetRz(float(a.Roll));
		input->SetFov(float(pose.Fov));
		input->SetCameraControlMode(true);
	}
}
bool MirvCam_RigNavigation() { return g_RigNavigation; }
void MirvCam_SetRigNavigation(bool cameras) { g_RigNavigation = cameras; }
std::string MirvCam_PreviewLabel()
{
	auto found = g_CameraRigs.find(g_CameraRigName);
	if (found == g_CameraRigs.end())
		return "Cameras | none";
	return "Cameras | " + std::to_string(found->second.Id) + ": " + found->first.substr(0, 40) +
		   (g_RigUnavailable			 ? " [unavailable]"
			: found->second.Rig.Attached ? " [attached]"
										 : " [fixed]");
}
bool MirvCam_PreviewKey(unsigned int key, bool down, bool repeat)
{
	if (!g_RigPreview)
		return false;
	if (key == 'E' || key == 'Y' || key == VK_DELETE)
		return true;
	if (key != VK_UP && key != VK_DOWN && key != VK_HOME && key != VK_END && key != VK_ESCAPE)
		return false;
	if (!down || repeat)
		return true;
	if (key == VK_ESCAPE)
	{
		MirvCam_EndRigPreview();
		return true;
	}
	std::vector<std::pair<unsigned long long, std::string>> ordered;
	for (const auto &entry : g_CameraRigs)
		ordered.emplace_back(entry.second.Id, entry.first);
	std::sort(ordered.begin(), ordered.end());
	if (ordered.empty())
	{
		MirvCam_EndRigPreview();
		return true;
	}
	size_t index = 0;
	for (size_t i = 0; i < ordered.size(); ++i)
		if (ordered[i].second == g_CameraRigName)
			index = i;
	if (key == VK_HOME)
		index = 0;
	else if (key == VK_END)
		index = ordered.size() - 1;
	else if (key == VK_UP)
		index = (index + 1) % ordered.size();
	else
		index = (index + ordered.size() - 1) % ordered.size();
	g_CameraRigName = ordered[index].second;
	g_CameraSource = CameraSource::Rig;
	return true;
}
bool MirvCam_CameraCommand(advancedfx::ICommandArgs *args)
{
	SyncRigLevel();
	if (args->ArgC() < 2)
		return false;
	std::string action = args->ArgV(1);
	std::transform(action.begin(), action.end(), action.begin(), [](unsigned char c) { return char(tolower(c)); });
	struct Forward : advancedfx::ICommandArgs
	{
		std::vector<std::string> Words;
		int ArgC() override { return int(Words.size()); }
		const char *ArgV(int i) override { return Words.at(i).c_str(); }
	} command;
	command.Words = {"mirv_cam", action};
	if (action == "add")
	{
		if (args->ArgC() > 3)
			return true;
		if (args->ArgC() == 2)
			while (g_CameraRigs.count("Camera " + std::to_string(g_NextCameraId)))
				++g_NextCameraId;
		command.Words[1] = "add";
		command.Words.push_back(args->ArgC() == 3 ? args->ArgV(2) : "Camera " + std::to_string(g_NextCameraId));
		const auto count = g_CameraRigs.size();
		const bool previousNavigation = g_RigNavigation;
		CameraStorageCommand(&command);
		if (g_CameraRigs.size() > count)
		{
			auto added = g_CameraRigs.find(command.Words[2]);
			Tier0_Msg("Added camera %llu: %s.\n", added->second.Id, added->first.c_str());
			MirvCam_EndRigPreview();
			g_RigNavigation = previousNavigation;
			g_EditingCameraName.clear();
		}
		return true;
	}
	if (action == "preview")
	{
		if (args->ArgC() != 3)
		{
			Tier0_Msg("mirv_cam preview <camera ID/name>|cameras|keys|toggle|off\n");
			return true;
		}
		std::string reference = args->ArgV(2);
		if (!_stricmp(reference.c_str(), "toggle"))
		{
			MirvCam_TogglePreviewNavigation();
			return true;
		}
		if (reference == "off" || reference == "keys")
		{
			MirvCam_EndRigPreview();
			g_EditingCameraName.clear();
			if (reference == "keys")
				g_RigNavigation = false;
			MirvCampathPreview_SetActive(reference == "keys");
			return true;
		}
		auto found = reference == "cameras" ? g_CameraRigs.find(g_CameraRigName) : FindRig(reference);
		if (found == g_CameraRigs.end() && reference == "cameras" && !g_CameraRigs.empty())
			found = g_CameraRigs.begin();
		if (found == g_CameraRigs.end())
		{
			Tier0_Warning("No matching camera.\n");
			return true;
		}
		MirvCampathPreview_SetActive(false);
		auto current = RigCapture();
		if (current.Valid())
			g_LastRigPose = current;
		g_CameraRigName = found->first;
		g_EditingCameraName.clear();
		g_CameraSource = CameraSource::Rig;
		g_RigPreview = true;
		g_RigNavigation = true;
		return true;
	}
	if (action == "edit")
	{
		if (args->ArgC() != 3)
			return true;
		auto found = FindRig(args->ArgV(2));
		if (found == g_CameraRigs.end())
			return true;
		advancedfx::CameraPose pose;
		if (!EvaluateNamedRig(found->second, pose))
			pose = found->second.LastWorld;
		MirvCampathPreview_SetActive(false);
		g_CameraRigName = found->first;
		g_LastRigPose = pose;
		g_CameraSource = CameraSource::Rig;
		MirvCam_EndRigPreview();
		g_EditingCameraName = found->first;
		return true;
	}
	if (action == "rename")
	{
		if (args->ArgC() != 4)
			return true;
		auto found = FindRig(args->ArgV(2));
		std::string name = args->ArgV(3);
		if (found == g_CameraRigs.end() || name.empty() || name.size() > 128 || g_CameraRigs.count(name) ||
			std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }) ||
			std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
		{
			Tier0_Warning("Use a unique, nonnumeric camera name.\n");
			return true;
		}
		auto node = g_CameraRigs.extract(found);
		if (g_CameraRigName == node.key())
			g_CameraRigName = name;
		if (g_EditingCameraName == node.key())
			g_EditingCameraName = name;
		if (g_LegacyCameraName == node.key())
			g_LegacyCameraName = name;
		node.key() = name;
		g_CameraRigs.insert(std::move(node));
		return true;
	}
	if (action == "capture" || action == "pose" || action == "remove" || action == "list")
	{
		for (int i = 2; i < args->ArgC(); ++i)
			command.Words.push_back(args->ArgV(i));
		CameraStorageCommand(&command);
		if (g_CameraRigs.empty())
			g_RigPreview = false;
		return true;
	}
	return false;
}
bool MirvCam_SetPreviewNavigation(bool cameras)
{
	struct Args : advancedfx::ICommandArgs
	{
		const char *Mode;
		int ArgC() override { return 3; }
		const char *ArgV(int i) override { return i == 0 ? "mirv_cam" : i == 1 ? "preview" : Mode; }
	} args;
	args.Mode = cameras ? "cameras" : "keys";
	MirvCam_CameraCommand(&args);
	return cameras ? MirvCam_RigPreviewActive() : MirvCampathPreview_IsActive();
}
bool MirvCam_TogglePreviewNavigation()
{
	const bool cameras = !g_RigNavigation;
	// Switching to an unavailable destination must leave the current view intact.
	if (!cameras && g_Hook_VClient_RenderView.m_CamPath.GetSize() == 0)
	{
		Tier0_Warning("No keyframes to preview; camera navigation retained.\n");
		return false;
	}
	if (cameras && g_CameraRigs.empty())
	{
		Tier0_Warning("No cameras to preview; keyframe navigation retained.\n");
		return false;
	}
	return MirvCam_SetPreviewNavigation(cameras);
}
bool MirvCam_SelectedCameraAction(const char *action)
{
	if (std::strcmp(action, "add") && std::strcmp(action, "edit") && std::strcmp(action, "capture"))
		return false;
	if (std::strcmp(action, "add") && g_CameraRigs.find(g_CameraRigName) == g_CameraRigs.end())
	{
		Tier0_Warning("Select a camera first.\n");
		return false;
	}
	struct Args : advancedfx::ICommandArgs
	{
		const char *Action;
		std::string Name;
		int ArgC() override { return !std::strcmp(Action, "add") ? 2 : 3; }
		const char *ArgV(int i) override { return i == 0 ? "mirv_cam" : i == 1 ? Action : Name.c_str(); }
	} args;
	args.Action = action;
	args.Name = g_CameraRigName;
	const auto before = g_CameraRigs.size();
	const bool handled = MirvCam_CameraCommand(&args);
	return !std::strcmp(action, "add") ? g_CameraRigs.size() > before : handled;
}
CON_COMMAND(mirv_cam, "Control camera source entity and offset.")
{
	if (MirvCam_CameraCommand(args)) return;
	int argc = args->ArgC();

	if (2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if (!_stricmp("collision", arg1)) { MirvCameraCollisionCommand(args); return; }

		if (0 == _stricmp("source", arg1))
		{
			if (3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if (0 == _stricmp("calcVecAng", arg2))
				{
					if (4 <= argc)
					{
						IMirvVecAngCalc * vecAng = g_MirvVecAngCalcs.GetByName(args->ArgV(3));

						if (!vecAng)
							Tier0_Warning("No vecAng calc \"%s\" exists.\n", args->ArgV(3));

						g_MirvCam.Source_set(vecAng);

						return;
					}

					IMirvVecAngCalc * vecAng = g_MirvCam.Source_get();

					Tier0_Msg(
						"mirv_cam source calcVecAng <sVecAngCalcName> - Calc to use as source (<sVecAngCalcName> is name from mirv_calcs vecAng).\n"
						"Current value: %s"
						, vecAng ? "" : "(none)"
					);

					if (vecAng) { vecAng->Console_PrintBegin(); vecAng->Console_PrintEnd(); }
					Tier0_Msg("\n");

					return;
				}
				else if (0 == _stricmp("calcVecAngClear", arg2))
				{
					g_MirvCam.Source_set(0);	

					return;
				}
				else if (0 == _stricmp("handle", arg2))
				{
					if (4 <= argc)
					{
						if (SourceSdkVer_TF2 == g_SourceSdkVer || SourceSdkVer_TF2Classified == g_SourceSdkVer)
						{
							Tier0_Warning(
								"mirv_cam source handle is not supported for TF2.\n"
								"Use mirv_calcs vecAng add tf2Entity <sName> <iEntityIndex> [<iBoneIndex>] and mirv_cam source calcVecAng <sName> instead.\n"
							);
							return;
						}

						char const * arg3 = args->ArgV(3);

						if (0 == _stricmp("none", arg3))
							g_MirvCam.m_SourceHandle = SOURCESDK_CSGO_INVALID_EHANDLE_INDEX;
						else
							g_MirvCam.m_SourceHandle = atoi(arg3);

						g_MirvCam.RebuildCalc();

						return;
					}

					Tier0_Msg(
						"mirv_cam source handle none|<n> - Handle to use as source, use mirv_listentities to find the entityHandle or mirv_streams ... picker.\n"
						"Handle: %i%s\n"
						, g_MirvCam.m_SourceHandle.ToInt()
						, g_MirvCam.m_SourceHandle.IsValid() ? "" : " (none)"
					);

					return;
				}
				else
				if (0 == _stricmp("origin", arg2))
				{
					if (4<= argc)
					{
						char const * arg3 = args->ArgV(3);

						if (0 == _stricmp("net", arg3))
						{
							g_MirvCam.m_SourceOrigin = CMirvCam::O_Net;

							g_MirvCam.RebuildCalc();

							return;
						}
						else
						if (0 == _stricmp("view", arg3))
						{
							g_MirvCam.m_SourceOrigin = CMirvCam::O_View;

							g_MirvCam.RebuildCalc();

							return;
						}
					}

					char const * curValue = "[unknown]";
					switch (g_MirvCam.m_SourceOrigin)
					{
					case CMirvCam::O_Net:
						curValue = "net";
						break;
					case CMirvCam::O_View:
						curValue = "view";
						break;
					}

					Tier0_Msg(
						"mirv_cam source origin net|view - Source origin to use.\n"
						"Current value: %s\n",
						curValue
					);
					return;
				}
				else if (0 == _stricmp("angles", arg2))
				{
					if (4 <= argc)
					{
						char const * arg3 = args->ArgV(3);

						if (0 == _stricmp("net", arg3))
						{
							g_MirvCam.m_SourceAngles = CMirvCam::A_Net;

							g_MirvCam.RebuildCalc();

							return;
						}
						else
							if (0 == _stricmp("view", arg3))
							{
								g_MirvCam.m_SourceAngles = CMirvCam::A_View;

								g_MirvCam.RebuildCalc();

								return;
							}
					}

					char const * curValue = "[unknown]";
					switch (g_MirvCam.m_SourceAngles)
					{
					case CMirvCam::A_Net:
						curValue = "net";
						break;
					case CMirvCam::A_View:
						curValue = "view";
						break;
					}

					Tier0_Msg(
						"mirv_cam source angles net|view - Source angles to use.\n"
						"Current value: %s\n",
						curValue
					);
					return;
				}
				else if (0 == _stricmp("attachment", arg2))
				{
					if (4 <= argc)
					{
						g_MirvCam.m_SourceAttachment = args->ArgV(3);

						g_MirvCam.RebuildCalc();

						return;
					}

					Tier0_Msg(
						"mirv_cam source attachment <sName> - use e.g. \"muzzle_flash\" for 3rd person weapon entites for great results.\n"
						"Current value: %s\n"
						, g_MirvCam.m_SourceAttachment.c_str()
					);
					return;
				}
				else if (0 == _stricmp("attachmentNone", arg2))
				{
					g_MirvCam.m_SourceAttachment.clear();

					g_MirvCam.RebuildCalc();

					return;
				}
				else
				if (0 == _stricmp("originUse", arg2))
				{
					if (6 <= argc)
					{
						g_MirvCam.m_SourceUseX = 0 != atoi(args->ArgV(3));
						g_MirvCam.m_SourceUseY = 0 != atoi(args->ArgV(4));
						g_MirvCam.m_SourceUseZ = 0 != atoi(args->ArgV(5));
						return;
					}

					Tier0_Msg(
						"mirv_cam source originUse <iUseX> <iUseY> <iUseZ> - Use 0 for don't use, 1 for use.\n"
						"Current value: %i %i %i\n"
						, g_MirvCam.m_SourceUseX ? 1 : 0
						, g_MirvCam.m_SourceUseY ? 1 : 0
						, g_MirvCam.m_SourceUseZ ? 1 : 0
					);
					return;
				}
				else if (0 == _stricmp("anglesUse", arg2))
				{
					if (6 <= argc)
					{
						g_MirvCam.m_SourceUseXRotation = 0 != atoi(args->ArgV(3));
						g_MirvCam.m_SourceUseYRotation = 0 != atoi(args->ArgV(4));
						g_MirvCam.m_SourceUseZRotation = 0 != atoi(args->ArgV(5));
						return;
					}

					Tier0_Msg(
						"mirv_cam source anglesUse <iUseRotX> <iUseRotY> <iUseRotZ> - Use 0 for don't use, 1 for use.\n"
						"Current value: %i %i %i\n"
						, g_MirvCam.m_SourceUseXRotation ? 1 : 0
						, g_MirvCam.m_SourceUseYRotation ? 1 : 0
						, g_MirvCam.m_SourceUseZRotation ? 1 : 0
					);
					return;
				}
			}

			Tier0_Msg(
				"mirv_cam source calcVecAng [...] - Calc to use as source (overrides handle, origin, angles, attachment, attachmentNone).\n"
				"mirv_cam source calcVecAngClear - Clear source (overrides handle, origin, angles, attachment, attachmentNone).\n"
				"mirv_cam source handle [...] - Entity handle to use as source.\n"
				"mirv_cam source origin [...] - Controls source origin type.\n"
				"mirv_cam source angles [...] - Controls source angles type.\n"
				"mirv_cam source attachment [...] - Controls if and what attachment to use as source.\n"
				"mirv_cam source attachmentNone - Use no attachment.\n"
				"mirv_cam source originUse [...] - Controls which components of source origin to use.\n"
				"mirv_cam source anglesUse [...] - Controls which components of source angles to use.\n"
			);
			return;
		}
		else if (0 == _stricmp("fov", arg1))
		{
			if (3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if (0 == _stricmp("calc", arg2))
				{
					if (4 <= argc)
					{
						IMirvFovCalc * calc = g_MirvFovCalcs.GetByName(args->ArgV(3));

						if (!calc)
							Tier0_Warning("No fov calc \"%s\" exists.\n", args->ArgV(3));

						g_MirvCam.Fov_set(calc);

						return;
					}

					IMirvFovCalc * calc = g_MirvCam.Fov_get();

					Tier0_Msg(
						"mirv_cam fov calc <sFovCalcName> - Calc to use as source (<sFovCalcName> is name from mirv_calcs fov).\n"
						"Current value: %s"
						, calc ? "" : "(none)"
					);

					if (calc) { calc->Console_PrintBegin(); calc->Console_PrintEnd(); }
					Tier0_Msg("\n");

					return;
				}
				else if (0 == _stricmp("calcClear", arg2))
				{
					g_MirvCam.Fov_set(0);

					return;
				}
			}

			Tier0_Msg(
				"mirv_cam fov calc [...] - Calc to use for fov.\n"
				"mirv_cam fov calcClear - Clear calc.\n"
			);
			return;
		}
		else
		if (0 == _stricmp("offset", arg1))
		{
			if (5 <= argc)
			{
				g_MirvCam.m_OffsetForwad = (float)atof(args->ArgV(2));
				g_MirvCam.m_OffsetLeft = (float)atof(args->ArgV(3));
				g_MirvCam.m_OffsetUp = (float)atof(args->ArgV(4));

				if (8 <= argc)
				{
					g_MirvCam.m_OffsetForwardRot = (float)atof(args->ArgV(5));
					g_MirvCam.m_OffseLeftRot = (float)atof(args->ArgV(6));
					g_MirvCam.m_OffsetUpRot = (float)atof(args->ArgV(7));
				}
				return;
			}
			else if (3 <= argc && 0 == _stricmp("none", args->ArgV(2)))
			{
				g_MirvCam.m_OffsetForwad = 0;
				g_MirvCam.m_OffsetLeft = 0;
				g_MirvCam.m_OffsetUp = 0;

				g_MirvCam.m_OffsetForwardRot = 0;
				g_MirvCam.m_OffseLeftRot = 0;
				g_MirvCam.m_OffsetUpRot =0;

				return;
			}


			Tier0_Msg(
				"mirv_cam offset none - No camera offset.\n"
				"mirv_cam offset <fXForward> <fYLeft> <fZUp> [<fRotXForward> <fRotYLeft> <fRotZUp>] - Camera offset location, rotation optional, floating point values.\n"
				"Current value: <fXForward>=%f <fYLeft>=%f <fZUp>=%f <fRotXForward>=%f <fRotYLeft>=%f <fRotZUp>=%f\n"
				, g_MirvCam.m_OffsetForwad, g_MirvCam.m_OffsetLeft, g_MirvCam.m_OffsetUp
				, g_MirvCam.m_OffsetForwardRot, g_MirvCam.m_OffseLeftRot, g_MirvCam.m_OffsetUpRot
			);
			return;
		}
		else if (0 == _stricmp("attach", arg1))
		{
			if (argc >= 3 && !_stricmp(args->ArgV(2), "follow"))
			{
				MirvCameraAttachFollowCommand(args);
				return;
			}
			if (!(SourceSdkVer_TF2 == g_SourceSdkVer || SourceSdkVer_TF2Classified == g_SourceSdkVer))
			{
				Tier0_Warning("mirv_cam attach is only supported for TF2.\n");
				return;
			}

			if (3 <= argc && 0 == _stricmp("clear", args->ArgV(2)))
			{
				ClearCameraAttachment(g_MirvCamAttachCalc && g_MirvCam.Source_get() == g_MirvCamAttachCalc);
				g_MirvCam.Source_set(0);
				MirvCam_SetAttachCalc(0);
				return;
			}

			if (3 <= argc && 0 == _stricmp("capture", args->ArgV(2)))
			{
				if (AfxHookSource::CameraOverlay::SceneCachePreviewActive())
				{
					MirvCam_SelectedCameraAction("capture");
					return;
				}
				// In camera editing/preview, capture belongs to that camera, not a hidden previous attachment.
				if (g_RigPreview || !g_EditingCameraName.empty())
				{
					auto selected = g_CameraRigs.find(g_CameraRigName);
					advancedfx::CameraPose target;
					if (selected == g_CameraRigs.end() || !selected->second.Rig.Attached || !RigTarget(selected->second, target))
					{
						Tier0_Warning("The selected camera has no available attachment. Use mirv_cam capture <ID/name> for a stationary camera.\n");
						return;
					}
					auto calc = selected->second.Target.get();
					g_MirvCam.Source_set(calc);
					if (g_MirvCamAttachCalc != calc) MirvCam_SetAttachCalc(calc);
					g_LegacyCameraName = selected->first;
				}
				if (!g_MirvCamAttachCalc || g_MirvCam.Source_get() != g_MirvCamAttachCalc)
				{
					Tier0_Warning("The camera is not attached, use mirv_cam attach <target> [<bone>] first.\n");
					return;
				}

				float localOffset[3];

				double origin[3], angles[3];
				std::copy(g_Hook_VClient_RenderView.LastCameraOrigin, g_Hook_VClient_RenderView.LastCameraOrigin + 3,
						  origin);
				std::copy(g_Hook_VClient_RenderView.LastCameraAngles, g_Hook_VClient_RenderView.LastCameraAngles + 3,
						  angles);
				const bool recaptured = MirvCameraAttachedCapturePose(g_MirvCamAttachCalc, origin, angles);
				if (!Mirv_Tf2_CaptureCamera(g_MirvCamAttachCalc, origin, angles, localOffset))
				{
					Tier0_Warning("Could not capture the camera, the target is not available right now.\n");
					return;
				}

				CaptureLegacyCamera(RigCapture());
				g_EditingCameraName.clear();
				if (!recaptured)
					MirvCameraAttachedReset();
				// The captured pose already includes everything, so the offsets must not be applied on top of it.
				g_MirvCam.m_OffsetForwad = 0;
				g_MirvCam.m_OffsetLeft = 0;
				g_MirvCam.m_OffsetUp = 0;
				g_MirvCam.m_OffsetForwardRot = 0;
				g_MirvCam.m_OffseLeftRot = 0;
				g_MirvCam.m_OffsetUpRot = 0;

				Tier0_Msg("Captured the current camera relative to the target (forward %.2f, left %.2f, up %.2f), it "
						  "now follows the target.\n"
						  "mirv_cam offset was cleared. If you used the free camera, end it with mirv_input end.\n",
						  localOffset[0], localOffset[1], localOffset[2]);
				return;
			}

			if (AfxHookSource::CameraOverlay::SceneCachePreviewActive())
			{
				Tier0_Warning("Return to live demo playback to attach a camera, then preview it through Scene Cache.\n");
				return;
			}
			if (3 <= argc)
			{
				const char *target = args->ArgV(2);
				int entityIndex = -1;

				bool numeric = '\0' != target[0];
				for (const char *c = target; *c; ++c)
				{
					if (*c < '0' || '9' < *c)
					{
						numeric = false;
						break;
					}
				}

				if (numeric)
				{
					char *end = nullptr;
					const long slot = std::strtol(target, &end, 10);
					if (end == target || *end || slot < 0 || slot >= INT_MAX)
					{
						Tier0_Warning("Entity address must be a non-negative integer below INT_MAX.\n");
						return;
					}
					entityIndex = int(slot);
				}
				else
				{
					// Find a player by (part of) the name, ignoring case.
					std::string needle(target);
					std::transform(needle.begin(), needle.end(), needle.begin(),
								   [](unsigned char c) { return (char)tolower(c); });

					int matches = 0;
					std::string matchList;
					const int maxPlayers = Mirv_Tf2_GetMaxPlayers();

					for (int i = 1; i <= maxPlayers; ++i)
					{
						char name[64];
						if (!Mirv_Tf2_GetPlayerInfo(i, name, sizeof(name), 0))
							continue;

						std::string hay(name);
						std::transform(hay.begin(), hay.end(), hay.begin(),
									   [](unsigned char c) { return (char)tolower(c); });

						if (std::string::npos == hay.find(needle))
							continue;

						if (0 == matches)
							entityIndex = i;

						++matches;
						matchList += "  " + std::to_string(i) + ": " + name + "\n";
					}

					if (0 == matches)
					{
						Tier0_Warning("No player with \"%s\" in the name found (see mirv_listplayers).\n", target);
						return;
					}

					if (1 < matches)
					{
						Tier0_Warning("More than one player matches \"%s\", use the entity index instead:\n%s", target,
									  matchList.c_str());
						return;
					}
				}

				if (entityIndex < 0)
				{
					Tier0_Warning("Entity index %i is out of range.\n", entityIndex);
					return;
				}

				if (entityIndex >= 2048 && Mirv_Tf2_HandleForAddress(entityIndex) == 0xffffffffu)
				{
					Tier0_Warning("Client entity ID %i is unavailable; see mirv_listentities.\n", entityIndex);
					return;
				}
				int boneIndex = -1;

				if (4 <= argc && !Mirv_Tf2_ParseBoneArg(entityIndex, args->ArgV(3), boneIndex))
					return;

				const float previousOffsets[] = {g_MirvCam.m_OffsetForwad, g_MirvCam.m_OffsetLeft,
												 g_MirvCam.m_OffsetUp,	   g_MirvCam.m_OffsetForwardRot,
												 g_MirvCam.m_OffseLeftRot, g_MirvCam.m_OffsetUpRot};
				auto restoreOffsets = [&]()
				{
					g_MirvCam.m_OffsetForwad = previousOffsets[0];
					g_MirvCam.m_OffsetLeft = previousOffsets[1];
					g_MirvCam.m_OffsetUp = previousOffsets[2];
					g_MirvCam.m_OffsetForwardRot = previousOffsets[3];
					g_MirvCam.m_OffseLeftRot = previousOffsets[4];
					g_MirvCam.m_OffsetUpRot = previousOffsets[5];
				};
				if (7 <= argc)
				{
					g_MirvCam.m_OffsetForwad = (float)atof(args->ArgV(4));
					g_MirvCam.m_OffsetLeft = (float)atof(args->ArgV(5));
					g_MirvCam.m_OffsetUp = (float)atof(args->ArgV(6));

					if (10 <= argc)
					{
						g_MirvCam.m_OffsetForwardRot = (float)atof(args->ArgV(7));
						g_MirvCam.m_OffseLeftRot = (float)atof(args->ArgV(8));
						g_MirvCam.m_OffsetUpRot = (float)atof(args->ArgV(9));
					}
				}

				// Register first; an unavailable target must not replace the current source.
				auto calc = g_MirvVecAngCalcs.NewTf2EntityIndexCalc(0, entityIndex, boneIndex, g_MirvCamAttachMode);
				if (!calc)
				{
					restoreOffsets();
					return;
				}
				calc->AddRef();
				std::shared_ptr<IMirvVecAngCalc> pending(calc, [](IMirvVecAngCalc *p) { p->Release(); });
				SOURCESDK::Vector o;
				SOURCESDK::QAngle a;
				if (!calc->CalcVecAng(o, a))
				{
					restoreOffsets();
					Tier0_Warning("Attachment target unavailable; camera retained.\n");
					return;
				}
				float x = o.x, y = o.y, z = o.z, pitch = a.x, yaw = a.y, roll = a.z;
				g_MirvCam.ApplyOffset(x, y, z, roll, pitch, yaw);
				auto world = RigCapture();
				world.Position[0] = x;
				world.Position[1] = y;
				world.Position[2] = z;
				world.Rotation = Afx::Math::Quaternion::FromQREulerAngles(
					Afx::Math::QREulerAngles::FromQEulerAngles({pitch, yaw, roll}));
				if (!RegisterAttachedCamera(calc, entityIndex, g_MirvCamAttachMode, world))
				{
					restoreOffsets();
					Tier0_Warning("Could not register attached camera (target unavailable or camera limit reached).\n");
					return;
				}
				MirvCam_EndRigPreview();
				g_MirvCam.Source_set(calc);
				MirvCam_SetAttachCalc(calc);
				if (g_Hook_VClient_RenderView.m_MirvInput)
					g_Hook_VClient_RenderView.m_MirvInput->SetCameraControlMode(false);

				if (0 <= boneIndex)
					Tier0_Msg("Camera source set to entity %i, bone %i (angles: %s).\n", entityIndex, boneIndex,
							  MirvCam_AttachModeName(g_MirvCamAttachMode));
				else
					Tier0_Msg("Camera source set to entity %i.\n", entityIndex);
				return;
			}

			Tier0_Msg(
				"mirv_cam attach <entityIndex or part of player name> [<iBoneIndex or part of bone name> [<fXForward> "
				"<fYLeft> <fZUp> [<fRotXForward> <fRotYLeft> <fRotZUp>]]] - (TF2) Use the entity (or its bone) as "
				"camera source, optionally with offset (same as mirv_cam offset). Use -1 as bone for no bone.\n"
				"mirv_cam attach capture - Use the current camera (i.e. the free camera from mirv_input camera) as the "
				"camera's position and angles relative to the attached target. Do mirv_cam attach first, fly the free "
				"camera to the framing you want, then capture and end the free camera with mirv_input end.\n"
				"mirv_cam attach clear - Detach the selected camera at its current pose and clear the live "
				"attachment.\n"
				"mirv_cam attach follow [...] - Optional demo-time positional following; 0 keeps rigid attachment.\n"
				"Use mirv_listplayers to find players and mirv_listbones to find bones. The angles used for bones are "
				"controlled by mirv_cam attachMode.\n");
			return;
		}
		else if (0 == _stricmp("attachMode", arg1))
		{
			if (3 <= argc)
			{
				char const * modeName = args->ArgV(2);

				if (0 == _stricmp("bone", modeName)) g_MirvCamAttachMode = MIRV_TF2_BONEMODE_BONE;
				else if (0 == _stricmp("entity", modeName)) g_MirvCamAttachMode = MIRV_TF2_BONEMODE_ENTITY;
				else if (0 == _stricmp("aligned", modeName)) g_MirvCamAttachMode = MIRV_TF2_BONEMODE_ALIGNED;
				else if (0 == _stricmp("world", modeName)) g_MirvCamAttachMode = MIRV_TF2_BONEMODE_WORLD;
				else
					Tier0_Warning("Unknown mode \"%s\", use bone, entity, aligned or world.\n", modeName);

				return;
			}

			Tier0_Msg(
				"mirv_cam attachMode bone|entity|aligned|world - Angles for mirv_cam attach with a bone:\n"
				"bone - The bone's own angles (depends on the skeleton, usually needs rotation offsets).\n"
				"entity - The entity's facing (for players the body direction), the position is still the bone's.\n"
				"aligned - Starts looking like the entity (facing forward) and then follows the bone's rotation.\n"
				"world - Keeps the camera's current heading and offset direction while following translation.\n"
				"Set before attaching. Current value: %s\n"
				, MirvCam_AttachModeName(g_MirvCamAttachMode)
			);
			return;
		}
		else if (0 == _stricmp("order", arg1))
		{
			CSubWrpCommandArgs subArgs(args, 2);
			g_Hook_VClient_RenderView.Console_Overrides(&subArgs);
			return;
		}
	}

	Tier0_Msg(
		"mirv_cam add [name] | list | remove <ID/name> | rename <ID/name> <name> - Session cameras.\n"
		"mirv_cam preview <ID/name>|cameras|keys|toggle|off - Preview cameras or keyframes.\n"
		"mirv_cam edit <ID/name> | capture <ID/name> - Move to a camera for editing, then recapture.\n"
		"mirv_cam attach <entity/name> [bone] [offsets...] - Attach and register a camera; edit a camera first to reattach it.\n"
		"mirv_cam pose <ID/name> <x> <y> <z> <pitch> <yaw> <roll> <fov> - Edit a camera.\n"
		"mirv_cam source [...] - Control camera location.\n"
		"mirv_cam fov [...] - Control camera fov.\n"
		"mirv_cam attach [...] - (TF2) Attach the camera to a player or entity (and bone).\n"
		"mirv_cam attachMode [...] - (TF2) Controls which angles attach uses for bones.\n"
		"mirv_cam offset [...] - Control camera offset (in local space).\n"
		"mirv_cam order [...] - Control order of camera overrides.\n"
		"mirv_cam collision [...] - Optional TF2/TF2C free-camera and campath collision.\n"
	);
}

bool MirvCam_RigViewActive() { SyncRigLevel(); return g_CameraSource != CameraSource::Legacy; }
