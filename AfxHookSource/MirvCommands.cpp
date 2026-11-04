#include "stdafx.h"

// Hint: for now commands are registered upon the first client.dll CreateInterface() call

#include <shared/StringTools.h>

#include <SourceInterfaces.h>

#include <shared/CinematicPath.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <locale>
#include <limits>
#include <stdexcept>
#include <utility>
#include "RenderView.h"
#include "MirvTime.h"

#include "WrpVEngineClient.h"
#include "WrpConsole.h"

#ifndef _WIN64
#include "csgo_CHudDeathNotice.h"
#include "csgo_SndMixTimeScalePatch.h"
#endif //#ifndef _WIN64

#include <shared/hooks/gameOverlayRenderer.h>
#include "AfxStreams.h"
#include "addresses.h"
#include "CampathDrawer.h"

#ifndef _WIN64
#include "csgo_S_StartSound.h"
#endif //#ifndef _WIN64

#include "d3d9Hooks.h"
#include "aiming.h"
#include "EntityIndexDrawer.h"
#include "tf2/ClientToolsTf2.h"
#include "../shared/CommandSystem.h"
#include <shared/binutils.h>

#ifndef _WIN64
#include "csgo/ClientToolsCSgo.h"
#include "csgo_CBasePlayer.h"
#include "csgo_CCSGameMovement.h"
#include "csgo_vphysics.h"
#include "csgo_c_baseentity.h"
#include "csgo_c_baseanimatingoverlay.h"
#endif //#ifndef _WIN64

#include "../shared/FovScaling.h"

#ifndef _WIN64
//#include "csgo_CDemoFile.h"
#include "csgo_Stdshader_dx9_Hooks.h"
#include "csgo_models_replace.h"
//#include <csgo/sdk_src/public/tier0/memalloc.h>
#endif //#ifndef _WIN64

#include <shared/MirvCampath.h>
#include <shared/AfxDetours.h>
#include "../shared/MirvSkip.h"

#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

extern WrpVEngineClient * g_VEngineClient;

HMODULE g_H_EngineDll = 0;


#if AFXSTREAMS_REFTRACKER

CON_COMMAND(__mirv_streams_ref, "")
{
	Tier0_Msg("Current tracked count: %i\n", AfxStreams_RefTracker_Get());
}

#endif

#ifdef _DEBUG
CON_COMMAND(__mirv_dumpmemoryleaks, "")
{
	_CrtDumpMemoryLeaks();
}

CON_COMMAND(__mirv_memdiff, "")
{
	static int state = 0;
	static _CrtMemState s1;
	static _CrtMemState s2;
	_CrtMemState s3;

	switch (state)
	{
	case 0:
		_CrtMemCheckpoint(&s1);
		state = 1;
		break;
	case 1:
		_CrtMemCheckpoint(&s2);
		if (_CrtMemDifference(&s3, &s1, &s2))
			_CrtMemDumpStatistics(&s3);
		state = 2;
		break;
	case 2:
		_CrtMemCheckpoint(&s1);
		if (_CrtMemDifference(&s3, &s2, &s1))
			_CrtMemDumpStatistics(&s3);
		state = 1;
		break;
	}
}

#endif

void PrintInfo();

CON_COMMAND(__mirv_info, "")
{
	PrintInfo();
}


void mirv_test_msg_parallel_func() {
	for (size_t i = 0; i < 150; i++) {
		Sleep(150);
		Tier0_Warning("Thread: %i @ %i\n", i, GetTickCount());
	}
}

CON_COMMAND(__mirv_test_msg_parallel, "") {
	Tier0_Warning("Main: START @ %i\n", GetTickCount());
	std::thread thread(mirv_test_msg_parallel_func);
	for (size_t i = 0; i < 150; i++) {
		Sleep(100);
		Tier0_Warning("Main: %i @ %i\n", i, GetTickCount());
	}
	Tier0_Warning("Main: JOIN @ %i\n", GetTickCount());
	thread.join();
	Tier0_Warning("Main: FINISH @ %i\n", GetTickCount());
}


#ifndef _WIN64

CON_COMMAND(__mirv_test6, "")
{
	int argc = args->ArgC();

	if (2 > argc)
		return;


	if (!SOURCESDK::g_Entitylist_csgo)
		return;

	int idx = atoi(args->ArgV(1));
	
	SOURCESDK::IClientEntity_csgo * ce = SOURCESDK::g_Entitylist_csgo->GetClientEntity(idx);
	SOURCESDK::C_BaseEntity_csgo * be = ce ? ce->GetBaseEntity() : 0;

	if (be)
	{
		static float old_m_flAnimTime = 0;
		static float old_m_flSimualtionTime = 0;
		static float old_curtime = 0;

		float m_flAnimTime = *(float *)((char *)be +0x25c);
		float m_flSimulationTime = *(float *)((char *)be + 0x264);
		float curtime = g_Hook_VClient_RenderView.GetGlobals()->curtime_get();

		float delta_m_flAnimTime = m_flAnimTime - old_m_flAnimTime;
		float delta_m_flSimualtionTime = m_flSimulationTime - old_m_flSimualtionTime;
		float delta_curtime = curtime - old_curtime;

		bool oneIsNeg = delta_m_flAnimTime < 0 || delta_m_flSimualtionTime < 0 || delta_curtime < 0;

		if (!oneIsNeg)
			Tier0_Msg(
				"%i: animTime=%f (%f), simulationTime=%f (%f) | curTime=%f (%f)\n"
				, idx, m_flAnimTime, delta_m_flAnimTime, m_flSimulationTime, delta_m_flSimualtionTime, curtime, delta_curtime
			);
		else
			Tier0_Warning(
				"%i: animTime=%f (%f), simulationTime=%f (%f) | curTime=%f (%f)\n"
				, idx, m_flAnimTime, delta_m_flAnimTime, m_flSimulationTime, delta_m_flSimualtionTime, curtime, delta_curtime
			);

		old_m_flAnimTime = m_flAnimTime;
		old_m_flSimualtionTime = m_flSimulationTime;
		old_curtime = curtime;
	}
}

CON_COMMAND(__mirv_ct, "")
{
	int argc = args->ArgC();

	if (2 <= argc)
	{
		char const * cmd1 = args->ArgV(1);

		if (!_stricmp("debug", cmd1) && 3 <= argc)
		{
			if(CClientToolsCsgo * instance = CClientToolsCsgo::Instance()) instance->DebugEntIndex(atoi(args->ArgV(2)));
		}

	}
}

#endif //#ifndef _WIN64


CON_COMMAND(__mirv_addr, "")
{
	int argc = args->ArgC();

	if (3 <= argc)
	{
		HMODULE hModule = GetModuleHandleA(args->ArgV(1));

		if (!hModule)
		{
			Tier0_Warning("Invalid module name.\n");
			return;
		}

		char const * cmd1 = args->ArgV(2);

		Afx::BinUtils::ImageSectionsReader sections(hModule);
		if (!sections.Eof())
		{
			Afx::BinUtils::MemRange search = sections.GetMemRange();

			Afx::BinUtils::MemRange result;

			int numResults = 0;

			while (!search.IsEmpty())
			{
				result = FindPatternString(search, cmd1);

				if (result.IsEmpty())
					break;

				Tier0_Msg(
					"%i: [0x%08x,0x%08x)\n",
					numResults,
					result.Start - (size_t)hModule,
					result.End - (size_t)hModule
				);

				search = Afx::BinUtils::MemRange(result.End, search.End);

				++numResults;

				if (100 < numResults)
				{
					Tier0_Msg("RESULT LIMIT REACHED\n");
					break;
				}
			}
		}
	}
	else
		Tier0_Msg("__mirv_addr <moduleName> <hexPatternString>\n");
}

CON_COMMAND(__mirv_test3,"") {
	static int offset = 13;

	if(2 <= args->ArgC()) offset = atoi(args->ArgV(1));

	if(g_Hook_VClient_RenderView.GetGlobals()) {
		void * pGlobals = g_Hook_VClient_RenderView.GetGlobals()->GetRaw();
		advancedfx::Message("g_pGlobals[%i]: int: %i , float: %f\n",offset,(pGlobals ? *(int *)((unsigned char *)pGlobals +offset*sizeof(int)) : 0),(pGlobals ? *(float *)((unsigned char *)pGlobals +offset*sizeof(float)) : 0));
	}
}

CON_COMMAND(__mirv_test2, "")
{
	WrpVEngineClientDemoInfoEx * di = g_VEngineClient->GetDemoInfoEx();

	if(di && g_Hook_VClient_RenderView.GetGlobals())
	{
		Tier0_Msg(
			"GetDemoRecordingTick=%i\n"
			"GetDemoPlaybackTick=%i\n"
			"GetDemoPlaybackStartTick=%i\n"
			"GetDemoPlaybackTimeScale=%f\n"
			"GetDemoPlaybackTotalTicks=%i\n",
			di->GetDemoRecordingTick(),
			di->GetDemoPlaybackTick(),
			di->GetDemoPlaybackStartTick(),
			di->GetDemoPlaybackTimeScale(),
			di->GetDemoPlaybackTotalTicks()
		);

		double curTime = g_Hook_VClient_RenderView.GetGlobals()->curtime_get();
		int client_current_tick = di->GetDemoPlaybackTick();
		double tick_interval = g_Hook_VClient_RenderView.GetGlobals()->interval_per_tick_get();
		double interpolation_amount = g_Hook_VClient_RenderView.GetGlobals()->interpolation_amount_get();

		double result = client_current_tick * tick_interval + interpolation_amount* tick_interval;

		Tier0_Msg(
			"curtime(%f) == client_current_tick(%i) * tick_interval(%f) +interpolation_amount(%f) * tick_interval == %f\n",
			curTime,
			client_current_tick,
			tick_interval,
			interpolation_amount,
			result
		);
	}
}

bool g_bD3D9DebugPrint = false;

CON_COMMAND(__mirv_test, "")
{
	g_bD3D9DebugPrint = true;
}

#ifndef _WIN64
CON_COMMAND(__mirv_skyboxscale, "print skyboxscale in CS:GO")
{
	if(AFXADDR_GET(csgo_C_BasePlayer_OFS_m_skybox3d_scale) != (AfxAddr)-1) {
		if(auto pLocalPlayer = CClientToolsCsgo::GetLocalPlayer())
		{
			int skyBoxScale = *(int *)((unsigned char *)pLocalPlayer +AFXADDR_GET(csgo_C_BasePlayer_OFS_m_skybox3d_scale));

			Tier0_Msg("skyBoxScale: %i\n", skyBoxScale);
			return;
		}
	}
	Tier0_Msg("skyBoxScale: n/a\n");
}
#endif //#ifndef _WIN64

#ifndef _WIN64
CON_COMMAND(__mirv_show_renderview_count, "") {
	int argc = args->ArgC();

	if (2 <= argc) {
		g_AfxStreams.Console_ShowRenderViewCountSet(0 != atoi(args->ArgV(1)));
		return;
	}

	Tier0_Msg(
		"%s 0|1\n"
		"Current value: %i\n",
		args->ArgV(0),
		g_AfxStreams.Console_ShowRenderViewCountGet() ? 1 : 0
	);
}
#endif //#ifndef _WIN64


CON_COMMAND(mirv_streams, "Access to streams system.")
{
#ifndef _WIN64
	bool bIsCsgo = g_SourceSdkVer == SourceSdkVer::SourceSdkVer_CSGO || g_SourceSdkVer == SourceSdkVer_CSCO;
#endif //#ifndef _WIN64
	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * cmd1 = args->ArgV(1);

#ifndef _WIN64
		if(bIsCsgo && 0 == _stricmp(cmd1, "add"))
		{
			if(3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				if(!_stricmp(cmd2, "normal"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add normal <name> - Add a normal stream with name <name>.\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "baseFx"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddBaseFxStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add baseFx <name> - Add a baseFx stream with name <name>.\n"
					);
					return;
				}
				else if (!_stricmp(cmd2, "hudWhite"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddHudWhiteStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add hudWhite <name> - Add a hud stream on white background with name <name>.\n"
					);
					return;
				}
				else if (!_stricmp(cmd2, "hudBlack"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddHudBlackStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add hudBlack <name> - Add a hud stream on black background with name <name>.\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "depth"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddDepthStream(cmd3, !(5 <= argc && 0 == _stricmp(args->ArgV(4), "draw")));

						return;
					}

					Tier0_Msg(
						"mirv_streams add depth <name> [draw]- Add a depth stream with name <name>. Draw to force the old method.\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "matteWorld"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddMatteWorldStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add matteWorld <name> - Add a matte world stream with name <name>.\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "depthWorld"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddDepthWorldStream(cmd3, !(5 <= argc && 0 == _stricmp(args->ArgV(4), "draw")));

						return;
					}

					Tier0_Msg(
						"mirv_streams add depthWorld <name> [draw] - Add a depth world stream with name <name>. Draw to force the old method.\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "matteEntity"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddMatteEntityStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add matteEnitity <name> - Add a matte entity stream with name <name>.\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "depthEntity"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddDepthEntityStream(cmd3, !(5 <= argc && 0 == _stricmp(args->ArgV(4), "draw")));

						return;
					}

					Tier0_Msg(
						"mirv_streams add depthEntity <name> [draw] - Add a depth entity stream with name <name>. Draw to force the old method.\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "alphaMatteEntity"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddAlphaMatteEntityStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add alphaMatteEntity <name> - DEPRECATED: Add an alpha matte entity stream with name <name>.\n"
					);
					return;
				}				
				else
				if(!_stricmp(cmd2, "alphaMatte"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddAlphaMatteStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add alphaMatte <name> - DEPRECATED: Add an alpha matte stream with name <name>.\n"
					);
					return;
				}				
				else
				if(!_stricmp(cmd2, "alphaEntity"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddAlphaEntityStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add alphaEntity <name> -DEPRECATED:  Add an alpha entity stream with name <name>.\n"
					);
					return;
				}				
				else
				if(!_stricmp(cmd2, "alphaWorld"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddAlphaWorldStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add alphaWorld <name> - DEPRECATED: Add an alpha world stream with name <name>.\n"
					);
					return;
				}	
				else if(!_stricmp(cmd2, "matte"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddMatteStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add matte <name> - Add a matte stream with name <name>.\n"
					);
					return;
				}	
			}

			Tier0_Msg(
				"mirv_streams add normal [...] - Add a normal stream.\n"
				"mirv_streams add baseFx [...] - Add a stream that allows effects, but should not look different from normal stream unless you edit its settings.\n"
				//"mirv_streams add hud [...] - Add a HUD stream (this is two streams (hudWhite / hudBlack) combined into one (RGBA)).\n"
				"mirv_streams add hudWhite [...] - Add a HUD stream on white background.\n"
				"mirv_streams add hudBlack [...] - Add a HUD stream on black background.\n"
				"mirv_streams add depth [...] - Add a depth stream.\n"
				"mirv_streams add matteWorld [...] - Add a matte world stream.\n"
				"mirv_streams add depthWorld [...] - Add a depth world stream.\n"
				"mirv_streams add matteEntity [...] - Add a matte entity stream.\n"
				"mirv_streams add depthEntity [...] - Add a depth entity stream.\n"
				"mirv_streams add matte [...] - Add a matte stream (use drawMatte action to set what shall be drawn inside the matte).\n"
			);
			return;
		}
		else
		if(bIsCsgo && 0 == _stricmp(cmd1, "edit"))
		{
			if(3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				CSubWrpCommandArgs subArgs(args, 3);

				g_AfxStreams.Console_EditStream(cmd2, &subArgs);
				return;
			}

			Tier0_Msg(
				"mirv_streams edit <streamName> [...] - Edit the stream with name <streamName>, you can get the value from mirv_streams print. <streamName> can match multiple streams when using wildcards (\\* = wildcard and \\\\ = \\), however this will only do something useful when the streams matched are compatible!\n"
			);
			return;
		}
		else if (bIsCsgo && 0 == _stricmp(cmd1, "move"))
		{
			CSubWrpCommandArgs subArgs(args, 2);

			g_AfxStreams.Console_MoveStream(&subArgs);
			return;
		}
		else
		if(bIsCsgo && 0 == _stricmp(cmd1, "remove"))
		{
			if(3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);
				
				g_AfxStreams.Console_RemoveStream(cmd2);
				return;
			}

			Tier0_Msg(
				"mirv_streams remove <streamName> - Remove a stream with name <streamName>, you can get the name from mirv_streams print.\n"
			);
			return;
		}
		else
		if(bIsCsgo && 0 == _stricmp(cmd1, "preview"))
		{
			if(3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				if (4 <= argc)
				{
					g_AfxStreams.Console_PreviewStream(cmd2, atoi(args->ArgV(3)));
					return;
				}
				
				g_AfxStreams.Console_PreviewStream(cmd2, 0);
				return;
			}

			Tier0_Msg(
				"mirv_streams preview <streamName> [<iSlot>]- Preview the stream with name <streamName>, you can get the name from mirv_streams print. To end previewing enter \"\" (empty string) for <streamName>!\n"
			);
			return;
		}
		else
		if(bIsCsgo && 0 == _stricmp(cmd1, "previewEnd"))
		{
			if (3 <= argc)
			{
				g_AfxStreams.Console_PreviewStream("", atoi(args->ArgV(2)));
				return;
			}

			g_AfxStreams.Console_PreviewStream("", -1);
			return;
		}
		else
		if (bIsCsgo && 0 == _stricmp(cmd1, "previewSuspend"))
		{
			if (3 <= argc)
			{
				g_AfxStreams.Console_PreviewSuspend_set(0 != atoi(args->ArgV(2)));
				return;
			}

			Tier0_Msg(
				"mirv_streams previewSuspend 0|1 - If to suspend preview of all streams.\n"
				"Current value: %i\n",
				g_AfxStreams.Console_PreviewSuspend_get() != 0 ? 1 : 0);
			return;
		}
		else
		if(bIsCsgo && 0 == _stricmp(cmd1, "print"))
		{
			g_AfxStreams.Console_PrintStreams();

			return;
		}
		else
		if(bIsCsgo && 0 == _stricmp(cmd1, "print2"))
		{
			g_AfxStreams.Console_PrintStreams2();

			return;
		}
		else
#endif //#ifndef _WIN64
		if(!_stricmp(cmd1, "record"))
		{
			if(3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				if(!_stricmp(cmd2, "name"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_RecordName_set(cmd3);
						return;
					}

					Tier0_Msg(
						"mirv_streams record name <name> - Set record name to <name>.\n"
						"Current value: %s.\n",
						g_AfxStreams.Console_RecordName_get()
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "start"))
				{
					g_AfxStreams.Console_Record_Start();
					return;
				}
				else
				if(!_stricmp(cmd2, "end"))
				{
					g_AfxStreams.Console_Record_End();
					return;
				}
				else
				if(!_stricmp(cmd2, "format"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_RecordFormat_set(cmd3);
						return;
					}

					Tier0_Msg(
						"mirv_streams record format tga|bmp - Set record format to tga or bmp.\n"
						"Current value: %s.\n",
						g_AfxStreams.Console_RecordFormat_get()
					);
					return;
				}
#ifndef _WIN64				
				else
				if(bIsCsgo && 0 == _stricmp(cmd2, "presentOnScreen"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_PresentRecordOnScreen_set(0 != atoi(cmd3));
						return;
					}

					Tier0_Msg(
						"mirv_streams record presentOnScreen 0|1 - Whether to show recording on screen (where possible) (1) [May cause epileptic seizures!] or not (0).\n"
						"Current value: %s.\n",
						g_AfxStreams.Console_PresentRecordOnScreen_get() ? "1" : "0"
					);
					return;
				}
				else if (bIsCsgo && 0 == _stricmp(cmd2, "matPostprocessEnable"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_MatPostprocessEnable_set(atoi(cmd3));
						return;
					}

					Tier0_Msg(
						"mirv_streams record matPostprocessEnable <iValue> - Positive value: Force integer value <iValue> for mat_postprocess_enable during recording (changing not recommended).\n"
						"Current value: %i.\n",
						g_AfxStreams.Console_MatPostprocessEnable_get()
					);
					return;
				}
				else if (bIsCsgo && 0 == _stricmp(cmd2, "matDynamicTonemapping"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_MatDynamicToneMapping_set(atoi(cmd3));
						return;
					}

					Tier0_Msg(
						"mirv_streams record matDynamicTonemapping <iValue> - Positive value: Force integer value <iValue> for mat_dynamic_tonemapping during recording (changing not recommended).\n"
						"Current value: %i.\n",
						g_AfxStreams.Console_MatDynamicToneMapping_get()
					);
					return;
				}
				else if (bIsCsgo && 0 == _stricmp(cmd2, "matMotionBlurEnabled"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_MatMotionBlurEnabled_set(atoi(cmd3));
						return;
					}

					Tier0_Msg(
						"mirv_streams record matMotionBlurEnabled <iValue> - Positive value: Force integer value <iValue> for mat_motion_blur_enabled during recording (changing not recommended).\n"
						"Current value: %i.\n",
						g_AfxStreams.Console_MatMotionBlurEnabled_get()
					);
					return;
				}
				else if(bIsCsgo && 0 == _stricmp(cmd2, "matForceTonemapScale"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_MatForceTonemapScale_set((float)atof(cmd3));
						return;
					}

					Tier0_Msg(
						"mirv_streams record matForceTonemapScale <fValue> - Positive value: Force floating point value <fValue> for mat_force_tonemap_scale during recording (can fix random bomb plant brightness if enabled, but breaks auto brightness adjustment).\n"
						"Current value: %f.\n",
						g_AfxStreams.Console_MatForceTonemapScale_get()
					);
					return;
				}
#endif //#ifndef _WIN64
				else if (0 == _stricmp(cmd2, "screen"))
				{
					CSubWrpCommandArgs subArgs(args, 3);

					g_AfxStreams.Console_RecordScreen(&subArgs);
					return;
				}
				else
				if (!_stricmp(cmd2, "startMovieWav"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_StartMovieWav_set(0 != atoi(cmd3));
						return;
					}

					Tier0_Msg(
						"mirv_streams record startMovieWav 0|1 - Whether to record WAV audio (1) or not (0).\n"
						"Current value: %s.\n",
						g_AfxStreams.Console_StartMovieWav_get() ? "1" : "0"
					);
					return;
				}
#ifndef _WIN64				
				else if (bIsCsgo && 0 == _stricmp(cmd2, "voices"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_RecordVoices_set(0 != atoi(cmd3));
						return;
					}

					Tier0_Msg(
						"mirv_streams record voices 0|1 - Whether to record voice WAV audio into separate files (1) or not (0).\n"
						"Current value: %s.\n",
						g_AfxStreams.Console_RecordVoices_get() ? "1" : "0"
					);
					return;
				}
#endif //#ifndef _WIN64				
				else
				if (0 == _stricmp(cmd2, "bvh"))
				{
					CSubWrpCommandArgs subArgs(args, 3);

					g_AfxStreams.Console_Bvh(&subArgs);
					return;
				}
				else if(0 == _stricmp(cmd2,"campath")) {
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						if (!_stricmp("enabled", cmd3))
						{
							if (5 <= argc)
							{
								g_AfxStreams.SetCampathAutoSave(0 != atoi(args->ArgV(4)));
								return;
							}

							Tier0_Msg(
								"mirv_streams record campath enabled 0|1 - Disable (0) or enable (1).\n"
								"Current value: %i\n"
								, g_AfxStreams.GetCampathAutoSave() ? 1 : 0
							);
							return;
						}
					}

					Tier0_Msg(
						"mirv_streams record campath enabled [...]\n"
					);
					return;					
				}
				else
				if (0 == _stricmp(cmd2, "cam"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						if (!_stricmp("enabled", cmd3))
						{
							if (5 <= argc)
							{
								g_AfxStreams.CamExport_set(0 != atoi(args->ArgV(4)));
								return;
							}

							Tier0_Msg(
								"mirv_streams record cam enabled 0|1 - Disable (0) or enable (1).\n"
								"Current value: %i\n"
								, g_AfxStreams.CamExport_get() ? 1 : 0
							);
							return;
						}
					}

					Tier0_Msg(
						"mirv_streams record cam enabled [...]\n"
					);
					return;
				}
				else
				if (!_stricmp(cmd2, "agr"))
				{
					CSubWrpCommandArgs subArgs(args, 3);

					g_AfxStreams.Console_GameRecording(&subArgs);
					return;
				}
				else if (!_stricmp(cmd2, "fps")) {
					if (4 <= argc)
					{
						char const* cmd3 = args->ArgV(3);
						if (0 == _stricmp(cmd3, "default")) {
							g_AfxStreams.SetOverrideFps(false);
							return;
						}
						else if (!StringIsAlphas(cmd3)) {
							g_AfxStreams.SetOverrideFpsValue((float)atof(cmd3));
							g_AfxStreams.SetOverrideFps(true);
							return;
						}
					}

					Tier0_Msg(
						"mirv_streams record fps default|<fValue>\n"
					);
					if (g_AfxStreams.GetOverrideFps()) {
						Tier0_Msg(
							"Current value: %f\n", g_AfxStreams.GetOverrideFpsValue()
						);
					}
					else {
						Tier0_Msg(
							"Current value: default\n"
						);
					}
					return;
				}
			}

			Tier0_Msg(
				"mirv_streams record name [...] - Set/get record name.\n"
				"mirv_streams record start - Begin recording.\n"
				"mirv_streams record end - End recording.\n"
				"mirv_streams record format [...] - Set/get file format.\n"
				"mirv_streams record fps [...] - Allows to override input FPS for games where we can not detect it (not needed for CS:GO).\n"
			);
#ifndef _WIN64			
			if (bIsCsgo) Tier0_Msg(
				"mirv_streams record presentOnScreen [...] - Controls screen presentation during recording.\n"
				"mirv_streams record matPostprocessEnable [...] - Control forcing of mat_postprocess_enable.\n"
				"mirv_streams record matDynamicTonemapping [...] - Control forcing of mat_dynamic_tonemapping.\n"
				"mirv_streams record matMotionBlurEnabled [...] - Control forcing of mat_motion_blur_enabled.\n"
				"mirv_streams record matForceTonemapScale [...] - Control forcing of mat_force_tonemap_scale.\n"
			);
#endif //#ifndef _WIN64
			Tier0_Msg(
				"mirv_streams record screen [...] - Controls capturing the game content drawn to screen right before being presented.\n"
				"mirv_streams record startMovieWav [...] - Controls WAV audio recording.\n"
			);
#ifndef _WIN64
			if (bIsCsgo) Tier0_Msg(
				"mirv_streams record voices [...] - Controls voice WAV audio recording.\n"
			);
#endif //#ifndef _WIN64
			Tier0_Msg(
				"mirv_streams record bvh [...] - Controls the HLAE/BVH camera motion data capture output.\n"
				"mirv_streams record cam [...] - Controls the camera motion data capture output (can be imported with mirv_camio).\n"
				"mirv_streams record campath [...] - Save current campath into take folder (if not empty).\n"
			);
			Tier0_Msg(
				"mirv_streams record agr [...] - Controls afxGameRecord (.agr) game state recording.\n"
			);
			return;
		}
#ifndef _WIN64
		else
		if(bIsCsgo && 0 == _stricmp(cmd1, "actions"))
		{
			if(3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				if(!_stricmp(cmd2, "add"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						
						if(!_stricmp(cmd3, "replace"))
						{
							CSubWrpCommandArgs subArgs(args, 4);

							CAfxBaseFxStream::Console_AddReplaceAction(&subArgs);
							return;
						}
						else if (!_stricmp(cmd3, "glowColorMap"))
						{
							CSubWrpCommandArgs subArgs(args, 4);

							CAfxBaseFxStream::Console_AddGlowColorMapAction(&subArgs);
							return;
						}
					}

					Tier0_Msg(
						"mirv_streams actions add replace [...] - Add replace action.\n"
						"mirv_streams actions add glowColorMap [...] - Add glowColorMap action (can be edited after adding it).\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "print"))
				{
					CAfxBaseFxStream::Console_ListActions();
					return;
				}
				else
				if(!_stricmp(cmd2, "remove"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						CAfxBaseFxStream::RemoveAction(CAfxBaseFxStream::CActionKey(cmd3));
						return;
					}
				}
				else if(!_stricmp(cmd2, "edit"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						if (CAfxBaseFxStream::CAction* action = CAfxBaseFxStream::GetAction(CAfxBaseFxStream::CActionKey(cmd3)))
						{
							CSubWrpCommandArgs subArgs(args, 4);
							action->Console_Edit(&subArgs);
							return;
						}
						Tier0_Warning("No action with key \"%s\" found.\n", cmd3);
						return;
					}
				}
			}
			Tier0_Msg(
				"mirv_streams actions add [...] - Add an action.\n"
				"mirv_streams actions print - Print available actions.\n"
				"mirv_streams actions remove <actionName> - Remove an action named <actionName>.\n"
				"mirv_streams actions edit <actionName> [...] - Edit action named <actionName>.\n"
			);
			return;
		}
#endif //#ifndef _WIN64		
		else if (0 == _stricmp("settings", cmd1))
		{
			CSubWrpCommandArgs subArgs(args, 2);
			advancedfx::CRecordingSettings::Console(&subArgs);
			return;
		}
#ifndef _WIN64		
		else if (bIsCsgo && 0 == _stricmp("mainStream", cmd1))
		{
			CSubWrpCommandArgs subArgs(args, 2);
			g_AfxStreams.Console_MainStream(&subArgs);
			return;
		}
#endif //#ifndef _WIN64
	}

#ifndef _WIN64
	if (bIsCsgo) Tier0_Msg(
		"mirv_streams add [...]- Add a stream.\n"
		"mirv_streams edit [...]- Edit a stream.\n"
		"mirv_streams move [...] - Move a stream in the list.\n"
		"mirv_streams remove [...] - Remove a stream.\n"
		"mirv_streams preview [...] - Preview a stream.\n"
		"mirv_streams previewEnd [<iSlot>] - End preview.\n"
		"mirv_streams previewSuspend [...] - Suspend all previews.\n"
		"mirv_streams print - Print current streams.\n"
		"mirv_streams print2 - Print current streams.\n"
	);
#endif //#ifndef _WIN64
	Tier0_Msg(
		"mirv_streams record [...] - Recording control.\n"
	);
#ifndef _WIN64	
	if (bIsCsgo) Tier0_Msg(
		"mirv_streams actions [...] - Actions control (for baseFx based streams).\n"
	);
#endif //#ifndef _WIN64	
	Tier0_Msg(
		"mirv_streams settings [...] - Recording settings.\n"
	);
#ifndef _WIN64	
	if (bIsCsgo) Tier0_Msg(
		"mirv_streams mainStream [...] - Controls which stream is the main stream for caching full-scene state (default is first).\n"
	);
#endif //#ifndef _WIN64
	return;
}

void ReplaceAll(std::string & str, const std::string & from, const std::string & to)
{
	if (from.empty())
		return;
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::wstring::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length();
	}
}

#ifndef _WIN64
CON_COMMAND(mirv_exec, "command execution")
{
	SOURCESDK::CSGO::ICvar * pCvar = WrpConCommands::GetVEngineCvar_CSGO();
	if (!pCvar)
	{
		Tier0_Warning("Error: No suitable Cvar interface found.\n");
		return;
	}

	int argC = args->ArgC();
	const char * arg0 = args->ArgV(0);

	if (2 <= argC)
	{
		const char * arg1 = args->ArgV(1);

		if (auto cmd = pCvar->FindCommand(arg1))
		{
			const char **ppargV = (const char **)malloc(sizeof(char *)*(argC - 1));

			std::string * strings = new std::string[argC - 1];

			for (int i = 1; i < argC; ++i)
			{
				std::string & str = strings[i - 1];

				str = args->ArgV(i);

				ReplaceAll(str, "{QUOTE}", "\"");
				ReplaceAll(str, "{QUOTE2}", "{QUOTE}");
				ReplaceAll(str, "{QUOTE3}", "{QUOTE2}");
				ReplaceAll(str, "{QUOTE4}", "{QUOTE3}");
				ReplaceAll(str, "{QUOTE5}", "{QUOTE4}");
				ReplaceAll(str, "{QUOTE6}", "{QUOTE5}");
				ReplaceAll(str, "{QUOTE7}", "{QUOTE6}");
				ReplaceAll(str, "{QUOTE8}", "{QUOTE7}");
				ReplaceAll(str, "{QUOTE9}", "{QUOTE8}");
				ReplaceAll(str, "{QUOTE10}", "{QUOTE9}");
				ReplaceAll(str, "\\{", "{");
				ReplaceAll(str, "\\}", "}");

				ppargV[i-1] = str.c_str();
			}

			SOURCESDK::CSGO::CCommand ccmd(argC - 1, ppargV);

			cmd->Dispatch(ccmd);

			free(ppargV);
			return;
		}
		else
		{
			Tier0_Warning("AFXERROR: Command %s not found.\n", arg1);
			return;
		}
	}

	Tier0_Msg(
		"%s <sCommandName> \"<arg1>\" ... \"<argN>\" - Pass arguments <arg1> to <argN> to command named <sCommandName>,  use {QUOTE} for \", \\{ for {, \\} for }.\n"
		, arg0
	);
}
#endif //#ifndef _WIN64

CON_COMMAND(__mirv_exec, "client command execution: __mirv_exec <as you would have typed here>") {
	unsigned int len=0;
	char *ttt, *ct;

	for(int i=0; i<args->ArgC(); i++)
	{
		len += strlen(args->ArgV(i))+1;
	}

	if(len<1) len=1;

	ct = ttt = (char *)malloc(sizeof(char)*len);

	for(int i=1; i<args->ArgC(); i++) {
		char const * cur = args->ArgV(i);
		unsigned int lcur = strlen(cur);
		
		if(1<i) {
			strcpy(ct, " ");
			ct++;
		}

		strcpy(ct, cur);
		ct += lcur;
	}

	*ct = 0;

	g_VEngineClient->ExecuteClientCmd(ttt);

	free(ttt);
}

bool GetCurrentDemoTick(int &outTick)
{
	WrpVEngineClientDemoInfoEx * di = g_VEngineClient->GetDemoInfoEx();

	if(di)
	{
		outTick = di->GetDemoPlaybackTick();
		return true;
	}

	return false;
}

bool GetDemoTimeFromClientTime(double curTime, double clientTime, double &outDemoTime)
{
	WrpVEngineClientDemoInfoEx * di = g_VEngineClient->GetDemoInfoEx();
	WrpGlobals * gl = g_Hook_VClient_RenderView.GetGlobals();
	if(di && gl)
	{
		int current_tick = di->GetDemoPlaybackTick();
		double tick_interval = gl->interval_per_tick_get();
		outDemoTime = clientTime - (curTime - current_tick * tick_interval);
		return true;
	}
	return false;
}


bool GetDemoTickFromDemoTime(double curTime, double demoTime, int& outTick)
{
	WrpVEngineClientDemoInfoEx* di = g_VEngineClient->GetDemoInfoEx();
	WrpGlobals* gl = g_Hook_VClient_RenderView.GetGlobals();

	if (di && gl)
	{
		double tick_interval = gl->interval_per_tick_get();
		outTick = (int)round(demoTime / tick_interval);

		return true;
	}

	return false;
}

bool GetCurrentDemoTime(double &outDemoTime)
{
	WrpVEngineClientDemoInfoEx * di = g_VEngineClient->GetDemoInfoEx();
	WrpGlobals * gl = g_Hook_VClient_RenderView.GetGlobals();

	if(gl)
	{
		int client_current_tick = di->GetDemoPlaybackTick();

		double tick_interval = gl->interval_per_tick_get();

		double interpolation_amount = gl->interpolation_amount_get();

		double demoTime = (client_current_tick +interpolation_amount) * tick_interval;

		outDemoTime = demoTime;

		return true;
	}

	return false;
}

bool GetDemoTickFromClientTime(double curTime, double targetTime, int& outTick)
{
	double demoTime;
	return GetDemoTimeFromClientTime(curTime, targetTime, demoTime) && GetDemoTickFromDemoTime(curTime, demoTime, outTick);
}

extern class CExecuteClientCmdForCommandSystem : public IExecuteClientCmdForCommandSystem {
public:
    virtual void ExecuteClientCmd(const char * value) {
        if(g_VEngineClient) g_VEngineClient->ClientCmd_Unrestricted(value); // // We don't use ExecuteCliendCmd here, because it might be executed at awkward times that make the engine crash (e.g. when using playdemo).
    }
} g_ExecuteClientCmdForCommandSystem;

class CMirvCampath_Time : public IMirvCampath_Time
{
public:
	virtual double GetTime() {
		return g_MirvTime.GetTime();
	}
	virtual double GetCurTime() {
		return g_MirvTime.GetCurTime();
	}
	virtual bool GetCurrentDemoTick(int& outTick) {
		return ::GetCurrentDemoTick(outTick);
	}
	virtual bool GetCurrentDemoTime(double& outDemoTime) {
		return ::GetCurrentDemoTime(outDemoTime);
	}
	virtual bool GetDemoTickFromDemoTime(double curTime, double time, int& outTick) {
		return ::GetDemoTickFromDemoTime(curTime, time, outTick);
	}
	virtual bool GetDemoTimeFromClientTime(double curTime, double time, double& outDemoTime) {
		return ::GetDemoTimeFromClientTime(curTime, time, outDemoTime);
	}
    virtual bool GetDemoTickFromClientTime(double curTime, double targetTime, int& outTick) {
        return ::GetDemoTickFromClientTime(curTime, targetTime, outTick);
    }
} g_MirvCampath_Time;

class CMirvSkip_GotoDemoTick : public IMirvSkip_GotoDemoTick {
	virtual void GotoDemoTick(int tick) {
        std::ostringstream oss;
        oss << "demo_gototick " << tick;
        g_VEngineClient->ExecuteClientCmd(oss.str().c_str());
	}
} g_MirvSkip_GotoDemoTick;

CON_COMMAND(mirv_skip, "for skipping through demos (uses demo_gototick)")
{
    MirvSkip_ConsoleCommand(args, &g_MirvCampath_Time, &g_MirvSkip_GotoDemoTick);
}

class CMirvCampath_Camera : public IMirvCampath_Camera
{
public:
	virtual SMirvCameraValue GetCamera() {
		return SMirvCameraValue(
			g_Hook_VClient_RenderView.LastCameraOrigin[0],
			g_Hook_VClient_RenderView.LastCameraOrigin[1],
			g_Hook_VClient_RenderView.LastCameraOrigin[2],
			g_Hook_VClient_RenderView.LastCameraAngles[0],
			g_Hook_VClient_RenderView.LastCameraAngles[1],
			g_Hook_VClient_RenderView.LastCameraAngles[2],
			g_Hook_VClient_RenderView.LastCameraFov
		);
	}
} g_MirvCampath_Camera;


class CMirvCampath_Drawer : public IMirvCampath_Drawer
{
public:
	virtual bool GetEnabled() {
		return g_CampathDrawer.Draw_get();
	}
	virtual void SetEnabled(bool value) {
		g_CampathDrawer.Draw_set(value);
	}
	virtual bool GetDrawKeyframeAxis() {
		return g_CampathDrawer.GetDrawKeyframeAxis();
	}
	virtual void SetDrawKeyframeAxis(bool value) {
		g_CampathDrawer.SetDrawKeyframeAxis(value);
	}
	virtual bool GetDrawKeyframeCam() {
		return g_CampathDrawer.GetDrawKeyframeCam();
	}
	virtual void SetDrawKeyframeCam(bool value) {
		g_CampathDrawer.SetDrawKeyframeCam(value);
	}

	virtual float GetDrawKeyframeIndex() { return g_CampathDrawer.GetDrawKeyframeIndex(); }
	virtual void SetDrawKeyframeIndex(float value) { g_CampathDrawer.SetDrawKeyframeIndex(value); }

} g_MirvCampath_Drawer;
 
// Campath preview uses the existing CamPath evaluator and mirv_input free camera.
static bool g_CampathPreviewActive = false;
static bool g_CampathPreviewForward = false;
static bool g_CampathPreviewBackward = false;
static double g_CampathPreviewTime = 0.0;
static int g_CampathPreviewFrame = -1;
static DWORD g_CampathPreviewLastMs = 0;
// 0 disables the dedicated toggle key.
static UINT g_CampathPreviewToggleKey = 0;
static bool g_CampathPreviewToggleHeld = false;

static void CampathPreview_StopTravel()
{
	g_CampathPreviewForward = false;
	g_CampathPreviewBackward = false;
}

static bool CampathPreview_IsRepeatKey(LPARAM lParam)
{
	return 0 != (lParam & (1LL << 30));
}

static UINT CampathPreview_ParseToggleKey(const char * name)
{
	if (!name || !*name)
		return 0;
	if (0 == _stricmp(name, "none") || 0 == _stricmp(name, "off"))
		return 0;

	if (!name[1])
	{
		const char c = name[0];
		if (c >= 'a' && c <= 'z')
			return (UINT)(c - 'a' + 'A');
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
			return (UINT)c;
	}

	if (name[0] == 'f' || name[0] == 'F')
	{
		char * end = nullptr;
		const long f = strtol(name + 1, &end, 10);
		if (end != name + 1 && !*end && f >= 1 && f <= 12)
			return VK_F1 + (UINT)(f - 1);
	}

	if (0 == _stricmp(name, "insert")) return VK_INSERT;
	if (0 == _stricmp(name, "home")) return VK_HOME;
	if (0 == _stricmp(name, "end")) return VK_END;
	if (0 == _stricmp(name, "pageup")) return VK_PRIOR;
	if (0 == _stricmp(name, "pagedown")) return VK_NEXT;
	return 0;
}

// Keyframes can be previewed even when the path cannot be interpolated.
static bool CampathPreview_CanShow(const CamPath & path)
{
	return path.GetSize() != 0;
}

static CamPathIterator CampathPreview_FindNearestKeyframe(CamPath & path, double time)
{
	CamPathIterator nearest = path.GetBegin();
	double nearestDistance = std::fabs(nearest.GetTime() - time);

	for (CamPathIterator it = path.GetBegin(); it != path.GetEnd(); ++it)
	{
		const double distance = std::fabs(it.GetTime() - time);
		if (distance < nearestDistance)
		{
			nearest = it;
			nearestDistance = distance;
		}
	}

	return nearest;
}

static bool CampathPreview_SetCamera()
{
	CamPath & path = g_Hook_VClient_RenderView.m_CamPath;
	MirvInput * input = g_Hook_VClient_RenderView.m_MirvInput;
	if (!input || !CampathPreview_CanShow(path))
		return false;

	if (g_CampathPreviewTime < path.GetLowerBound())
		g_CampathPreviewTime = path.GetLowerBound();
	if (g_CampathPreviewTime > path.GetUpperBound())
		g_CampathPreviewTime = path.GetUpperBound();

	CamPathValue value;
	if (path.CanEval())
	{
		value = path.Eval(g_CampathPreviewTime);
	}
	else
	{
		// Without interpolation, use the nearest actual keyframe and its time.
		CamPathIterator nearest = CampathPreview_FindNearestKeyframe(path, g_CampathPreviewTime);
		g_CampathPreviewTime = nearest.GetTime();
		value = nearest.GetValue();
	}

	const Afx::Math::QEulerAngles angles = value.R.ToQREulerAngles().ToQEulerAngles();
	input->SetCameraControlMode(true);
	input->SetTx((float)value.X);
	input->SetTy((float)value.Y);
	input->SetTz((float)value.Z);
	input->SetRx((float)angles.Pitch);
	input->SetRy((float)angles.Yaw);
	input->SetRz((float)angles.Roll);
	input->SetFov((float)value.Fov);
	return true;
}

static bool CampathPreview_Enter()
{
	CamPath & path = g_Hook_VClient_RenderView.m_CamPath;
	if (!g_Hook_VClient_RenderView.IsInstalled() || !CampathPreview_CanShow(path) || !g_Hook_VClient_RenderView.m_MirvInput)
	{
		Tier0_Warning("Campath preview: no usable keyframes or free camera is unavailable.\n");
		return false;
	}

	if (!g_CampathPreviewActive)
	{
		const double t = (double)g_MirvTime.GetTime() - path.GetOffset();
		g_CampathPreviewTime = t < path.GetLowerBound() ? path.GetLowerBound() : (t > path.GetUpperBound() ? path.GetUpperBound() : t);
	}

	g_CampathPreviewActive = true;
	g_CampathPreviewFrame = -1;
	g_CampathPreviewLastMs = GetTickCount();
	CampathPreview_StopTravel();
	CampathPreview_SetCamera();
	Tier0_Msg("Campath preview active.\n");
	return true;
}

void MirvCampathPreview_Stop()
{
    g_CampathPreviewActive = false;
    CampathPreview_StopTravel();
    g_CampathPreviewLastMs = 0;
}

static void CampathPreview_Exit()
{
	if (g_CampathPreviewActive)
		CampathPreview_SetCamera();
	g_CampathPreviewActive = false;
	CampathPreview_StopTravel();
	g_CampathPreviewLastMs = 0;
	Tier0_Msg("Campath preview exited; free camera retained.\n");
}

// Called before the mirv_input view override is evaluated.
void MirvCampathPreview_Update()
{
	if (MirvCinematic_OwnsCamera() || MirvCinematic_IsEditing()) { MirvCampathPreview_Stop(); return; }
	if (!g_CampathPreviewActive)
		return;

	// Do not re-enable free camera after it was explicitly exited.
	if (!g_Hook_VClient_RenderView.m_MirvInput ||
		!g_Hook_VClient_RenderView.m_MirvInput->GetCameraControlMode())
	{
		g_CampathPreviewActive = false;
		CampathPreview_StopTravel();
		g_CampathPreviewLastMs = 0;
		return;
	}

	// Ignore held navigation keys while the console or menu owns input.
	if (g_Hook_VClient_RenderView.IsMenuActive())
	{
		CampathPreview_StopTravel();
		g_CampathPreviewLastMs = GetTickCount();
		return;
	}

	CamPath & path = g_Hook_VClient_RenderView.m_CamPath;
	if (!CampathPreview_CanShow(path))
	{
		g_CampathPreviewActive = false;
		CampathPreview_StopTravel();
		return;
	}

	// RenderView can run more than once per game frame.
	const int frame = g_MirvTime.GetFrameCount();
	if (frame != g_CampathPreviewFrame)
	{
		g_CampathPreviewFrame = frame;
		const DWORD nowMs = GetTickCount();
		const DWORD elapsedMs = g_CampathPreviewLastMs ? nowMs - g_CampathPreviewLastMs : 0;
		g_CampathPreviewLastMs = nowMs;
		const int direction = (g_CampathPreviewForward ? 1 : 0) - (g_CampathPreviewBackward ? 1 : 0);
		if (direction && path.CanEval())
		{
			float scale = 1.0f;
			if (g_VEngineClient)
			{
				WrpVEngineClientDemoInfoEx * demo = g_VEngineClient->GetDemoInfoEx();
				if (demo)
					scale = demo->GetDemoPlaybackTimeScale();
			}
			// A paused demo must not prevent preview navigation.
			if (!(scale > 0.0f))
				scale = 1.0f;
			const float dt = (float)(elapsedMs > 100 ? 100 : elapsedMs) / 1000.0f;
			g_CampathPreviewTime += direction * (double)dt * (double)scale;
		}
	}

	CampathPreview_SetCamera();
}

static bool CampathPreview_GotoIndex(int index)
{
	CamPath & path = g_Hook_VClient_RenderView.m_CamPath;
	if (index < 0 || (size_t)index >= path.GetSize())
		return false;

	CamPathIterator it = path.GetBegin();
	for (int i = 0; i < index; ++i)
		++it;

	g_CampathPreviewTime = it.GetTime();
	if (!CampathPreview_SetCamera())
		return false;

	// Keep the selected keyframe editable through HLAE's regular campath commands.
	path.SelectNone();
	path.SelectAdd((size_t)index, (size_t)index);
	return true;
}

static void CampathPreview_StepKeyframe(int direction)
{
	if (!g_CampathPreviewActive)
		return;

	CamPath & path = g_Hook_VClient_RenderView.m_CamPath;
	int index = 0;
	if (direction > 0)
	{
		for (CamPathIterator it = path.GetBegin(); it != path.GetEnd(); ++it, ++index)
		{
			if (it.GetTime() > g_CampathPreviewTime + 0.00001)
			{
				CampathPreview_GotoIndex(index);
				return;
			}
		}
	}
	else
	{
		int previous = -1;
		for (CamPathIterator it = path.GetBegin(); it != path.GetEnd(); ++it, ++index)
		{
			if (it.GetTime() >= g_CampathPreviewTime - 0.00001)
				break;
			previous = index;
		}
		if (previous >= 0)
			CampathPreview_GotoIndex(previous);
	}
}

// The HLAE window procedure handles preview keys before regular mirv_input controls.
bool MirvCampathPreview_KeyEvent(UINT message, WPARAM key, LPARAM lParam)
{
	const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
	const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
	if (!down && !up)
		return false;

	if (g_Hook_VClient_RenderView.IsMenuActive())
	{
		CampathPreview_StopTravel();
		g_CampathPreviewToggleHeld = false;
		return false;
	}

	// Handle the toggle even when preview is inactive, before mirv_input sees it.
	if (g_CampathPreviewToggleKey && key == g_CampathPreviewToggleKey)
	{
		if (up)
			g_CampathPreviewToggleHeld = false;
		else if (!g_CampathPreviewToggleHeld && !CampathPreview_IsRepeatKey(lParam))
		{
			g_CampathPreviewToggleHeld = true;
			if (g_CampathPreviewActive)
				CampathPreview_Exit();
			else
				CampathPreview_Enter();
		}
		return true;
	}

	if (!g_CampathPreviewActive)
		return false;
	if (key != VK_LEFT && key != VK_RIGHT && key != VK_UP && key != VK_DOWN
		&& key != VK_HOME && key != VK_END)
		return false;

	if (key == VK_LEFT || key == VK_RIGHT)
	{
		CamPath & path = g_Hook_VClient_RenderView.m_CamPath;
		if (path.CanEval())
		{
			if (key == VK_LEFT)
				g_CampathPreviewBackward = down;
			else
				g_CampathPreviewForward = down;
		}
		else if (down && !CampathPreview_IsRepeatKey(lParam))
		{
			// Without interpolation, move between actual keyframes.
			CampathPreview_StepKeyframe(key == VK_RIGHT ? 1 : -1);
		}
	}
	else if (down && !CampathPreview_IsRepeatKey(lParam))
	{
		if (key == VK_HOME || key == VK_END)
		{
			CampathPreview_StopTravel();
			g_CampathPreviewLastMs = GetTickCount();
			CamPath & path = g_Hook_VClient_RenderView.m_CamPath;
			if (path.GetSize() != 0)
				CampathPreview_GotoIndex(key == VK_HOME ? 0 : (int)path.GetSize() - 1);
		}
		else
		{
			CampathPreview_StepKeyframe(key == VK_UP ? 1 : -1);
		}
	}
	return true;
}

void MirvCampathPreview_ReleaseKeys()
{
	CampathPreview_StopTravel();
	g_CampathPreviewToggleHeld = false;
}

static void CampathPreview_PrintHelp()
{
	Tier0_Msg(
		"mirv_campath preview enter - Begin preview mode (one or more keyframes).\n"
		"mirv_campath preview toggle - Toggle Preview Mode on/off.\n"
		"mirv_campath preview toggleKey <key|none> - Set a dedicated key (letters, digits, F1..F12, insert, home, end, pageup, pagedown).\n"
		"mirv_campath preview <index> - Select a keyframe (0-based).\n"
		"mirv_campath preview interp <0..1> - Position between nearest keyframe and the following one.\n"
		"mirv_campath preview seek - Seek the demo to the current preview time.\n"
		"mirv_campath preview exit - Leave preview, retaining the free camera pose.\n"
		"Preview keys (console closed): UP/DOWN select next/previous; HOME/END select first/last.\n"
		"LEFT/RIGHT scrub interpolated paths, or step through keyframes if interpolation is unavailable.\n"
		"HOME/END jump without seeking the demo (unless assigned as toggleKey); use preview seek to seek.\n"
	);
}

// New campath editing uses the existing Source command and input entry points.
namespace
{
using namespace cinematic;
// Keep path state beside classic m_CamPath in the existing view owner.
Editor & GetEditorState()
{
	return g_Hook_VClient_RenderView.m_NewCampath;
}
bool editing = false, preview = false, playing = false;
bool held[256] = {};
bool swallowed[256] = {};
int scrub = 0;
double previewTime = 0;
ULONGLONG lastMs = 0;
int lastFrame = -1;
struct CaptureMark
{
	bool set = false, wall = false;
	double value = 0;
	std::uint64_t path = 0;
	int point = -1;
} mark, holdMark;

void need(bool ok, const char * why)
{
	if (!ok)
		throw std::runtime_error(why);
}
double demoTime()
{
	auto * engine = g_VEngineClient;
	auto * globals = g_Hook_VClient_RenderView.GetGlobals();
	auto * demo = engine ? engine->GetDemoInfoEx() : nullptr;
	need(engine && engine->IsPlayingDemo() && demo && globals, "A running demo is required for demo timing");
	double tick = globals->interval_per_tick_get();
	need(tick > 0 && std::isfinite(tick), "Demo tick interval unavailable");
	return (double(demo->GetDemoPlaybackTick()) + globals->interpolation_amount_get()) * tick;
}
double wallTime()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
Pose camera()
{
	auto & r = g_Hook_VClient_RenderView;
	need(r.IsInstalled(), "Camera hook unavailable");
	return {{r.LastCameraOrigin[0], r.LastCameraOrigin[1], r.LastCameraOrigin[2]},
			Euler(r.LastCameraAngles[0], r.LastCameraAngles[1], r.LastCameraAngles[2]),
			r.LastCameraFov};
}
void retainCamera()
{
	auto * input = g_Hook_VClient_RenderView.m_MirvInput;
	if (!input)
		return;
	Pose p = preview && GetEditorState().state.selected >= 0 && !GetEditorState().active().points.empty()
				 ? GetEditorState().active().evaluate(previewTime)
				 : camera();
	Vec a = Angles(p.rotation);
	input->SetCameraControlMode(true);
	input->SetTx(float(p.position.X));
	input->SetTy(float(p.position.Y));
	input->SetTz(float(p.position.Z));
	input->SetRx(float(a.X));
	input->SetRy(float(a.Y));
	input->SetRz(float(a.Z));
	input->SetFov(float(p.fov));
}
void stopPreview()
{
	if (preview)
		retainCamera();
	preview = false;
	scrub = 0;
}
void enterPreview()
{
	need(!GetEditorState().active().points.empty(), "Capture at least one point first");
	MirvCampathPreview_Stop();
	playing = false;
	preview = true;
	if (auto * input = g_Hook_VClient_RenderView.m_MirvInput)
	{
		input->SetCameraControlMode(false);
		input->SetCameraControlMode(true);
	}
	previewTime = std::clamp(previewTime, 0.0, GetEditorState().active().duration);
	lastMs = GetTickCount64();
	MirvCinematic_ReleaseKeys();
}
void editOn()
{
	MirvCampathPreview_Stop();
	editing = true;
	playing = false;
	auto * input = g_Hook_VClient_RenderView.m_MirvInput;
	if (input)
		input->SetCameraControlMode(true);
}
Path & path(State & s, bool create = false)
{
	if (s.selected < 0 && create)
	{
		Path p;
		p.id = s.nextId++;
		p.name = "Path " + std::to_string(p.id);
		s.paths.push_back(p);
		s.selected = int(s.paths.size() - 1);
		s.point = -1;
	}
	need(s.selected >= 0 && s.selected < int(s.paths.size()), "No path selected");
	return s.paths[s.selected];
}
Point & point(State & s)
{
	auto & p = path(s);
	need(s.point >= 0 && s.point < int(p.points.size()), "Select a point first");
	return p.points[s.point];
}
void capture(bool replace = false, int at = -1)
{
	State n = GetEditorState().state;
	auto & p = path(n, true);
	Pose current = camera();
	if (p.points.empty())
		p.anchor.origin = current.position;
	p.capture(current, replace ? n.point : at, replace);
	n.point = replace ? n.point : at < 0 ? int(p.points.size() - 1) : at;
	GetEditorState().commit(std::move(n));
	if (preview)
		previewTime = GetEditorState().active().pointTime(GetEditorState().state.point);
}
void selectPoint(int i)
{
	need(i >= 0 && i < int(GetEditorState().active().points.size()), "Invalid point index");
	GetEditorState().state.point = i;
	if (preview)
		previewTime = GetEditorState().active().pointTime(i);
}
std::filesystem::path filename(const char * utf8)
{
	std::filesystem::path p = std::filesystem::u8path(utf8);
	need(!p.empty(), "Filename required");
	if (!p.has_extension())
		p += ".xml";
	return p;
}
void saveFile(const std::filesystem::path & file, const std::string & text, bool overwrite)
{
	// Never truncate an existing file. Write a unique sibling, then atomically
	// rename it; without overwrite, MoveFileEx fails if a destination exists.
	auto tmp = file;
	tmp += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
	HANDLE handle = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	need(handle != INVALID_HANDLE_VALUE, "Cannot create temporary save file");
	DWORD wrote = 0;
	bool ok = WriteFile(handle, text.data(), DWORD(text.size()), &wrote, nullptr) && wrote == text.size();
	if (ok)
		ok = FlushFileBuffers(handle) != FALSE;
	CloseHandle(handle);
	if (ok)
		ok = MoveFileExW(tmp.c_str(), file.c_str(),
						 MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0)) != FALSE;
	if (!ok)
	{
		DeleteFileW(tmp.c_str());
		throw std::runtime_error("Save failed: file exists or is not writable (use overwrite explicitly)");
	}
}
std::string readFile(const std::filesystem::path & file)
{
	std::ifstream in(file, std::ios::binary | std::ios::ate);
	need(bool(in), "Cannot open XML file");
	auto size = in.tellg();
	need(size >= 0 && size <= 16 * 1024 * 1024, "XML file exceeds 16 MiB");
	std::string s(std::size_t(size), '\0');
	in.seekg(0);
	if (size > 0)
		in.read(&s[0], size);
	need(bool(in), "Cannot read complete XML file");
	return s;
}
struct Args
{
	IWrpCommandArgs * a;
	int offset;
	int count() const
	{
		return a->ArgC() - offset;
	}
	std::string at(int i) const
	{
		need(i >= 0 && i < count(), "Missing argument; see mirv_campath cinematic help");
		return a->ArgV(offset + i);
	}
	bool is(int i, const char * s) const
	{
		return i < count() && 0 == _stricmp(a->ArgV(offset + i), s);
	}
	double num(int i) const
	{
		auto s = at(i);
		std::istringstream in(s);
		in.imbue(std::locale::classic());
		double v;
		in >> std::noskipws >> v;
		need(bool(in) && in.peek() == std::char_traits<char>::eof() && std::isfinite(v), "Expected a finite number");
		return v;
	}
	int index(int i) const
	{
		double v = num(i);
		need(v >= 0 && v <= 100000 && v == std::floor(v), "Expected a nonnegative integer");
		return int(v);
	}
	bool flag(int i) const
	{
		double v = num(i);
		need(v == 0 || v == 1, "Expected 0 or 1");
		return v != 0;
	}
	Vec vec(int i) const
	{
		return {num(i), num(i + 1), num(i + 2)};
	}
	void size(int n) const
	{
		need(count() == n, "Wrong argument count; see mirv_campath cinematic help");
	}
};
void help()
{
	Tier0_Msg("Cinematic campath (Source 1). Enable: mirv_campath edit on; disable: mirv_campath edit off.\n"
			  "While editing, use mirv_campath <command>; outside, use mirv_campath cinematic <command>.\n"
			  "All indices are 0-based; times are seconds; track time/progress is 0..1; angles are pitch yaw "
			  "roll.\n"
			  "add | insert <index> | replace | delete | select <point> | point <x y z pitch yaw roll fov>\n"
			  "new [name] | paths | use <index|name> | rename <name> | duplicate | remove confirm\n"
			  "undo | redo | preview [enter|exit|toggle|seek|seconds] | play | stop\n"
			  "curve linear|smooth|bezier | tension <0..1> | handle in|out <x y z|capture>\n"
			  "orbit <centerX centerY centerZ radius xy|xz|yz sweepDegrees [startDegrees [points]]>\n"
			  "anchor <x y z pitch yaw roll> | anchor capture | translate <x y z> | rotate <pitch yaw roll> | "
			  "scale <factor>\n"
			  "grid <units> | lock x|y|z <localValue|off> | constrain | corner stop|round\n"
			  "duration <seconds> | start <demoSeconds|capture> | end capture | schedule <0|1> | finish "
			  "release|hold\n"
			  "segment <pointIndex> <seconds|auto> | hold <seconds> | stopPoint <0|1> | ease "
			  "linear|in|out|smooth|smoother|custom\n"
			  "mark begin|end [demo|wall] | holdMark begin|end [demo|wall]\n"
			  "speed set <normalizedTime> <progress> | speed delete <time> | speed clear (custom segment easing)\n"
			  "motion set <normalizedTime> <pathProgress> | motion clear (whole path; reversals allowed; replaces "
			  "holds/segments)\n"
			  "rotation set <normalizedTime> [pitch yaw roll] | rotation clear | rotation interp "
			  "linear|smooth|step\n"
			  "fov set <normalizedTime> <degrees> | fov clear | fov interp linear|smooth|step\n"
			  "aim <worldX worldY worldZ|off> | transition <seconds> | overlaps\n"
			  "save <file> [overwrite] | saveAll <file> [overwrite] | load <file> [append|replace [discard]]\n"
			  "exportLegacy <file> [overwrite] (bakes to 60 Hz; original files can be imported with load)\n"
			  "draw all|paths|cameras|labels|handles|constraints|direction <0|1>\n"
			  "mirv_campath draw keyIndex <fHeight> - New path label size (default 32 pixels).\n"
			  "Keys: Insert capture, Shift+Insert replace, Delete remove point, Tab preview/freecam, Ctrl+Z/Y "
			  "undo/redo.\n"
			  "Preview only: Up/Down previous/next point, Home/End first/last, Left/Right scrub.\n"
			  "Classic commands remain available after edit off. Full guide: doc/CINEMATIC_CAMERA.md\n");
}
Interp interpolation(const Args & a, int i)
{
	if (a.is(i, "linear"))
		return Interp::Linear;
	if (a.is(i, "smooth"))
		return Interp::Smooth;
	if (a.is(i, "step"))
		return Interp::Step;
	throw std::runtime_error("Interpolation must be linear, smooth or step");
}
template <class Key> void put(std::vector<Key> & keys, Key k)
{
	auto it = std::lower_bound(keys.begin(), keys.end(), k.time, [](const Key & v, double t) { return v.time < t; });
	if (it != keys.end() && it->time == k.time)
		*it = k;
	else
		keys.insert(it, k);
}
void run(const Args & a)
{
	if (!a.count() || a.is(0, "help"))
	{
		help();
		return;
	}
	if (a.is(0, "paths"))
	{
		a.size(1);
		for (std::size_t i = 0; i < GetEditorState().state.paths.size(); ++i)
		{
			auto & p = GetEditorState().state.paths[i];
			Tier0_Msg("%s%u: %s - %u points, %.3fs, length %.2f, start %.3f, scheduled %i\n",
					  int(i) == GetEditorState().state.selected ? "* " : "  ", unsigned(i), p.name.c_str(),
					  unsigned(p.points.size()), p.duration, p.Length(), p.start, p.scheduled);
		}
		Tier0_Msg("Unsaved changes: %s\n", GetEditorState().dirty() ? "yes" : "no");
		return;
	}
	if (a.is(0, "new"))
	{
		need(a.count() <= 2, "new [name]");
		stopPreview();
		GetEditorState().create(a.count() == 2 ? a.at(1) : "");
		return;
	}
	if (a.is(0, "use"))
	{
		a.size(2);
		stopPreview();
		std::string which = a.at(1);
		int chosen = -1;
		for (std::size_t i = 0; i < GetEditorState().state.paths.size(); ++i)
			if (GetEditorState().state.paths[i].name == which)
			{
				need(chosen < 0, "Ambiguous name; use a path index");
				chosen = int(i);
			}
		if (chosen < 0)
		{
			chosen = a.index(1);
			need(chosen < int(GetEditorState().state.paths.size()), "Invalid path index");
		}
		GetEditorState().state.selected = chosen;
		GetEditorState().state.point = -1;
		previewTime = 0;
		return;
	}
	if (a.is(0, "add"))
	{
		a.size(1);
		capture();
		return;
	}
	if (a.is(0, "insert"))
	{
		a.size(2);
		capture(false, a.index(1));
		return;
	}
	if (a.is(0, "replace"))
	{
		a.size(1);
		capture(true);
		return;
	}
	if (a.is(0, "select"))
	{
		a.size(2);
		selectPoint(a.index(1));
		return;
	}
	if (a.is(0, "duplicate"))
	{
		a.size(1);
		stopPreview();
		GetEditorState().duplicate();
		return;
	}
	if (a.is(0, "remove"))
	{
		a.size(2);
		need(a.is(1, "confirm"), "Use remove confirm; undo can restore it");
		stopPreview();
		GetEditorState().remove();
		return;
	}
	if (a.is(0, "undo") || a.is(0, "redo"))
	{
		a.size(1);
		bool ok = a.is(0, "undo") ? GetEditorState().undo() : GetEditorState().redo();
		if (!ok)
			Tier0_Msg("Nothing to %s.\n", a.at(0).c_str());
		MirvCinematic_ReleaseKeys();
		return;
	}
	if (a.is(0, "preview"))
	{
		need(a.count() <= 2, "preview [enter|exit|toggle|seek|seconds]");
		if (a.is(1, "exit"))
		{
			stopPreview();
			return;
		}
		if (a.is(1, "toggle") && preview)
		{
			stopPreview();
			return;
		}
		if (a.is(1, "seek"))
		{
			auto & p = GetEditorState().active();
			double t = p.start + previewTime;
			auto * globals = g_Hook_VClient_RenderView.GetGlobals();
			demoTime();
			double tick = t / globals->interval_per_tick_get();
			need(tick >= 0 && tick < (std::numeric_limits<int>::max)(), "Seek outside demo tick range");
			std::string cmd = "demo_gototick " + std::to_string(int(std::round(tick)));
			g_VEngineClient->ExecuteClientCmd(cmd.c_str());
			return;
		}
		double t = previewTime;
		if (a.count() == 2 && !a.is(1, "enter") && !a.is(1, "toggle"))
		{
			t = a.num(1);
			need(t >= 0 && t <= GetEditorState().active().duration, "Preview time outside movement");
		}
		enterPreview();
		previewTime = t;
		return;
	}
	if (a.is(0, "play"))
	{
		a.size(1);
		need(!GetEditorState().state.paths.empty(), "No paths to play");
		demoTime();
		stopPreview();
		MirvCampathPreview_Stop();
		playing = true;
		if (auto * i = g_Hook_VClient_RenderView.m_MirvInput)
			i->SetCameraControlMode(false);
		return;
	}
	if (a.is(0, "stop"))
	{
		a.size(1);
		stopPreview();
		playing = false;
		return;
	}
	if (a.is(0, "overlaps"))
	{
		a.size(1);
		auto pairs = GetEditorState().overlaps();
		for (auto & s : pairs)
			Tier0_Msg("Overlap: %s\n", s.c_str());
		Tier0_Msg("%u overlap(s); latest start wins, larger stable ID breaks ties.\n", unsigned(pairs.size()));
		return;
	}
	if (a.is(0, "draw"))
	{
		if (a.is(1, "keyIndex"))
		{
			if (a.count() == 2)
			{
				Tier0_Msg(
					"mirv_campath draw keyIndex <fHeight> - New path label height in pixels.\nCurrent value: %g\n",
					g_CampathDrawer.GetNewLabelHeight());
				return;
			}
			a.size(3);
			double height = a.num(2);
			need(height >= 0 && height <= 200, "Label height must be 0..200 pixels");
			g_CampathDrawer.SetNewLabelHeight((float)height);
			return;
		}
		a.size(3);
		bool value = a.flag(2);
		const char * names[] = {"paths", "cameras", "labels", "handles", "constraints", "direction"};
		if (a.is(1, "all"))
		{
			for (int layer = 0; layer < 6; ++layer)
				g_CampathDrawer.SetNewDrawLayer(layer, value);
			return;
		}
		for (int i = 0; i < 6; ++i)
			if (a.is(1, names[i]))
			{
				g_CampathDrawer.SetNewDrawLayer(i, value);
				return;
			}
		throw std::runtime_error("Unknown draw layer");
	}
	if (a.is(0, "save") || a.is(0, "saveAll") || a.is(0, "exportLegacy"))
	{
		need(a.count() == 2 || (a.count() == 3 && a.is(2, "overwrite")),
			 "save/saveAll/exportLegacy <file> [overwrite]");
		bool all = a.is(0, "saveAll");
		std::string text = a.is(0, "exportLegacy") ? GetEditorState().legacyXml() : GetEditorState().xml(all);
		saveFile(filename(a.at(1).c_str()), text, a.count() == 3);
		if (all)
			GetEditorState().markSaved();
		Tier0_Msg("Saved XML.\n");
		return;
	}
	if (a.is(0, "load"))
	{
		need(a.count() >= 2 && a.count() <= 4, "load <file> [append|replace [discard]]");
		bool replace = a.is(2, "replace");
		need(a.count() < 3 || replace || a.is(2, "append"), "Choose append or replace");
		need(a.count() < 4 || (replace && a.is(3, "discard")), "Only replace accepts discard");
		need(!replace || !GetEditorState().dirty() || a.is(3, "discard"),
			 "Unsaved changes: saveAll first, append, or use load <file> replace discard");
		std::string xml = readFile(filename(a.at(1).c_str()));
		State verified = Deserialize(xml);
		(void)verified;
		stopPreview();
		GetEditorState().loadXml(xml, replace);
		if (replace)
			GetEditorState().markSaved();
		Tier0_Msg("Loaded XML (%s); undo is available.\n", replace ? "replace" : "append");
		return;
	}
	State n = GetEditorState().state;
	auto & p = path(n);
	if (a.is(0, "rename"))
	{
		a.size(2);
		p.name = a.at(1);
	}
	else if (a.is(0, "delete"))
	{
		a.size(1);
		point(n);
		p.points.erase(p.points.begin() + n.point);
		n.point = p.points.empty() ? -1 : (std::min)(n.point, int(p.points.size() - 1));
	}
	else if (a.is(0, "point"))
	{
		a.size(8);
		Pose v{a.vec(1), Euler(a.num(4), a.num(5), a.num(6)), a.num(7)};
		p.capture(v, n.point, true);
	}
	else if (a.is(0, "curve"))
	{
		a.size(2);
		Curve c;
		if (a.is(1, "linear"))
			c = Curve::Linear;
		else if (a.is(1, "smooth"))
			c = Curve::Smooth;
		else if (a.is(1, "bezier"))
			c = Curve::Bezier;
		else
			throw std::runtime_error("Curve must be linear, smooth or bezier");
		if (p.curve == Curve::Circle)
			for (std::size_t i = 0; i < p.points.size(); ++i)
				p.points[i].pose.position = p.position(double(i));
		p.curve = c;
	}
	else if (a.is(0, "tension"))
	{
		a.size(2);
		p.tension = a.num(1);
	}
	else if (a.is(0, "handle"))
	{
		need(a.count() == 5 || (a.count() == 3 && a.is(2, "capture")), "handle in|out <localX localY localZ|capture>");
		need(a.is(1, "in") || a.is(1, "out"), "Choose in or out handle");
		auto & v = point(n);
		Vec h = a.count() == 3 ? p.toLocal(camera()).position - v.pose.position : a.vec(2);
		h = p.constraints.apply(v.pose.position + h) - v.pose.position;
		if (a.is(1, "in"))
			v.inHandle = h;
		else
			v.outHandle = h;
		p.curve = Curve::Bezier;
	}
	else if (a.is(0, "orbit"))
	{
		need(a.count() >= 7 && a.count() <= 9, "orbit <worldCenterX Y Z radius xy|xz|yz sweep [start [points]]>");
		p.circleCenter = p.toLocal({a.vec(1), {}, 90}).position;
		p.circleRadius = a.num(4) / p.anchor.scale;
		p.circlePlane = a.is(5, "xy") ? 0 : a.is(5, "xz") ? 1 : a.is(5, "yz") ? 2 : -1;
		p.circleSweep = a.num(6);
		p.circleStart = a.count() > 7 ? a.num(7) : 0;
		int count = a.count() > 8 ? a.index(8) : 9;
		need(count >= 2 && count <= 4096, "Orbit needs 2..4096 points");
		p.points.assign(std::size_t(count), Point{});
		p.curve = Curve::Circle;
		for (int i = 0; i < count; ++i)
		{
			p.points[i].pose = p.toLocal(camera());
			p.points[i].pose.position = p.position(i);
		}
		n.point = 0;
	}
	else if (a.is(0, "anchor"))
	{
		if (a.count() == 2 && a.is(1, "capture"))
		{
			Pose c = camera();
			p.anchor.origin = c.position;
			p.anchor.rotation = c.rotation;
		}
		else
		{
			a.size(7);
			p.anchor.origin = a.vec(1);
			p.anchor.rotation = Euler(a.num(4), a.num(5), a.num(6));
		}
	}
	else if (a.is(0, "translate"))
	{
		a.size(4);
		p.anchor.origin = p.anchor.origin + a.vec(1);
	}
	else if (a.is(0, "rotate"))
	{
		a.size(4);
		p.anchor.rotation = Euler(a.num(1), a.num(2), a.num(3)) * p.anchor.rotation;
	}
	else if (a.is(0, "scale"))
	{
		a.size(2);
		p.anchor.scale = a.num(1);
	}
	else if (a.is(0, "grid"))
	{
		a.size(2);
		p.constraints.grid = a.num(1);
	}
	else if (a.is(0, "lock"))
	{
		a.size(3);
		int axis = a.is(1, "x") ? 0 : a.is(1, "y") ? 1 : a.is(1, "z") ? 2 : -1;
		need(axis >= 0, "Axis must be x, y or z");
		p.constraints.locked[axis] = !a.is(2, "off");
		if (!a.is(2, "off"))
		{
			double value = a.num(2);
			if (axis == 0)
				p.constraints.plane.X = value;
			else if (axis == 1)
				p.constraints.plane.Y = value;
			else
				p.constraints.plane.Z = value;
		}
	}
	else if (a.is(0, "constrain"))
	{
		a.size(1);
		need(p.curve != Curve::Circle, "Convert circle to another curve before constraining");
		for (auto & pt : p.points)
		{
			pt.pose.position = p.constraints.apply(pt.pose.position);
			pt.inHandle = p.constraints.apply(pt.pose.position + pt.inHandle) - pt.pose.position;
			pt.outHandle = p.constraints.apply(pt.pose.position + pt.outHandle) - pt.pose.position;
		}
	}
	else if (a.is(0, "corner"))
	{
		a.size(2);
		need(a.is(1, "stop") || a.is(1, "round"), "corner stop|round");
		point(n).stop = a.is(1, "stop");
		p.curve = a.is(1, "round") ? Curve::Smooth : Curve::Linear;
	}
	else if (a.is(0, "duration"))
	{
		a.size(2);
		p.duration = a.num(1);
	}
	else if (a.is(0, "start"))
	{
		a.size(2);
		p.start = a.is(1, "capture") ? demoTime() : a.num(1);
		p.scheduled = true;
	}
	else if (a.is(0, "end"))
	{
		a.size(2);
		need(a.is(1, "capture"), "end capture");
		p.duration = demoTime() - p.start;
	}
	else if (a.is(0, "schedule"))
	{
		a.size(2);
		p.scheduled = a.flag(1);
	}
	else if (a.is(0, "finish"))
	{
		a.size(2);
		need(a.is(1, "release") || a.is(1, "hold"), "finish release|hold");
		p.holdEnd = a.is(1, "hold");
	}
	else if (a.is(0, "transition"))
	{
		a.size(2);
		p.transition = a.num(1);
	}
	else if (a.is(0, "segment"))
	{
		a.size(3);
		int i = a.index(1);
		need(i + 1 < int(p.points.size()), "Select an outgoing segment index");
		p.points[i].seconds = a.is(2, "auto") ? 0 : a.num(2);
		need(a.is(2, "auto") || p.points[i].seconds > 0, "Segment duration must be positive or auto");
	}
	else if (a.is(0, "hold"))
	{
		a.size(2);
		point(n).hold = a.num(1);
	}
	else if (a.is(0, "stopPoint"))
	{
		a.size(2);
		point(n).stop = a.flag(1);
	}
	else if (a.is(0, "ease"))
	{
		a.size(2);
		const char * names[] = {"linear", "in", "out", "smooth", "smoother", "custom"};
		int chosen = -1;
		for (int i = 0; i < 6; ++i)
			if (a.is(1, names[i]))
				chosen = i;
		need(chosen >= 0, "Unknown easing");
		p.ease = Ease(chosen);
	}
	else if (a.is(0, "mark") || a.is(0, "holdMark"))
	{
		need(a.count() == 2 || a.count() == 3, "mark/holdMark begin|end [demo|wall]");
		bool isHold = a.is(0, "holdMark");
		auto & m = isHold ? holdMark : mark;
		need(a.is(1, "begin") || a.is(1, "end"), "Choose begin or end");
		bool wall = a.count() == 3 ? a.is(2, "wall") : a.is(1, "end") && m.set ? m.wall : false;
		need(a.count() < 3 || wall || a.is(2, "demo"), "Clock must be demo or wall");
		double now = wall ? wallTime() : demoTime();
		if (a.is(1, "begin"))
		{
			if (isHold)
				point(n);
			m = {true, wall, now, p.id, n.point};
			Tier0_Msg("Timing mark captured (%s).\n", wall ? "wall" : "demo");
			return;
		}
		need(m.set && m.wall == wall && m.path == p.id, "Capture a matching begin mark on this path first");
		double dt = now - m.value;
		need(dt >= 0, "End mark precedes begin mark");
		if (isHold)
		{
			need(n.point == m.point, "Select the point where hold capture began");
			point(n).hold = dt;
		}
		else
		{
			need(dt > 0, "Duration must be positive");
			p.duration = dt;
			if (!wall)
			{
				p.start = m.value;
				p.scheduled = true;
			}
		}
		GetEditorState().commit(std::move(n));
		m.set = false;
		return;
	}
	else if (a.is(0, "speed") || a.is(0, "motion") || a.is(0, "rotation") || a.is(0, "fov"))
	{
		if (a.is(1, "interp"))
		{
			a.size(3);
			need(a.is(0, "rotation") || a.is(0, "fov"), "Only rotation/fov accept interp");
			if (a.is(0, "rotation"))
				p.rotationInterp = interpolation(a, 2);
			else
				p.fovInterp = interpolation(a, 2);
		}
		else if (a.is(1, "clear"))
		{
			a.size(2);
			if (a.is(0, "speed"))
				p.speed = {{0, 0}, {1, 1}};
			else if (a.is(0, "motion"))
				p.progress.clear();
			else if (a.is(0, "rotation"))
				p.rotations.clear();
			else
				p.fovs.clear();
		}
		else if (a.is(1, "delete"))
		{
			a.size(3);
			double t = a.num(2);
			auto erase = [&](auto & keys) {
				auto it = std::find_if(keys.begin(), keys.end(), [&](const auto & k) { return k.time == t; });
				need(it != keys.end(), "No key at this normalized time");
				keys.erase(it);
			};
			if (a.is(0, "rotation"))
				erase(p.rotations);
			else if (a.is(0, "fov"))
				erase(p.fovs);
			else if (a.is(0, "speed"))
				erase(p.speed);
			else
				erase(p.progress);
		}
		else
		{
			need(a.is(1, "set"), "Use set, delete, clear or interp");
			double t = a.num(2);
			if (a.is(0, "rotation"))
			{
				need(a.count() == 3 || a.count() == 6, "rotation set <time> [pitch yaw roll]");
				Quat q = a.count() == 3 ? p.toLocal(camera()).rotation
										: p.anchor.rotation.Conjugate() * Euler(a.num(3), a.num(4), a.num(5));
				put(p.rotations, RotationKey{t, q});
			}
			else
			{
				a.size(4);
				double value = a.num(3);
				auto & track = a.is(0, "speed") ? p.speed : a.is(0, "motion") ? p.progress : p.fovs;
				if (a.is(0, "motion") && track.empty())
					track = {{0, 0}, {1, 1}};
				put(track, ScalarKey{t, value});
			}
		}
	}
	else if (a.is(0, "aim"))
	{
		if (a.count() == 2 && a.is(1, "off"))
			p.fixedAim = false;
		else
		{
			a.size(4);
			p.fixedAim = true;
			p.aimTarget = p.toLocal({a.vec(1), {}, 90}).position;
		}
	}
	else
		throw std::runtime_error("Unknown cinematic command; see mirv_campath cinematic help");
	GetEditorState().commit(std::move(n));
	if (preview && GetEditorState().state.selected >= 0)
		previewTime = std::clamp(previewTime, 0.0, GetEditorState().active().duration);
}
} // namespace
bool MirvCinematic_DrawEnabled(int layer)
{
	return g_CampathDrawer.GetNewDrawLayer(layer) && (editing || preview);
}
bool MirvCinematic_Command(IWrpCommandArgs * args)
{
	bool explicitCommand = args->ArgC() > 1 && 0 == _stricmp(args->ArgV(1), "cinematic");
	// Only the two new mode toggles belong to this editor. In particular,
	// legacy `mirv_campath edit start/position/angles/...` must keep reaching
	// the classic command handler when cinematic editing is off.
	bool editCommand = args->ArgC() == 3 && 0 == _stricmp(args->ArgV(1), "edit") &&
					   (0 == _stricmp(args->ArgV(2), "on") || 0 == _stricmp(args->ArgV(2), "off"));
	if (!explicitCommand && !editCommand && !editing)
		return false;
	try
	{
		if (editCommand)
		{
			need(args->ArgC() == 3, "mirv_campath edit on|off");
			if (0 == _stricmp(args->ArgV(2), "on"))
				editOn();
			else
			{
				need(0 == _stricmp(args->ArgV(2), "off"), "mirv_campath edit on|off");
				stopPreview();
				editing = false;
				playing = false;
				MirvCinematic_ReleaseKeys();
			}
			Tier0_Msg("Cinematic editor %s.\n", editing ? "on" : "off");
		}
		else
			run({args, explicitCommand ? 2 : 1});
	}
	catch (const std::exception & e)
	{
		Tier0_Warning("Cinematic camera: %s\n", e.what());
	}
	return true;
}
bool MirvCinematic_OwnsCamera()
{
	return preview || playing;
}
bool MirvCinematic_IsEditing()
{
	return editing;
}
void MirvCinematic_Update()
{
	if (preview &&
		(GetEditorState().state.selected < 0 || GetEditorState().active().points.empty() ||
		 !g_Hook_VClient_RenderView.m_MirvInput || !g_Hook_VClient_RenderView.m_MirvInput->GetCameraControlMode()))
	{
		preview = false;
		scrub = 0;
	}
	if (preview)
	{
		int frame = g_MirvTime.GetFrameCount();
		ULONGLONG now = GetTickCount64();
		if (g_Hook_VClient_RenderView.IsMenuActive())
		{
			MirvCinematic_ReleaseKeys();
			lastMs = now;
			return;
		}
		if (frame != lastFrame)
		{
			lastFrame = frame;
			double dt = lastMs ? double((std::min)(now - lastMs, ULONGLONG(100))) / 1000 : 0;
			lastMs = now;
			previewTime = std::clamp(previewTime + scrub * dt, 0.0, GetEditorState().active().duration);
		}
	}
}
bool MirvCinematic_Override(float & x, float & y, float & z, float & pitch, float & yaw, float & roll, float & fov)
{
	try
	{
		Pose p;
		if (preview)
			p = GetEditorState().active().evaluate(previewTime);
		else if (!playing || !GetEditorState().evaluate(demoTime(), p))
			return false;
		Vec a = Angles(p.rotation);
		x = float(p.position.X);
		y = float(p.position.Y);
		z = float(p.position.Z);
		pitch = float(a.X);
		yaw = float(a.Y);
		roll = float(a.Z);
		fov = float(p.fov);
		return true;
	}
	catch (const std::exception &)
	{
		return false;
	}
}
void MirvCinematic_ReleaseKeys()
{
	for (bool & v : held)
		v = false;
	scrub = 0;
	lastMs = GetTickCount64();
}
bool MirvCinematic_KeyEvent(UINT message, WPARAM key, LPARAM flags)
{
	if (message == WM_KEYUP || message == WM_SYSKEYUP)
	{
		if (key < 256 && swallowed[key])
		{
			swallowed[key] = false;
			held[key] = false;
			scrub = (held[VK_RIGHT] ? 1 : 0) - (held[VK_LEFT] ? 1 : 0);
			return true;
		}
	}
	if (!editing)
		return false;
	if (message == WM_CHAR || message == WM_SYSCHAR)
	{
		return !g_Hook_VClient_RenderView.IsMenuActive() && (key == 9 || key == 26 || key == 25);
	}
	bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN, up = message == WM_KEYUP || message == WM_SYSKEYUP;
	if (!down && !up)
		return false;
	if (key >= 256)
		return false;
	if (g_Hook_VClient_RenderView.IsMenuActive())
		return false;
	bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	bool reserved = key == VK_INSERT || key == VK_DELETE || key == VK_TAB || (control && (key == 'Z' || key == 'Y')) ||
					(preview && (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN ||
								 key == VK_HOME || key == VK_END));
	if (!reserved)
		return false;
	bool repeat = held[key] || (flags & (1LL << 30));
	held[key] = down;
	swallowed[key] = down;
	if (up || repeat)
		return true;
	try
	{
		if (key == VK_INSERT)
			capture((GetKeyState(VK_SHIFT) & 0x8000) != 0);
		else if (key == VK_DELETE)
		{
			State n = GetEditorState().state;
			auto & p = path(n);
			point(n);
			p.points.erase(p.points.begin() + n.point);
			n.point = p.points.empty() ? -1 : (std::min)(n.point, int(p.points.size() - 1));
			GetEditorState().commit(std::move(n));
		}
		else if (key == VK_TAB)
		{
			if (preview)
				stopPreview();
			else
				enterPreview();
		}
		else if (control && key == 'Z')
			GetEditorState().undo();
		else if (control && key == 'Y')
			GetEditorState().redo();
		else if (key == VK_LEFT || key == VK_RIGHT)
			scrub = (held[VK_RIGHT] ? 1 : 0) - (held[VK_LEFT] ? 1 : 0);
		else
		{
			int i = GetEditorState().state.point;
			if (key == VK_HOME)
				i = 0;
			else if (key == VK_END)
				i = int(GetEditorState().active().points.size() - 1);
			else
				i += key == VK_UP ? -1 : 1;
			selectPoint((std::max)(0, (std::min)(i, int(GetEditorState().active().points.size() - 1))));
		}
	}
	catch (const std::exception & e)
	{
		Tier0_Warning("Cinematic camera: %s\n", e.what());
	}
	return true;
}

CON_COMMAND(mirv_campath, "camera paths")
{
    if (MirvCinematic_Command(args)) return;
	if (!g_Hook_VClient_RenderView.IsInstalled())
	{
		Tier0_Warning("Error: Hook not installed.\n");
		return;
	}

	if (2 <= args->ArgC() && 0 == _stricmp("preview", args->ArgV(1)))
	{
		CamPath & path = g_Hook_VClient_RenderView.m_CamPath;
		if (args->ArgC() == 2) { CampathPreview_PrintHelp(); return; }
		const char * cmd = args->ArgV(2);
		if (0 == _stricmp(cmd, "toggleKey")) {
            if (args->ArgC() == 3) {
                Tier0_Msg("Campath preview toggle key: VK %u (0 = disabled).\n", g_CampathPreviewToggleKey);
                return;
            }
            if (args->ArgC() == 4) {
                const char * keyName = args->ArgV(3);
                const UINT key = CampathPreview_ParseToggleKey(keyName);
                if (key == 0 && 0 != _stricmp(keyName, "none") && 0 != _stricmp(keyName, "off")) {
                    Tier0_Warning("Campath preview: unsupported key name. See mirv_campath preview.\n");
                    return;
                }
                g_CampathPreviewToggleKey = key;
                g_CampathPreviewToggleHeld = false;
                Tier0_Msg("Campath preview toggle key set to %s (VK %u).\n", keyName, key);
                return;
            }
            CampathPreview_PrintHelp();
            return;
        }
        if (0 == _stricmp(cmd, "exit") && args->ArgC() == 3) {
            CampathPreview_Exit();
            return;
        }
        if (0 == _stricmp(cmd, "toggle") && args->ArgC() == 3) {
            if (g_CampathPreviewActive) CampathPreview_Exit();
            else CampathPreview_Enter();
            return;
        }
        if (0 == _stricmp(cmd, "enter") && args->ArgC() == 3) {
            CampathPreview_Enter();
            return;
        }
        if (!CampathPreview_CanShow(path) || !g_Hook_VClient_RenderView.m_MirvInput) {
            Tier0_Warning("Campath preview: no usable keyframes or free camera is unavailable.\n");
            return;
        }
		if (!g_CampathPreviewActive) {
			Tier0_Warning("Campath preview: use mirv_campath preview enter first.\n");
			return;
		}
		if (0 == _stricmp(cmd, "interp") && args->ArgC() == 4) {
            if (!path.CanEval()) {
                Tier0_Warning("Campath preview: this path cannot interpolate yet; select a keyframe instead.\n");
                return;
            }
			char * end = nullptr;
			const double fraction = strtod(args->ArgV(3), &end);
			if (end == args->ArgV(3) || *end || !(fraction >= 0.0 && fraction <= 1.0)) {
				Tier0_Warning("Campath preview: interpolation fraction must be 0..1.\n");
				return;
			}
			CamPathIterator it = path.GetBegin();
			CamPathIterator following = it;
			++following;
			while (following != path.GetEnd() && following.GetTime() <= g_CampathPreviewTime) {
				++it;
				++following;
			}
			if (following == path.GetEnd()) {
				Tier0_Warning("Campath preview: no following keyframe.\n");
				return;
			}
			const double t0 = it.GetTime();
			it = following;
			g_CampathPreviewTime = t0 + fraction * (it.GetTime() - t0);
			CampathPreview_SetCamera();
			return;
		}
		if (0 == _stricmp(cmd, "seek") && args->ArgC() == 3) {
			const double curtime = g_MirvCampath_Time.GetTime();
			int tick = -1;
			if (!g_MirvCampath_Time.GetDemoTickFromClientTime(curtime, g_CampathPreviewTime + path.GetOffset(), tick) || tick < 0) {
				Tier0_Warning("Campath preview: unable to calculate demo tick.\n");
				return;
			}
			std::ostringstream seekCommand;
			seekCommand << "demo_gototick " << tick;
            int currentTick = -1;
            g_MirvCampath_Time.GetCurrentDemoTick(currentTick);
            Tier0_Msg("Campath preview: time %.6f (client %.6f), current demo tick %i, requested tick %i.\n",
                g_CampathPreviewTime, g_CampathPreviewTime + path.GetOffset(), currentTick, tick);
			g_VEngineClient->ExecuteClientCmd(seekCommand.str().c_str());
			return;
		}
		if (args->ArgC() == 3) {
			char * end = nullptr;
			const long value = strtol(cmd, &end, 10);
			if (end != cmd && !*end && value >= 0 && value < (long)path.GetSize()) {
				CampathPreview_GotoIndex((int)value);
				Tier0_Msg("Campath preview: keyframe %i.\n", (int)value);
				return;
			}
		}
		CampathPreview_PrintHelp();
		return;
	}

	MirvCampath_ConCommand(args, Tier0_Msg, Tier0_Warning, &g_Hook_VClient_RenderView.m_CamPath, &g_MirvCampath_Time, &g_MirvCampath_Camera, &g_MirvCampath_Drawer);
	if (args->ArgC() == 1) Tier0_Msg("mirv_campath preview [...] - Navigate campath with free camera; use mirv_campath preview for help.\n");
}

CON_COMMAND(mirv_camexport, "controls camera motion data export") {
	if(!g_Hook_VClient_RenderView.IsInstalled())
	{
		Tier0_Warning("Error: Hook not installed.\n");
		return;
	}

	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * arg1 = args->ArgV(1);
		if(0 == _stricmp("stop", arg1))
		{
			g_Hook_VClient_RenderView.ExportEnd();
			return;
		}
		else
		if(0 == _stricmp("start", arg1) && 4 <= argc) {
			char const * fileName = args->ArgV(2);
			double fps = atof(args->ArgV(3));
			if(fps < 0.1f) fps = 0.1f;

			std::wstring wideFileName;
			if(
				!UTF8StringToWideString(fileName, wideFileName)
				|| !g_Hook_VClient_RenderView.ExportBegin(wideFileName.c_str(), 1.0/fps)
			)
				Tier0_Msg("Error: exporting failed.");

			return;
		}
		if(0 == _stricmp("timeinfo", arg1))
		{			
			Tier0_Msg("Current (interpolated client) time: %f\n", g_MirvTime.GetTime());
			return;
		}

	}

	Tier0_Msg(
		"Usage:\n"
		"mirv_camexport start <filename> <fps>\n"
		"mirv_camexport stop\n"
		"mirv_camexport timeinfo\n"
	);
}

CON_COMMAND(mirv_camimport, "controls camera motion data import") {
	if(!g_Hook_VClient_RenderView.IsInstalled())
	{
		Tier0_Warning("Error: Hook not installed.\n");
		return;
	}

	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * arg1 = args->ArgV(1);
		if(0 == _stricmp("stop", arg1)) {
			g_Hook_VClient_RenderView.ImportEnd();
			return;
		}
		else
		if(0 == _stricmp("start", arg1) && 3 <= argc) {
			char const * fileName = args->ArgV(2);
			g_Hook_VClient_RenderView.SetImportBaseTime(g_MirvTime.GetTime());

			std::wstring wideFileName;
			if(!UTF8StringToWideString(fileName, wideFileName)
				|| !g_Hook_VClient_RenderView.ImportBegin(wideFileName.c_str())
			)
				Tier0_Msg("Loading failed.");
			return;
		}
		else
		if(0 == _stricmp("basetime", arg1)) {
			if(3 <= argc) {
				char const * newTime = args->ArgV(2);
				if(0 == _stricmp("current", newTime))
					g_Hook_VClient_RenderView.SetImportBaseTime(g_MirvTime.GetTime());
				else
					g_Hook_VClient_RenderView.SetImportBaseTime((float)atof(newTime));
				return;
			}

			Tier0_Msg(
				"Usage:\n"
				"mirv_camimport basetime <newTime> | current\n"
				"Current setting: %f\n",
				g_Hook_VClient_RenderView.GetImportBasteTime()
			);
			return;
		}
		else
		if(0 == _stricmp("toCamPath", arg1) && 4 <= argc)
		{
			char const * arg2 = args->ArgV(2);
			char const * arg3 = args->ArgV(3);

			if(!g_Hook_VClient_RenderView.ImportToCamPath(0 != atoi(arg2), atof(arg3)))
				Tier0_Warning("ERROR: Something went wrong when converting to mirv_campath!\n");
			return;
		}
	}
	Tier0_Msg(
		"Usage:\n"
		"mirv_camimport start <filename>\n"
		"mirv_camimport stop\n"
		"mirv_camimport basetime [...]\n"
		"mirv_camimport toCamPath 0|1 <fov> - Convert to mirv_campath, 0 = no interp adjust | 1 (recommended) = adjust campath interpolation (to linear), <fov> Field Of View value to use (90 recommended).\n"
	);
}

CON_COMMAND(mirv_cvar_unhide_all,"removes hidden and development only flags from all cvars.")
{
	SOURCESDK::CSGO::ICvar * pCvar = WrpConCommands::GetVEngineCvar_CSGO();
	if(!pCvar)
	{
		Tier0_Warning("Error: No suitable Cvar interface found.\n");
		return;
	}

	SOURCESDK::CSGO::ICvar::Iterator iter(pCvar);

	int nUnhidden = 0;

	for(iter.SetFirst(); iter.IsValid(); iter.Next())
	{
		SOURCESDK::CSGO::ConCommandBase * cmd = iter.Get();

		if(cmd->IsFlagSet(FCVAR_DEVELOPMENTONLY | FCVAR_HIDDEN))
			nUnhidden++;

		cmd->RemoveFlags(FCVAR_DEVELOPMENTONLY | FCVAR_HIDDEN);
	}

	Tier0_Msg("Removed FCVAR_DEVELOPMENTONLY or FCVAR_HIDDEN from %i ConVars.\n", nUnhidden);
}

CON_COMMAND(mirv_cvar_hack, "force console variables (including hidden ones)")
{
	int argc = args->ArgC();

	if(3 <= argc)
	{
		char const * cvarName = args->ArgV(1);
		float cvarValue = (float)atof(args->ArgV(2));

		WrpConVarRef * cvar = new WrpConVarRef(cvarName);

		cvar->SetDirectHack(cvarValue);

		delete cvar;

		return;
	}

	Tier0_Msg(
		"mirv_cvar_hack <cvarName> <floatValue> - Force value directly, this will only work for true cvars (not cmds) and only for a subset of those, might have unwanted side effects - use with caution!\n"
	);
}

#ifndef _WIN64
CON_COMMAND(mirv_deathmsg, "controls death notification options")
{
	csgo_CHudDeathNotice_Console(args);
}
#endif //#ifndef _WIN64

CON_COMMAND(mirv_fov,"allows overriding FOV (Field Of View) of the camera")
{
	if(!g_Hook_VClient_RenderView.IsInstalled())
	{
		Tier0_Warning("Error: Hook not installed.\n");
		return;
	}

	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if(!_stricmp(arg1,"default"))
		{
			g_Hook_VClient_RenderView.FovDefault();
			return;
		}
		else
		if(!_stricmp(arg1,"handleZoom"))
		{
			if(3 <= argc)
			{
				const char * arg2 = args->ArgV(2);

				if(!_stricmp(arg2, "enabled"))
				{
					if(4 <= argc)
					{
						const char * arg3 = args->ArgV(3);

						g_Hook_VClient_RenderView.handleZoomEnabled = 0 != atoi(arg3);
						return;
					}

					Tier0_Msg(
						"Usage:\n"
						"mirv_fov handleZoom enabled 0|1 - Enable (1), disable (0).\n"
						"Current value: %s\n",
						g_Hook_VClient_RenderView.handleZoomEnabled ? "1" : "0"
					);
					return;
				}
				else
				if(!_stricmp(arg2, "minUnzoomedFov"))
				{
					if(4 <= argc)
					{
						const char * arg3 = args->ArgV(3);

						if(!_stricmp(arg3, "current"))
						{
							g_Hook_VClient_RenderView.handleZoomMinUnzoomedFov = g_Hook_VClient_RenderView.LastCameraFov;
							return;
						}
						else if (0 == _stricmp("real", arg3) && 5 <= argc)
						{
							char const * arg4 = args->ArgV(4);

							g_Hook_VClient_RenderView.handleZoomMinUnzoomedFov = Auto_InverseFovScaling(g_Hook_VClient_RenderView.LastWidth, g_Hook_VClient_RenderView.LastHeight, atof(arg4));
							return;
						}
						else if(StringIsEmpty(arg3) || !StringIsAlphas(arg3))
						{
							g_Hook_VClient_RenderView.handleZoomMinUnzoomedFov = atof(arg3);
							return;
						}
					}

					Tier0_Msg(
						"Usage:\n"
						"mirv_fov handleZoom minUnzoomedFov current - Set current fov as threshold.\n"
						"mirv_fov handleZoom minUnzoomedFov [real] <f> - Set floating point value <f> as threshold.\n"
						"Current value: %f\n",
						g_Hook_VClient_RenderView.handleZoomMinUnzoomedFov
					);
					return;
				}
			}

			Tier0_Msg(
				"Usage:\n"
				"mirv_fov handleZoom enabled [...] - Whether to enable zoom handling (if enabled mirv_fov is only active if it's not below minUnzoomedFov (not zoomed)).\n"
				"mirv_fov handleZoom minUnzoomedFov [...] - Zoom detection threshold.\n"
			);
			return;
		}
		else if (0 == _stricmp("real", arg1) && 3 <= argc)
		{
			char const * arg2 = args->ArgV(2);


			g_Hook_VClient_RenderView.FovOverride(Auto_InverseFovScaling(g_Hook_VClient_RenderView.LastWidth, g_Hook_VClient_RenderView.LastHeight, atof(arg2)));
			return;
		}
		if(StringIsEmpty(arg1) || !StringIsAlphas(arg1))
		{
			g_Hook_VClient_RenderView.FovOverride(atof(arg1));
			return;
		}
	}

	Tier0_Msg(
		"Usage:\n"
		"mirv_fov [real] <f> - Override fov with given floating point value <f>.\n"
		"mirv_fov default - Revert to the game's default behaviour.\n"
		"mirv_fov handleZoom [...] - Handle zooming (e.g. AWP in CS:GO).\n"
	);
	{
		Tier0_Msg("Current value: ");

		double fovValue;
		if(!g_Hook_VClient_RenderView.GetFovOverride(fovValue))
			Tier0_Msg("default (currently: %f)\n", g_Hook_VClient_RenderView.LastCameraFov);
		else
			Tier0_Msg("%f\n", fovValue);
	}
}

#ifndef _WIN64
CON_COMMAND(mirv_replace_name, "allows replacing player names")
{
	csgo_ReplacePlayerName_Console(args);
}
#endif //#ifndef _WIN64

#ifndef _WIN64
CON_COMMAND(mirv_replace_team_name, "allows replacing player team names")
{
	csgo_ReplaceTeamName_Console(args);
}
#endif //#ifndef _WIN64

CON_COMMAND(mirv_input, "Input mode configuration.")
{
    if (args->ArgC() >= 3 && 0 == _stricmp(args->ArgV(1), "camera"))
    {
        if (0 == _stricmp(args->ArgV(2), "uiCursor"))
        {
            if (args->ArgC() >= 4)
            {
                g_Hook_VClient_RenderView.UiCursorSuspend_set(0 != atoi(args->ArgV(3)));
                return;
            }
            Tier0_Msg("mirv_input camera uiCursor 0|1 - Yield to Source's visible UI cursor. Experimental, default 0. Current: %i\n",
                g_Hook_VClient_RenderView.UiCursorSuspend_get() ? 1 : 0);
            return;
        }
        if (0 == _stricmp(args->ArgV(2), "uiCursorStatus"))
        {
            bool available = false, visible = false;
            g_Hook_VClient_RenderView.UiCursorStatus(available, visible);
            Tier0_Msg("VGUI_Surface030: %s, IsCursorVisible: %s, uiCursor: %i\n",
                available ? "available" : "unavailable",
                available ? (visible ? "1" : "0") : "n/a",
                g_Hook_VClient_RenderView.UiCursorSuspend_get() ? 1 : 0);
            Tier0_Msg("Note: opening the console itself may make the UI cursor visible.\n");
            return;
        }
    }
	g_Hook_VClient_RenderView.Console_MirvInput(args);
}

#ifndef _WIN64
CON_COMMAND(mirv_snd_timescale, "(CS:GO only) allows to override host_timescale value for sound system.")
{
	if(!Hook_csgo_SndMixTimeScalePatch())
	{
		Tier0_Warning("Error: Hook not installed.\n");
		return;
	}

	int argc = args->ArgC();

	if(2 <= argc)
	{
		const char * arg1 = args->ArgV(1);

		if(0 == _stricmp("default", arg1))
		{
			csgo_SndMixTimeScalePatch_enable = false;
			return;
		}
		else
		{
			csgo_SndMixTimeScalePatch_enable = true;
			csgo_SndMixTimeScalePatch_value = (float)atof(arg1);
			return;
		}
	}
	Tier0_Msg(
		"Usage:\n"
		"mirv_snd_timescale <fValue> - override sound system host_timescale value with floating point value <fValue>.\n"
		"mirv_snd_timescale default - don't override.\n"
	);
}
#endif //#ifndef _WIN64

CON_COMMAND(mirv_gameoverlay, "GameOverlayRenderer control.")
{
	int argc = args->ArgC();

	if(3 <= argc)
	{
		const char * arg1 = args->ArgV(1);

		if( 0 == _stricmp("enabled", arg1)
			|| 0 == _stricmp("enable", arg1))
		{
			bool value = 0 != atoi(args->ArgV(2));
			Tier0_Msg(
				"%s %s.\n",
				value ? "Enable" : "Disable",
				GameOverlay_Enable(value) ? "OK" : "FAILED"
			);
			return;
		}
	}

	Tier0_Msg(
		"Usage:\n"
		"mirv_gameoverlay enabled 0|1 - Disable/Enable the GameOverlay (will only do s.th. useful when it was enabled initially).\n"
	);
}

#ifndef _WIN64
CON_COMMAND(mirv_snd_filter, "Sound control (e.g. blocking sounds).")
{
	if(!csgo_S_StartSound_Install())
	{
		Tier0_Warning("Error: Hook not installed.\n");
		return;
	}

	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if(0 == _stricmp("block", arg1))
		{
			if(3 <= argc)
			{
				const char * arg2 = args->ArgV(2);

				csgo_S_StartSound_Block_Add(arg2);
				return;
			}
		}
		else
		if(0 == _stricmp("print", arg1))
		{
			csgo_S_StartSound_Block_Print();
			return;
		}
		else
		if(0 == _stricmp("remove", arg1))
		{
			if(3 <= argc)
			{
				const char * arg2 = args->ArgV(2);

				csgo_S_StartSound_Block_Remove(atoi(arg2));
				return;
			}
		}
		else
		if(0 == _stricmp("clear", arg1))
		{
			csgo_S_StartSound_Block_Clear();
			return;
		}
		else
		if(0 == _stricmp("debug", arg1))
		{
			if(3 <= argc)
			{
				const char * arg2 = args->ArgV(2);

				g_csgo_S_StartSound_Debug = 0 != atoi(arg2);
				return;
			}
			
			Tier0_Msg("Current value: %s\n", g_csgo_S_StartSound_Debug ? "1" : "0");
			return;
		}
	}

	Tier0_Msg(
		"Usage:\n"
		"mirv_snd_filter block <mask> - Blocks given <mask> string (for format see below).\n"
		"mirv_snd_filter print - Prints current blocks.\n"
		"mirv_snd_filter remove <index> - Removes the block with index <index> (You can get that from the print sub-command).\n"
		"mirv_snd_filter clear - Clears all blocks.\n"
		"mirv_snd_filter debug 0|1 - Print sounds played into console.\n"
		"<mask> - string to match, where \\* = wildcard and \\\\ = \\\n"
	);
}
#endif //#ifndef _WIN64

#ifndef _WIN64
typedef struct MirvListEntitiesEntry_s {
	int idx;
	double dist;
	SOURCESDK::C_BaseEntity_csgo * be;
	
	MirvListEntitiesEntry_s(int _idx, double _dist, SOURCESDK::C_BaseEntity_csgo * _be)
		: idx(_idx)
		, dist(_dist)
		, be(_be)
	{

	}

} MirvListEntitiesEntry_t;

bool mirv_listentities_dist_compare(const MirvListEntitiesEntry_t & first, const MirvListEntitiesEntry_t & second)
{
	return first.dist < second.dist;
}

void mirv_print_entity(int index, double dist, SOURCESDK::C_BaseEntity_csgo * be)
{
	const char * className = be->GetClassname();
	if (!className) className = "[NULL]";

	const char * entName = be->GetEntityName();
	if (!entName) entName = "[NULL]";

	Tier0_Msg(
		"%i (%f): %s::%s :%i\n"
		, index
		, dist
		, className
		, entName
		, be->GetRefEHandle().ToInt()
	);

	if (be->IsPlayer() && g_VEngineClient)
	{
		if (SOURCESDK::IVEngineClient_014_csgo * pEngineCsgo = g_VEngineClient->GetVEngineClient_csgo())
		{

			SOURCESDK::player_info_t_csgo pInfo;
			if (pEngineCsgo->GetPlayerInfo(index, &pInfo))
			{
				std::ostringstream oss;

				oss << pInfo.xuid;

				Tier0_Msg(
					"\tplayerInfo.xuid: %s\n"
					"\tplayerInfo.name: %s\n"
					"\tplayerInfo.userID: %i\n"
					"\tplayerInfo.guid: %s\n"
					, oss.str().c_str()
					, pInfo.name
					, pInfo.userID
					, pInfo.guid
				);
			}
		}
	}
}


#endif //#ifndef _WIN64

// TF2: List entities through IClientTools. Only what IClientTools exposes is available
// (entity index, class name, model name, player flag and position; no entity names,
// entity handles or player info).
struct MirvListEntitiesTf2Entry_t
{
	int idx;
	double dist;
	bool isPlayer;
	std::string className;
	std::string modelName;
};

static bool mirv_listentities_tf2_dist_compare(const MirvListEntitiesTf2Entry_t & first, const MirvListEntitiesTf2Entry_t & second)
{
	return first.dist < second.dist;
}

static void mirv_listentities_tf2(IWrpCommandArgs * args)
{
	CClientToolsTf2 * tools = CClientToolsTf2::Instance();
	SOURCESDK::TF2::IClientTools * iface = tools ? tools->GetClientTools() : 0;

	if (!iface)
	{
		Tier0_Warning("Client tools are not available!\n");
		return;
	}

	bool onlyPlayer = false;
	bool sortByDistance = false;
	const char * classWildCard = "\\*";

	for (int i = 1; i < args->ArgC(); ++i)
	{
		const char * argI = args->ArgV(i);

		if (0 == _stricmp(argI, "isPlayer=1"))
		{
			onlyPlayer = true;
		}
		else if (0 == _stricmp(argI, "sort=distance"))
		{
			sortByDistance = true;
		}
		else if (StringIBeginsWith(argI, "class="))
		{
			classWildCard = argI + strlen("class=");
		}
	}

	Vector3 cameraOrigin(
		g_Hook_VClient_RenderView.LastCameraOrigin[0],
		g_Hook_VClient_RenderView.LastCameraOrigin[1],
		g_Hook_VClient_RenderView.LastCameraOrigin[2]
	);

	std::vector<MirvListEntitiesTf2Entry_t> result;

	for (auto ent = iface->FirstEntity(); ent; ent = iface->NextEntity(ent))
	{
		const int idx = iface->GetEntIndex(ent);

		// Only networked entities can be addressed by index (e.g. with mirv_aim entityIndex).
		if (idx < 0 || idx >= 2048)
			continue;

		const bool isPlayer = iface->IsPlayer(ent);

		if (onlyPlayer && !isPlayer)
			continue;

		// Entities only have a tool handle while the client is in recording mode.
		// Otherwise attach temporarily and detach again afterwards, so listing has no lasting effect.
		bool attachedHere = false;
		SOURCESDK::TF2::HTOOLHANDLE hHandle = iface->GetToolHandleForEntityByIndex(idx);

		if (SOURCESDK::TF2::HTOOLHANDLE_INVALID == hHandle)
		{
			hHandle = iface->AttachToEntity(ent);
			attachedHere = SOURCESDK::TF2::HTOOLHANDLE_INVALID != hHandle;
		}

		if (SOURCESDK::TF2::HTOOLHANDLE_INVALID != hHandle)
		{
			const char * className = iface->GetClassname(hHandle);
			if (!className) className = "[NULL]";

			if (StringWildCard1Matched(classWildCard, className))
			{
				const char * modelName = iface->GetModelName(hHandle);
				if (!modelName) modelName = "";

				SOURCESDK::Vector vEntOrigin = iface->GetAbsOrigin(hHandle);
				Vector3 entOrigin(vEntOrigin.x, vEntOrigin.y, vEntOrigin.z);

				MirvListEntitiesTf2Entry_t entry;
				entry.idx = idx;
				entry.dist = (entOrigin - cameraOrigin).Length();
				entry.isPlayer = isPlayer;
				entry.className = className;
				entry.modelName = modelName;

				result.push_back(entry);
			}
		}

		if (attachedHere)
			iface->DetachFromEntity(ent);
	}

	if (sortByDistance)
		std::stable_sort(result.begin(), result.end(), mirv_listentities_tf2_dist_compare);

	Tier0_Msg(
		"Results:\n"
		"index (distance): className, model: modelName [player]\n"
	);

	for (std::vector<MirvListEntitiesTf2Entry_t>::const_iterator it = result.begin(); it != result.end(); ++it)
	{
		Tier0_Msg(
			"%i (%f): %s, model: %s%s\n"
			, it->idx
			, it->dist
			, it->className.c_str()
			, it->modelName.c_str()
			, it->isPlayer ? " [player]" : ""
		);
	}

	Tier0_Msg(
		"\n"
		"Usage:\n"
		"mirv_listentities [isPlayer=1] [sort=distance] [class=<wildCardString(\\* = wildcard, \\\\ = \\)>]\n"
	);
}

CON_COMMAND(mirv_listentities, "Print info about currently active entites. (CS:GO and TF2)")
{
	if (SourceSdkVer_TF2 == g_SourceSdkVer || SourceSdkVer_TF2Classified == g_SourceSdkVer)
	{
		mirv_listentities_tf2(args);
		return;
	}

#ifndef _WIN64
	if(!SOURCESDK::g_Entitylist_csgo)
	{
		Tier0_Warning("Not supported for your engine!\n");
		return;
	}

	bool onlyPlayer = false;
	bool sortByDistance = false;
	const char * classWildCard = "\\*";
	
	for (int i = 1; i < args->ArgC(); ++i)
	{
		const char * argI = args->ArgV(i);
		size_t argILen = strlen(argI);

		if (0 == _stricmp(argI, "isPlayer=1"))
		{
			onlyPlayer = true;
		}
		else if (0 == _stricmp(argI, "sort=distance"))
		{
			sortByDistance = true;
		}
		else if (StringIBeginsWith(argI, "class="))
		{
			classWildCard = argI + strlen("class=");
		}
	}

	std::list<MirvListEntitiesEntry_t> result;

	Vector3 cameraOrigin(
		g_Hook_VClient_RenderView.LastCameraOrigin[0],
		g_Hook_VClient_RenderView.LastCameraOrigin[1],
		g_Hook_VClient_RenderView.LastCameraOrigin[2]
	);

	int imax = SOURCESDK::g_Entitylist_csgo->GetHighestEntityIndex();

	for (int i = 0; i <= imax; ++i)
	{
		SOURCESDK::IClientEntity_csgo * ce = SOURCESDK::g_Entitylist_csgo->GetClientEntity(i);
		SOURCESDK::C_BaseEntity_csgo * be = ce ? ce->GetBaseEntity() : 0;

		if (be)
		{
		

			if ((!onlyPlayer || be->IsPlayer()))
			{
				const char * className = be->GetClassname();
				if (!className) className = "[NULL]";

				if (StringWildCard1Matched(classWildCard, className))
				{

					SOURCESDK::Vector vEntOrigin = be->GetAbsOrigin();
					Vector3 entOrigin(vEntOrigin.x, vEntOrigin.y, vEntOrigin.z);

					double dist = (entOrigin - cameraOrigin).Length();

					result.emplace_back(i, dist, be);
				}
			}
		}
	}

	if(sortByDistance)
		result.sort(mirv_listentities_dist_compare);

	Tier0_Msg(
		"Results:\n"
		"index (distance): className::entityName :entityHandle\n"
	);


	for(std::list<MirvListEntitiesEntry_t>::iterator it = result.begin(); it != result.end(); ++it)
	{
		int i = it->idx;
		SOURCESDK::C_BaseEntity_csgo * be = it->be;
		double dist = it->dist;

		mirv_print_entity(i, dist, be);
	}

	Tier0_Msg(
		"\n"
		"Usage:\n"
		"mirv_listentities [isPlayer=1] [sort=distance] [class=<wildCardString(\\* = wildcard, \\\\ = \\)>]\n"
	);
#else
	Tier0_Warning("Not supported for your engine!\n");
#endif //#ifndef _WIN64
}

struct MirvListPlayersEntry_t
{
	int idx;
	int userId;
	double dist;
	std::string name;
};

static bool mirv_listplayers_dist_compare(const MirvListPlayersEntry_t & first, const MirvListPlayersEntry_t & second)
{
	// Players with unknown distance (< 0) go last.
	const double a = first.dist < 0 ? 1e30 : first.dist;
	const double b = second.dist < 0 ? 1e30 : second.dist;
	return a < b;
}

CON_COMMAND(mirv_listplayers, "Print players with their entity index and name. (TF2)")
{
	if (!(SourceSdkVer_TF2 == g_SourceSdkVer || SourceSdkVer_TF2Classified == g_SourceSdkVer))
	{
		Tier0_Warning("Not supported for your engine!\n");
		return;
	}

	bool sortByDistance = false;

	for (int i = 1; i < args->ArgC(); ++i)
	{
		if (0 == _stricmp(args->ArgV(i), "sort=distance"))
			sortByDistance = true;
	}

	Vector3 cameraOrigin(
		g_Hook_VClient_RenderView.LastCameraOrigin[0],
		g_Hook_VClient_RenderView.LastCameraOrigin[1],
		g_Hook_VClient_RenderView.LastCameraOrigin[2]
	);

	std::vector<MirvListPlayersEntry_t> result;
	const int maxPlayers = Mirv_Tf2_GetMaxPlayers();

	for (int i = 1; i <= maxPlayers; ++i)
	{
		char name[64];
		int userId = 0;

		if (!Mirv_Tf2_GetPlayerInfo(i, name, sizeof(name), &userId))
			continue;

		MirvListPlayersEntry_t entry;
		entry.idx = i;
		entry.userId = userId;
		entry.name = name;
		entry.dist = -1.0;

		float o[3], a[3];
		if (Mirv_Tf2_GetEntityOriginAngles(i, o, a))
			entry.dist = (Vector3(o[0], o[1], o[2]) - cameraOrigin).Length();

		result.push_back(entry);
	}

	if (sortByDistance)
		std::stable_sort(result.begin(), result.end(), mirv_listplayers_dist_compare);

	Tier0_Msg("Results:\nentity index (distance): name [userid]\n");

	for (std::vector<MirvListPlayersEntry_t>::const_iterator it = result.begin(); it != result.end(); ++it)
	{
		if (0 <= it->dist)
			Tier0_Msg("%i (%f): %s [userid %i]\n", it->idx, it->dist, it->name.c_str(), it->userId);
		else
			Tier0_Msg("%i (?): %s [userid %i]\n", it->idx, it->name.c_str(), it->userId);
	}

	if (result.empty())
		Tier0_Msg("(no players found, are you in a game or demo?)\n");

	Tier0_Msg(
		"\n"
		"Usage:\n"
		"mirv_listplayers [sort=distance]\n"
		"Use the entity index with mirv_cam attach, mirv_listbones and mirv_aim entityIndex.\n"
	);
}

static bool MirvCommands_ContainsNoCase(const char * hay, const char * needle)
{
	std::string h(hay);
	std::string n(needle);
	std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return (char)tolower(c); });
	std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)tolower(c); });
	return std::string::npos != h.find(n);
}

CON_COMMAND(mirv_listbones, "Print the bones of an entity with their names and positions. (TF2)")
{
	if (!(SourceSdkVer_TF2 == g_SourceSdkVer || SourceSdkVer_TF2Classified == g_SourceSdkVer))
	{
		Tier0_Warning("Not supported for your engine!\n");
		return;
	}

	if (args->ArgC() < 2)
	{
		Tier0_Msg(
			"Usage:\n"
			"mirv_listbones <iEntityIndex> [<sNamePart>] - Print the bones of the entity (index, name, world position, position relative to the entity and parent bone), optionally only those with <sNamePart> in the name.\n"
			"Use mirv_listplayers or mirv_listentities to find the entity index.\n"
		);
		return;
	}

	const int entityIndex = atoi(args->ArgV(1));
	const char * filter = 3 <= args->ArgC() ? args->ArgV(2) : 0;

	MirvTf2Bone_t bones[MIRV_TF2_MAX_BONES];
	int count = 0;

	if (!Mirv_Tf2_GetBones(entityIndex, bones, count))
	{
		Tier0_Warning("Could not get bones of entity %i (no such entity or it has no model).\n", entityIndex);
		return;
	}

	MirvTf2BoneName_t names[MIRV_TF2_MAX_BONES];
	const int numNames = Mirv_Tf2_GetBoneNames(entityIndex, names);
	const bool haveNames = 0 < numNames;

	if (filter && !haveNames)
	{
		Tier0_Warning("Bone names are not available, ignoring the name filter.\n");
		filter = 0;
	}

	float entOrigin[3] = { 0, 0, 0 };
	float entAngles[3] = { 0, 0, 0 };
	const bool haveEntity = Mirv_Tf2_GetEntityOriginAngles(entityIndex, entOrigin, entAngles);

	const double deg2rad = 0.017453292519943295;
	const double yawSin = sin(entAngles[1] * deg2rad);
	const double yawCos = cos(entAngles[1] * deg2rad);

	int numValid = 0;
	int highest = -1;
	int lowest = -1;

	for (int i = 0; i < count; ++i)
	{
		if (!bones[i].valid)
			continue;

		++numValid;

		if (-1 == highest || bones[highest].origin[2] < bones[i].origin[2]) highest = i;
		if (-1 == lowest || bones[i].origin[2] < bones[lowest].origin[2]) lowest = i;
	}

	{
		float absAngles[3], renderAngles[3];

		if (Mirv_Tf2_GetEntityAnglesInfo(entityIndex, absAngles, renderAngles))
		{
			Tier0_Msg(
				"Entity angles (pitch yaw roll): abs %.1f %.1f %.1f, render %.1f %.1f %.1f (the entity's facing is the render angles if they are not all zero)\n"
				, absAngles[0], absAngles[1], absAngles[2]
				, renderAngles[0], renderAngles[1], renderAngles[2]
			);
		}
	}

	Tier0_Msg("Entity %i has %i bones (highest bone index: %i)%s.\n", entityIndex, numValid, count - 1, haveNames ? "" : ", bone names are not available");

	int numShown = 0;

	for (int i = 0; i < count; ++i)
	{
		if (!bones[i].valid)
			continue;

		const bool hasName = i < numNames && names[i].name[0];

		if (filter && !(hasName && MirvCommands_ContainsNoCase(names[i].name, filter)))
			continue;

		++numShown;

		const MirvTf2Bone_t & bone = bones[i];
		const char * mark = i == highest ? "  <- highest" : (i == lowest ? "  <- lowest" : "");

		char nameCol[80];
		if (haveNames)
			snprintf(nameCol, sizeof(nameCol), " %-22s", hasName ? names[i].name : "?");
		else
			nameCol[0] = 0;

		char parentCol[96];
		if (haveNames && i < numNames && 0 <= names[i].parent && names[i].parent < numNames)
			snprintf(parentCol, sizeof(parentCol), " | parent %i (%s)", names[i].parent, names[names[i].parent].name);
		else if (haveNames && i < numNames)
			snprintf(parentCol, sizeof(parentCol), " | root");
		else
			parentCol[0] = 0;

		if (haveEntity)
		{
			const double dx = bone.origin[0] - entOrigin[0];
			const double dy = bone.origin[1] - entOrigin[1];
			const double dz = bone.origin[2] - entOrigin[2];

			Tier0_Msg(
				"%3i:%s pos %9.2f %9.2f %9.2f | forward %8.2f left %8.2f up %8.2f%s%s\n"
				, i
				, nameCol
				, bone.origin[0], bone.origin[1], bone.origin[2]
				, dx * yawCos + dy * yawSin
				, -dx * yawSin + dy * yawCos
				, dz
				, parentCol
				, mark
			);
		}
		else
		{
			Tier0_Msg(
				"%3i:%s pos %9.2f %9.2f %9.2f%s%s\n"
				, i
				, nameCol
				, bone.origin[0], bone.origin[1], bone.origin[2]
				, parentCol
				, mark
			);
		}
	}

	if (filter)
		Tier0_Msg("%i bones match \"%s\".\n", numShown, filter);

	Tier0_Msg("Attach the camera to a bone with: mirv_cam attach %i <iBoneIndex or part of bone name>\n", entityIndex);
}

CON_COMMAND(mirv_drawentities, "Draw the entity index over entities in the view. (TF2)")
{
	if (!(SourceSdkVer_TF2 == g_SourceSdkVer || SourceSdkVer_TF2Classified == g_SourceSdkVer))
	{
		Tier0_Warning("Not supported for your engine!\n");
		return;
	}

	const int argc = args->ArgC();

	if (2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if (0 == _stricmp("enabled", arg1))
		{
			if (3 <= argc)
			{
				g_EntityIndexDrawer.Enabled_set(0 != atoi(args->ArgV(2)));
				return;
			}

			Tier0_Msg("mirv_drawentities enabled 0|1 - Current value: %i\n", g_EntityIndexDrawer.Enabled_get() ? 1 : 0);
			return;
		}
        else if (0 == _stricmp("filter", arg1))
        {
            // Categories correspond to MirvTf2EntityLabel_t::category.
            static const char * names[5] = { "players", "projectiles", "weapons", "cosmetics", "other" };
            if (3 <= argc)
            {
                const char * name = args->ArgV(2);
                if (0 == _stricmp("all", name))
                {
                    if (4 <= argc) { g_EntityIndexDrawer.FilterAll_set(0 != atoi(args->ArgV(3))); return; }
                }
                else
                {
                    for (int i = 0; i < 5; ++i)
                        if (0 == _stricmp(names[i], name))
                        {
                            if (4 <= argc) { g_EntityIndexDrawer.Filter_set(i, 0 != atoi(args->ArgV(3))); return; }
                            Tier0_Msg("mirv_drawentities filter %s 0|1 - Current: %i\n", names[i], g_EntityIndexDrawer.Filter_get(i) ? 1 : 0);
                            return;
                        }
                }
            }
            Tier0_Msg("mirv_drawentities filter <all|players|projectiles|weapons|cosmetics|other> 0|1\n");
            for (int i = 0; i < 5; ++i) Tier0_Msg("  %s: %i\n", names[i], g_EntityIndexDrawer.Filter_get(i) ? 1 : 0);
            return;
        }
		else if (0 == _stricmp("height", arg1))
		{
			if (3 <= argc)
			{
				g_EntityIndexDrawer.Height_set((float)atof(args->ArgV(2)));
				return;
			}

			Tier0_Msg("mirv_drawentities height <fUnits> - Current value: %f\n", g_EntityIndexDrawer.Height_get());
			return;
		}
		else if (0 == _stricmp("size", arg1))
		{
			if (3 <= argc)
			{
				g_EntityIndexDrawer.Size_set((float)atof(args->ArgV(2)));
				return;
			}

			Tier0_Msg("mirv_drawentities size <fPixels> - Current value: %f\n", g_EntityIndexDrawer.Size_get());
			return;
		}
		else if (0 == _stricmp("maxDistance", arg1))
		{
			if (3 <= argc)
			{
				g_EntityIndexDrawer.MaxDistance_set((float)atof(args->ArgV(2)));
				return;
			}

			Tier0_Msg("mirv_drawentities maxDistance <fUnits> - Current value: %f\n", g_EntityIndexDrawer.MaxDistance_get());
			return;
		}
		else if (0 == _stricmp("maxLabels", arg1))
		{
			if (3 <= argc)
			{
				g_EntityIndexDrawer.MaxLabels_set(atoi(args->ArgV(2)));
				return;
			}

			Tier0_Msg("mirv_drawentities maxLabels <iCount> - Current value: %i\n", g_EntityIndexDrawer.MaxLabels_get());
			return;
		}
        else if (0 == _stricmp("color", arg1))
        {
            static const char * names[5] = { "players", "projectiles", "weapons", "cosmetics", "other" };
            if (3 <= argc)
            {
                const char * name = args->ArgV(2);
                for (int i = 0; i < 5; ++i)
                    if (0 == _stricmp(names[i], name))
                    {
                        if (6 <= argc)
                        {
                            g_EntityIndexDrawer.CategoryColor_set(i, atoi(args->ArgV(3)), atoi(args->ArgV(4)), atoi(args->ArgV(5)));
                            return;
                        }
                        int r, g, b;
                        g_EntityIndexDrawer.CategoryColor_get(i, r, g, b);
                        Tier0_Msg("mirv_drawentities color %s <red> <green> <blue> - Current: %i %i %i\n", names[i], r, g, b);
                        return;
                    }
            }
            Tier0_Msg("mirv_drawentities color <players|projectiles|weapons|cosmetics|other> <red> <green> <blue> (0-255)\n");
            for (int i = 0; i < 5; ++i)
            {
                int r, g, b;
                g_EntityIndexDrawer.CategoryColor_get(i, r, g, b);
                Tier0_Msg("  %s: %i %i %i\n", names[i], r, g, b);
            }
            return;
        }
        else if (0 == _stricmp("stackLabels", arg1))
        {
            if (args->ArgC() >= 3)
            {
                g_EntityIndexDrawer.StackLabels_set(0 != atoi(args->ArgV(2)));
                return;
            }
            Tier0_Msg("mirv_drawentities stackLabels 0|1 - 0: exact projected entity anchor; 1: world-aware stacking (default). Current: %i\n", g_EntityIndexDrawer.StackLabels_get() ? 1 : 0);
            return;
        }
        else if (0 == _stricmp("stackWorldDistance", arg1))
        {
            if (args->ArgC() >= 3)
            {
                g_EntityIndexDrawer.StackWorldDistance_set((float)atof(args->ArgV(2)));
                return;
            }
            Tier0_Msg("mirv_drawentities stackWorldDistance <fUnits> - Max 3D anchor distance eligible for stacking (default 32). Current: %g\n", g_EntityIndexDrawer.StackWorldDistance_get());
            return;
        }
		else if (0 == _stricmp("viewCheck", arg1))
		{
			if (3 <= argc)
			{
				g_EntityIndexDrawer.ViewCheck_set(0 != atoi(args->ArgV(2)));
				return;
			}

			Tier0_Msg("mirv_drawentities viewCheck 0|1 - Current value: %i\n", g_EntityIndexDrawer.ViewCheck_get() ? 1 : 0);
			return;
		}
		else if (0 == _stricmp("status", arg1))
		{
			unsigned int calls, mainViewCalls;
			int lastFound, lastPosted;
			double lastEyeDelta;

			g_EntityIndexDrawer.Stats_get(calls, mainViewCalls, lastFound, lastPosted, lastEyeDelta);

			Tier0_Msg(
				"Enabled: %i\n"
				"View updates seen: %u, of which on the main view: %u\n"
				"Last main view: %i entities found, %i labels sent to the renderer\n"
				"Last distance of the matrix's eye to the camera: %f (the main view is below 2)\n"
				"If the main view count stays at 0 while enabled, try mirv_drawentities viewCheck 0.\n"
				, g_EntityIndexDrawer.Enabled_get() ? 1 : 0
				, calls, mainViewCalls
				, lastFound, lastPosted
				, lastEyeDelta
			);
			return;
		}
	}

	Tier0_Msg(
		"Usage:\n"
		"mirv_drawentities enabled 0|1 - Draw the entity index above entities (nearest ones are drawn on top).\n"
		"mirv_drawentities filter <all|players|projectiles|weapons|cosmetics|other> 0|1 - Toggle categories (all on by default).\n"
		"mirv_drawentities height <fUnits> - Vertical offset from the entity anchor (default 0).\n"
		"mirv_drawentities size <fPixels> - Digit height in pixels (default 32).\n"
		"mirv_drawentities maxDistance <fUnits> - Only entities up to this distance from the camera, 0: no limit (default).\n"
		"mirv_drawentities maxLabels <iCount> - At most this many labels, the nearest ones (default 64).\n"
		"mirv_drawentities color <category> <red> <green> <blue> - RGB color for players/projectiles/weapons/cosmetics/other (0-255).\n"
		"mirv_drawentities stackLabels 0|1 - 0: exact entity anchor, 1: world-aware stacking (default).\n"
        "mirv_drawentities stackWorldDistance <fUnits> - Maximum 3D distance for stacking (default 32).\n"
		"mirv_drawentities viewCheck 0|1 - 1 (default): Only draw on the main view (not i.e. the 3D skybox).\n"
		"mirv_drawentities status - Print what the drawer is doing, for troubleshooting.\n"
		"The index can be used with mirv_cam attach, mirv_listbones and so on. The labels are part of the game's picture, so they show up in recordings too: turn them off before recording.\n"
	);
}

#ifndef _WIN64
extern SOURCESDK::CSGO::IEngineTrace * g_pClientEngineTrace;

class CMirvTraceFilter : public SOURCESDK::CSGO::CTraceFilter
{
public:
	CMirvTraceFilter(const SOURCESDK::CSGO::IHandleEntity *passentity);
	virtual bool ShouldHitEntity(SOURCESDK::CSGO::IHandleEntity *pHandleEntity, int contentsMask);

private:
	const SOURCESDK::CSGO::IHandleEntity *m_pPassEnt;
};

CMirvTraceFilter::CMirvTraceFilter(const SOURCESDK::CSGO::IHandleEntity *passedict)
{
	m_pPassEnt = passedict;
}

bool CMirvTraceFilter::ShouldHitEntity(SOURCESDK::CSGO::IHandleEntity *pHandleEntity, int contentsMask)
{
	return pHandleEntity && pHandleEntity != m_pPassEnt;
}

CON_COMMAND(mirv_traceentity, "Trace entity from current view position")
{
	if (!(SOURCESDK::g_Entitylist_csgo && CClientToolsCsgo::Instance() && g_pClientEngineTrace))
	{
		Tier0_Warning("Not supported for your engine / missing hooks,!\n");
		return;
	}

	double forward[3], right[3], up[3];

	double Rx = (g_Hook_VClient_RenderView.LastCameraAngles[0]);
	double Ry = (g_Hook_VClient_RenderView.LastCameraAngles[1]);
	double Rz = (g_Hook_VClient_RenderView.LastCameraAngles[2]);
	
	MakeVectors(Rz, Rx, Ry, forward, right, up);

	double Tx = (g_Hook_VClient_RenderView.LastCameraOrigin[0] + SOURCESDK_CSGO_MAX_TRACE_LENGTH * forward[0] - 0 * right[0] + 0 * up[0]);
	double Ty = (g_Hook_VClient_RenderView.LastCameraOrigin[1] + SOURCESDK_CSGO_MAX_TRACE_LENGTH * forward[1] - 0 * right[1] + 0 * up[1]);
	double Tz = (g_Hook_VClient_RenderView.LastCameraOrigin[2] + SOURCESDK_CSGO_MAX_TRACE_LENGTH * forward[2] - 0 * right[2] + 0 * up[2]);

	SOURCESDK::Vector vecAbsStart;
	vecAbsStart.Init((float)g_Hook_VClient_RenderView.LastCameraOrigin[0], (float)g_Hook_VClient_RenderView.LastCameraOrigin[1], (float)g_Hook_VClient_RenderView.LastCameraOrigin[2]);

	SOURCESDK::Vector vecAbsEnd;
	vecAbsEnd.Init((float)Tx, (float)Ty, (float)Tz);

	SOURCESDK::CSGO::Ray_t ray;
	ray.Init(vecAbsStart, vecAbsEnd);
	
	SOURCESDK::C_BaseEntity_csgo * localPlayer = reinterpret_cast<SOURCESDK::C_BaseEntity_csgo *>(CClientToolsCsgo::Instance()->GetClientToolsInterface()->GetLocalPlayer());
	CMirvTraceFilter traceFilter(localPlayer);

	SOURCESDK::CSGO::trace_t tr;

	g_pClientEngineTrace->TraceRay(ray, SOURCESDK_CSGO_MASK_ALL, &traceFilter, &tr);

	Tier0_Msg(
		"Result:\n"
		"index (distance): className::entityName :entityHandle\n"
	);


	if (tr.DidHit() && tr.m_pEnt)
	{
		SOURCESDK::Vector vecSE;

		SOURCESDK::CSGO::VectorSubtract(tr.endpos, vecAbsStart, vecSE);

		mirv_print_entity(tr.GetEntityIndex(), vecSE.LengthSqr(), tr.m_pEnt);
	}
}

#endif //#ifndef _WIN64

CON_COMMAND(mirv_aim, "Aiming system control.")
{
	if(!g_Hook_VClient_RenderView.GetGlobals())
	{
		Tier0_Warning("Error: Required hooks missing.\n");
		return;
	}

	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if(0 == _stricmp("active", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				g_Aiming.Active = 0 != atoi(arg2);
				return;
			}

			Tier0_Msg(
				"mirv_aim active 0|1 - Whether aiming is active (1) or not (0).\n"
				"Current value: %s\n",
				g_Aiming.Active ? "1" : "0"
			);
			return;
		}
		else
		if(0 == _stricmp("softDeactivate", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				g_Aiming.SoftDeactivate = 0 != atoi(arg2);
				return;
			}

			Tier0_Msg(
				"mirv_aim softDeactivate 0|1 - Whether to support soft deactivation (1) or not (0).\n"
				"Current value: %s\n",
				g_Aiming.SoftDeactivate ? "1" : "0"
			);
			return;
		}
		else if (0 == _stricmp("calcVecAng", arg1))
		{
			if (3 <= argc)
			{
				IMirvVecAngCalc * vecAng = g_MirvVecAngCalcs.GetByName(args->ArgV(2));

				if (!vecAng)
					Tier0_Warning("No vecAng calc \"%s\" exists.\n", args->ArgV(2));

				g_Aiming.Source_set(vecAng);

				return;
			}

			IMirvVecAngCalc * vecAng = g_Aiming.Source_get();

			Tier0_Msg(
				"mirv_aim calcVecAng <sClacVecAngName> - Calc to use as source (<sClacVecAngName> is name from mirv_calcs vecAng).\n"
				"Current value: %s\n"
				, vecAng ? "" : "(none)"
			);

			if (vecAng) { vecAng->Console_PrintBegin(); vecAng->Console_PrintEnd(); }
			Tier0_Msg("\n");

			return;
		}
		else if (0 == _stricmp("calcVecAngClear", arg1))
		{
			g_Aiming.Source_set(0);
		}
		else
		if(0 == _stricmp("entityIndex", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				g_Aiming.EntityIndex = atoi(arg2);
				g_Aiming.RebuildCalc();
				return;
			}

			Tier0_Msg(
				"mirv_aim entityIndex <n> - Entity index to aim after (use mirv_listentities to get one). Use invalid index (i.e. -1) to deactivate re-targeting.\n"
				"Current value: %i\n"
				, g_Aiming.EntityIndex
			);

			return;
		}
		else
		if(0 == _stricmp("point", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if(0 == _stricmp("cam", arg2))
				{
					Vector3 offset(0, 0, 0);

					if(6 <= argc)
					{
						char const * arg3 = args->ArgV(3);
						char const * arg4 = args->ArgV(4);
						char const * arg5 = args->ArgV(5);

						offset.X = atof(arg3);
						offset.Y = atof(arg4);
						offset.Z = atof(arg5);
					}

					g_Aiming.TargetPoint(
						Vector3(g_Hook_VClient_RenderView.LastCameraOrigin[0],
							g_Hook_VClient_RenderView.LastCameraOrigin[1],
							g_Hook_VClient_RenderView.LastCameraOrigin[2])
						+ offset);
					return;
				}
				else
				if(0 == _stricmp("last", arg2))
				{
					Vector3 offset(0, 0, 0);

					if(6 <= argc)
					{
						char const * arg3 = args->ArgV(3);
						char const * arg4 = args->ArgV(4);
						char const * arg5 = args->ArgV(5);

						offset.X = atof(arg3);
						offset.Y = atof(arg4);
						offset.Z = atof(arg5);
					}

					g_Aiming.TargetPointFromLast(offset);
					return;
				}
				else
				if(0 == _stricmp("abs", arg2) && 6 <= argc)
				{
					Vector3 offset(0, 0, 0);

					{
						char const * arg3 = args->ArgV(3);
						char const * arg4 = args->ArgV(4);
						char const * arg5 = args->ArgV(5);

						offset.X = atof(arg3);
						offset.Y = atof(arg4);
						offset.Z = atof(arg5);
					}

					g_Aiming.TargetPoint(offset);
					return;
				}
			}

			Tier0_Msg(
				"mirv_aim point abs <fX> <fY> <fz> - Absolute point in world coordinates.\n"
				"mirv_aim point cam [<fOffsetX> <fOffsetY> <fOffsetZ>] - Aim at current camera position with optional offset in world coordinates.\n"
				"mirv_aim point last [<fOffsetX> <fOffsetY> <fOffsetZ>] - Aim at last target position with optional offset in world coordinates.\n"
			);
			return;
		}
		else
		if(0 == _stricmp("offset", arg1))
		{
			if(5 <= argc)
			{
				char const * arg2 = args->ArgV(2);
				char const * arg3 = args->ArgV(3);
				char const * arg4 = args->ArgV(4);

				g_Aiming.OffSet = Vector3(
					atof(arg2), atof(arg3), atof(arg4)
				);
				return;
			}

			Tier0_Msg(
				"mirv_aim offset <x> <y> <z> - Offset in target local space to aim at (3 floating point values).\n"
				"Current value: %f %f %f\n",
				g_Aiming.OffSet.X, g_Aiming.OffSet.Y, g_Aiming.OffSet.Z
			);
			return;
		}
		else
		if(0 == _stricmp("snapTo", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				g_Aiming.SnapTo = 0 != atoi(arg2);
				return;
			}

			Tier0_Msg(
				"mirv_aim snapTo 0|1 - Whether to aim non-soft (1) or not (0).\n"
				"Current value: %s\n",
				g_Aiming.SnapTo ? "1" : "0"
			);
			return;
		}
		else
		if(0 == _stricmp("velLimit", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				g_Aiming.LimitVelocity = atof(arg2);
				return;
			}

			Tier0_Msg(
				"mirv_aim velLimit <fValue> - Max velocity (in angle degrees per second^1) possible (floating point value).\n"
				"Current value: %f\n",
				g_Aiming.LimitVelocity
			);
			return;
		}
		else
		if(0 == _stricmp("accelLimit", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				g_Aiming.LimitAcceleration = atof(arg2);
				return;
			}

			Tier0_Msg(
				"mirv_aim accelLimit <fValue> - Max acceleration (in angle degrees per second^2) possible (floating point value).\n"
				"Current value: %f\n",
				g_Aiming.LimitAcceleration
			);
			return;
		}
/*
		else
		if(0 == _stricmp("jerkLimit", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				g_Aiming.LimitJerk = atof(arg2);
				return;
			}

			Tier0_Msg(
				"mirv_aim jerkLimit <fValue> - Max jerk (in angle degrees per second^3) possible (floating point value).\n"
				"Current value: %f\n",
				g_Aiming.LimitJerk
			);
			return;
		}
*/
		else
		if(0 == _stricmp("origin", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if(0 == _stricmp("net", arg2))
				{
					g_Aiming.Origin = Aiming::O_Net;
					g_Aiming.RebuildCalc();
					return;
				}
				else
				if(0 == _stricmp("view", arg2))
				{
					g_Aiming.Origin = Aiming::O_View;
					g_Aiming.RebuildCalc();
					return;
				}
			}

			char const * curValue = "[unknown]";
			switch(g_Aiming.Origin)
			{
			case Aiming::O_Net:
				curValue = "net";
				break;
			case Aiming::O_View:
				curValue = "view";
				break;
			}

			Tier0_Msg(
				"mirv_aim origin net|view - Target origin to use.\n"
				"Current value: %s\n",
				curValue
			);
			return;
		}
		else
		if(0 == _stricmp("angles", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if(0 == _stricmp("net", arg2))
				{
					g_Aiming.Angles = Aiming::A_Net;
					g_Aiming.RebuildCalc();
					return;
				}
				else
				if(0 == _stricmp("view", arg2))
				{
					g_Aiming.Angles = Aiming::A_View;
					g_Aiming.RebuildCalc();
					return;
				}
			}

			char const * curValue = "[unknown]";
			switch(g_Aiming.Angles)
			{
			case Aiming::A_Net:
				curValue = "net";
				break;
			case Aiming::A_View:
				curValue = "view";
				break;
			}

			Tier0_Msg(
				"mirv_aim angles net|view - Target angles to use.\n"
				"Current value: %s\n",
				curValue
			);
			return;
		}
		else
		if(0 == _stricmp("up", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if(0 == _stricmp("input", arg2))
				{
					g_Aiming.Up = Aiming::U_Input;
					return;
				}
				else
				if(0 == _stricmp("world", arg2))
				{
					g_Aiming.Up = Aiming::U_World;
					return;
				}
			}

			char const * curValue = "[unknown]";
			switch(g_Aiming.Up)
			{
			case Aiming::U_Input:
				curValue = "input";
				break;
			case Aiming::U_World:
				curValue = "world";
				break;
			}

			Tier0_Msg(
				"mirv_aim up input|world - Camera up direction to use.\n"
				"Current value: %s\n",
				curValue
			);
			return;
		}
	}

	Tier0_Msg(
		"mirv_aim active [...] - Whether aiming is active.\n"
		"mirv_aim softDeactivate [...] - Whether to support soft deactivation (for snapTo 0).\n"
		"mirv_aim calcVecAng [...] - Source for target (overrides entityIndex, point, origin, angles).\n"
		"mirv_aim calcVecAngClear - Clears source for target (no target) (overrides entityIndex, point, origin, angles).\n"
		"mirv_aim entityIndex [...] - Entity index to aim after (use mirv_listentities to get one).\n"
		"mirv_aim point [...] - Point to aim after.\n"
		"mirv_aim origin [...] - Target origin to use.\n"
		"mirv_aim angles [...] - Target angles to use.\n"
		"mirv_aim offset [...] - Offset in target space to aim at.\n"
		"mirv_aim up [...] - How to determine the camera up direction.\n"
		"mirv_aim snapTo [...] - Whether to aim non-soft or soft.\n"
		"mirv_aim velLimit [...] - Max velocity possible (for snapTo 0).\n"
		"mirv_aim accelLimit [...] - Max acceleration possible (for snapTo 0).\n"
		// does not work atm // "mirv_aim jerkLimit [...] - Max jerk possible (for snapTo 0).\n"
	);
}


extern class CommandSystem g_CommandSystem;

CON_COMMAND(mirv_cmd, "Command system (for scheduling commands).")
{
	g_CommandSystem.Console_Command(args);
}

extern int g_iForcePostDataUpdateChanged;
extern bool g_b_Suppress_csgo_engine_Do_CCLCMsg_FileCRCCheck;
extern bool Install_csgo_engine_Do_CCLCMsg_FileCRCCheck();

extern float g_Mirv_Pov_Interp_OrgFac[2];
extern float g_Mirv_Pov_Interp_PingFac[2];
extern float g_Mirv_Pov_Interp_Offset[2];

extern float g_panorama_fps;


bool g_bBlockHostError = false;

typedef void (*Host_Error_t)(const char * fmt, ...);
Host_Error_t g_Org_HostError = nullptr;
void New_HostError(const char * fmt, ...) {
	va_list args;
	va_start(args, fmt);
	char buff[1024];
	vsnprintf(buff,1023,fmt,args);
	buff[1023] = 0;
	advancedfx::Warning("Suppressing Host_Error: %s\n",buff);
	va_end(args);
}

CON_COMMAND(mirv_fix, "Various fixes")
{
	int argc = args->ArgC();

	if (2 <= argc)
	{
		char const * cmd1 = args->ArgV(1);

#ifndef _WIN64
		if (!_stricmp("physicsMaxFps", cmd1))
		{
			if (!Hook_csgo_vphsyics_frametime_lowerlimit())
			{
				Tier0_Warning("Error: Required hooks not installed.\n");
				return;
			}

			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				csgo_vphysics_SetMaxFps(atof(cmd2));
				return;
			}

			Tier0_Msg(
				"mirv_fix physicsMaxFps <floatFPSval> - Set the FPS limit for physics.\n"
				"Current value: %f\n",
				csgo_vphysics_GetMaxFps()
			);
			return;
		}
		else
			if (!_stricmp("blockObserverTarget", cmd1))
		{
			if (!Hook_csgo_C_BasePlayer_RecvProxy_ObserverTarget())
			{
				Tier0_Warning("Error: Required hooks not installed.\n");
				return;
			}

			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				g_csgo_Block_C_BasePlayer_RecvProxy_ObserverTarget = 0 != atoi(cmd2);
				return;
			}

			Tier0_Msg(
				"mirv_fix blockObserverTarget 0|1 - Fixes unwanted player switching e.g. upon bomb plant (blocks C_BasePlayer::RecvProxy_ObserverTarget).\n"
				"Current value: %i\n",
				g_csgo_Block_C_BasePlayer_RecvProxy_ObserverTarget ? 1 : 0
			);
			return;
		}
		else
		if (!_stricmp("oldDuckFix", cmd1))
		{
			if (!Hook_csgo_CCSGameMovement_DuckFix())
			{
				Tier0_Warning("Error: Required hooks not installed.\n");
				return;
			}

			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				g_Enable_csgo_CCSGameMovement_DuckFix = 0 != atoi(cmd2);
				return;
			}

			Tier0_Msg(
				"mirv_fix oldDuckFix 0|1 - Can fix player stuck in duck for old demos.\n"
				"Current value: %i\n",
				g_Enable_csgo_CCSGameMovement_DuckFix ? 1 : 0
			);
			return;
		}
		else
		if (!_stricmp("playerAnimState", cmd1))
		{
			if (!Hook_csgo_PlayerAnimStateFix())
			{
				Tier0_Warning("Error: Required hooks not installed.\n");
				return;
			}

			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				Enable_csgo_PlayerAnimStateFix_set(atoi(cmd2));

				if(args->ArgC() >= 4)
					g_csgo_mystique_annimation_factor = (float)atof(args->ArgV(3));

				return;
			}

			Tier0_Msg(
				"mirv_fix playerAnimState 0|1|2|3- Fixes twitching of player arms, see https://github.com/advancedfx/advancedfx/wiki/Source%%3ASmoother-Demos , 0 - disabled, 1 - enabled, 2/3 - debug.\n"
				"Current value: %i\n",
				Enable_csgo_PlayerAnimStateFix_get()
			);
			return;
		}
		/*
		else if (0 == _stricmp("demoIndexTicks", cmd1))
		{
			if (!Hook_csgo_DemoFile())
			{
				Tier0_Warning("Error: Required hooks not installed.\n");
				return;
			}

			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				g_bFixCDemoFileTicks = atoi(cmd2);
				return;
			}

			Tier0_Msg(
				"mirv_fix demoIndexTicks <iValue> - Set to 1 or greater to enable indexing (sane values are 1920 ticks or greater), set 0 or less to disable.\n"
				"Current value: %i\n",
				g_bFixCDemoFileTicks
			);
			return;
		}
		*/
		else if (0 == _stricmp("selectedPlayerGlow", cmd1) && 3 == argc)
		{
			if (!AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS))
			{
				Tier0_Warning("Error: Required hooks not installed.\n");
				return;
			}

			MdtMemBlockInfos mdtInfos;

			MdtMemAccessBegin((LPVOID)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS), 7 * sizeof(unsigned char), &mdtInfos);

			int iArg2 = atoi(args->ArgV(2));

			if (2 == iArg2)
			{
				// Always glow selected.
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 0) = 0x90;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 1) = 0x90;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 5) = 0x90;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 6) = 0x90;
			}
			else if (0 != iArg2)
			{
				// Normal mode.
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 0) = 0x75;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 1) = 0x05;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 5) = 0x74;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 6) = 0x16;
			}
			else
			{
				// Never glow slected.
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 0) = 0x90;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 1) = 0x90;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 5) = 0xEB;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 6) = 0x16;
			}

			MdtMemAccessEnd(&mdtInfos);

			return;
		}
		else if (0 == _stricmp("forcePostDataUpdateChanged", cmd1))
		{
			if (3 <= argc)
			{
				g_iForcePostDataUpdateChanged = atoi(args->ArgV(2));
				return;
			}

			Tier0_Msg(
				"mirv_fix forcePostDataUpdateChanged -1|<iEntityIndex>\n"
				"Current value: %i\n"
				, g_iForcePostDataUpdateChanged);

			return;
		}
		else if (0 == _stricmp("forceDoAnimationEvents", cmd1))
		{
			if (3 <= argc)
			{
				if (AFXADDR_GET(csgo_client_C_TEPlayerAnimEvent_PostDataUpdate_NewModelAnims_JNZ)) {

					bool bEnable = 0 != atoi(args->ArgV(2));

					MdtMemBlockInfos mdtInfos;

					MdtMemAccessBegin((LPVOID)AFXADDR_GET(csgo_client_C_TEPlayerAnimEvent_PostDataUpdate_NewModelAnims_JNZ), 2 * sizeof(unsigned char), &mdtInfos);

					if (bEnable)
					{
						*((unsigned char*)AFXADDR_GET(csgo_client_C_TEPlayerAnimEvent_PostDataUpdate_NewModelAnims_JNZ) + 0) = 0x90;
						*((unsigned char*)AFXADDR_GET(csgo_client_C_TEPlayerAnimEvent_PostDataUpdate_NewModelAnims_JNZ) + 1) = 0x90;
					}
					else
					{
						*((unsigned char*)AFXADDR_GET(csgo_client_C_TEPlayerAnimEvent_PostDataUpdate_NewModelAnims_JNZ) + 0) = 0x75;
						*((unsigned char*)AFXADDR_GET(csgo_client_C_TEPlayerAnimEvent_PostDataUpdate_NewModelAnims_JNZ) + 1) = 0x14;
					}

					MdtMemAccessEnd(&mdtInfos);

					return;
				}
				else {
					Tier0_Warning("Error: Required hooks not installed.\n");
					return;
				}
			}
		}
		else if (0 == _stricmp("suppressFileCRCCheck", cmd1))
		{
			if(!Install_csgo_engine_Do_CCLCMsg_FileCRCCheck()) {
				Tier0_Warning("Error: Missing hooks.\n");
			} else if(argc < 3) {
				Tier0_Msg("mirv_fix suppressFileCRCCheck 0|1\nCurrent Value: %i\n", g_b_Suppress_csgo_engine_Do_CCLCMsg_FileCRCCheck?1:0);
			} else {
				g_b_Suppress_csgo_engine_Do_CCLCMsg_FileCRCCheck = 0 != atoi(args->ArgV(2));
			}

			return;
		}
		else
#endif //#ifndef _WIN64		
		if (!_stricmp("panoramaTiming", cmd1))
		{
			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);
				if(0 == _stricmp("factor", cmd2) && 4 <= argc) {
					float value = (float)atof(args->ArgV(3));
					if(0 < value)
						g_panorama_fps = -value;
					else
						Tier0_Warning("Error: <fFactor> must be greater than 0.\n");
					return;
				}
				else if(0 == _stricmp("framerate", cmd2) && 4 <= argc) {
					float value = (float)atof(args->ArgV(3));
					if(0 < value)
						g_panorama_fps = value;
					else
						Tier0_Warning("Error: <fFrameRateOrFps> must be greater than 0.\n");
					return;
				}
				else if(0 == _stricmp("disable", cmd2)) {
					g_panorama_fps = 0;
					return;
				}
				else if(0 == _stricmp("1", cmd2)) {
					g_panorama_fps = -1;
					return;
				}
				else if(0 == _stricmp("0", cmd2)) {
					g_panorama_fps = 0;
					return;
				}
			}

			Tier0_Msg(
				"mirv_fix panoramaTiming 0 - Shortcut for mirv_fix panoramaTiming disable.\n"
				"mirv_fix panoramaTiming 1 - Shortcut for mirv_fix panoramaTiming factor 1.\n"
				"mirv_fix panoramaTiming factor <fFactor> - Use realtime multiplied with <fFactor>.\n"
				"mirv_fix panoramaTiming framerate <fFrameRateOrFps> - Fix Panorama UI timing (default: 1).\n"
				"mirv_fix panoramaTiming disable - Restore original broken behavior.\n"
				"Default: factor 1\n"
				"Current value: "
			);
			if(0 == g_panorama_fps)
				Tier0_Msg("disable");
			else if(g_panorama_fps < 0) {
				Tier0_Msg("factor %f", -g_panorama_fps);
			}
			else {
				Tier0_Msg("framerate %f", g_panorama_fps);
			}
			Tier0_Msg("\n");
			return;
		}
		else if (0 == _stricmp("suppressHostError", cmd1) && 3 <= argc)
		{
			if (!AFXADDR_GET(engine_HostError))
			{
				Tier0_Warning("Error: Required hooks not installed.\n");
				return;
			}
			
			bool isOn = 0 != atoi(args->ArgV(2));
			bool wasOn = nullptr != g_Org_HostError;

			if(wasOn != isOn) {
				if(isOn) {
					g_Org_HostError = (Host_Error_t)AFXADDR_GET(engine_HostError);
					DetourTransactionBegin();
					DetourUpdateThread(GetCurrentThread());
					DetourAttach(&(PVOID&)g_Org_HostError, New_HostError);
					if(NO_ERROR != DetourTransactionCommit()) {
						advancedfx::Warning("DetourAttach failed\n");
						g_Org_HostError = nullptr;
					}
				} else if(g_Org_HostError) {
					DetourTransactionBegin();
					DetourUpdateThread(GetCurrentThread());
					DetourDetach(&(PVOID&)g_Org_HostError, New_HostError);
					if(NO_ERROR != DetourTransactionCommit()) {
						advancedfx::Warning("DetourDetach failed\n");
					}
					g_Org_HostError = nullptr;
				}
			}

			return;
		}
	}

#ifndef _WIN64
	Tier0_Msg(		
		"mirv_fix physicsMaxFps [...] - Can raise the FPS limit for physics (e.g. rag dolls, so they don't freeze upon high host_framerate).\n"
		"mirv_fix blockObserverTarget [...] - Fixes unwanted player switching, e.g. upon bomb plant (blocks C_BasePlayer::RecvProxy_ObserverTarget).\n"
		"mirv_fix oldDuckFix [...] - Can fix player stuck in duck for old demos.\n"
		"mirv_fix playerAnimState [...] - Fixes twitching of player arms/legs, see https://github.com/advancedfx/advancedfx/wiki/Source%3ASmoother-Demos\n"
		//"mirv_fix demoIndexTicks [...] - Tries to make backward skipping faster in demos.\n"
		"mirv_fix selectedPlayerGlow 0|1|2 - 1: Game default, 2: Always glow, 0 : never glow.\n"
		"mirv_fix forcePostDataUpdateChanged [...].\n"
		"mirv_fix forceDoAnimationEvents 0|1 - Only useful in combination with replacing old models with new ones for forcing animation events to be played, defaut is 0 (off).\n"
		"mirv_fix suppressFileCRCCheck 0|1 - This is only useful with HLAE special builds and it's on by default.\n"
	);
#endif //#ifndef _WIN64	
	Tier0_Msg(
		"mirv_fix panoramaTiming [...]\n"
	);
	if(AFXADDR_GET(engine_HostError)) Tier0_Msg(
		"mirv_fix suppressHostError 0|1"
	);
}


#ifndef _WIN64

extern SOURCESDK::CSGO::vgui::IPanel * g_pVGuiPanel_csgo;
extern SOURCESDK::CSGO::vgui::ISurface *g_pVGuiSurface_csgo;

bool MirvFindVPanel(SOURCESDK::CSGO::vgui::VPANEL panel, char const * panelName, SOURCESDK::CSGO::vgui::VPANEL * outPanel)
{
	if (!outPanel)
		return false;

	if (!strcmp(panelName, g_pVGuiPanel_csgo->GetName(panel)))
	{
		*outPanel = panel;
		return true;
	}			

	for (int i = 0; i < g_pVGuiPanel_csgo->GetChildCount(panel); ++i)
	{
		if (MirvFindVPanel(g_pVGuiPanel_csgo->GetChild(panel, i), panelName, outPanel))
			return true;
	}

	return false;
}

bool MirvVPanelSetVisible(char const * panelName, bool visible)
{
	SOURCESDK::CSGO::vgui::VPANEL panel;

	if (MirvFindVPanel(g_pVGuiSurface_csgo->GetEmbeddedPanel(), panelName, &panel))
	{
		g_pVGuiPanel_csgo->SetVisible(panel, visible);
		return true;
	}

	return false;
}

void MirvVPanelOnCommand(SOURCESDK::CSGO::vgui::Panel * panel, char const * command)
{
	void ** vtable = *(void***)panel;

	void * onCommand = (void *)vtable[96];

	__asm push command
	__asm mov ecx, panel
	__asm call onCommand
}

void MirvDoVPanelOnCommand(char const * panelName, char const * destinationModule, char const * panelCommand)
{
	if (!(g_pVGuiSurface_csgo && g_pVGuiPanel_csgo))
	{
		Tier0_Warning("Error: Missing dependencies.\n");
		return;
	}

	SOURCESDK::CSGO::vgui::VPANEL vpanel;

	if (MirvFindVPanel(g_pVGuiSurface_csgo->GetEmbeddedPanel(), panelName, &vpanel))
	{
		SOURCESDK::CSGO::vgui::Panel * panel = g_pVGuiPanel_csgo->GetPanel(vpanel, destinationModule);
		if (panel)
			MirvVPanelOnCommand(panel, panelCommand);
		else
			Tier0_Warning("Error: GetPanel failed for %s\n", panelName);
	}
	else
		Tier0_Warning("Error: Invalid panel name %s\n", panelName);
}

CON_COMMAND(mirv_vpanel, "VGUI Panel access")
{
	if (!(g_pVGuiSurface_csgo && g_pVGuiPanel_csgo))
	{
		Tier0_Warning("Error: Missing dependencies.\n");
		return;
	}

	int argc = args->ArgC();

	if (2 <= argc)
	{
		char const * cmd1 = args->ArgV(1);


		if (!_stricmp("hide", cmd1))
		{
			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				if (!MirvVPanelSetVisible(cmd2, false))
				{
					Tier0_Warning("Error: Invalid panel name %s\n", cmd2);
				}		
				return;
			}

			Tier0_Msg(
				"mirv_vpanel hide <panelName>\n"
			);
			return;
		}
		else
		if (!_stricmp("show", cmd1))
		{
			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				if (!MirvVPanelSetVisible(cmd2, true))
				{
					Tier0_Warning("Error: Invalid panel name %s\n", cmd2);
				}
				return;
			}

			Tier0_Msg(
				"mirv_vpanel show <panelName>\n"
			);
			return;
		}
		else if (!_stricmp("command", cmd1))
		{
			if (5 <= argc)
			{
				char const * panelName = args->ArgV(2);
				char const * panelModule = args->ArgV(3);
				char const * panelCommand = args->ArgV(4);

				MirvDoVPanelOnCommand(panelName, panelModule, panelCommand);
				return;
			}

			Tier0_Msg(
				"mirv_vpanel command <panelName> <sModule> <comand> - Execute <command> on panel with name <panelName> in module <sModule> (options are case-sensitive, <sModule> can e.g. be BaseUI or ClientDLL).\n"
				"Example: mirv_vpanel command DemoUIPanel BaseUI pause\n"
			);
			return;
		}
	}

	Tier0_Msg(
		"mirv_vpanel hide [...]\n"
		"mirv_vpanel show [...]\n"
		"mirv_vpanel command [...]\n"
		"Hint: To find panel names use vgui_drawtree 1.\n"
	);
}

#endif //#ifndef _WIN64


CON_COMMAND(mirv_camio, "New camera motion data import / export.")
{
	g_Hook_VClient_RenderView.Console_CamIO(args);
}

CON_COMMAND(mirv_loadlibrary, "Load a DLL.")
{
	int argc = args->ArgC();

	if (2 <= argc)
	{
		char const * cmd1 = args->ArgV(1);

		std::wstring wCmd1;
		if (UTF8StringToWideString(cmd1, wCmd1))
		{

			if (0 != LoadLibraryW(wCmd1.c_str()))
			{
				Tier0_Msg("LoadLibraryA OK.\n");
			}
			else
			{
				Tier0_Warning("LoadLibraryA failed.\n");
			}
		}
		else
		{
			Tier0_Warning("Failed to convert \"%s\" from UFT8 to UTF-16.\n", cmd1);
		}

		return;
	}

	Tier0_Msg(
		"mirv_loadlibrary <sDllFilePath> - Load DLL at given path.\n"
	);
}

#ifndef _WIN64
CON_COMMAND(mirv_time, "time control")
{
	int argc = args->ArgC();

	if (2 <= argc)
	{
		const char * cmd1 = args->ArgV(1);

		if (0 == _stricmp("mode", cmd1))
		{
			if(3 <= argc)
			{
				const char * cmd2 = args->ArgV(2);

				g_MirvTime.SetMode(0 == _stricmp("resumePaused", cmd2) ? CMirvTime::TimeMode_ResumePaused : CMirvTime::TimeMode_CurTime);
				return;
			}

			Tier0_Msg(
				"mirv_time mode curTime|resumePaused - Time mode, curTime pauses when the demo pauses, resumePaused resumes even then. Default is curTime.\n"
				"Current value: %s\n"
				, CMirvTime::TimeMode_ResumePaused == g_MirvTime.GetMode() ? "resumePaused" : "curTime"
			);
			return;
		}
		else if (0 == _stricmp("pausedTime", cmd1))
		{
			if (3 <= argc)
			{
				const char * cmd2 = args->ArgV(2);

				g_MirvTime.SetPausedTime((float)atof(cmd2));
				return;
			}

			Tier0_Msg(
				"mirv_time pausedTime <fSeconds> - Set currently added paused time.\n"
				"Current value: %f\n"
				, g_MirvTime.GetPausedTime()
			);
			return;
		}
		else if (0 == _stricmp("drive", cmd1))
		{
			if (3 <= argc)
			{
				const char* cmd2 = args->ArgV(2);

				if (0 == _stricmp("default", cmd2))
				{
					g_MirvTime.SetDriveTimeEnabled(false);
				}
				else
				{

					float value = (float)atof(cmd2);
					float minValue = AFX_MATH_EPS;
					float maxValue = 1.0f / AFX_MATH_EPS;

					if (value < minValue)
					{
						value = minValue;
						Tier0_Warning("AFXWARNING: Limiting to minimum: %f\n.", minValue);
					}
					else if (maxValue < value)
					{
						value = maxValue;
						Tier0_Warning("AFXWARNING: Limiting to maximum: %f\n.", maxValue);
					}

					g_MirvTime.SetDriveTimeFactor(value);
					g_MirvTime.SetDriveTimeEnabled(true);
				}
				return;
			}

			Tier0_Msg(
				"mirv_time drive <fFactor> - Set the time drive factor.\n"
				"mirv_time drive default - Disable time drive.\n"
				"Current value: "
			);
			if (g_MirvTime.GetDriveTimeEnabled()) Tier0_Msg("%f\n", g_MirvTime.GetDriveTimeFactor());
			else Tier0_Msg("default\n");
			return;
		}
	}

	Tier0_Msg(
		"mirv_time mode [...]\n"
		"mirv_time pausedTime [...]\n"
		"mirv_time drive [...]\n"
	);
}
#endif //#ifndef _WIN64

#ifndef _WIN64
void Mirv_Pov_Interp_CompensateLatencyOn();
void Mirv_Pov_Interp_CompensateLatencyOff();
void Mirv_Pov_Interp_Default();

extern bool g_csgo_spectatortools_extend_overviewmap;
#endif //#ifndef _WIN64

CON_COMMAND(mirv_cfg, "general HLAE configuration")
{
	int argC = args->ArgC();
	const char * arg0 = args->ArgV(0);

	if (2 <= argC)
	{
		const char * arg1 = args->ArgV(1);

#ifndef _WIN64
		if(0 == _stricmp("mirvForceSpectatorToolsMapOverviewShowAll", arg1))
		{
			if (3 <= argC)
			{
				g_csgo_spectatortools_extend_overviewmap = 0 != atoi(args->ArgV(2));
				return;
			}

			Tier0_Msg(
				"%s mirvForceSpectatorToolsMapOverviewShowAll 0|1\n"
				"Current value: %i\n"
				, arg0
				, g_csgo_spectatortools_extend_overviewmap ? 1 : 0
			);
			return;
		}
		else
#endif //#ifndef _WIN64		
		if (0 == _stricmp("fovScaling", arg1))
		{
			CSubWrpCommandArgs subArgs(args, 2);
			Console_MirvFovScaling(&subArgs);
			return;
		}
#ifndef _WIN64		
		else if (0 == _stricmp("forceViewOverride", arg1))
		{
			if (3 <= argC)
			{
				g_Hook_VClient_RenderView.ForceViewOverride = 0 != atoi(args->ArgV(2));
				return;
			}

			Tier0_Msg(
				"%s forceViewOverride 0|1\n"
				"Current value: %i\n"
				, arg0
				, g_Hook_VClient_RenderView.ForceViewOverride ? 1 : 0
			);
			return;
		}
		else if (0 == _stricmp("forceViewOverrideHltv", arg1))
		{
			if (3 <= argC)
			{
				g_Hook_VClient_RenderView.ForceViewOverrideHltv = 0 != atoi(args->ArgV(2));
				return;
			}

			Tier0_Msg(
				"%s forceViewOverrideHltv 0|1\n"
				"Current value: %i\n"
				, arg0
				, g_Hook_VClient_RenderView.ForceViewOverrideHltv ? 1 : 0
			);
			return;
		}
		else if (0 == _stricmp("viewOverrideReset", arg1))
		{
			if (3 <= argC)
			{
				g_Hook_VClient_RenderView.ViewOverrideReset = 0 != atoi(args->ArgV(2));
				return;
			}

			Tier0_Msg(
				"%s viewOverrideReset 0|1\n"
				"Current value: %i\n"
				, arg0
				, g_Hook_VClient_RenderView.ViewOverrideReset ? 1 : 0
			);
			return;
		}
		else if(0 == _stricmp("mirvPov", arg1)) {
			if(3 <= argC) {
				const char * cmd2 = args->ArgV(2);
				
				if(0 == _stricmp("interpDefault", cmd2)) {
					Mirv_Pov_Interp_Default();
					return;
				}
				else if(0 == _stricmp("interpCompensateLatencyOn", cmd2)) {
					Mirv_Pov_Interp_CompensateLatencyOn();
					return;
				}
				else if(0 == _stricmp("interpCompensateLatencyOff", cmd2)) {
					Mirv_Pov_Interp_CompensateLatencyOff();
					return;
				}
				else if(4 <= argC) {
					const char * cmd3 = args->ArgV(3);

					int iDim = 0 == _stricmp("local", cmd3) ? 1 : 0;
					if(iDim ||0 == _stricmp("other", cmd3))  {
						if(0 == _stricmp("interpPingFac", cmd2)) {
							if(5 <= argC)
							{
								g_Mirv_Pov_Interp_PingFac[iDim] = (float)atof(args->ArgV(4));
								return;
							}

							Tier0_Msg(
								"%s interpPingFac pingFac %s <fValue>\n"
								"Current value: %f\n"
								, arg0
								, cmd3
								, g_Mirv_Pov_Interp_PingFac[iDim]
							);
							return;
						}
						else if(0 == _stricmp("interpOffset", cmd2)) {
							if(5 <= argC)
							{
								g_Mirv_Pov_Interp_Offset[iDim] = (float)atof(args->ArgV(4));
								return;
							}

							Tier0_Msg(
								"%s mirvPovInterp interpOffset %s <fValue>\n"
								"Current value: %f\n"
								, arg0
								, cmd3
								, g_Mirv_Pov_Interp_Offset[iDim]
							);
							return;
						}
						else if(0 == _stricmp("interpFacOrg", cmd2)) {
							if(5 <= argC)
							{
								g_Mirv_Pov_Interp_OrgFac[iDim] = (float)atof(args->ArgV(4));
								return;
							}

							Tier0_Msg(
								"%s mirvPovInterp interpFacOrg %s <fValue>\n"
								"Current value: %f\n"
								, arg0
								, cmd3
								, g_Mirv_Pov_Interp_OrgFac[iDim]
							);
							return;
						}
					}
				}
			}
			
			Tier0_Msg(
				"%s mirvPov interpDefault - Restore default interp settings.\n"
				"%s mirvPov interpCompensateLatencyOn - Enable (approximate) latency compensation interp settings.\n"
				"%s mirvPov interpCompensateLatencyOff - Disable (approximate) latency compensation interp settings.\n"
				"%s mirvPov interpPingFac local|other [...] - Default: local=1 and other=0. Factor multiplied with ping / 1000.0 to delay interp for non-POV player-local entities.\n"
				"%s mirvPov interpOffset local|other [...] - Default: local=0 and other=0. Constant factor to add to interp.\n"
				"%s mirvPov interpFacOrg local|other [...] - Default: local=1 and other=1. Factor multiplied with original value by engine (depends on your cl_interp value!).\n"
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
			);
			return;
		}
#endif //#ifndef _WIN64
	}

	Tier0_Msg(
		"%s fovScaling [...] - Set default fov scaling.\n"
#ifndef _WIN64		
		"%s forceViewOverride [...] - If to force the view override onto the local player, can fix a few bugs (CS:GO only).\n"
		"%s forceViewOverrideHltv [...] - If to force the view override onto the HLTVCamera (e.g. for GOTV). (CS:GO only).\n"
		"%s viewOverrideReset [...] - If to reset roll to 0 and fov to 90 (unscaled) after ending a view override (CS:GO only).\n"
		"%s mirvForceSpectatorToolsMapOverviewShowAll [...] - If to extend map overivew in mirv_force_spectatortools.\n"
#endif //#ifndef _WIN64
		, arg0
#ifndef _WIN64
		, arg0
		, arg0
		, arg0
		, arg0
#endif //#ifndef _WIN64
	);
#ifndef _WIN64
	Tier0_Msg(
		"%s mirvPov [...] - Tampers with the interp to get more accurate POV view on average.\n"
		, arg0
	);
#endif //#ifndef _WIN64
}

#ifndef _WIN64
CON_COMMAND(mirv_guides, "Draw guides on screen (CS:GO).")
{
	int argC = args->ArgC();
	const char * arg0 = args->ArgV(0);

	if (3 <= argC)
	{
		const char * arg1 = args->ArgV(1);
		const char * arg2 = args->ArgV(2);

		if (0 == _stricmp("enabled", arg2))
		{
			if (0 == _stricmp("phiGrid", arg1))
			{
				if (4 <= argC)
				{
					g_AfxStreams.DrawPhiGrid = 0 != atoi(args->ArgV(3));
					return;
				}

				Tier0_Msg(
					"%s phiGrid enabled 0|1\n"
					"Current value: %i\n"
					, arg0
					, g_AfxStreams.DrawPhiGrid ? 1 : 0
				);
				return;
			}
			else if (0 == _stricmp("ruleOfThirds", arg1))
			{
				if (4 <= argC)
				{
					g_AfxStreams.DrawRuleOfThirds = 0 != atoi(args->ArgV(3));
					return;
				}

				Tier0_Msg(
					"%s ruleOfThirds enabled 0|1\n"
					"Current value: %i\n"
					, arg0
					, g_AfxStreams.DrawRuleOfThirds ? 1 : 0
				);
				return;
			}
		}
	}

	Tier0_Msg(
		"%s phiGrid enabled [...]\n"
		"%s ruleOfThirds enabled [...]\n"
		, arg0
		, arg0
	);
}

extern bool csgo_CNetChan_ProcessMessages_Install(void);

CON_COMMAND(mirv_models, "model tools") {
	if (!g_CCsgoModelsReplace.InstallHooks()) {
		Tier0_Warning("AFXERROR: Missing hooks.\n");
		return;
	}

	int argC = args->ArgC();
	const char* arg0 = args->ArgV(0);

	if (2 <= argC) {
		const char* arg1 = args->ArgV(1);

		if (0 == _stricmp("replace", arg1)) {

			if (3 <= argC) {
				const char* arg2 = args->ArgV(2);

				if (0 == _stricmp("byWcName", arg2)) {

					if (4 <= argC) {
						const char* arg3 = args->ArgV(3);

						if (0 == _stricmp(arg3, "add") && 6 <= argC) {
							g_CCsgoModelsReplace.Add(args->ArgV(4), args->ArgV(5), 7 <= argC ? (0 != atoi(args->ArgV(6))) : true);
							return;
						}
						else if (0 == _stricmp(arg3, "remove") && 5 <= argC) {
							g_CCsgoModelsReplace.Remove(atoi(args->ArgV(4)));
							return;
						}
						else if (0 == _stricmp(arg3, "move") && 6 <= argC) {
							g_CCsgoModelsReplace.MoveIndex(atoi(args->ArgV(4)), atoi(args->ArgV(5)));
							return;
						}
						else if (0 == _stricmp(arg3, "print")) {
							g_CCsgoModelsReplace.Print();
							return;
						}
						else if (0 == _stricmp(arg3, "clear")) {
							g_CCsgoModelsReplace.Clear();
							return;
						}
						else if (0 == _stricmp(arg3, "debug")) {
							if (5 <= argC) {
								g_CCsgoModelsReplace.SetDebug(atoi(args->ArgV(4)));
								return;
							}

							Tier0_Msg(
								"%s replace byWcName debug 0|1\n"
								"Current value: %i\n",
								arg0, g_CCsgoModelsReplace.GetDebug() ? 1 : 0
							);
							return;
						}
					}

					Tier0_Msg(
						"%s replace byWcName add <sWildCardNameSource> <sNameTarget> [<iTransparent=0|1>] - Add a replacement, <iTransparent> defaults to 1 if omitted and tries to hide the replacement a bit more from the engine.\n"
						"%s replace byWcName remove <iIndex>\n"
						"%s replace byWcName move <iSourceIndex> <iTargetIndex>\n"
						"%s replace byWcName print\n"
						"%s replace byWcName clear\n"
						"%s replace byWcName debug [...]\n"
						"Wildcard strings: \\* = wildcard and \\\\ = \\\n"
						, arg0
						, arg0
						, arg0
						, arg0
						, arg0
						, arg0
					);

					return;
				}
			}

			Tier0_Msg(
				"%s replace byWcName [...] - Replace by wildcard string name.\n"
				, arg0
			);

			return;
		}
	}

	Tier0_Msg(
		"%s replace [...] - Replaces models (be aware of model caching, might need restart of game is already loaded!)\n"
		, arg0
	);
}

extern SOURCESDK::C_BaseEntity_csgo * GetSpectatorPlayerForKey(int key);

CON_COMMAND(mirv_spec_player_key, "Spectate player by spectatorslot index") {

	int argC = args->ArgC();
	const char* arg0 = args->ArgV(0);

	if (2 <= argC) {
		if(SOURCESDK::C_BaseEntity_csgo * be = GetSpectatorPlayerForKey(atoi(args->ArgV(1)))) {
			std::string str("spec_player ");
			str.append(std::to_string(be->entindex()));
			g_VEngineClient->ExecuteClientCmd(str.c_str());
		}
		return;
	}
	Tier0_Msg(
		"%s <iKeyNumber> - Spectate player for given spectator slot key number.\n"
		, arg0
	);	
}

#endif //#ifndef _WIN64
