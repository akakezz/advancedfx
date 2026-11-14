#pragma once

#include "SourceInterfaces.h"

#include "MirvCalcs.h"
#include <shared/CameraRig.h>
#include <shared/SceneCache.h>

#include <string>

class CMirvCam
{
public:
	enum Origin_e {
		O_Net,
		O_View
	};

	enum Angles_e {
		A_Net,
		A_View
	};

	/// <returns>true if overriden</returns>
	bool ApplySource(float & x, float & y, float & z, float & xRotation, float & yRotation, float & zRotation);

	/// <returns>true if overriden</returns>
	bool ApplyOffset(float & x, float & y, float & z, float & xRotation, float & yRotation, float & zRotation);

	/// <returns>true if overriden</returns>
	bool ApplyFov(float & fov);

public:
	SOURCESDK::CSGO::CBaseHandle m_SourceHandle;
	Origin_e m_SourceOrigin = O_View;
	Angles_e m_SourceAngles = A_View;
	std::string m_SourceAttachment;

	void RebuildCalc(void);

	bool m_SourceUseX = true;
	bool m_SourceUseY = true;
	bool m_SourceUseZ = true;
	bool m_SourceUseXRotation = true;
	bool m_SourceUseYRotation = true;
	bool m_SourceUseZRotation = true;
	float m_OffsetForwad = 0;
	float m_OffsetLeft = 0;
	float m_OffsetUp = 0;
	float m_OffsetForwardRot = 0;
	float m_OffseLeftRot = 0;
	float m_OffsetUpRot = 0;

	~CMirvCam()
	{
		Source_set(0);
	}

	IMirvVecAngCalc * Source_get(void)
	{
		return m_Source;
	}

	void Source_set(IMirvVecAngCalc * value)
	{
		if (m_Source) m_Source->Release();
		m_Source = value;
		if (m_Source) m_Source->AddRef();
	}

	IMirvFovCalc * Fov_get(void)
	{
		return m_Fov;
	}

	void Fov_set(IMirvFovCalc * value)
	{
		if (m_Fov) m_Fov->Release();
		m_Fov = value;
		if (m_Fov) m_Fov->AddRef();
	}

private:
	IMirvVecAngCalc * m_Source = 0;
	IMirvFovCalc * m_Fov = 0;
};

int MirvCam_GetAttachMode();
bool MirvCam_GetAttachAnchor(double origin[3], void *&entity, const void *&identity, double entityOrigin[3], int *mode = nullptr);
extern CMirvCam g_MirvCam;

bool MirvCam_EvaluateRig(double clientTime, advancedfx::CameraPose &pose, int frame = -1, double frameStep = 0);
bool MirvCam_RigViewActive();

bool MirvCam_CameraCommand(advancedfx::ICommandArgs *args);
bool MirvCam_RigPreviewActive();
void MirvCam_EndRigPreview();
bool MirvCam_PreviewKey(unsigned int key, bool down, bool repeat);
std::string MirvCam_PreviewLabel();
bool MirvCam_SetPreviewNavigation(bool cameras);
bool MirvCam_TogglePreviewNavigation();
bool MirvCam_RigNavigation();
void MirvCam_SetRigNavigation(bool cameras);
bool MirvCam_SelectedCameraAction(const char *action);

// Camera management uses value snapshots; selection only changes the view while previewing cameras.
std::vector<advancedfx::CameraInfo> MirvCam_Cameras(bool evaluate = false);
size_t MirvCam_CameraCount();
bool MirvCam_SelectCamera(std::uint64_t id);
void MirvCam_ClearCameraSelection();
bool MirvCam_RenameSelectedCamera(const char *name);
bool MirvCam_CameraAction(const char *action, std::uint64_t id);
bool MirvCam_RenameCamera(std::uint64_t id, const char *name);
void MirvCam_NotifyDemoFile(const char *file, const char *gameDirectory);
bool MirvCam_EditCamera(std::uint64_t id, const advancedfx::CameraPose &pose);
bool MirvCam_HistoryUndo(bool redo);

class Aiming;
bool MirvCam_AimCommand(advancedfx::ICommandArgs *args, void (*command)(advancedfx::ICommandArgs *, Aiming &));

std::vector<advancedfx::CameraClip> MirvCam_Clips();
bool MirvCam_EditClip(advancedfx::CameraClip &clip);
bool MirvCam_RemoveClip(std::uint64_t id);
bool MirvCam_EnableSequence(bool enabled);
bool MirvCam_SequenceEnabled();
void MirvCam_ClearClips();
bool MirvCam_ResumeSequencePreview();

bool MirvCam_SaveProject(const wchar_t *file);
bool MirvCam_LoadProject(const wchar_t *file);
bool MirvCam_Bake(const wchar_t *file, const char *source, double begin, double end, double step);

void MirvCam_CaptureCameras(double time, int frame, double step,
							std::vector<advancedfx::SceneCache::CameraSample> &samples);

std::string MirvCam_BakeStatus();

void MirvCam_BeginFreeCamera();
