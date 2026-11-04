#include "stdafx.h"
#include <chrono>

//
// These hooks mainly hook into
// CViewRender::SetUpView inside the client.dll
//



#include "RenderView.h"

// Preview controller is implemented by the mirv_campath console command.
extern void MirvCampathPreview_Update();

#include "MirvTime.h"

#include <shared/AfxDetours.h>
#include <shared/StringTools.h>

#include <shared/bvhimport.h>
#include <shared/bvhexport.h>

#include "addresses.h"
#include "WrpVEngineClient.h"
#include "SourceInterfaces.h"

#include "aiming.h"

#include "MirvCam.h"
#include "AfxInterop.h"
#ifndef _WIN64
#include "csgo/ClientToolsCSgo.h"
#endif //#ifndef _WIN64
#include "MirvPgl.h"
#include "../shared/MirvCamIO.h"

#undef CreateEvent

extern WrpVEngineClient * g_VEngineClient;
extern SOURCESDK::CreateInterfaceFn g_AppSystemFactory;

// Source SDK's VEngineVGui001 exposes the game's main-menu state.
// Keep the vtable layout (destructor, GetPanel, IsGameUIVisible), but do
// NOT enumerate VGUI panels here: the bundled CS:GO SDK declares VPANEL
// as 32-bit, which truncates panel pointers in x64 TF2 Classified.
class FreecamEngineVGui
{
public:
	virtual ~FreecamEngineVGui() {}
	virtual void * GetPanel(int type) = 0;
	virtual bool IsGameUIVisible() = 0;
};

static bool FreecamMenuVisible()
{
	if (!g_AppSystemFactory) return false;
	static FreecamEngineVGui * engineVGui = static_cast<FreecamEngineVGui *>(
		g_AppSystemFactory("VEngineVGui001", nullptr));
	return engineVGui && engineVGui->IsGameUIVisible();
}

// TF2 uses VGUI_Surface030. In Source SDK 2013, IsCursorVisible is
// vtable slot 53; slot 52 is SetCursorAlwaysVisible (do not invoke it).
class FreecamSurface030
{
public:
	virtual void Unused00() = 0;
	virtual void Unused01() = 0;
	virtual void Unused02() = 0;
	virtual void Unused03() = 0;
	virtual void Unused04() = 0;
	virtual void Unused05() = 0;
	virtual void Unused06() = 0;
	virtual void Unused07() = 0;
	virtual void Unused08() = 0;
	virtual void Unused09() = 0;
	virtual void Unused10() = 0;
	virtual void Unused11() = 0;
	virtual void Unused12() = 0;
	virtual void Unused13() = 0;
	virtual void Unused14() = 0;
	virtual void Unused15() = 0;
	virtual void Unused16() = 0;
	virtual void Unused17() = 0;
	virtual void Unused18() = 0;
	virtual void Unused19() = 0;
	virtual void Unused20() = 0;
	virtual void Unused21() = 0;
	virtual void Unused22() = 0;
	virtual void Unused23() = 0;
	virtual void Unused24() = 0;
	virtual void Unused25() = 0;
	virtual void Unused26() = 0;
	virtual void Unused27() = 0;
	virtual void Unused28() = 0;
	virtual void Unused29() = 0;
	virtual void Unused30() = 0;
	virtual void Unused31() = 0;
	virtual void Unused32() = 0;
	virtual void Unused33() = 0;
	virtual void Unused34() = 0;
	virtual void Unused35() = 0;
	virtual void Unused36() = 0;
	virtual void Unused37() = 0;
	virtual void Unused38() = 0;
	virtual void Unused39() = 0;
	virtual void Unused40() = 0;
	virtual void Unused41() = 0;
	virtual void Unused42() = 0;
	virtual void Unused43() = 0;
	virtual void Unused44() = 0;
	virtual void Unused45() = 0;
	virtual void Unused46() = 0;
	virtual void Unused47() = 0;
	virtual void Unused48() = 0;
	virtual void Unused49() = 0;
	virtual void Unused50() = 0;
	virtual void Unused51() = 0;
	virtual void Unused52() = 0; // SetCursorAlwaysVisible(bool), never call.
	virtual bool IsCursorVisible() = 0; // 53, read-only.
};

static FreecamSurface030 * FreecamGetSurface030()
{
	static bool attempted = false;
	static FreecamSurface030 * surface = nullptr;
	if (!attempted && g_AppSystemFactory)
	{
		attempted = true;
		surface = static_cast<FreecamSurface030 *>(g_AppSystemFactory("VGUI_Surface030", nullptr));
	}
	return surface;
}

BvhExport * g_BvhExport = NULL;

BvhImport g_BvhImport;

// Create singelton instance:
Hook_VClient_RenderView g_Hook_VClient_RenderView;


unsigned int g_OfsCvarFloatValue;

float GetCvarFloat(void * pcvar)
{
	float * pf = (float *)(*(unsigned char **)pcvar +g_OfsCvarFloatValue);

	float f = *pf;

	return f;
}

void SetCvarFloat(void * pcvar, float value)
{
	float * pf = (float *)(*(unsigned char **)pcvar +g_OfsCvarFloatValue);

	*pf = value;
}


// Hook_VClient_RenderView /////////////////////////////////////////////////////

Hook_VClient_RenderView::Hook_VClient_RenderView()
: m_Globals(0)
, handleZoomEnabled(false)
, handleZoomMinUnzoomedFov(90.0)
{
	m_Export = false;
	m_FovOverride = false;
	m_FovValue = 0.0;
	m_Import = false;
	m_ImportBaseTime = 0;
	m_IsInstalled = false;

	LastCameraOrigin[0] = 0.0;
	LastCameraOrigin[1] = 0.0;
	LastCameraOrigin[2] = 0.0;
	LastCameraAngles[0] = 0.0;
	LastCameraAngles[1] = 0.0;
	LastCameraAngles[2] = 0.0;
	LastCameraFov = 90.0;

	CurrentCameraOrigin[0] = 0.0;
	CurrentCameraOrigin[1] = 0.0;
	CurrentCameraOrigin[2] = 0.0;
	CurrentCameraAngles[0] = 0.0;
	CurrentCameraAngles[1] = 0.0;
	CurrentCameraAngles[2] = 0.0;
	CurrentCameraFov = 90.0;

	m_MirvInput = new MirvInput(this);
	m_MirvInput->MenuAwareEscape_set(true);

	SetDefaultOverrides();
}


Hook_VClient_RenderView::~Hook_VClient_RenderView() {
	ExportEnd();
	ImportEnd();

	delete m_CamExport;
	delete m_CamImport;

	delete m_MirvInput;
}

bool Hook_VClient_RenderView::UiCursorStatus(bool & available, bool & visible) const
{
	// Other games may expose a different surface ABI.
	if (g_SourceSdkVer != SourceSdkVer_TF2 && g_SourceSdkVer != SourceSdkVer_TF2Classified)
	{
		available = false;
		visible = false;
		return false;
	}
	FreecamSurface030 * surface = FreecamGetSurface030();
	available = nullptr != surface;
	visible = available ? surface->IsCursorVisible() : false;
	return available;
}

bool Hook_VClient_RenderView::GetSuspendMirvInput() {
	if (g_VEngineClient && g_VEngineClient->Con_IsVisible()) return true;
	if (!m_MirvInput || !m_MirvInput->GetCameraControlMode()) return false;
	if (FreecamMenuVisible()) return true;

	// Optionally yield freecam input while TF2's UI cursor is visible.
	if (m_UiCursorSuspend)
	{
		bool available = false, visible = false;
		UiCursorStatus(available, visible);
		if (available && visible) return true;
	}
	return false;
}

void Hook_VClient_RenderView::GetLastCameraData(double & x, double & y, double & z, double & rX, double & rY, double & rZ, double & fov) {
	x = LastCameraOrigin[0];
	y = LastCameraOrigin[1];
	z = LastCameraOrigin[2];
	rX = LastCameraAngles[0];
	rY = LastCameraAngles[1];
	rZ = LastCameraAngles[2];
	fov = LastCameraFov;
}

void Hook_VClient_RenderView::GetGameCameraData(double & x, double & y, double & z, double & rX, double & rY, double & rZ, double & fov) {
	x = GameCameraOrigin[0];
	y = GameCameraOrigin[1];
	z = GameCameraOrigin[2];
	rX = GameCameraAngles[0];
	rY = GameCameraAngles[1];
	rZ = GameCameraAngles[2];
	fov = GameCameraFov;
}

double Hook_VClient_RenderView::GetInverseScaledFov(double fov) {
	return Auto_InverseFovScaling(LastWidth, LastHeight, fov);
}


bool Hook_VClient_RenderView::ExportBegin(wchar_t const *fileName, double frameTime) {
	ExportEnd();

	g_BvhExport = new BvhExport(fileName, "MdtCam", frameTime);
	m_Export = true;

	return m_Export;
}

void Hook_VClient_RenderView::ExportEnd() {

	delete g_BvhExport;
	g_BvhExport = 0;
	m_Export = false;
}


bool Hook_VClient_RenderView::GetFovOverride(double &outValue)
{
	outValue = m_FovValue;

	return m_FovOverride;
}

void Hook_VClient_RenderView::FovOverride(double value)
{
	m_FovValue = value;
	m_FovOverride = true;
}

void Hook_VClient_RenderView::FovDefault()
{
	m_FovOverride = false;
}

float Hook_VClient_RenderView::GetImportBasteTime() {
	return m_ImportBaseTime;
}

bool Hook_VClient_RenderView::ImportBegin(wchar_t const *fileName)
{
	ImportEnd();

	m_Import = g_BvhImport.LoadMotionFile(fileName);

	return m_Import;
}

void Hook_VClient_RenderView::ImportEnd() {
	g_BvhImport.CloseMotionFile();

	m_Import = false;
}

void Hook_VClient_RenderView::Install(WrpGlobals * globals)
{
	if(m_IsInstalled)
		return;

	m_Globals = globals;

	m_IsInstalled = true;
}

WrpGlobals * Hook_VClient_RenderView::GetGlobals()
{
	return m_Globals;
}

bool Hook_VClient_RenderView::IsInstalled(void) {
	return m_IsInstalled;
}

typedef void (__fastcall * csgo_C_HLTVCamera_SpecCameraGotoPos_t)(void * This, void * Edx, SOURCESDK::Vector vecPos, SOURCESDK::QAngle angAngle, int nPlayerIndex );

void TrySetView(float Tx, float Ty, float Tz, float Rx, float Ry, float Rz, float fov, float * outOrgRz, float * outOrgFov)
{
	static bool firstRun = true;
	static SOURCESDK::CSGO::IServerTools * serverTools = nullptr;

#ifndef _WIN64
	if (firstRun && g_Hook_VClient_RenderView.ForceViewOverride)
	{
		firstRun = false;
		
		if (SourceSdkVer_CSGO == g_SourceSdkVer || SourceSdkVer_CSCO == g_SourceSdkVer)
		{
			if (CClientToolsCsgo::Instance())
			{

				if (HMODULE hServerModule = GetModuleHandleA("server"))
				{
					if (SOURCESDK::CreateInterfaceFn createInterfaceFn = (SOURCESDK::CreateInterfaceFn)GetProcAddress(hServerModule, "CreateInterface"))
					{
						int returnCode = 0;

						serverTools = (SOURCESDK::CSGO::IServerTools *)createInterfaceFn(SOURCESDK_CSGO_VSERVERTOOLS_INTERFACE_VERSION, &returnCode);
					}
				}
			}

			if (!serverTools)
			{
				Tier0_Warning(
					"AFXERROR: Could not get interfaces required for server view override.\n");
			}
		}
	}

	if(g_Hook_VClient_RenderView.ForceViewOverrideHltv && g_VEngineClient && g_VEngineClient->IsHLTV() && AFXADDR_GET(csgo_client_s_HLTVCamera) && AFXADDR_GET(csgo_client_CHLTVCamera_SpecCameraGotoPos)) {
		csgo_C_HLTVCamera_SpecCameraGotoPos_t fn = (csgo_C_HLTVCamera_SpecCameraGotoPos_t)AFXADDR_GET(csgo_client_CHLTVCamera_SpecCameraGotoPos);
		SOURCESDK::Vector origin;
		SOURCESDK::QAngle angles;

		origin.x = Tx;
		origin.y = Ty;
		origin.z = Tz;

		angles.x = Rx;
		angles.y = Ry;
		angles.z = Rz;

		fn((void*)AFXADDR_GET(csgo_client_s_HLTVCamera), nullptr, origin, angles, 0);
	}

	if (g_Hook_VClient_RenderView.ForceViewOverride && serverTools)
	{
		if (SOURCESDK::C_BaseEntity_csgo * localPlayer = reinterpret_cast<SOURCESDK::C_BaseEntity_csgo *>(CClientToolsCsgo::Instance()->GetClientToolsInterface()->GetLocalPlayer()))
		{

			SOURCESDK::Vector origin;
			SOURCESDK::QAngle angles;

			if (outOrgRz)
			{
				serverTools->GetPlayerPosition(origin, angles, localPlayer);
				*outOrgRz = angles.z;
			}

			if (outOrgFov)
			{
				*outOrgFov = (float)serverTools->GetPlayerFOV(localPlayer);
			}

			origin.x = Tx;
			origin.y = Ty;
			origin.z = Tz;

			angles.x = Rx;
			angles.y = Ry;
			angles.z = Rz;

			serverTools->SnapPlayerToPosition(origin, angles, localPlayer);
			serverTools->SetPlayerFOV((int)fov, localPlayer);
		}
	}
#endif //#ifndef _WIN64
}

void Hook_VClient_RenderView::OnViewOverride(float &Tx, float &Ty, float &Tz, float &Rx, float &Ry, float &Rz, float &Fov)
{
	MirvCinematic_Update();
	MirvCampathPreview_Update();
	bool originOrAnglesOverriden = false;

	float curTime = g_MirvTime.GetTime();

	GameCameraOrigin[0] = Tx;
	GameCameraOrigin[1] = Ty;
	GameCameraOrigin[2] = Tz;
	GameCameraAngles[0] = Rx;
	GameCameraAngles[1] = Ry;
	GameCameraAngles[2] = Rz;
	GameCameraFov = Fov;

	for (int i = 0; i < 11; ++i)
	{
		CurrentCameraOrigin[0] = Tx;
		CurrentCameraOrigin[1] = Ty;
		CurrentCameraOrigin[2] = Tz;
		CurrentCameraAngles[0] = Rx;
		CurrentCameraAngles[1] = Ry;
		CurrentCameraAngles[2] = Rz;
		CurrentCameraFov = Fov;

		switch (m_Overrides[i])
		{
		case Override_CamSource:
			if (g_MirvCam.ApplySource(Tx, Ty, Tz, Rz, Rx, Ry)) originOrAnglesOverriden = true;
			break;

		case Override_Campath:
			if (MirvCinematic_OwnsCamera()) break;
			if (m_CamPath.Enabled_get() && m_CamPath.CanEval())
			{
				double campathCurTime = curTime - m_CamPath.GetOffset();
				if(m_CamPath.GetHold()) {
					if(campathCurTime > m_CamPath.GetUpperBound()) campathCurTime = m_CamPath.GetUpperBound();
					else if(campathCurTime < m_CamPath.GetLowerBound()) campathCurTime = m_CamPath.GetLowerBound();
				}
				// no extrapolation:
				if (m_CamPath.GetLowerBound() <= campathCurTime && campathCurTime <= m_CamPath.GetUpperBound())
				{
					CamPathValue val = m_CamPath.Eval(campathCurTime);
					QEulerAngles ang = val.R.ToQREulerAngles().ToQEulerAngles();

					//Tier0_Msg("================",curTime);
					//Tier0_Msg("currenTime = %f",curTime);
					//Tier0_Msg("vCp = %f %f %f\n", val.X, val.Y, val.Z);

					originOrAnglesOverriden = true;

					Tx = (float)val.X;
					Ty = (float)val.Y;
					Tz = (float)val.Z;

					Rx = (float)ang.Pitch;
					Ry = (float)ang.Yaw;
					Rz = (float)ang.Roll;

					Fov = (float)val.Fov;
				}
			}
			break;

		case Override_Bvh:
			if (m_Import)
			{
				double Tf[6];

				if (g_BvhImport.GetCamPosition(
					curTime - m_ImportBaseTime,
					Tf
				)) {
					originOrAnglesOverriden = true;

					Ty = (float)(-Tf[0]);
					Tz = (float)(+Tf[1]);
					Tx = (float)(-Tf[2]);
					Rz = (float)(-Tf[3]);
					Rx = (float)(-Tf[4]);
					Ry = (float)(+Tf[5]);
				}
			}
			break;

		case Override_Camio:
			if (m_CamImport)
			{
				CamIO::CamData camData;

				if (m_CamImport->GetCamData(curTime, LastWidth, LastHeight, camData))
				{
					originOrAnglesOverriden = true;

					Tx = (float)camData.XPosition;
					Ty = (float)camData.YPosition;
					Tz = (float)camData.ZPosition;
					Rx = (float)camData.YRotation;
					Ry = (float)camData.ZRotation;
					Rz = (float)camData.XRotation;
					Fov = (float)camData.Fov;
				}
			}
			break;

		case Override_Fov:
			if (m_FovOverride && (!handleZoomEnabled || handleZoomMinUnzoomedFov <= Fov)) Fov = (float)m_FovValue;
			break;

		case Override_Input:
            if (MirvCinematic_OwnsCamera()) {
                if (MirvCinematic_Override(Tx, Ty, Tz, Rx, Ry, Rz, Fov)) originOrAnglesOverriden = true;
                else {
                    Tx = GameCameraOrigin[0]; Ty = GameCameraOrigin[1]; Tz = GameCameraOrigin[2];
                    Rx = GameCameraAngles[0]; Ry = GameCameraAngles[1]; Rz = GameCameraAngles[2];
                    Fov = GameCameraFov;
                }
                break;
            }
			if (m_MirvInput->Override(m_LastFrameTime, Tx, Ty, Tz, Rx, Ry, Rz, Fov)) originOrAnglesOverriden = true;
			break;

		case Override_Aim:
			if (m_Globals)
			{
				double dRx = Rx;
				double dRy = Ry;
				double dRz = Rz;

				if (g_Aiming.Aim(m_Globals->absoluteframetime_get(), Vector3(Tx, Ty, Tz), dRx, dRy, dRz))
				{
					originOrAnglesOverriden = true;

					Rx = (float)dRx;
					Ry = (float)dRy;
					Rz = (float)dRz;
				}
			}
			break;

		case Override_CamOffset:
			if (g_MirvCam.ApplyOffset(Tx, Ty, Tz, Rz, Rx, Ry)) originOrAnglesOverriden = true;
			break;

		case Override_CamFov:
			g_MirvCam.ApplyFov(Fov);
			break;

		case Override_MirvPgl:
#ifndef _WIN64
			if (MirvPgl::OnViewOverride(Tx, Ty, Tz, Rx, Ry, Rz, Fov)) originOrAnglesOverriden = true;
#endif //#ifndef _WIN64
			break;

		case Override_Interop:
#ifndef _WIN64
#ifdef AFX_INTEROP
			if (AfxInterop::OnViewOverride(Tx, Ty, Tz, Rx, Ry, Rz, Fov)) originOrAnglesOverriden = true;
#endif
#endif //#ifndef _WIN64
			break;
		}

		// limit fov to sane values:
		if (Fov < 1) Fov = 1;
		else if (Fov > 179) Fov = 179;
	}
	
	static bool viewOverriding = false;

	if (originOrAnglesOverriden && (this->ForceViewOverride || this->ForceViewOverrideHltv))
	{
		TrySetView(Tx, Ty, Tz, Rx, Ry, Rz, Fov, nullptr, nullptr);

		viewOverriding = true;
	}
	else if (viewOverriding)
	{
		viewOverriding = false;

		if (this->ViewOverrideReset)
		{
			TrySetView(Tx, Ty, Tz, Rx, Ry, 0.0f, 90.0f, nullptr, nullptr);
		}
	}


	if(m_Export) {
		g_BvhExport->WriteFrame(
			-Ty, +Tz, -Tx,
			-Rz, -Rx, +Ry
		);
	}

	if (m_CamExport)
	{
		CamIO::CamData camData;

		camData.Time = curTime;
		camData.XPosition = Tx;
		camData.YPosition = Ty;
		camData.ZPosition = Tz;
		camData.YRotation = Rx;
		camData.ZRotation = Ry;
		camData.XRotation = Rz;
		camData.Fov = Fov;

		m_CamExport->WriteFrame(LastWidth, LastHeight, camData);
	}

	LastCameraOrigin[0] = Tx;
	LastCameraOrigin[1] = Ty;
	LastCameraOrigin[2] = Tz;
	LastCameraAngles[0] = Rx;
	LastCameraAngles[1] = Ry;
	LastCameraAngles[2] = Rz;
	LastCameraFov = Fov;

	m_MirvInput->Supply_MouseFrameEnd();

	// Free-camera movement uses wall-clock frame time, not the demo-scaled
	// engine absolute frame time. Campath preview keeps its own timing.
	using FreeCameraClock = std::chrono::steady_clock;
	static FreeCameraClock::time_point lastFreeCameraFrame = FreeCameraClock::now();
	const FreeCameraClock::time_point nowFreeCameraFrame = FreeCameraClock::now();
	const double elapsed = std::chrono::duration<double>(nowFreeCameraFrame - lastFreeCameraFrame).count();
	lastFreeCameraFrame = nowFreeCameraFrame;
	// Avoid large jumps after loading, debugging, or switching applications.
	const float realFrameTime = (float)(elapsed < 0.0 ? 0.0 : (elapsed > 0.1 ? 0.1 : elapsed));
	m_LastFrameTime = m_MirvInput->IgnoreTimescale_get()
		? realFrameTime : m_Globals->absoluteframetime_get();

	//Tier0_Msg("Hook_VClient_RenderView::OnViewOverride: curTime = %f, LastCameraOrigin=%f,%f,%f\n",curTime,LastCameraOrigin[0],LastCameraOrigin[1],LastCameraOrigin[2]);
}

void Hook_VClient_RenderView::SetImportBaseTime(float value) {
	m_ImportBaseTime = value;
}

bool Hook_VClient_RenderView::ImportToCamPath(bool adjustInterp, double fov)
{
	if(!m_Import)
		return false;

	bool bOk = g_BvhImport.CopyToCampath(m_ImportBaseTime, fov, m_CamPath);

	if(bOk)
	{
		ImportEnd();

		if(adjustInterp)
		{
			m_CamPath.PositionInterpMethod_set(CamPath::DI_LINEAR);
			m_CamPath.RotationInterpMethod_set(CamPath::QI_SLINEAR);
			m_CamPath.FovInterpMethod_set(CamPath::DI_LINEAR);
		}

		m_CamPath.Enabled_set(true);
	}

	return bOk;
}

double MirvCamIO_GetTimeFn(void) {
	return g_MirvTime.GetTime();
}

void Hook_VClient_RenderView::Console_CamIO(IWrpCommandArgs * args)
{
	MirvCamIO_ConsoleCommand(args, m_CamImport, m_CamExport, MirvCamIO_GetTimeFn);
}

void Hook_VClient_RenderView::SetDefaultOverrides()
{
	m_Overrides[0] = Override_CamSource;
	m_Overrides[1] = Override_Campath;
	m_Overrides[2] = Override_Bvh;
	m_Overrides[3] = Override_Camio;
	m_Overrides[4] = Override_Fov;
	m_Overrides[5] = Override_Input;
	m_Overrides[6] = Override_Aim;
	m_Overrides[7] = Override_CamOffset;
	m_Overrides[8] = Override_CamFov;
	m_Overrides[9] = Override_MirvPgl;
	m_Overrides[10] = Override_Interop;
}

bool Hook_VClient_RenderView::OverrideFromString(const char * value, Override & outOverride)
{
	if (0 == _stricmp("camSource", value)) {
		outOverride = Override_CamSource;
		return true;
	}
	if (0 == _stricmp("camPath", value)) {
		outOverride = Override_Campath;
		return true;
	}
	if (0 == _stricmp("bvh", value)) {
		outOverride = Override_Bvh;
		return true;
	}
	if (0 == _stricmp("camIo", value)) {
		outOverride = Override_Camio;
		return true;
	}
	if (0 == _stricmp("fov", value)) {
		outOverride = Override_Fov;
		return true;
	}
	if (0 == _stricmp("input", value)) {
		outOverride = Override_Input;
		return true;
	}
	if (0 == _stricmp("aim", value)) {
		outOverride = Override_Aim;
		return true;
	}
	if (0 == _stricmp("camOffset", value)) {
		outOverride = Override_CamOffset;
		return true;
	}
	if (0 == _stricmp("camFov", value)) {
		outOverride = Override_CamFov;
		return true;
	}
	if (0 == _stricmp("mirvPgl", value)) {
		outOverride = Override_MirvPgl;
		return true;
	}
	if (0 == _stricmp("interop", value)) {
		outOverride = Override_Interop;
		return true;
	}

	return false;
}

void Hook_VClient_RenderView::Console_Overrides(IWrpCommandArgs * args)
{
	int argC = args->ArgC();
	const char * arg0 = args->ArgV(0);

	if (2 <= argC)
	{
		const char * arg1 = args->ArgV(1);

		if (0 == _stricmp("default", arg1))
		{
			SetDefaultOverrides();
			return;
		}
		else if (0 == _stricmp("print", arg1))
		{
			const char * text[11] = {
				"camSource (of mirv_cam)"
				, "camPath"
				, "bvh"
				, "camIo"
				, "fov (mirv_fov)"
				, "input (mirv_input)"
				, "aim (mirv_aim)"
				, "camOffset (of mirv_cam)"
				, "camFov (of mirv_cam)"
				, "mirvPgl"
				, "interop (afx_interop)"
			};

			for (int i = 0; i < 11; ++i)
			{
				int value = m_Overrides[i];

				if (0 <= value && value < 11)
				{
					Tier0_Msg("%i: %s\n", i, text[m_Overrides[i]]);
				}
				else Tier0_Msg("%i: [n/a]\n", i);
			}
			return;
		}
		else if (0 == _stricmp("move", arg1) && 4 == argC)
		{
			Override overrideVal;

			if (!OverrideFromString(args->ArgV(2), overrideVal))
			{
				Tier0_Warning("AFXERROR: %s is not a valid overide name.\n", args->ArgV(2));
				return;
			}

			int pos = atoi(args->ArgV(3));

			if (pos < 0) pos = 0;
			else if (9 < pos) pos = 9;

			if (m_Overrides[pos] != overrideVal)
			{
				int orgPos;

				for (int i = 0; i < 11; ++i)
				{
					if (m_Overrides[i] == overrideVal)
					{
						orgPos = i;
						break;
					}
				}

				if (pos < orgPos)
				{
					for (int i = orgPos; i > pos; --i)
					{
						m_Overrides[i] = m_Overrides[i - 1];
					}
				}
				else // if (pos > orgPos)
				{
					for (int i = orgPos; i < pos; ++i)
					{
						m_Overrides[i] = m_Overrides[i + 1];
					}
				}

				m_Overrides[pos] = overrideVal;
			}

			return;
		}
	}

	Tier0_Msg(
		"%s default - Restore default override order.\n"
		"%s print - Print current override order.\n"
		"%s move <sOverrideName> <iPos> - Move override <sOverrideName> to position <iPos>.\n"
		, arg0
		, arg0
		, arg0
	);
}