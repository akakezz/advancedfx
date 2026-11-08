#include "stdafx.h"

#include "MirvCam.h"

#include "WrpVEngineClient.h"
#include "WrpConsole.h"

#include "RenderView.h"
#include "addresses.h"

#include <shared/AfxMath.h>

#include <string>
#include <algorithm>
#include <ctype.h>
#include <stdlib.h>

// How mirv_cam attach gets the angles when a bone is used.
static int g_MirvCamAttachMode = MIRV_TF2_BONEMODE_ALIGNED;

// The calc made by the last mirv_cam attach (used by mirv_cam attach capture).
static IMirvVecAngCalc * g_MirvCamAttachCalc = 0;

static void MirvCam_SetAttachCalc(IMirvVecAngCalc * calc)
{
	if (calc) calc->AddRef();
	if (g_MirvCamAttachCalc) g_MirvCamAttachCalc->Release();
	g_MirvCamAttachCalc = calc;
}

static char const * MirvCam_AttachModeName(int mode)
{
	return MIRV_TF2_BONEMODE_ENTITY == mode ? "entity" : (MIRV_TF2_BONEMODE_ALIGNED == mode ? "aligned" : "bone");
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
		if (!_stricmp(field, "free") || !_stricmp(field, "campath") || !_stricmp(field, "players"))
		{
			valid = boolean;
			if (!_stricmp(field, "free"))
				settings.Free = !std::strcmp(value, "1");
			else if (!_stricmp(field, "campath"))
				settings.Campath = !std::strcmp(value, "1");
			else
				settings.Players = !std::strcmp(value, "1");
		}
		else if (!_stricmp(field, "radius"))
		{
			char *end = nullptr;
			settings.Radius = std::strtod(value, &end);
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
		              "enabled; radius must be finite, 1..64 units.\n");
	settings = MirvCameraCollisionGet();
	Tier0_Msg("Camera collision: free %d, campath %d, radius %.2f, players %d, response %s.\n", settings.Free,
	          settings.Campath, settings.Radius, settings.Players, settings.Slide ? "slide" : "stop");
	if (argc == 2 || !valid)
		Tier0_Msg("mirv_cam collision free|campath|players 0|1\n"
		          "mirv_cam collision radius <1..64> | response stop|slide | reset | status\n"
		          "Both camera switches default off. Local recorder is ignored; other client-solid player "
		          "hulls are optional.\n"
		          "Correction changes only the live view. Deeply embedded starts may require disabling "
		          "collision and moving clear.\n");
}
// End camera collision console command.

CON_COMMAND(mirv_cam, "Control camera source entity and offset.")
{
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
			if (!(SourceSdkVer_TF2 == g_SourceSdkVer || SourceSdkVer_TF2Classified == g_SourceSdkVer))
			{
				Tier0_Warning("mirv_cam attach is only supported for TF2.\n");
				return;
			}

			if (3 <= argc && 0 == _stricmp("clear", args->ArgV(2)))
			{
				g_MirvCam.Source_set(0);
				MirvCam_SetAttachCalc(0);
				return;
			}

			if (3 <= argc && 0 == _stricmp("capture", args->ArgV(2)))
			{
				if (!g_MirvCamAttachCalc || g_MirvCam.Source_get() != g_MirvCamAttachCalc)
				{
					Tier0_Warning("The camera is not attached, use mirv_cam attach <target> [<bone>] first.\n");
					return;
				}

				float localOffset[3];

				if (!Mirv_Tf2_CaptureCamera(g_MirvCamAttachCalc, g_Hook_VClient_RenderView.LastCameraOrigin, g_Hook_VClient_RenderView.LastCameraAngles, localOffset))
				{
					Tier0_Warning("Could not capture the camera, the target is not available right now.\n");
					return;
				}

				// The captured pose already includes everything, so the offsets must not be applied on top of it.
				g_MirvCam.m_OffsetForwad = 0;
				g_MirvCam.m_OffsetLeft = 0;
				g_MirvCam.m_OffsetUp = 0;
				g_MirvCam.m_OffsetForwardRot = 0;
				g_MirvCam.m_OffseLeftRot = 0;
				g_MirvCam.m_OffsetUpRot = 0;

				Tier0_Msg(
					"Captured the current camera relative to the target (forward %.2f, left %.2f, up %.2f), it now follows the target.\n"
					"mirv_cam offset was cleared. If you used the free camera, end it with mirv_input end.\n"
					, localOffset[0], localOffset[1], localOffset[2]
				);
				return;
			}

			if (3 <= argc)
			{
				const char * target = args->ArgV(2);
				int entityIndex = -1;

				bool numeric = '\0' != target[0];
				for (const char * c = target; *c; ++c)
				{
					if (*c < '0' || '9' < *c) { numeric = false; break; }
				}

				if (numeric)
				{
					entityIndex = atoi(target);
				}
				else
				{
					// Find a player by (part of) the name, ignoring case.
					std::string needle(target);
					std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)tolower(c); });

					int matches = 0;
					std::string matchList;
					const int maxPlayers = Mirv_Tf2_GetMaxPlayers();

					for (int i = 1; i <= maxPlayers; ++i)
					{
						char name[64];
						if (!Mirv_Tf2_GetPlayerInfo(i, name, sizeof(name), 0))
							continue;

						std::string hay(name);
						std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return (char)tolower(c); });

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
						Tier0_Warning("More than one player matches \"%s\", use the entity index instead:\n%s", target, matchList.c_str());
						return;
					}
				}

				if (entityIndex < 0 || 2048 <= entityIndex)
				{
					Tier0_Warning("Entity index %i is out of range.\n", entityIndex);
					return;
				}

				int boneIndex = -1;

				if (4 <= argc && !Mirv_Tf2_ParseBoneArg(entityIndex, args->ArgV(3), boneIndex))
					return;

				{
					IMirvVecAngCalc * calc = g_MirvVecAngCalcs.NewTf2EntityIndexCalc(0, entityIndex, boneIndex, g_MirvCamAttachMode);

					g_MirvCam.Source_set(calc);
					MirvCam_SetAttachCalc(calc);
				}

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

				if (0 <= boneIndex)
					Tier0_Msg("Camera source set to entity %i, bone %i (angles: %s).\n", entityIndex, boneIndex, MirvCam_AttachModeName(g_MirvCamAttachMode));
				else
					Tier0_Msg("Camera source set to entity %i.\n", entityIndex);
				return;
			}

			Tier0_Msg(
				"mirv_cam attach <entityIndex or part of player name> [<iBoneIndex or part of bone name> [<fXForward> <fYLeft> <fZUp> [<fRotXForward> <fRotYLeft> <fRotZUp>]]] - (TF2) Use the entity (or its bone) as camera source, optionally with offset (same as mirv_cam offset). Use -1 as bone for no bone.\n"
				"mirv_cam attach capture - Use the current camera (i.e. the free camera from mirv_input camera) as the camera's position and angles relative to the attached target. Do mirv_cam attach first, fly the free camera to the framing you want, then capture and end the free camera with mirv_input end.\n"
				"mirv_cam attach clear - Clear the camera source.\n"
				"Use mirv_listplayers to find players and mirv_listbones to find bones. The angles used for bones are controlled by mirv_cam attachMode.\n"
			);
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
				else
					Tier0_Warning("Unknown mode \"%s\", use bone, entity or aligned.\n", modeName);

				return;
			}

			Tier0_Msg(
				"mirv_cam attachMode bone|entity|aligned - Angles for mirv_cam attach with a bone:\n"
				"bone - The bone's own angles (depends on the skeleton, usually needs rotation offsets).\n"
				"entity - The entity's facing (for players the body direction), the position is still the bone's.\n"
				"aligned - Starts looking like the entity (facing forward) and then follows the bone's rotation.\n"
				"Current value: %s\n"
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
		"mirv_cam source [...] - Control camera location.\n"
		"mirv_cam fov [...] - Control camera fov.\n"
		"mirv_cam attach [...] - (TF2) Attach the camera to a player or entity (and bone).\n"
		"mirv_cam attachMode [...] - (TF2) Controls which angles attach uses for bones.\n"
		"mirv_cam offset [...] - Control camera offset (in local space).\n"
		"mirv_cam order [...] - Control order of camera overrides.\n"
		"mirv_cam collision [...] - Optional TF2/TF2C free-camera and campath collision.\n"
	);
}
