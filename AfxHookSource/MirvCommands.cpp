#include "stdafx.h"

// Hint: for now commands are registered upon the first client.dll CreateInterface() call

#include <shared/StringTools.h>

#include <SourceInterfaces.h>

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
#include "Gui.h"
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

	virtual float GetDrawKeyframeIndex()
	{
		return g_CampathDrawer.GetDrawKeyframeIndex();
	}
	virtual void SetDrawKeyframeIndex(float value)
	{
		g_CampathDrawer.SetDrawKeyframeIndex(value);
	}

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

enum class CampathSeekMode
{
	Play,
	Pause,
	Timescale
};
static CampathSeekMode g_CampathSeekMode = CampathSeekMode::Timescale;
static double g_CampathSeekScale = 0;
static bool g_CampathPreviewHud = true;
struct CampathPendingSeek
{
	int Tick = -1, Initial = 0, Frame = 0, Total = 0;
	DWORD Started = 0;
	CampathSeekMode Mode = CampathSeekMode::Timescale;
	double Scale = 0, PreviousScale = 1;
	bool PreviousPaused = false;
} g_CampathPendingSeek;

static void CampathPreview_SeekTick(int tick)
{
	auto * demo = g_VEngineClient ? g_VEngineClient->GetDemoInfoEx() : nullptr;
	if (!demo || !g_VEngineClient->IsPlayingDemo() || tick < 0 || tick > demo->GetDemoPlaybackTotalTicks())
	{
		Tier0_Warning("Campath preview: invalid demo seek target.\n");
		return;
	}
	if (g_CampathPendingSeek.Tick < 0 || g_CampathPendingSeek.Total != demo->GetDemoPlaybackTotalTicks())
	{
		g_CampathPendingSeek.PreviousScale = demo->GetDemoPlaybackTimeScale();
		g_CampathPendingSeek.PreviousPaused = g_VEngineClient->IsPaused();
	}
	g_CampathPendingSeek.Tick = tick;
	g_CampathPendingSeek.Initial = demo->GetDemoPlaybackTick();
	g_CampathPendingSeek.Total = demo->GetDemoPlaybackTotalTicks();
	g_CampathPendingSeek.Frame = g_MirvTime.GetFrameCount();
	g_CampathPendingSeek.Started = GetTickCount();
	g_CampathPendingSeek.Mode = g_CampathSeekMode;
	g_CampathPendingSeek.Scale = g_CampathSeekScale;
	// Let Source perform the seek at normal speed and pause exactly at its
	// destination. Apply the requested state only after that seek has arrived.
	std::string command = "demo_timescale 1; demo_resume; demo_gototick " + std::to_string(tick) + " 0 1";
	g_VEngineClient->ExecuteClientCmd(command.c_str());
}

static void CampathPreview_UpdateSeek()
{
	auto & seek = g_CampathPendingSeek;
	if (seek.Tick < 0)
		return;
	auto * demo = g_VEngineClient ? g_VEngineClient->GetDemoInfoEx() : nullptr;
	if (!demo || !g_VEngineClient->IsPlayingDemo() || demo->GetDemoPlaybackTotalTicks() != seek.Total)
	{
		seek.Tick = -1;
		return;
	}
	int current = demo->GetDemoPlaybackTick();
	bool arrived = g_MirvTime.GetFrameCount() != seek.Frame && current >= seek.Tick &&
				   (seek.Tick >= seek.Initial || current < seek.Initial) && g_VEngineClient->IsPaused();
	bool timeout = DWORD(GetTickCount() - seek.Started) > 60000;
	if (!arrived && !timeout)
		return;
	std::ostringstream command;
	command.imbue(std::locale::classic());
	if (timeout && !arrived)
	{
		command << "demo_timescale " << seek.PreviousScale << (seek.PreviousPaused ? "; demo_pause" : "; demo_resume");
		Tier0_Warning("Campath preview: seek did not reach its target; restoring playback state.\n");
	}
	else if (seek.Mode == CampathSeekMode::Pause)
		command << "demo_timescale 1; demo_pause";
	else
		command << "demo_timescale " << (seek.Mode == CampathSeekMode::Play ? 1 : seek.Scale) << "; demo_resume";
	seek.Tick = -1;
	g_VEngineClient->ExecuteClientCmd(command.str().c_str());
}

static void CampathPreview_SetTravel(bool forward, bool backward)
{
	if (forward != g_CampathPreviewForward || backward != g_CampathPreviewBackward)
		if (auto * input = g_Hook_VClient_RenderView.m_MirvInput)
			input->ClearMovementInput();
	g_CampathPreviewForward = forward;
	g_CampathPreviewBackward = backward;
}
static void CampathPreview_StopTravel()
{
	CampathPreview_SetTravel(false, false);
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

	if (!_strnicmp(name, "vk", 2))
	{
		char * end = nullptr;
		long value = strtol(name + 2, &end, 10);
		if (end != name + 2 && !*end && value > 0 && value < 256)
			return (UINT)value;
	}
	if (!_stricmp(name, "shift"))
		return VK_SHIFT;
	if (!_stricmp(name, "lshift"))
		return VK_LSHIFT;
	if (!_stricmp(name, "rshift"))
		return VK_RSHIFT;
	if (!_stricmp(name, "rctrl") || !_stricmp(name, "rightctrl"))
		return VK_RCONTROL;
	if (!_stricmp(name, "lctrl") || !_stricmp(name, "leftctrl"))
		return VK_LCONTROL;
	if (!_stricmp(name, "enter"))
		return VK_RETURN;
	if (!_stricmp(name, "delete"))
		return VK_DELETE;
	if (!_stricmp(name, "tab"))
		return VK_TAB;
	if (!_stricmp(name, "escape"))
		return VK_ESCAPE;
	if (!_stricmp(name, "console") || !_stricmp(name, "grave"))
		return VK_OEM_3;
	if (!_stricmp(name, "["))
		return VK_OEM_4;
	if (!_stricmp(name, "]"))
		return VK_OEM_6;
	if (!_stricmp(name, "left"))
		return VK_LEFT;
	if (!_stricmp(name, "right"))
		return VK_RIGHT;
	if (!_stricmp(name, "up"))
		return VK_UP;
	if (!_stricmp(name, "down"))
		return VK_DOWN;
	if (0 == _stricmp(name, "insert"))
		return VK_INSERT;
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
	CamPath & path = g_Hook_VClient_RenderView.GetActiveCamPath();
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
	CamPath & path = g_Hook_VClient_RenderView.GetActiveCamPath();
	if (!g_Hook_VClient_RenderView.IsInstalled() || !g_Hook_VClient_RenderView.m_MirvInput)
	{
		Tier0_Warning("Campath preview: no usable keyframes or free camera is unavailable.\n");
		return false;
	}

	if (!g_CampathPreviewActive)
	{
		double t = g_MirvTime.GetTime() - path.GetOffset();
		g_CampathPreviewTime = path.GetSize() ? std::clamp(t, path.GetLowerBound(), path.GetUpperBound()) : t;
	}

	g_CampathPreviewActive = true;
	MirvCampathEditor_OnPreview(true);
	g_CampathPreviewFrame = -1;
	g_CampathPreviewLastMs = GetTickCount();
	CampathPreview_StopTravel();
	CampathPreview_SetCamera();
	Tier0_Msg("Campath preview active.\n");
	return true;
}

void MirvCampathPreview_Stop()
{
	MirvCampathEditor_OnPreview(false);
	g_CampathPreviewActive = false;
	CampathPreview_StopTravel();
	g_CampathPreviewLastMs = 0;
}

static void CampathPreview_Exit()
{
	MirvCampathEditor_OnPreview(false);
	g_CampathPreviewActive = false;
	CampathPreview_StopTravel();
	g_CampathPreviewLastMs = 0;
	Tier0_Msg("Campath preview exited; free camera retained.\n");
}

// Called before the mirv_input view override is evaluated.
void MirvCampathPreview_Update()
{
	CampathPreview_UpdateSeek();

	if (!g_CampathPreviewActive)
		return;

	// Do not re-enable free camera after it was explicitly exited.
	if (!g_Hook_VClient_RenderView.m_MirvInput ||
		!g_Hook_VClient_RenderView.m_MirvInput->GetCameraControlMode())
	{
		MirvCampathPreview_Stop();
		CampathPreview_StopTravel();
		g_CampathPreviewLastMs = 0;
		return;
	}

	// Ignore held navigation keys while the console or menu owns input.
	if (g_Hook_VClient_RenderView.IsMenuActive())
	{
		MirvCampathEditor_ReleaseKeys(true);
		g_CampathPreviewLastMs = GetTickCount();
		return;
	}

	CamPath & path = g_Hook_VClient_RenderView.GetActiveCamPath();
	if (!CampathPreview_CanShow(path))
	{
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

	if (g_CampathPreviewForward || g_CampathPreviewBackward)
	{
		// Keep keyboard and mouse deltas out of the free-camera override which
		// follows this update. Preview retains its input ownership and pose.
		g_Hook_VClient_RenderView.m_MirvInput->ClearMovementInput();
		CampathPreview_SetCamera();
	}
}

static bool CampathPreview_GotoIndex(int index)
{
	CamPath & path = g_Hook_VClient_RenderView.GetActiveCamPath();
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

void MirvCampathPreview_ReleaseKeys()
{
	CampathPreview_StopTravel();
	g_CampathPreviewToggleHeld = false;
}

static void CampathPreview_PrintHelp()
{
	Tier0_Msg(
		"mirv_campath preview allowSource 0|1 - Forward unused keys to Source.\nmirv_campath graph - Easing editor (F7 "
		"in preview).\nmirv_campath preview undo|redo - Undo/redo a data edit.\nmirv_campath preview bind - Keyboard "
		"actions.\nE captures pose/time; Shift+E replaces; [ and ] capture handles; Ctrl+Z / Ctrl+Shift+Z "
		"undo/redo.\nY captures hold "
		"release; Shift+Y clears it; PageUp/Down selects paths.\n");
	Tier0_Msg("mirv_campath preview enter - Begin preview mode and keyboard editing.\n"
			  "mirv_campath preview toggle - Toggle Preview Mode on/off.\n"
			  "mirv_campath preview toggleKey <key|none> - Set a dedicated key (letters, digits, F1..F12, insert, "
			  "home, end, pageup, pagedown).\n"
			  "mirv_campath preview <index> - Select a keyframe (0-based).\n"
			  "mirv_campath preview interp <0..1> - Position between nearest keyframe and the following one.\n"
			  "mirv_campath preview seek - Seek using the configured playback behavior (RCtrl).\n"
			  "mirv_campath preview seekMode play|pause|timescale [value] - Default: timescale 0.\n"
			  "mirv_campath preview hud 0|1 - Preview indicator. Ctrl+A selects all; Ctrl+Shift+A anchors.\n"
			  "mirv_campath preview exit - Leave preview, retaining the free camera pose.\n"
			  "Preview keys (console closed): UP/DOWN select previous/next; HOME/END select first/last.\n"
			  "LEFT/RIGHT scrub interpolated paths; use UP/DOWN to visit individual keyframes.\n"
			  "HOME/END jump without seeking the demo (unless assigned as toggleKey); use preview seek to seek.\n");
}

// Curve editing extends the classic timestamped campath command and input flow.
namespace
{
using Afx::Math::Vector3;
using Vec = Vector3;
bool ownsPreviewInput = false, previousUiCursor = false;
bool uiPassThrough[256] = {}, uiPassThroughInitialized = false;
bool swallowed[256] = {};
void need(bool value, const char * message)
{
	if (!value)
		throw std::runtime_error(message);
}
CamPath & activePath()
{
	return g_Hook_VClient_RenderView.GetActiveCamPath();
}
void initPassThrough()
{
	if (uiPassThroughInitialized)
		return;
	uiPassThroughInitialized = true;
	uiPassThrough[VK_ESCAPE] = uiPassThrough[VK_OEM_3] = uiPassThrough[VK_F2] = true;
	// Source needs the modifier press as well as F2 for its native demo UI.
	// Preview chords still read Shift from GetKeyState and own their main key.
	uiPassThrough[VK_SHIFT] = uiPassThrough[VK_LSHIFT] = uiPassThrough[VK_RSHIFT] = true;
}
bool allowSourceCommands = false;
bool previewUsesKey(unsigned key);
bool passThroughKey(unsigned key, void *)
{
	initPassThrough();
	if (key == VK_MENU || ((GetKeyState(VK_MENU) & 0x8000) && (key == VK_TAB || key == VK_F4)) ||
		(key < 256 && uiPassThrough[key]))
		return true;
	if (!allowSourceCommands)
		return false;
	auto * input = g_Hook_VClient_RenderView.m_MirvInput;
	return !(input && input->IsCameraKey(key)) && !previewUsesKey(key);
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
		need(i >= 0 && i < count(), "Missing argument.");
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
		need(count() == n, "Invalid argument count.");
	}
};

enum EditorAction
{
	EA_Add,
	EA_Replace,
	EA_Delete,
	EA_Visit,
	EA_Previous,
	EA_Next,
	EA_First,
	EA_Last,
	EA_Left,
	EA_Right,
	EA_Seek,
	EA_In,
	EA_Out,
	EA_Reset,
	EA_Curve,
	EA_Draw,
	EA_Enabled,
	EA_SelectAll,
	EA_AnchorPath,
	EA_Graph,
	EA_Undo,
	EA_Redo,
	EA_HoldCapture,
	EA_HoldClear,
	EA_PrevPath,
	EA_NextPath,
	EA_NewPath,
	EA_Count
};
struct EditorBinding
{
	const char * Name;
	UINT Key;
	unsigned Modifiers;
};
const EditorBinding defaultBindings[EA_Count] = {
	{"add", 'E', 0},
	{"replace", 'E', 1},
	{"delete", VK_DELETE, 0},
	{"visit", VK_TAB, 0},
	{"previousPoint", VK_UP, 0},
	{"nextPoint", VK_DOWN, 0},
	{"firstPoint", VK_HOME, 0},
	{"lastPoint", VK_END, 0},
	{"scrubLeft", VK_LEFT, 0},
	{"scrubRight", VK_RIGHT, 0},
	{"seek", VK_RCONTROL, 0},
	{"handleIn", VK_OEM_4, 0},
	{"handleOut", VK_OEM_6, 0},
	{"resetHandles", 'B', 3},
	{"curveType", 'B', 2},
	{"draw", VK_F4, 0},
	{"enabled", 'P', 2},
	{"selectAll", 'A', 2},
	{"anchorPath", 'A', 3},
	{"graph", VK_F7, 0},
	{"undo", 'Z', 2},
	{"redo", 'Z', 3},
	{"holdCapture", 'Y', 0},
	{"holdClear", 'Y', 1},
	{"previousPath", VK_PRIOR, 0},
	{"nextPath", VK_NEXT, 0},
	{"newPath", 'N', 2},
};
struct EditorCommandBinding
{
	EditorBinding Binding;
	std::string Command;
};
std::vector<EditorCommandBinding> editorCommands;
EditorBinding editorBindings[EA_Count];
bool bindingsInitialized = false, actionHeld[EA_Count] = {};
int pressedAction[256] = {};
std::string keyName(UINT key)
{
	if (!key)
		return "none";
	if (key >= VK_F1 && key <= VK_F12)
		return "F" + std::to_string(key - VK_F1 + 1);
	if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
		return std::string(1, (char)key);
	switch (key)
	{
	case VK_SHIFT:
		return "shift";
	case VK_LSHIFT:
		return "lshift";
	case VK_RSHIFT:
		return "rshift";
	case VK_RCONTROL:
		return "rctrl";
	case VK_LCONTROL:
		return "lctrl";
	case VK_RETURN:
		return "enter";
	case VK_INSERT:
		return "insert";
	case VK_DELETE:
		return "delete";
	case VK_TAB:
		return "tab";
	case VK_HOME:
		return "home";
	case VK_END:
		return "end";
	case VK_PRIOR:
		return "pageup";
	case VK_NEXT:
		return "pagedown";
	case VK_LEFT:
		return "left";
	case VK_RIGHT:
		return "right";
	case VK_UP:
		return "up";
	case VK_DOWN:
		return "down";
	case VK_OEM_4:
		return "[";
	case VK_OEM_6:
		return "]";
	case VK_OEM_3:
		return "console";
	case VK_ESCAPE:
		return "escape";
	}
	return "vk" + std::to_string(key);
}
void initBindings()
{
	if (bindingsInitialized)
		return;
	std::copy(std::begin(defaultBindings), std::end(defaultBindings), editorBindings);
	std::fill(std::begin(pressedAction), std::end(pressedAction), -1);
	bindingsInitialized = true;
}
std::string bindingName(const EditorBinding & binding)
{
	if (!binding.Key)
		return "none";
	return std::string(binding.Modifiers & 2 ? "ctrl+" : "") + (binding.Modifiers & 1 ? "shift+" : "") +
		   (binding.Modifiers & 4 ? "alt+" : "") + keyName(binding.Key);
}
EditorBinding parseBinding(std::string text, EditorBinding binding)
{
	binding.Modifiers = 0;
	while (true)
	{
		auto split = text.find('+');
		if (split == std::string::npos)
			break;
		std::string modifier = text.substr(0, split);
		unsigned flag = !_stricmp(modifier.c_str(), "shift")  ? 1
						: !_stricmp(modifier.c_str(), "ctrl") ? 2
						: !_stricmp(modifier.c_str(), "alt")  ? 4
															  : 0;
		need(flag && !(binding.Modifiers & flag), "Modifiers must be shift, ctrl or alt, without duplicates");
		binding.Modifiers |= flag;
		text.erase(0, split + 1);
	}
	binding.Key = CampathPreview_ParseToggleKey(text.c_str());
	need(binding.Key || !_stricmp(text.c_str(), "none") || !_stricmp(text.c_str(), "off"), "Unknown key name");
	initPassThrough();
	need(!binding.Key || (!uiPassThrough[binding.Key] && binding.Key != VK_SHIFT && binding.Key != VK_CONTROL &&
						  binding.Key != VK_MENU),
		 "This key belongs to menus/UI or is a modifier");
	need(!(binding.Modifiers & 4) || (binding.Key != VK_TAB && binding.Key != VK_F4),
		 "Alt+Tab and Alt+F4 belong to the operating system");
	return binding;
}
bool previewUsesKey(unsigned key)
{
	initBindings();
	unsigned modifiers = (GetKeyState(VK_SHIFT) & 0x8000 ? 1 : 0) | (GetKeyState(VK_CONTROL) & 0x8000 ? 2 : 0) |
						 (GetKeyState(VK_MENU) & 0x8000 ? 4 : 0);
	if (key == g_CampathPreviewToggleKey || key == VK_SHIFT || key == VK_CONTROL || key == VK_LCONTROL ||
		key == VK_RCONTROL)
		return true;
	for (auto & binding : editorBindings)
		if (binding.Key == key && binding.Modifiers == modifiers)
			return true;
	for (auto & custom : editorCommands)
		if (custom.Binding.Key == key && custom.Binding.Modifiers == modifiers)
			return true;
	return false;
}

bool editSettings(const Args & a)
{
	if (!a.is(0, "preview"))
		return false;
	if (a.is(1, "hud"))
	{
		if (a.count() == 2)
		{
			Tier0_Msg("mirv_campath preview hud 0|1 - Compact preview indicator.\nCurrent value: %i\n",
					  g_CampathPreviewHud);
			return true;
		}
		a.size(3);
		g_CampathPreviewHud = a.flag(2);
		return true;
	}
	if (a.is(1, "seekMode"))
	{
		if (a.count() == 2)
		{
			const char * mode = g_CampathSeekMode == CampathSeekMode::Play	  ? "play"
								: g_CampathSeekMode == CampathSeekMode::Pause ? "pause"
																			  : "timescale";
			Tier0_Msg("mirv_campath preview seekMode play|pause|timescale [value]\nCurrent value: %s", mode);
			if (g_CampathSeekMode == CampathSeekMode::Timescale)
				Tier0_Msg(" %g", g_CampathSeekScale);
			Tier0_Msg("\n");
			return true;
		}
		need(a.count() == 3 || a.count() == 4, "Use preview seekMode play|pause|timescale [value]");
		if (a.is(2, "timescale"))
		{
			double value = a.count() == 4 ? a.num(3) : 0;
			need(value >= 0 && value <= 1000, "Timescale must be in 0..1000");
			g_CampathSeekScale = value;
			g_CampathSeekMode = CampathSeekMode::Timescale;
		}
		else
		{
			a.size(3);
			need(a.is(2, "play") || a.is(2, "pause"), "Choose play, pause or timescale");
			g_CampathSeekMode = a.is(2, "play") ? CampathSeekMode::Play : CampathSeekMode::Pause;
		}
		return true;
	}
	if (a.is(1, "allowSource"))
	{
		if (a.count() == 2)
		{
			Tier0_Msg(
				"mirv_campath preview allowSource 0|1 - Forward keys unused by preview/freecam.\nCurrent value: %i\n",
				allowSourceCommands);
			return true;
		}
		a.size(3);
		allowSourceCommands = a.flag(2);
		return true;
	}

	if (a.is(1, "command"))
	{
		initBindings();
		if (a.count() == 2)
		{
			Tier0_Msg("mirv_campath preview command <chord> <quoted console command|none>\n");
			for (const auto & binding : editorCommands)
				Tier0_Msg("  %s: %s\n", bindingName(binding.Binding).c_str(), binding.Command.c_str());
			return true;
		}
		need(a.count() == 4, "Use preview command <chord> <quoted console command|none>");
		auto binding = parseBinding(a.at(2), {"command", 0, 0});
		need(binding.Key != 0, "Choose a key");
		auto it = std::find_if(editorCommands.begin(), editorCommands.end(), [&](const EditorCommandBinding & v) {
			return v.Binding.Key == binding.Key && v.Binding.Modifiers == binding.Modifiers;
		});
		if (a.is(3, "none"))
		{
			if (it != editorCommands.end())
				editorCommands.erase(it);
			return true;
		}
		for (const auto & existing : editorBindings)
			need(existing.Key != binding.Key || existing.Modifiers != binding.Modifiers,
				 "Unbind the built-in action on this chord first");
		std::string command = a.at(3);
		need(!command.empty() && command.size() <= 4096, "Command must contain 1..4096 characters");
		if (it == editorCommands.end())
		{
			need(editorCommands.size() < 64, "Maximum 64 custom editor commands");
			editorCommands.push_back({binding, command});
		}
		else
			it->Command = command;
		MirvCampathEditor_ReleaseKeys();
		return true;
	}

	if (a.is(1, "bind"))
	{
		initBindings();
		if (a.count() == 3 && a.is(2, "reset"))
		{
			initPassThrough();
			for (const auto & custom : editorCommands)
				for (const auto & binding : defaultBindings)
					need(custom.Binding.Key != binding.Key || custom.Binding.Modifiers != binding.Modifiers,
						 "Remove custom commands conflicting with default bindings first");
			for (const auto & binding : defaultBindings)
				need(!uiPassThrough[binding.Key],
					 "Disable passthrough for default editor keys before resetting bindings");
			std::copy(std::begin(defaultBindings), std::end(defaultBindings), editorBindings);
			MirvCampathEditor_ReleaseKeys();
			return true;
		}
		if (a.count() == 2)
		{
			for (const auto & binding : editorBindings)
				Tier0_Msg("mirv_campath preview bind %s <key|none> - Current: %s\n", binding.Name,
						  bindingName(binding).c_str());
			return true;
		}
		need(a.count() == 3 || a.count() == 4, "Use preview bind [action] [key|none]");
		int selected = -1;
		for (int i = 0; i < EA_Count; ++i)
			if (a.is(2, editorBindings[i].Name))
				selected = i;
		need(selected >= 0, "Unknown preview action; use preview bind to list actions");
		if (a.count() == 3)
		{
			Tier0_Msg("mirv_campath preview bind %s <key|none>\nCurrent value: %s\n", editorBindings[selected].Name,
					  bindingName(editorBindings[selected]).c_str());
			return true;
		}
		auto binding = parseBinding(a.at(3), editorBindings[selected]);
		for (int i = 0; i < EA_Count; ++i)
			need(i == selected || !binding.Key || binding.Key != editorBindings[i].Key ||
					 binding.Modifiers != editorBindings[i].Modifiers,
				 "Another editor action already uses this shortcut");
		for (const auto & custom : editorCommands)
			need(!binding.Key || custom.Binding.Key != binding.Key || custom.Binding.Modifiers != binding.Modifiers,
				 "Remove the custom command on this chord first");
		editorBindings[selected] = binding;
		MirvCampathEditor_ReleaseKeys();
		return true;
	}
	if (a.is(1, "passthrough"))
	{
		initPassThrough();
		if (a.count() == 2)
		{
			Tier0_Msg("mirv_campath preview passthrough <key> 0|1 - Keys allowed to reach Source.\n");
			for (UINT i = 1; i < 256; ++i)
				if (uiPassThrough[i])
					Tier0_Msg("  %s\n", keyName(i).c_str());
			return true;
		}
		UINT key = CampathPreview_ParseToggleKey(a.at(2).c_str());
		need(key > 0, "Unknown key name");
		if (a.count() == 3)
		{
			Tier0_Msg("Current value for %s: %i\n", keyName(key).c_str(), uiPassThrough[key]);
			return true;
		}
		a.size(4);
		bool enabled = a.flag(3);
		need(key != VK_ESCAPE || enabled, "Escape remains available for menus");
		initBindings();
		if (enabled)
			for (const auto & binding : editorBindings)
				need(binding.Key != key, "Unbind the editor action using this key first");
		if (enabled)
			for (const auto & custom : editorCommands)
				need(custom.Binding.Key != key, "Remove custom commands using this key first");
		uiPassThrough[key] = enabled;
		MirvCampathEditor_ReleaseKeys();
		return true;
	}
	return false;
}

CamPathIterator selectedKey()
{
	auto & path = activePath();
	auto selected = path.GetEnd();
	for (auto it = path.GetBegin(); it != path.GetEnd(); ++it)
		if (it.GetValue().Selected)
		{
			need(selected == path.GetEnd(), "Select exactly one keyframe");
			selected = it;
		}
	need(selected != path.GetEnd(), "Select a keyframe first (Up/Down in preview or select #<index> #<index>)");
	return selected;
}
void seekCursor()
{
	need(activePath().GetSize() > 0, "No camera points");
	need(g_VEngineClient && g_VEngineClient->IsPlayingDemo() && g_VEngineClient->GetDemoInfoEx() &&
			 g_Hook_VClient_RenderView.GetGlobals(),
		 "A running demo is required for seeking");
	g_CampathPreviewTime = std::clamp(g_CampathPreviewTime, activePath().GetLowerBound(), activePath().GetUpperBound());
	int tick = 0;
	need(GetDemoTickFromClientTime(g_MirvTime.GetTime(), g_CampathPreviewTime + activePath().GetOffset(), tick),
		 "Unable to calculate the demo tick");
	need(tick >= 0, "Seek precedes demo start");
	CampathPreview_SeekTick(tick);
}
void captureHandle(bool outgoing)
{
	auto key = selectedKey();
	auto pose = g_MirvCampath_Camera.GetCamera();
	auto value = key.GetValue();
	need(activePath().SetHandle(key.GetTime(), outgoing, Vector3(pose.X - value.X, pose.Y - value.Y, pose.Z - value.Z)),
		 "Invalid curve handle");
}
void curveHelp()
{
	Tier0_Msg(
		"mirv_campath curve type default|linear|smooth|monotone|step|cubic|bezier - Choose position interpolation.\n"
		"mirv_campath curve handle in|out capture - Capture the selected keyframe handle at the camera.\n"
		"mirv_campath curve handle in|out <x> <y> <z> - Set a world-space offset.\n"
		"mirv_campath curve handle mode aligned|free - Link selected handles (new keys: aligned).\n"
		"mirv_campath curve reset - Reset selected keyframe handles to native interpolation.\n");
	Tier0_Msg("Current curve: %s\n", activePath().Bezier_get()
										 ? "bezier"
										 : CamPath::DoubleInterp_ToString(activePath().PositionInterpMethod_get()));
}
void nativeCommand(std::initializer_list<const char *> values)
{
	CFakeWrpCommandArgs args("mirv_campath");
	for (auto value : values)
		args.AddArg(value);
	MirvCampath_ConCommand(&args, Tier0_Msg, Tier0_Warning, &activePath(), &g_MirvCampath_Time, &g_MirvCampath_Camera,
						   &g_MirvCampath_Drawer);
}
void executeEditorAction(EditorAction action)
{
	auto & path = activePath();
	switch (action)
	{
	case EA_Undo:
	case EA_Redo: {
		auto & paths = g_Hook_VClient_RenderView.m_CamPaths;
		bool ok = action == EA_Undo ? paths.Undo() : paths.Redo();
		if (!ok)
			Tier0_Msg("Nothing to %s.\n", action == EA_Undo ? "undo" : "redo");
		if (activePath().GetSize())
			g_CampathPreviewTime =
				std::clamp(g_CampathPreviewTime, activePath().GetLowerBound(), activePath().GetUpperBound());
		MirvCampathEditor_ReleaseKeys();
		break;
	}
	case EA_HoldCapture:
	case EA_HoldClear: {
		CFakeWrpCommandArgs args("mirv_campath");
		args.AddArg("edit");
		args.AddArg("hold");
		args.AddArg(action == EA_HoldCapture ? "capture" : "off");
		MirvCampathEditor_Command(&args);
		break;
	}
	case EA_NewPath:
		g_Hook_VClient_RenderView.m_CamPaths.New();
		break;
	case EA_PrevPath:
	case EA_NextPath: {
		auto & paths = g_Hook_VClient_RenderView.m_CamPaths;
		std::size_t next = (paths.ActiveIndex() + (action == EA_NextPath ? 1 : paths.Size() - 1)) % paths.Size();
		paths.Use(next);
		if (activePath().GetSize())
			CampathPreview_GotoIndex(0);
		break;
	}

	case EA_Add: {
		nativeCommand({"add"});
		int i = 0;
		double time = g_MirvTime.GetTime() - path.GetOffset();
		for (auto it = path.GetBegin(); it != path.GetEnd(); ++it, ++i)
			if (it.GetTime() == time)
			{
				CampathPreview_GotoIndex(i);
				break;
			}
		break;
	}
	case EA_Replace: {
		auto key = selectedKey();
		auto previous = key.GetValue();
		auto pose = g_MirvCampath_Camera.GetCamera();
		CamPathValue value(pose.X, pose.Y, pose.Z, pose.Pitch, pose.Yaw, pose.Roll, pose.Fov);
		value.Selected = previous.Selected;
		value.InHandle = previous.InHandle;
		value.OutHandle = previous.OutHandle;
		value.HasInHandle = previous.HasInHandle;
		value.HasOutHandle = previous.HasOutHandle;
		value.HandlesAligned = previous.HandlesAligned;
		value.HoldDuration = previous.HoldDuration;
		value.Ease = previous.Ease;
		path.Add(key.GetTime(), value);
		break;
	}
	case EA_Delete: {
		auto key = selectedKey();
		path.Remove(key.GetTime());
		break;
	}
	case EA_Visit:
		need(CampathPreview_SetCamera(), "No usable camera points");
		break;
	case EA_Previous:
	case EA_Next: {
		need(path.GetSize() > 0, "No camera points");
		int i = 0, chosen = -1;
		for (auto it = path.GetBegin(); it != path.GetEnd(); ++it, ++i)
		{
			if (action == EA_Next && it.GetTime() > g_CampathPreviewTime + 1e-5)
			{
				chosen = i;
				break;
			}
			if (action == EA_Previous && it.GetTime() < g_CampathPreviewTime - 1e-5)
				chosen = i;
		}
		if (chosen >= 0)
			CampathPreview_GotoIndex(chosen);
		break;
	}
	case EA_First:
	case EA_Last:
		need(path.GetSize() > 0, "No camera points");
		CampathPreview_GotoIndex(action == EA_First ? 0 : int(path.GetSize() - 1));
		break;
	case EA_Seek:
		seekCursor();
		break;
	case EA_In:
	case EA_Out:
		captureHandle(action == EA_Out);
		break;
	case EA_Reset: {
		auto key = selectedKey();
		path.ResetHandles(key.GetTime());
		break;
	}
	case EA_Curve: {
		const char * mode = "bezier";
		if (path.Bezier_get())
			mode = "linear";
		else if (path.PositionInterpMethod_get() == CamPath::DI_LINEAR)
			mode = "smooth";
		else if (path.PositionInterpMethod_get() == CamPath::DI_SMOOTH)
			mode = "monotone";
		else if (path.PositionInterpMethod_get() == CamPath::DI_MONOTONE)
			mode = "step";
		CFakeWrpCommandArgs args("mirv_campath");
		for (auto text : {"edit", "interp", "all", mode})
			args.AddArg(text);
		MirvCampathEditor_Command(&args);
		Tier0_Msg("Campath interpolation: %s.\n", mode);
		break;
	}
	case EA_Draw:
		g_CampathDrawer.Draw_set(!g_CampathDrawer.Draw_get());
		break;
	case EA_Enabled:
		g_Hook_VClient_RenderView.m_CamPaths.Enabled_set(!g_Hook_VClient_RenderView.m_CamPaths.Enabled_get());
		break;
	case EA_Graph:
		need(AfxHookSource::Gui::SetCampathGraphOpen(!AfxHookSource::Gui::GetCampathGraphOpen()),
			 "Graph editor is unavailable in this build");
		break;
	case EA_SelectAll:
		nativeCommand({"select", "all"});
		break;
	case EA_AnchorPath: {
		auto anchor = selectedKey();
		int index = 0;
		std::vector<std::size_t> selected;
		int anchorIndex = 0;
		for (auto it = path.GetBegin(); it != path.GetEnd(); ++it, ++index)
		{
			if (it.GetValue().Selected)
				selected.push_back(index);
			if (it.GetTime() == anchor.GetTime())
				anchorIndex = index;
		}
		std::string name = "#" + std::to_string(anchorIndex);
		nativeCommand({"select", "all"});
		nativeCommand({"edit", "anchor", name.c_str(), "current"});
		path.SelectNone();
		for (auto point : selected)
			path.SelectAdd(point, point);
		break;
	}
	case EA_Left:
	case EA_Right:
		break;
	default:
		break;
	}
}
} // namespace
bool MirvCampathEditor_Command(IWrpCommandArgs * args)
{
	AfxHookSource::Gui::FinishCampathGraphEdit();
	auto & paths = g_Hook_VClient_RenderView.m_CamPaths;
	auto before = paths.Capture();
	try
	{
		Args a{args, 1};
		auto changed = [&]() {
			paths.Record(before);
			return true;
		};
		if (editSettings(a))
			return true;
		if (a.is(0, "system") || a.is(0, "cinematic"))
			throw std::runtime_error("There is one campath system; use the ordinary mirv_campath commands");
		if (a.is(0, "enabled") || a.is(0, "enable"))
		{
			if (a.count() == 1)
			{
				Tier0_Msg("mirv_campath enabled 0|1\nCurrent value: %i\n", paths.Enabled_get());
				return true;
			}
			a.size(2);
			paths.Enabled_set(a.flag(1));
			return true;
		}
		if (a.is(0, "graph"))
		{
			need(a.count() == 1 || a.count() == 2, "Use graph [0|1]");
			need(AfxHookSource::Gui::SetCampathGraphOpen(a.count() == 1 ? !AfxHookSource::Gui::GetCampathGraphOpen()
																		: a.flag(1)),
				 "Graph editor is unavailable in this build");
			return true;
		}
		if (a.is(0, "edit") && a.is(1, "speed"))
		{
			if (a.count() == 2)
			{
				Tier0_Msg("mirv_campath edit speed continuous|native - Travel speed through ordinary keys; keeps curve "
						  "and arrival times.\nCurrent value: %s\n",
						  activePath().SpeedContinuous_get() ? "continuous" : "native");
				return true;
			}
			a.size(3);
			need(a.is(2, "continuous") || a.is(2, "native"), "Choose continuous or native");
			activePath().SpeedContinuous_set(a.is(2, "continuous"));
			return changed();
		}
		if (a.is(0, "edit") && a.is(1, "ease"))
		{
			if (a.count() < 4)
			{
				Tier0_Msg("mirv_campath edit ease all|selected <function> [in|out|inout]\nFunctions: inherit, linear, "
						  "sine, quad, cubic, quart, quint, expo, circ, back, bounce, elastic, bezier.\nBezier: ... "
						  "bezier <x1> <y1> <x2> <y2>\nBack: ... back <direction> [overshoot]\nElastic: ... elastic "
						  "<direction> [amplitude period]\nCurrent path easing: %s %s\n",
						  CamPathEase::ToString(activePath().GetEase().Type),
						  CamPathEase::ToString(activePath().GetEase().Mode));
				return true;
			}
			need(a.is(2, "all") || a.is(2, "selected"), "Choose all or selected outgoing segments");
			CamPathEase ease;
			need(CamPathEase::FromString(a.at(3).c_str(), ease.Type), "Unknown easing function");
			if (ease.Type == CamPathEase::Bezier)
			{
				a.size(8);
				ease.X1 = a.num(4);
				ease.Y1 = a.num(5);
				ease.X2 = a.num(6);
				ease.Y2 = a.num(7);
			}
			else
			{
				if (a.count() >= 5)
					need(CamPathEase::FromString(a.at(4).c_str(), ease.Mode), "Choose in, out or inout");
				if (ease.Type == CamPathEase::Back && a.count() == 6)
					ease.Overshoot = a.num(5);
				else if (ease.Type == CamPathEase::Elastic && a.count() == 7)
				{
					ease.Amplitude = a.num(5);
					ease.Period = a.num(6);
				}
				else
					need(a.count() == 4 || a.count() == 5, "Unexpected easing parameters");
			}
			need(ease.Valid(),
				 "Invalid easing parameters; Bezier time handles must stay ordered in 0..1 and values in -5..5");
			if (a.is(2, "all"))
				activePath().SetEase(ease);
			else
			{
				std::vector<double> times;
				for (auto it = activePath().GetBegin(); it != activePath().GetEnd(); ++it)
					if (it.GetValue().Selected && std::next(it.wrapped) != activePath().GetEnd().wrapped)
						times.push_back(it.GetTime());
				need(!times.empty(), "Select a keyframe with an outgoing segment first");
				for (double time : times)
					activePath().SetKeyEase(time, ease);
			}
			return changed();
		}

		if (a.is(0, "path"))
		{
			auto find = [&](int arg) {
				auto text = a.at(arg);
				for (std::size_t i = 0; i < paths.Size(); ++i)
					if (paths.At(i).GetName() == text)
						return i;
				int i = a.index(arg);
				need(std::size_t(i) < paths.Size(), "Unknown path name/index");
				return std::size_t(i);
			};
			if (a.count() == 1)
			{
				Tier0_Msg("mirv_campath path new [name]\nmirv_campath path list\nmirv_campath path use "
						  "<name|index>\nmirv_campath path rename <name>\nmirv_campath path remove "
						  "[name|index]\nmirv_campath path enabled 0|1\n");
				return true;
			}
			if (a.is(1, "list"))
			{
				a.size(2);
				for (std::size_t i = 0; i < paths.Size(); ++i)
				{
					auto & p = paths.At(i);
					Tier0_Msg("%s%u: %s - %u keys, enabled %i\n", i == paths.ActiveIndex() ? "* " : "  ", unsigned(i),
							  p.GetName().c_str(), unsigned(p.GetSize()), p.Enabled_get());
				}
				return true;
			}
			if (a.is(1, "use"))
			{
				a.size(3);
				paths.Use(find(2));
				if (g_CampathPreviewActive && activePath().GetSize())
					CampathPreview_GotoIndex(0);
				return true;
			}
			if (a.is(1, "new"))
			{
				need(a.count() == 2 || a.count() == 3, "Use path new [name]");
				paths.New(a.count() == 3 ? a.at(2) : "");
				return changed();
			}
			if (a.is(1, "rename"))
			{
				a.size(3);
				paths.Rename(a.at(2));
				return changed();
			}
			if (a.is(1, "remove"))
			{
				need(a.count() == 2 || a.count() == 3, "Use path remove [name|index]");
				paths.Remove(a.count() == 3 ? find(2) : paths.ActiveIndex());
				return changed();
			}
			if (a.is(1, "enabled"))
			{
				if (a.count() == 2)
				{
					Tier0_Msg("mirv_campath path enabled 0|1\nCurrent value: %i\n", activePath().Enabled_get());
					return true;
				}
				a.size(3);
				activePath().Enabled_set(a.flag(2));
				return changed();
			}
			throw std::runtime_error("Unknown path command; use mirv_campath path");
		}
		if (a.is(0, "saveAll") || a.is(0, "load"))
		{
			a.size(2);
			auto file = std::filesystem::u8path(a.at(1)).wstring();
			bool ok = a.is(0, "saveAll") ? paths.SaveAll(file.c_str()) : paths.Load(file.c_str());
			need(ok, "Unable to save/load campath XML");
			return changed();
		}
		if (a.is(0, "preview") && (a.is(1, "undo") || a.is(1, "redo")))
		{
			a.size(2);
			bool ok = a.is(1, "undo") ? paths.Undo() : paths.Redo();
			if (!ok)
				Tier0_Msg("Nothing to %s.\n", a.at(1).c_str());
			MirvCampathEditor_ReleaseKeys();
			if (activePath().GetSize())
				g_CampathPreviewTime =
					std::clamp(g_CampathPreviewTime, activePath().GetLowerBound(), activePath().GetUpperBound());
			return true;
		}
		if (a.is(0, "edit") && (a.is(1, "on") || a.is(1, "off")))
			throw std::runtime_error("Use mirv_campath preview enter or preview exit; there is no second edit mode");
		if (a.is(0, "edit") && a.is(1, "hold"))
		{
			if (a.count() == 2)
			{
				Tier0_Msg("mirv_campath edit hold capture - Capture the release tick.\nmirv_campath edit hold tick "
						  "<tick>\nmirv_campath edit hold off\n");
				return true;
			}
			auto key = selectedKey();
			double release = key.GetTime();
			if (!a.is(2, "off"))
			{
				need(g_VEngineClient && g_VEngineClient->IsPlayingDemo() && g_VEngineClient->GetDemoInfoEx() &&
						 g_Hook_VClient_RenderView.GetGlobals(),
					 "A running demo is required for a release tick");
				int tick = 0;
				if (a.is(2, "capture"))
				{
					a.size(3);
					need(GetCurrentDemoTick(tick), "Demo tick unavailable");
				}
				else
				{
					need(a.is(2, "tick"), "Use capture, tick <tick>, or off");
					a.size(4);
					double number = a.num(3);
					need(number >= 0 && number <= (std::numeric_limits<int>::max)() && number == std::floor(number),
						 "Expected a nonnegative demo tick");
					tick = int(number);
				}
				double demo = 0;
				need(GetDemoTimeFromClientTime(g_MirvTime.GetTime(), g_MirvTime.GetTime(), demo),
					 "Demo time unavailable");
				double interval = g_Hook_VClient_RenderView.GetGlobals()->interval_per_tick_get();
				need(interval > 0, "Demo tick interval unavailable");
				release = g_MirvTime.GetTime() - activePath().GetOffset() + tick * interval - demo;
			}
			else
				a.size(3);
			need(activePath().SetKeyHold(key.GetTime(), release),
				 "Release must be at/after arrival and before the next keyframe");
			return changed();
		}
		if (a.is(0, "edit") && a.is(1, "interp") && a.is(2, "all"))
		{
			if (a.count() == 3)
			{
				Tier0_Msg("mirv_campath edit interp all linear|smooth|monotone|step|cubic|bezier\n");
				return true;
			}
			a.size(4);
			CamPath::DoubleInterp mode;
			bool bezier = a.is(3, "bezier");
			need(bezier || CamPath::DoubleInterp_FromString(a.at(3).c_str(), mode), "Unknown interpolation mode");
			if (bezier)
				mode = CamPath::DI_SMOOTH;
			activePath().PositionInterpMethod_set(mode);
			activePath().Bezier_set(bezier);
			CamPath::QuaternionInterp rotation = CamPath::QI_SMOOTH;
			if (mode == CamPath::DI_STEP)
				rotation = CamPath::QI_STEP;
			else if (mode == CamPath::DI_CUBIC || mode == CamPath::DI_DEFAULT)
				rotation = CamPath::QI_SCUBIC;
			else if (mode == CamPath::DI_LINEAR || bezier)
				rotation = CamPath::QI_SLINEAR;
			activePath().RotationInterpMethod_set(rotation);
			activePath().FovInterpMethod_set(mode == CamPath::DI_SMOOTH ? CamPath::DI_MONOTONE : mode);
			return changed();
		}
		if (a.is(0, "curve"))
		{
			if (a.count() == 1)
			{
				curveHelp();
				return true;
			}
			if (a.is(1, "type"))
			{
				if (a.count() == 2)
				{
					curveHelp();
					return true;
				}
				a.size(3);
				if (a.is(2, "bezier"))
					activePath().Bezier_set(true);
				else
				{
					CamPath::DoubleInterp mode;
					need(CamPath::DoubleInterp_FromString(a.at(2).c_str(), mode),
						 "Choose default, linear, smooth, monotone, step, cubic or bezier");
					activePath().Bezier_set(false);
					activePath().PositionInterpMethod_set(mode);
				}
				return changed();
			}
			if (a.is(1, "reset"))
			{
				a.size(2);
				activePath().ResetHandles(selectedKey().GetTime());
				return changed();
			}
			if (a.is(1, "handle") && a.is(2, "mode"))
			{
				if (a.count() == 3)
				{
					Tier0_Msg("mirv_campath curve handle mode aligned|free - Selected keyframes.\n");
					return true;
				}
				a.size(4);
				need(a.is(3, "aligned") || a.is(3, "free"), "Choose aligned or free");
				bool selected = false;
				for (auto it = activePath().GetBegin(); it != activePath().GetEnd(); ++it)
					if (it.GetValue().Selected)
					{
						need(activePath().SetHandleMode(it.GetTime(), a.is(3, "aligned")), "Invalid curve handles");
						selected = true;
					}
				need(selected, "Select keyframes first");
				return changed();
			}
			if (a.is(1, "handle"))
			{
				need(a.count() == 4 || a.count() == 6, "Use curve handle in|out capture|<x y z>");
				need(a.is(2, "in") || a.is(2, "out"), "Choose in or out");
				if (a.count() == 4)
				{
					need(a.is(3, "capture"), "Choose capture or an offset");
					captureHandle(a.is(2, "out"));
				}
				else
					need(activePath().SetHandle(selectedKey().GetTime(), a.is(2, "out"), a.vec(3)),
						 "Invalid curve handle");
				return changed();
			}
			curveHelp();
			return true;
		}
		if (a.is(0, "draw") && (a.is(1, "stackLabels") || a.is(1, "stackWorldDistance")))
		{
			if (a.count() == 2)
			{
				Tier0_Msg("mirv_campath draw stackLabels 0|1 (current %i)\nmirv_campath draw stackWorldDistance "
						  "<units> (current %g)\n",
						  g_CampathDrawer.GetStackLabels(), g_CampathDrawer.GetStackWorldDistance());
				return true;
			}
			a.size(3);
			if (a.is(1, "stackLabels"))
				g_CampathDrawer.SetStackLabels(a.flag(2));
			else
			{
				double value = a.num(2);
				need(value >= 0 && value <= 1e6, "Stack distance must be 0..1000000");
				g_CampathDrawer.SetStackWorldDistance(float(value));
			}
			return true;
		}

		if (a.is(0, "draw") && a.is(1, "handles"))
		{
			if (a.count() == 2)
			{
				Tier0_Msg("mirv_campath draw handles 0|1\nCurrent value: %i\n", g_CampathDrawer.GetDrawHandles());
				return true;
			}
			a.size(3);
			g_CampathDrawer.SetDrawHandles(a.flag(2));
			return true;
		}
		if (a.is(0, "draw") && a.is(1, "textSize"))
		{
			if (a.count() == 2)
			{
				Tier0_Msg("mirv_campath draw textSize <world height> - Names and hold labels.\nCurrent value: %g\n",
						  g_CampathDrawer.GetLabelHeight());
				return true;
			}
			a.size(3);
			double height = a.num(2);
			need(height >= 0 && height <= 200, "Text height must be 0..200 world units");
			g_CampathDrawer.SetLabelHeight(float(height));
			return true;
		}
		if (!a.count())
			Tier0_Msg("mirv_campath path - Named paths.\nmirv_campath saveAll <file.xml>\nmirv_campath curve - Curve "
					  "handles.\nmirv_campath graph - Toggle the easing editor.\n");
		if (a.is(0, "draw") && a.count() == 1)
			Tier0_Msg("mirv_campath draw textSize <world height> - Path/hold labels.\n"
					  "mirv_campath draw handles 0|1 - Curve handles.\n"
					  "mirv_campath draw stackLabels 0|1 - Stack nearby overlapping indices.\n"
					  "mirv_campath draw stackWorldDistance <units> - Nearby threshold (default 32).\n");
		if (a.is(0, "edit") && a.count() == 1)
			Tier0_Msg("mirv_campath edit speed continuous|native - Travel speed.\nmirv_campath edit hold - Keyframe "
					  "holds.\nmirv_campath edit ease - Segment "
					  "timing/easing.\nmirv_campath edit interp all - Set interpolation in "
					  "one command.\n");
		return false;
	}
	catch (const std::exception & error)
	{
		if (!(before == paths.Capture()))
			paths.Restore(before);
		Tier0_Warning("Campath: %s\n", error.what());
		return true;
	}
}
bool MirvCampathEditor_IsUiPassThrough(unsigned key)
{
	initPassThrough();
	return key < 256 && uiPassThrough[key];
}
bool MirvCampathEditor_ShowHud()
{
	return g_CampathPreviewActive && g_CampathPreviewHud;
}

bool MirvCampathEditor_IsGraphKey(unsigned key)
{
	if (!g_CampathPreviewActive)
		return false;
	initBindings();
	unsigned modifiers = (GetKeyState(VK_SHIFT) & 0x8000 ? 1 : 0) | (GetKeyState(VK_CONTROL) & 0x8000 ? 2 : 0) |
						 (GetKeyState(VK_MENU) & 0x8000 ? 4 : 0);
	return editorBindings[EA_Graph].Key == key && editorBindings[EA_Graph].Modifiers == modifiers;
}

bool MirvCampathEditor_IsEditing()
{
	return g_CampathPreviewActive;
}
void MirvCampathEditor_OnPreview(bool active)
{
	auto & view = g_Hook_VClient_RenderView;
	auto * input = view.m_MirvInput;
	if (active)
	{
		if (!ownsPreviewInput)
			previousUiCursor = view.UiCursorSuspend_get();
		ownsPreviewInput = true;
		view.UiCursorSuspend_set(true);
		if (input)
		{
			input->SetCameraControlMode(true);
			input->SetKeyboardCapture(true, passThroughKey);
		}
		g_CampathDrawer.Draw_set(true);
	}
	else
	{
		if (ownsPreviewInput)
			view.UiCursorSuspend_set(previousUiCursor);
		ownsPreviewInput = false;
		if (input)
			input->SetKeyboardCapture(false);
	}
	MirvCampathEditor_ReleaseKeys();
}
void MirvCampathEditor_ReleaseKeys(bool yieldKeyboard)
{
	initBindings();
	if (yieldKeyboard)
		std::fill(std::begin(swallowed), std::end(swallowed), false);
	std::fill(std::begin(actionHeld), std::end(actionHeld), false);
	std::fill(std::begin(pressedAction), std::end(pressedAction), -1);
	CampathPreview_StopTravel();
	g_CampathPreviewLastMs = GetTickCount();
}
bool MirvCampathEditor_KeyEvent(UINT message, WPARAM key, LPARAM flags)
{
	bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN, up = message == WM_KEYUP || message == WM_SYSKEYUP;
	if ((!down && !up) || key >= 256)
		return false;
	if (key == VK_CONTROL)
		key = (flags & (1LL << 24)) ? VK_RCONTROL : VK_LCONTROL;
	initBindings();
	if (g_Hook_VClient_RenderView.IsMenuActive())
	{
		MirvCampathEditor_ReleaseKeys(true);
		return false;
	}
	if (up && swallowed[key])
	{
		swallowed[key] = false;
		int i = pressedAction[key];
		pressedAction[key] = -1;
		if (i >= 0)
			actionHeld[i] = false;
		CampathPreview_SetTravel(actionHeld[EA_Right], actionHeld[EA_Left]);
		return true;
	}
	if (down && swallowed[key])
		return true;
	if (g_CampathPreviewToggleKey && key == g_CampathPreviewToggleKey)
	{
		if (up)
			g_CampathPreviewToggleHeld = false;
		else if (!g_CampathPreviewToggleHeld && !(flags & (1LL << 30)))
		{
			g_CampathPreviewToggleHeld = true;
			if (g_CampathPreviewActive)
				CampathPreview_Exit();
			else
				CampathPreview_Enter();
		}
		return true;
	}

	if (!MirvCampathEditor_IsEditing() || passThroughKey(unsigned(key), nullptr))
		return false;
	unsigned modifiers = (GetKeyState(VK_SHIFT) & 0x8000 ? 1 : 0) | (GetKeyState(VK_CONTROL) & 0x8000 ? 2 : 0) |
						 (GetKeyState(VK_MENU) & 0x8000 ? 4 : 0);
	if (key == VK_RCONTROL || key == VK_LCONTROL)
		modifiers &= ~2u;
	for (int i = 0; i < EA_Count; ++i)
	{
		auto binding = editorBindings[i];
		if (binding.Key != key || binding.Modifiers != modifiers)
			continue;
		if (!down)
			return false;
		if (flags & (1LL << 30))
			return true;
		swallowed[key] = true;
		pressedAction[key] = i;
		actionHeld[i] = true;
		CampathPreview_SetTravel(actionHeld[EA_Right], actionHeld[EA_Left]);
		auto & paths = g_Hook_VClient_RenderView.m_CamPaths;
		AfxHookSource::Gui::FinishCampathGraphEdit();
		auto before = paths.Capture();
		try
		{
			executeEditorAction(EditorAction(i));
			if (i == EA_Add || i == EA_Replace || i == EA_Delete || i == EA_In || i == EA_Out || i == EA_Reset ||
				i == EA_NewPath || i == EA_AnchorPath)
				paths.Record(before);
		}
		catch (const std::exception & error)
		{
			if (!(before == paths.Capture()))
				paths.Restore(before);
			Tier0_Warning("Campath preview: %s\n", error.what());
		}

		return true;
	}
	for (const auto & binding : editorCommands)
		if (binding.Binding.Key == key && binding.Binding.Modifiers == modifiers)
		{
			if (!down)
				return false;
			if (flags & (1LL << 30))
				return true;
			swallowed[key] = true;
			pressedAction[key] = -1;
			std::string command = binding.Command;
			if (g_VEngineClient)
				g_VEngineClient->ClientCmd_Unrestricted(command.c_str());
			return true;
		}
	return false;
}

CON_COMMAND(mirv_campath, "camera paths")
{
	if (MirvCampathEditor_Command(args))
		return;
	if (!g_Hook_VClient_RenderView.IsInstalled())
	{
		Tier0_Warning("Error: Hook not installed.\n");
		return;
	}

	if (2 <= args->ArgC() && 0 == _stricmp("preview", args->ArgV(1)))
	{
		CamPath & path = g_Hook_VClient_RenderView.GetActiveCamPath();
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
			try
			{
				seekCursor();
			}
			catch (const std::exception & error)
			{
				Tier0_Warning("Campath preview: %s\n", error.what());
			}
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

	auto & paths = g_Hook_VClient_RenderView.m_CamPaths;
	auto before = paths.Capture();
	try
	{
		MirvCampath_ConCommand(args, Tier0_Msg, Tier0_Warning, &paths.Active(), &g_MirvCampath_Time,
							   &g_MirvCampath_Camera, &g_MirvCampath_Drawer);
		if (args->ArgC() == 5 && !_stricmp(args->ArgV(1), "edit") && !_stricmp(args->ArgV(2), "interp") &&
			!_stricmp(args->ArgV(3), "position"))
		{
			CamPath::DoubleInterp mode;
			if (CamPath::DoubleInterp_FromString(args->ArgV(4), mode))
				paths.Active().Bezier_set(false);
		}
		paths.Active().Validate();
		if (args->ArgC() < 2 || _stricmp(args->ArgV(1), "select"))
			paths.Record(before);
	}
	catch (const std::exception & error)
	{
		paths.Restore(before);
		Tier0_Warning("Campath: %s\n", error.what());
	}
	if (args->ArgC() == 1)
		Tier0_Msg("mirv_campath preview - Preview, keyboard controls and undo/redo.\n");
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
