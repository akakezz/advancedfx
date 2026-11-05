#include "stdafx.h"

// TODO:
// - Lots.
// - Maybe handle RawInput mouse buttons in drag mode (so the game doesn't see them, but currently it doesn't care about them anyway.).

#include "Gui.h"

#ifndef AFX_ENABLE_GUI
#define AFX_ENABLE_GUI 0
#endif

#if AFX_ENABLE_GUI

#include <imgui.h>
#include "RenderView.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cfloat>

#include <SourceInterfaces.h>
#include "WrpVEngineClient.h"
#include "addresses.h"
#include "hlaeFolder.h"
#include <list>
#include <string>
#include <vector>
#include <mutex>

extern SourceSdkVer g_SourceSdkVer;
extern WrpVEngineClient * g_VEngineClient;

namespace AfxHookSource {
namespace Gui {

bool m_Active = true;
bool m_KeyDownEaten[512] = {};
POINT m_GameCursorPos = { 0, 0 };
HCURSOR m_OwnCursor = 0;
HCURSOR m_GameCursor = 0;
std::string m_IniFilename;
std::string m_LogFileName;
bool m_FirstEndScene = true;

// Data
static HWND                     g_hWnd = 0;
static INT64                    g_Time = 0;
static INT64                    g_TicksPerSecond = 0;
static LPDIRECT3DDEVICE9        g_pd3dDevice = NULL;
static LPDIRECT3DTEXTURE9       g_FontTexture = NULL;

static class Cursors_s
{
private:
	HCURSOR m_None = 0;
	HCURSOR m_Arrow = LoadCursor(NULL, IDC_ARROW);
	HCURSOR m_TextInput = LoadCursor(NULL, IDC_IBEAM);
	HCURSOR m_ResizeAll = LoadCursor(NULL, IDC_SIZEALL);
	HCURSOR m_ResizeNS = LoadCursor(NULL, IDC_SIZENS);
	HCURSOR m_ResizeEW = LoadCursor(NULL, IDC_SIZEWE);
	HCURSOR m_ResizeNESW = LoadCursor(NULL, IDC_SIZENESW);
	HCURSOR m_ResizeNWSE = LoadCursor(NULL, IDC_SIZENWSE);

	HCURSOR m_Hand = LoadCursor(NULL, IDC_HAND);

public:
	HCURSOR GetCursor(ImGuiMouseCursor imGuiMouseCursor)
	{
		switch (imGuiMouseCursor)
		{
		case ImGuiMouseCursor_None:
			return m_None;
		case ImGuiMouseCursor_Arrow:
			return m_Arrow;
		case ImGuiMouseCursor_TextInput:
			return m_TextInput;
		case ImGuiMouseCursor_ResizeAll:
			return m_ResizeAll;
		case ImGuiMouseCursor_ResizeNS:
			return m_ResizeNS;
		case ImGuiMouseCursor_ResizeEW:
			return m_ResizeEW;
		case ImGuiMouseCursor_ResizeNESW:
			return m_ResizeNESW;
		case ImGuiMouseCursor_ResizeNWSE:
			return m_ResizeNWSE;
		}

		return m_Arrow;
	}

} m_Cursors;

struct CUSTOMVERTEX
{
	float    pos[3];
	D3DCOLOR col;
	float    uv[2];
};
#define D3DFVF_CUSTOMVERTEX (D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1)

bool IsSupported()
{
	return ImGui::GetCurrentContext() != nullptr;
}

class CImGuiRenderCacheManager
{
public:
	~CImGuiRenderCacheManager()
	{
		Dx9_Destroy();
		for (auto * cache : m_FreeCaches)
			delete cache;
	}

	void Dx9_Create(IDirect3DDevice9 * device)
	{
		Dx9_Destroy();

		m_Device = device;

		std::unique_lock<std::mutex> freeLock(m_FreeCachesMutex);

		for (std::list<CImGuiRenderCache *>::iterator it = m_FreeCaches.begin(); it != m_FreeCaches.end(); ++it)
		{
			(*it)->Dx9_Create(device);
		}
	}

	void Dx9_Destroy()
	{
		std::unique_lock<std::mutex> freeLock(m_FreeCachesMutex);
		std::unique_lock<std::mutex> queuedLock(m_QueuedCachesMutex);

		while (!m_QueuedCaches.empty())
		{
			m_FreeCaches.push_back(m_QueuedCaches.front());
			m_QueuedCaches.pop_front();
		}

		for (std::list<CImGuiRenderCache *>::iterator it = m_FreeCaches.begin(); it != m_FreeCaches.end(); ++it)
		{
			(*it)->Dx9_Destroy();
		}

		m_Device = 0;
	}

	void Dx9_Render()
	{
		CImGuiRenderCache * cache = 0;

		{
			std::unique_lock<std::mutex> queuedLock(m_QueuedCachesMutex);

			if (0 < m_QueuedCaches.size())
			{
				cache = m_QueuedCaches.front();

				m_QueuedCaches.pop_front();
			}
		}

		if (cache)
		{
			cache->Render();

			{
				std::unique_lock<std::mutex> freeLock(m_FreeCachesMutex);

				m_FreeCaches.push_back(cache);
			}
		}
	}

	void CacheRender(ImDrawData* draw_data)
	{
		CImGuiRenderCache * cache = 0;

		{
			std::unique_lock<std::mutex> freeLock(m_FreeCachesMutex);

			if (0 < m_FreeCaches.size())
			{
				cache = m_FreeCaches.front();
				m_FreeCaches.pop_front();
			}
		}

		if (!cache)
		{
			cache = new CImGuiRenderCache();
			if (m_Device) cache->Dx9_Create(m_Device);
		}

		cache->CacheRender(draw_data);

		CImGuiRenderCache * stale = nullptr;
		{
			std::unique_lock<std::mutex> queuedLock(m_QueuedCachesMutex);
			if (m_QueuedCaches.size() >= 2)
			{
				stale = m_QueuedCaches.front();
				m_QueuedCaches.pop_front();
			}
			m_QueuedCaches.push_back(cache);
		}
		if (stale)
		{
			std::unique_lock<std::mutex> freeLock(m_FreeCachesMutex);
			m_FreeCaches.push_back(stale);
		}
	}

private:
	class CImGuiRenderCache
	{
	public:
		void Dx9_Create(IDirect3DDevice9 * device)
		{
			Dx9_Destroy();

			m_Device = device;
		}

		void Dx9_Destroy()
		{
			if (m_pIB)
			{
				m_pIB->Release();
				m_pIB = 0;
			}

			if (m_pVB)
			{
				m_pVB->Release();
				m_pVB = 0;
			}

			if (m_Device)
			{
				m_Device = 0;
			}
		}

		void CacheRender(ImDrawData* draw_data)
		{
			m_DrawListCount = 0;

			if (!m_Device || !draw_data || draw_data->TotalVtxCount == 0)
				return;

			ImGuiIO& io = ImGui::GetIO();

			// Avoid rendering when minimized
			if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
				return;

			m_Width = io.DisplaySize.x;
			m_Height = io.DisplaySize.y;

			if (m_DrawListSize < draw_data->CmdListsCount)
			{
				m_DrawLists.resize(draw_data->CmdListsCount);
				m_DrawListSize = draw_data->CmdListsCount;
			}

			// Create and grow buffers if needed
			if (!m_pVB || m_VertexBufferSize < draw_data->TotalVtxCount)
			{
				if (m_pVB) { m_pVB->Release(); m_pVB = NULL; }
				m_VertexBufferSize = draw_data->TotalVtxCount + 5000;
				if (m_Device->CreateVertexBuffer(m_VertexBufferSize * sizeof(CUSTOMVERTEX), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_CUSTOMVERTEX, D3DPOOL_DEFAULT, &m_pVB, NULL) < 0)
					return;
			}
			if (!m_pIB || m_IndexBufferSize < draw_data->TotalIdxCount)
			{
				if (m_pIB) { m_pIB->Release(); m_pIB = NULL; }
				m_IndexBufferSize = draw_data->TotalIdxCount + 10000;
				if (m_Device->CreateIndexBuffer(m_IndexBufferSize * sizeof(ImDrawIdx), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, sizeof(ImDrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32, D3DPOOL_DEFAULT, &m_pIB, NULL) < 0)
					return;
			}

			// Copy and convert all vertices into a single contiguous buffer
			CUSTOMVERTEX* vtx_dst;
			ImDrawIdx* idx_dst;

			if (m_pVB->Lock(0, (UINT)(draw_data->TotalVtxCount * sizeof(CUSTOMVERTEX)), (void**)&vtx_dst, D3DLOCK_DISCARD) < 0)
				return;
			if (m_pIB->Lock(0, (UINT)(draw_data->TotalIdxCount * sizeof(ImDrawIdx)), (void **)&idx_dst,
							D3DLOCK_DISCARD) < 0)
			{
				m_pVB->Unlock();
				return;
			}

			for (int n = 0; n < draw_data->CmdListsCount; n++)
			{
				const ImDrawList* cmd_list = draw_data->CmdLists[n];
				const ImDrawVert* vtx_src = cmd_list->VtxBuffer.Data;
				for (int i = 0; i < cmd_list->VtxBuffer.Size; i++)
				{
					vtx_dst->pos[0] = vtx_src->pos.x;
					vtx_dst->pos[1] = vtx_src->pos.y;
					vtx_dst->pos[2] = 0.0f;
					vtx_dst->col = (vtx_src->col & 0xFF00FF00) | ((vtx_src->col & 0xFF0000) >> 16) | ((vtx_src->col & 0xFF) << 16);     // RGBA --> ARGB for DirectX9
					vtx_dst->uv[0] = vtx_src->uv.x;
					vtx_dst->uv[1] = vtx_src->uv.y;
					vtx_dst++;
					vtx_src++;
				}
				memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
				idx_dst += cmd_list->IdxBuffer.Size;
			}
			m_pVB->Unlock();
			m_pIB->Unlock();

			// Copy command list:
			//

			m_DrawListCount = draw_data->CmdListsCount;

			for (int n = 0; n < draw_data->CmdListsCount; n++)
			{
				CDrawList & drawList = m_DrawLists[n];

				const ImDrawList* cmd_list = draw_data->CmdLists[n];

				if (drawList.m_DrawCommandSize < cmd_list->CmdBuffer.Size)
				{
					drawList.m_DrawCommands.resize(cmd_list->CmdBuffer.Size);
					drawList.m_DrawCommandSize = cmd_list->CmdBuffer.Size;
				}

				drawList.m_DrawCommandCount = cmd_list->CmdBuffer.Size;

				for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
				{
					drawList.m_DrawCommands[cmd_i] = cmd_list->CmdBuffer[cmd_i];
				}

				drawList.m_VtxBuffer_Size = cmd_list->VtxBuffer.Size;
				drawList.m_IdxBuffer_Size = cmd_list->IdxBuffer.Size;
			}

		}

		void Render(void)
		{
			if (!m_Device)
				return;

			if (0 == m_DrawListCount)
				return;

			// Backup the DX9 state
			IDirect3DStateBlock9* d3d9_state_block = NULL;
			if (m_Device->CreateStateBlock(D3DSBT_ALL, &d3d9_state_block) < 0)
				return;

			m_Device->SetStreamSource(0, m_pVB, 0, sizeof(CUSTOMVERTEX));
			m_Device->SetIndices(m_pIB);
			m_Device->SetFVF(D3DFVF_CUSTOMVERTEX);

			// Setup viewport
			D3DVIEWPORT9 vp;
			vp.X = vp.Y = 0;
			vp.Width = (DWORD)m_Width;
			vp.Height = (DWORD)m_Height;
			vp.MinZ = 0.0f;
			vp.MaxZ = 1.0f;
			m_Device->SetViewport(&vp);

			// Additional state (added):
			m_Device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
			m_Device->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_RED);
			m_Device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);

			// Setup render state: fixed-pipeline, alpha-blending, no face culling, no depth testing
			m_Device->SetPixelShader(NULL);
			m_Device->SetVertexShader(NULL);
			m_Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			m_Device->SetRenderState(D3DRS_LIGHTING, FALSE);
			m_Device->SetRenderState(D3DRS_ZENABLE, FALSE);
			m_Device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			m_Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
			m_Device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
			m_Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			m_Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			m_Device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
			m_Device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
			m_Device->SetRenderState(D3DRS_FOGENABLE, FALSE);
			m_Device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
			m_Device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
			m_Device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
			m_Device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
			m_Device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
			m_Device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
			m_Device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
			m_Device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
			m_Device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			m_Device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			m_Device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
			m_Device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);

			m_Device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			m_Device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			m_Device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
			m_Device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
			m_Device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
			m_Device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
			m_Device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			m_Device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);


			// Setup orthographic projection matrix
			// Being agnostic of whether <d3dx9.h> or <DirectXMath.h> can be used, we aren't relying on D3DXMatrixIdentity()/D3DXMatrixOrthoOffCenterLH() or DirectX::XMMatrixIdentity()/DirectX::XMMatrixOrthographicOffCenterLH()
			{
				const float L = 0.5f, R = m_Width + 0.5f, T = 0.5f, B = m_Height + 0.5f;
				D3DMATRIX mat_identity = { { 1.0f, 0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 0.0f, 1.0f } };
				D3DMATRIX mat_projection =
				{
					2.0f / (R - L),   0.0f,         0.0f,  0.0f,
					0.0f,         2.0f / (T - B),   0.0f,  0.0f,
					0.0f,         0.0f,         0.5f,  0.0f,
					(L + R) / (L - R),  (T + B) / (B - T),  0.5f,  1.0f,
				};
				m_Device->SetTransform(D3DTS_WORLD, &mat_identity);
				m_Device->SetTransform(D3DTS_VIEW, &mat_identity);
				m_Device->SetTransform(D3DTS_PROJECTION, &mat_projection);
			}

			// Render command lists
			int vtx_offset = 0;
			int idx_offset = 0;
			for (int n = 0; n < m_DrawListCount; n++)
			{
				const CDrawList & cmd_list = m_DrawLists[n];
				for (int cmd_i = 0; cmd_i < cmd_list.m_DrawCommandCount; cmd_i++)
				{
					const ImDrawCmd* pcmd = &cmd_list.m_DrawCommands[cmd_i];
					if (pcmd->UserCallback)
					{
						pcmd->UserCallback(/* cmd_list */ NULL, pcmd);
					}
					else
					{
						const RECT r = { (LONG)pcmd->ClipRect.x, (LONG)pcmd->ClipRect.y, (LONG)pcmd->ClipRect.z, (LONG)pcmd->ClipRect.w };
						m_Device->SetTexture(0, pcmd->TextureId && pcmd->TextureId == (void *)1 ? g_FontTexture : NULL);
						m_Device->SetScissorRect(&r);
						m_Device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, vtx_offset + pcmd->VtxOffset, 0,
													   (UINT)(cmd_list.m_VtxBuffer_Size - pcmd->VtxOffset),
													   idx_offset + pcmd->IdxOffset, pcmd->ElemCount / 3);
					}
				}
				vtx_offset += cmd_list.m_VtxBuffer_Size;
				idx_offset += cmd_list.m_IdxBuffer_Size;
			}

			// Restore the DX9 state
			d3d9_state_block->Apply();
			d3d9_state_block->Release();
		}

	private:
		IDirect3DDevice9 * m_Device = 0;
		LPDIRECT3DVERTEXBUFFER9 m_pVB = 0;
		LPDIRECT3DINDEXBUFFER9 m_pIB = 0;
		int m_VertexBufferSize = 5000;
		int m_IndexBufferSize = 10000;

		struct CDrawList
		{
			int m_DrawCommandCount;
			int m_DrawCommandSize = 0;
			std::vector<ImDrawCmd> m_DrawCommands;
			int m_VtxBuffer_Size;
			int m_IdxBuffer_Size;
		};

		int m_DrawListCount;
		int m_DrawListSize = 0;
		std::vector<CDrawList> m_DrawLists;
		float m_Width;
		float m_Height;

	};

	std::mutex m_FreeCachesMutex;
	std::list<CImGuiRenderCache *> m_FreeCaches;
	std::mutex m_QueuedCachesMutex;
	std::list<CImGuiRenderCache *> m_QueuedCaches;
	IDirect3DDevice9 * m_Device = 0;
} m_ImGuiRernderCacheManager;


void CacheRenderDrawData(ImDrawData* data)
{
	m_ImGuiRernderCacheManager.CacheRender(data);
}


static bool IsAnyMouseButtonDown()
{
	ImGuiIO& io = ImGui::GetIO();
	for (int n = 0; n < ARRAYSIZE(io.MouseDown); n++)
		if (io.MouseDown[n])
			return true;
	return false;
}

// The graph edits timing on ordinary CamPath keyframes on the engine thread.
namespace
{
struct CampathGraphState
{
	bool Open = false, Focus = false, Fit = true, Boxing = false, Editing = false;
	bool WindowFocused = true;
	ImVec2 BoxStart;
	int Handle = -1;
	double KeyTime = 0;
	double XMin = 0, XMax = 1, YMin = -0.2, YMax = 1.2;
	CamPath * LastPath = nullptr;
	CamPathCollection::Snapshot Before;
	CamPathEase Preset;
} g_CampathGraph;

bool SourceOwnsUi()
{
	bool available = false, visible = false;
	g_Hook_VClient_RenderView.UiCursorStatus(available, visible);
	return g_Hook_VClient_RenderView.IsSourceUiActive() || (available && visible);
}

bool GraphInteractive()
{
	return g_CampathGraph.Open && g_CampathGraph.WindowFocused && g_hWnd && g_FontTexture && !SourceOwnsUi();
}

bool m_GraphInputOwned = false;
RECT m_GameClip = {};
bool m_GameClipEnabled = false;
HWND m_GameCapture = nullptr;
int m_GameCursorDisplayCount = 0;

void UpdateGraphInputOwnership()
{
	bool wanted = GraphInteractive();
	if (wanted == m_GraphInputOwned)
		return;
	MirvCampathEditor_ReleaseKeys(true);
	if (auto * input = g_Hook_VClient_RenderView.m_MirvInput)
		input->ClearMovementInput(true);
	if (wanted)
	{
		GetCursorPos(&m_GameCursorPos);
		m_GameCursor = GetCursor();
		m_GameCapture = GetCapture();
		m_GameClipEnabled = GetClipCursor(&m_GameClip) != FALSE;
		m_GameCursorDisplayCount = ShowCursor(TRUE) - 1;
		for (int count = m_GameCursorDisplayCount + 1; count < 0; ++count)
			ShowCursor(TRUE);
		m_GraphInputOwned = true;
		ClipCursor(nullptr);
		if (GetCapture() == g_hWnd)
			ReleaseCapture();
		m_OwnCursor = m_Cursors.GetCursor(ImGuiMouseCursor_Arrow);
		SetCursor(m_OwnCursor);
		POINT client = m_GameCursorPos;
		ScreenToClient(g_hWnd, &client);
		ImGui::GetIO().MousePos = ImVec2(float(client.x), float(client.y));
	}
	else
	{
		m_GraphInputOwned = false;
		FinishCampathGraphEdit();
		if (GetCapture() == g_hWnd)
			ReleaseCapture();
		// Restore the game's latest requests, including requests made while the
		// graph owned the cursor. Never warp/capture another foreground window.
		if (g_CampathGraph.WindowFocused)
		{
			ClipCursor(m_GameClipEnabled ? &m_GameClip : nullptr);
			SetCursorPos(m_GameCursorPos.x, m_GameCursorPos.y);
			if (m_GameCapture)
				SetCapture(m_GameCapture);
			SetCursor(m_GameCursor);
		}
		else
			ClipCursor(nullptr);
		int count = ShowCursor(TRUE) - 1;
		ShowCursor(FALSE);
		while (count > m_GameCursorDisplayCount)
		{
			ShowCursor(FALSE);
			--count;
		}
		while (count < m_GameCursorDisplayCount)
		{
			ShowCursor(TRUE);
			++count;
		}
		auto & io = ImGui::GetIO();
		std::fill(std::begin(io.MouseDown), std::end(io.MouseDown), false);
		std::fill(std::begin(io.KeysDown), std::end(io.KeysDown), false);
	}
}
} // namespace

void FinishCampathGraphEdit()
{
	auto & graph = g_CampathGraph;
	if (graph.Editing)
		g_Hook_VClient_RenderView.m_CamPaths.Record(graph.Before);
	graph.Editing = false;
	graph.Boxing = false;
	graph.Handle = -1;
}

bool GetCampathGraphOpen()
{
	return g_CampathGraph.Open;
}

bool SetCampathGraphOpen(bool value)
{
	if (!ImGui::GetCurrentContext())
		return false;
	if (!value)
	{
		FinishCampathGraphEdit();
	}
	if (value && !g_CampathGraph.Open)
	{
		g_CampathGraph.Focus = true;
		g_CampathGraph.Fit = true;
	}
	g_CampathGraph.Open = value;
	UpdateGraphInputOwnership();
	return true;
}

static void DrawCampathGraphCanvas(CamPath & path)
{
	auto & graph = g_CampathGraph;
	auto & io = ImGui::GetIO();
	std::vector<std::pair<double, CamPathValue>> keys;
	for (auto it = path.GetBegin(); it != path.GetEnd(); ++it)
		keys.emplace_back(it.GetTime(), it.GetValue());
	if (keys.size() < 2)
	{
		ImGui::TextUnformatted("Capture at least two camera keys to edit segment easing.");
		return;
	}
	if (graph.LastPath != &path)
	{
		graph.LastPath = &path;
		graph.Fit = true;
	}
	if (graph.Fit)
	{
		graph.XMin = keys.front().first;
		graph.XMax = path.GetUpperBound();
		graph.YMin = 0;
		graph.YMax = double(keys.size() - 1);
		for (std::size_t i = 0; i + 1 < keys.size(); ++i)
		{
			CamPathEase ease = path.GetKeyEase(keys[i].first, true);
			if (keys[i].second.Selected && ease.Type == CamPathEase::Bezier)
			{
				graph.YMin = (std::min)(graph.YMin, i + (std::min)(ease.Y1, ease.Y2));
				graph.YMax = (std::max)(graph.YMax, i + (std::max)(ease.Y1, ease.Y2));
			}
			int samples =
				ease.Type == CamPathEase::Elastic ? (std::max)(64, (std::min)(512, int(8 / ease.Period))) : 64;
			for (int sample = 0; sample <= samples; ++sample)
			{
				double t = keys[i].first + (keys[i + 1].first - keys[i].first) * sample / samples;
				double y = i + path.GetSegmentProgress(keys[i].first, t);
				graph.YMin = (std::min)(graph.YMin, y);
				graph.YMax = (std::max)(graph.YMax, y);
			}
		}
		double xMargin = (graph.XMax - graph.XMin) * 0.08, yMargin = (graph.YMax - graph.YMin) * 0.15;
		graph.XMin -= xMargin;
		graph.XMax += xMargin;
		graph.YMin -= yMargin;
		graph.YMax += yMargin;
		graph.Fit = false;
	}
	ImVec2 origin = ImGui::GetCursorScreenPos();
	ImVec2 size = ImGui::GetContentRegionAvail();
	size.x = (std::max)(size.x, 240.0f);
	size.y = (std::max)(size.y, 220.0f);
	ImGui::InvisibleButton("##CampathGraphCanvas", size,
						   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
	bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
	ImVec2 plotMin(origin.x + 48, origin.y + 16), plotMax(origin.x + size.x - 16, origin.y + size.y - 30);
	auto point = [&](double t, double progress) {
		return ImVec2(float(plotMin.x + (t - graph.XMin) / (graph.XMax - graph.XMin) * (plotMax.x - plotMin.x)),
					  float(plotMax.y - (progress - graph.YMin) / (graph.YMax - graph.YMin) * (plotMax.y - plotMin.y)));
	};
	auto world = [&](ImVec2 p) {
		return std::make_pair(graph.XMin + (p.x - plotMin.x) / (plotMax.x - plotMin.x) * (graph.XMax - graph.XMin),
							  graph.YMin + (plotMax.y - p.y) / (plotMax.y - plotMin.y) * (graph.YMax - graph.YMin));
	};
	auto distance = [](ImVec2 a, ImVec2 b) {
		double x = a.x - b.x, y = a.y - b.y;
		return x * x + y * y;
	};
	if (hovered && io.MouseWheel != 0)
	{
		auto center = world(io.MousePos);
		double scale = std::pow(0.85, io.MouseWheel);
		double xSpan = (graph.XMax - graph.XMin) * scale, ySpan = (graph.YMax - graph.YMin) * scale;
		if (xSpan > 1e-5 && xSpan < 1e9 && ySpan > 1e-5 && ySpan < 1e7)
		{
			graph.XMin = center.first + (graph.XMin - center.first) * scale;
			graph.XMax = graph.XMin + xSpan;
			graph.YMin = center.second + (graph.YMin - center.second) * scale;
			graph.YMax = graph.YMin + ySpan;
		}
	}
	if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0))
	{
		double dx = -io.MouseDelta.x / (plotMax.x - plotMin.x) * (graph.XMax - graph.XMin),
			   dy = io.MouseDelta.y / (plotMax.y - plotMin.y) * (graph.YMax - graph.YMin);
		graph.XMin += dx;
		graph.XMax += dx;
		graph.YMin += dy;
		graph.YMax += dy;
	}
	ImDrawList * draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(20, 23, 29, 255));
	for (int i = 0; i <= 8; ++i)
	{
		double t = graph.XMin + (graph.XMax - graph.XMin) * i / 8;
		float x = point(t, 0).x;
		char label[64];
		std::snprintf(label, sizeof(label), "%.3g", t);
		draw->AddLine(ImVec2(x, plotMin.y), ImVec2(x, plotMax.y), IM_COL32(52, 57, 65, 255));
		draw->AddText(ImVec2(x, plotMax.y + 4), IM_COL32(180, 186, 196, 255), label);
	}
	double yStep = (std::max)(1.0, std::ceil((graph.YMax - graph.YMin) / 10));
	for (double y = std::ceil(graph.YMin / yStep) * yStep; y <= graph.YMax; y += yStep)
	{
		float at = point(0, y).y;
		char label[32];
		std::snprintf(label, sizeof(label), "%.0f", y);
		draw->AddLine(ImVec2(plotMin.x, at), ImVec2(plotMax.x, at), IM_COL32(52, 57, 65, 255));
		draw->AddText(ImVec2(origin.x + 4, at - 7), IM_COL32(180, 186, 196, 255), label);
	}
	draw->PushClipRect(plotMin, plotMax, true);
	int hitKey = -1, hitHandle = -1;
	double hitTime = 0;
	for (std::size_t i = 0; i < keys.size(); ++i)
	{
		const auto & key = keys[i];
		ImU32 color = key.second.Selected ? IM_COL32(255, 193, 77, 255) : IM_COL32(86, 192, 232, 255);
		ImVec2 camera = point(key.first, double(i));
		if (hovered && distance(camera, io.MousePos) < 81)
			hitKey = int(i);
		draw->AddCircleFilled(camera, 4, color);
		if (key.second.HoldDuration > 0)
		{
			ImVec2 release = point(key.first + key.second.HoldDuration, double(i));
			draw->AddLine(camera, release, IM_COL32(224, 135, 72, 255), 3);
			draw->AddRectFilled(ImVec2(release.x - 3, release.y - 6), ImVec2(release.x + 3, release.y + 6),
								IM_COL32(224, 135, 72, 255));
		}
		if (i + 1 == keys.size() || keys[i + 1].first < graph.XMin || key.first > graph.XMax)
			continue;
		double end = keys[i + 1].first, release = key.first + key.second.HoldDuration;
		CamPathEase ease = path.GetKeyEase(key.first, true);
		int samples = ease.Type == CamPathEase::Elastic ? (std::max)(64, (std::min)(512, int(8 / ease.Period))) : 96;
		ImVec2 last = point(release, double(i));
		for (int sample = 1; sample <= samples; ++sample)
		{
			double t = release + (end - release) * sample / samples;
			ImVec2 next = point(t, i + path.GetSegmentProgress(key.first, t));
			draw->AddLine(last, next, color, 2);
			last = next;
		}
		ImVec2 segment = point((release + end) * 0.5, i + path.GetSegmentProgress(key.first, (release + end) * 0.5));
		draw->AddRectFilled(ImVec2(segment.x - 4, segment.y - 4), ImVec2(segment.x + 4, segment.y + 4), color);
		if (hovered && distance(segment, io.MousePos) < 81)
			hitKey = int(i);
		if (key.second.Selected && ease.Type == CamPathEase::Bezier)
		{
			ImVec2 a = point(release, double(i)), b = point(end, double(i + 1));
			ImVec2 handles[] = {point(release + (end - release) * ease.X1, i + ease.Y1),
								point(release + (end - release) * ease.X2, i + ease.Y2)};
			for (int h = 0; h < 2; ++h)
			{
				draw->AddLine(h ? b : a, handles[h], IM_COL32(165, 142, 198, 255));
				draw->AddCircleFilled(handles[h], 5, IM_COL32(209, 155, 255, 255));
				if (hovered && distance(handles[h], io.MousePos) < 100)
				{
					hitHandle = h;
					hitTime = key.first;
				}
			}
		}
	}
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		if (hitHandle >= 0)
		{
			graph.Before = g_Hook_VClient_RenderView.m_CamPaths.Capture();
			graph.Editing = true;
			graph.Handle = hitHandle;
			graph.KeyTime = hitTime;
		}
		else if (hitKey >= 0)
		{
			if (!io.KeyShift)
				path.SelectNone();
			path.SelectAdd(std::size_t(hitKey), std::size_t(hitKey));
			graph.Preset = path.GetKeyEase(keys[hitKey].first);
		}
		else
		{
			graph.Boxing = true;
			graph.BoxStart = io.MousePos;
		}
	}
	if (graph.Editing && graph.Handle >= 0 && io.MouseDown[0])
	{
		for (std::size_t i = 0; i + 1 < keys.size(); ++i)
			if (keys[i].first == graph.KeyTime)
			{
				double release = keys[i].first + keys[i].second.HoldDuration, end = keys[i + 1].first;
				auto cursor = world(io.MousePos);
				CamPathEase ease = path.GetKeyEase(graph.KeyTime, true);
				double x = std::clamp((cursor.first - release) / (end - release), 0.0, 1.0),
					   y = std::clamp(cursor.second - i, -5.0, 5.0);
				if (graph.Handle == 0)
				{
					ease.X1 = (std::min)(x, ease.X2);
					ease.Y1 = path.SmoothHolds() && keys[i].second.HoldDuration > 0 ? 0 : y;
				}
				else
				{
					ease.X2 = (std::max)(x, ease.X1);
					ease.Y2 = path.SmoothHolds() && keys[i + 1].second.HoldDuration > 0 ? 1 : y;
				}
				path.SetKeyEase(graph.KeyTime, ease);
				graph.Preset = ease;
				break;
			}
	}
	if (graph.Boxing)
	{
		ImVec2 a((std::min)(graph.BoxStart.x, io.MousePos.x), (std::min)(graph.BoxStart.y, io.MousePos.y));
		ImVec2 b((std::max)(graph.BoxStart.x, io.MousePos.x), (std::max)(graph.BoxStart.y, io.MousePos.y));
		draw->AddRectFilled(a, b, IM_COL32(100, 160, 255, 40));
		draw->AddRect(a, b, IM_COL32(120, 180, 255, 220));
		if (!io.MouseDown[0])
		{
			if (!io.KeyShift)
				path.SelectNone();
			for (std::size_t i = 0; i < keys.size(); ++i)
			{
				ImVec2 p = point(keys[i].first, double(i));
				bool inside = p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y;
				if (i + 1 < keys.size())
				{
					double release = keys[i].first + keys[i].second.HoldDuration;
					p = point((release + keys[i + 1].first) * 0.5,
							  i + path.GetSegmentProgress(keys[i].first, (release + keys[i + 1].first) * 0.5));
					inside = inside || (p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y);
				}
				if (inside)
					path.SelectAdd(i, i);
			}
			graph.Boxing = false;
		}
	}
	if (graph.Editing && !io.MouseDown[0])
		FinishCampathGraphEdit();
	draw->PopClipRect();
	if (hovered && hitKey >= 0 && !io.MouseDown[0])
	{
		ImGui::BeginTooltip();
		ImGui::Text("Key %i: %.3fs", hitKey, keys[hitKey].first);
		if (std::size_t(hitKey + 1) < keys.size())
		{
			auto ease = path.GetKeyEase(keys[hitKey].first);
			ImGui::Text("Outgoing: %s %s", CamPathEase::ToString(ease.Type), CamPathEase::ToString(ease.Mode));
		}
		ImGui::EndTooltip();
	}
}

static void DrawCampathGraph()
{
	auto & graph = g_CampathGraph;
	if (!graph.Open || SourceOwnsUi())
	{
		FinishCampathGraphEdit();
		std::fill(std::begin(ImGui::GetIO().MouseDown), std::end(ImGui::GetIO().MouseDown), false);
		std::fill(std::begin(ImGui::GetIO().KeysDown), std::end(ImGui::GetIO().KeysDown), false);
		return;
	}
	ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(640, 450), ImVec2(FLT_MAX, FLT_MAX));
	if (graph.Focus)
	{
		ImGui::SetNextWindowFocus();
		graph.Focus = false;
	}
	if (!ImGui::Begin("Campath easing graph", &graph.Open,
					  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
	{
		ImGui::End();
		if (!graph.Open)
			FinishCampathGraphEdit();
		return;
	}
	auto & paths = g_Hook_VClient_RenderView.m_CamPaths;
	bool undo = false, redo = false;
	if (ImGui::Button("Undo"))
		undo = true;
	ImGui::SameLine();
	if (ImGui::Button("Redo"))
		redo = true;
	ImGui::SameLine();
	if (ImGui::Button("Fit"))
		graph.Fit = true;
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
		!ImGui::GetIO().WantTextInput)
	{
		undo = undo || ImGui::IsKeyPressed('Z', false);
		redo = redo || ImGui::IsKeyPressed('Y', false);
	}
	if (undo || redo)
	{
		FinishCampathGraphEdit();
		if (undo)
			paths.Undo();
		else
			paths.Redo();
		graph.Fit = true;
	}
	ImGui::SameLine();
	if (ImGui::BeginCombo("Path", paths.Active().GetName().c_str()))
	{
		for (std::size_t i = 0; i < paths.Size(); ++i)
			if (ImGui::Selectable(paths.At(i).GetName().c_str(), i == paths.ActiveIndex()))
			{
				FinishCampathGraphEdit();
				paths.Use(i);
				graph.Fit = true;
			}
		ImGui::EndCombo();
	}
	auto & path = paths.Active();
	if (ImGui::BeginCombo("Easing preset", CamPathEase::ToString(graph.Preset.Type)))
	{
		for (int i = 0; i < CamPathEase::Count; ++i)
			if (ImGui::Selectable(CamPathEase::ToString(CamPathEase::Function(i)), graph.Preset.Type == i))
				graph.Preset.Type = CamPathEase::Function(i);
		ImGui::EndCombo();
	}
	if (graph.Preset.Type != CamPathEase::Bezier &&
		ImGui::BeginCombo("Direction", CamPathEase::ToString(graph.Preset.Mode)))
	{
		for (auto mode : {CamPathEase::In, CamPathEase::Out, CamPathEase::InOut})
			if (ImGui::Selectable(CamPathEase::ToString(mode), mode == graph.Preset.Mode))
				graph.Preset.Mode = mode;
		ImGui::EndCombo();
	}
	if (graph.Preset.Type == CamPathEase::Bezier)
	{
		float points[] = {float(graph.Preset.X1), float(graph.Preset.Y1), float(graph.Preset.X2),
						  float(graph.Preset.Y2)};
		if (ImGui::InputFloat4("Time/value handles", points))
		{
			graph.Preset.X1 = points[0];
			graph.Preset.Y1 = points[1];
			graph.Preset.X2 = points[2];
			graph.Preset.Y2 = points[3];
		}
	}
	if (graph.Preset.Type == CamPathEase::Back)
	{
		float value = float(graph.Preset.Overshoot);
		if (ImGui::SliderFloat("Overshoot", &value, 0, 10))
			graph.Preset.Overshoot = value;
	}
	if (graph.Preset.Type == CamPathEase::Elastic)
	{
		float amplitude = float(graph.Preset.Amplitude), period = float(graph.Preset.Period);
		if (ImGui::SliderFloat("Amplitude", &amplitude, 1, 10))
			graph.Preset.Amplitude = amplitude;
		if (ImGui::SliderFloat("Period", &period, 0.01f, 2))
			graph.Preset.Period = period;
	}
	std::vector<double> selected;
	for (auto it = path.GetBegin(); it != path.GetEnd(); ++it)
		if (it.GetValue().Selected && std::next(it.wrapped) != path.GetEnd().wrapped)
			selected.push_back(it.GetTime());
	ImGui::BeginDisabled(!graph.Preset.Valid() || selected.empty());
	if (ImGui::Button("Apply to selected segments"))
	{
		auto before = paths.Capture();
		for (double time : selected)
			path.SetKeyEase(time, graph.Preset);
		paths.Record(before);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!graph.Preset.Valid());
	if (ImGui::Button("Apply to entire path"))
	{
		auto before = paths.Capture();
		path.SetEase(graph.Preset);
		paths.Record(before);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Select all"))
		path.SelectAll();
	ImGui::TextUnformatted(
		"Click keys/squares or drag a box to select; Shift adds. Drag purple handles to edit Bezier easing.");
	ImGui::TextUnformatted(
		"Middle drag pans; wheel zooms. Time: path seconds; vertical: key progress. Orange bars: holds. Esc closes.");
	ImGui::Text("%u segments selected. Path default: %s %s. Smooth holds flatten tangents.", unsigned(selected.size()),
				CamPathEase::ToString(path.GetEase().Type), CamPathEase::ToString(path.GetEase().Mode));
	DrawCampathGraphCanvas(path);
	ImGui::End();
	if (!graph.Open)
		FinishCampathGraphEdit();
}

bool WantsKeyboardInput()
{
	// Ownership starts when the graph opens, before ImGui has seen a mouse event.
	return IsSupported() && GraphInteractive();
}

bool WndProcHandler(HWND hwnd, UINT msg, WPARAM & wParam, LPARAM & lParam)
{
	if (!IsSupported())
		return false;

	if (msg == WM_KILLFOCUS || (msg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE))
	{
		g_CampathGraph.WindowFocused = false;
		FinishCampathGraphEdit();
		std::fill(std::begin(m_KeyDownEaten), std::end(m_KeyDownEaten), false);
		ImGui::GetIO().AddFocusEvent(false);
	}
	if (msg == WM_SETFOCUS || (msg == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE))
	{
		g_CampathGraph.WindowFocused = true;
		ImGui::GetIO().AddFocusEvent(true);
	}
	UpdateGraphInputOwnership();
	if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wParam < 256 && m_KeyDownEaten[wParam])
		return true;
	if (GraphInteractive() && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) &&
		MirvCampathEditor_IsGraphKey(unsigned(wParam)))
	{
		if (!(lParam & (1LL << 30)))
			SetCampathGraphOpen(false);
		m_KeyDownEaten[wParam] = true;
		return true;
	}

	if ((msg == WM_KEYUP || msg == WM_SYSKEYUP) && wParam < 256 && m_KeyDownEaten[wParam])
	{
		m_KeyDownEaten[wParam] = false;
		ImGui::GetIO().KeysDown[wParam] = false;
		return true;
	}
	if (GraphInteractive() && msg == WM_KEYDOWN && wParam == VK_ESCAPE && !ImGui::GetIO().WantTextInput)
	{
		SetCampathGraphOpen(false);
		m_KeyDownEaten[wParam] = true;
		return true;
	}
	if (wParam == VK_OEM_3 && (msg == WM_KEYDOWN || msg == WM_KEYUP))
		return false;
	if ((GetKeyState(VK_MENU) & 0x8000) && (wParam == VK_TAB || wParam == VK_F4) &&
		(msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP))
		return false;

	if (!m_Active || !GraphInteractive())
		return false;

	ImGuiIO& io = ImGui::GetIO();
	switch (msg)
	{
	case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDBLCLK:
		return true;

	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
		{
			int button = 0;
			if (msg == WM_LBUTTONDOWN) button = 0;
			if (msg == WM_RBUTTONDOWN) button = 1;
			if (msg == WM_MBUTTONDOWN) button = 2;
			if (!IsAnyMouseButtonDown() && GetCapture() == NULL)
				SetCapture(hwnd);
			io.MouseDown[button] = true;
			return true;
		}
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
		{
			int button = 0;
			if (msg == WM_LBUTTONUP) button = 0;
			if (msg == WM_RBUTTONUP) button = 1;
			if (msg == WM_MBUTTONUP) button = 2;
			io.MouseDown[button] = false;
			if (!IsAnyMouseButtonDown() && GetCapture() == hwnd)
				ReleaseCapture();
			return true;
		}
	case WM_MOUSEWHEEL:
		io.MouseWheel += GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? +1.0f : -1.0f;
		return true;
	case WM_MOUSEMOVE:
		io.MousePos.x = (signed short)(lParam);
		io.MousePos.y = (signed short)(lParam >> 16);
		return true;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if (wParam < 256)
		{
			io.KeysDown[wParam] = 1;
			if (!(lParam & (1LL << 30)))
				m_KeyDownEaten[wParam] = true;
		}
		return true;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		{
			bool wasKeyDownEaten = false;
			if (wParam < 256)
			{
				io.KeysDown[wParam] = 0;
				wasKeyDownEaten = m_KeyDownEaten[wParam];
				m_KeyDownEaten[wParam] = false;
			}
			return wasKeyDownEaten;
		}
	case WM_CHAR:
		// You can also use ToAscii()+GetKeyboardState() to retrieve characters.
		if (wParam > 0 && wParam < 0x10000)
			io.AddInputCharacter((unsigned short)wParam);
		return true;
	case WM_INPUT:
		{
			HRAWINPUT hRawInput = (HRAWINPUT)lParam;
			RAWINPUT inp = {};
			UINT size = sizeof(inp);

			GetRawInputData(hRawInput, RID_INPUT, &inp, &size, sizeof(RAWINPUTHEADER));

			switch (inp.header.dwType)
			{
			case RIM_TYPEMOUSE:
				return true;
			case RIM_TYPEKEYBOARD:
				// Source must be able to release a press that preceded GUI focus.
				return !(inp.data.keyboard.Flags & RI_KEY_BREAK);
			}
			return false;
		}
	}
	return false;
}

bool OnSetCursorPos(__in int X, __in int Y)
{
	if (!IsSupported())
		return false;
	UpdateGraphInputOwnership();
	if (!m_GraphInputOwned)
		return false;
	m_GameCursorPos = {X, Y};
	return true;
}

bool OnGetCursorPos(__out LPPOINT point)
{
	if (!IsSupported())
		return false;
	UpdateGraphInputOwnership();
	if (!m_GraphInputOwned || !point)
		return false;
	*point = m_GameCursorPos;
	return true;
}

bool OnSetCursor(__in_opt HCURSOR cursor, HCURSOR & result)
{
	if (!IsSupported())
		return false;
	UpdateGraphInputOwnership();
	if (!m_GraphInputOwned)
		return false;
	result = m_GameCursor;
	m_GameCursor = cursor;
	SetCursor(m_OwnCursor);
	return true;
}

bool OnSetCapture(HWND window, HWND & result)
{
	if (!IsSupported())
		return false;
	UpdateGraphInputOwnership();
	if (!m_GraphInputOwned)
		return false;
	result = m_GameCapture;
	m_GameCapture = window;
	return true;
}

bool OnReleaseCapture()
{
	if (!IsSupported())
		return false;
	UpdateGraphInputOwnership();
	if (!m_GraphInputOwned)
		return false;
	m_GameCapture = nullptr;
	return true;
}

bool OnClipCursor(const RECT * rect)
{
	if (!IsSupported())
		return false;
	UpdateGraphInputOwnership();
	if (!m_GraphInputOwned)
		return false;
	m_GameClipEnabled = rect != nullptr;
	if (rect)
		m_GameClip = *rect;
	return true;
}

bool OnShowCursor(BOOL show, int & result)
{
	if (!IsSupported())
		return false;
	UpdateGraphInputOwnership();
	if (!m_GraphInputOwned)
		return false;
	m_GameCursorDisplayCount += show ? 1 : -1;
	result = m_GameCursorDisplayCount;
	return true;
}

bool DX9_CreateFontsTexture()
{
	// Build texture atlas
	ImGuiIO& io = ImGui::GetIO();
	unsigned char* pixels;
	int width, height, bytes_per_pixel;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytes_per_pixel);

	// Upload texture to graphics system
	g_FontTexture = NULL;
	if (g_pd3dDevice->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_FontTexture, NULL) < 0)
		return false;
	D3DLOCKED_RECT tex_locked_rect;
	if (g_FontTexture->LockRect(0, &tex_locked_rect, NULL, 0) != D3D_OK)
		return false;
	for (int y = 0; y < height; y++)
		memcpy((unsigned char *)tex_locked_rect.pBits + tex_locked_rect.Pitch * y, pixels + (width * bytes_per_pixel) * y, (width * bytes_per_pixel));
	g_FontTexture->UnlockRect(0);

	io.Fonts->TexID = (void *)1;

	return true;
}

bool DX9_CreateDeviceObjects()
{
	if (!g_pd3dDevice)
		return false;
	if (!DX9_CreateFontsTexture())
		return false;

	m_ImGuiRernderCacheManager.Dx9_Create(g_pd3dDevice);

	return true;
}

void DllProcessAttach(void)
{
	ImGui::SetCurrentContext(ImGui::CreateContext());
}

void DllProcessDetach(void)
{
	SetCampathGraphOpen(false);
	ImGui::DestroyContext(ImGui::GetCurrentContext());
}

bool On_Direct3DDevice9_Init(void* hwnd, IDirect3DDevice9* device)
{
	if (!IsSupported())
		return false;

	g_hWnd = (HWND)hwnd;
	g_pd3dDevice = device;

	if (!QueryPerformanceFrequency((LARGE_INTEGER *)&g_TicksPerSecond))
		return false;
	if (!QueryPerformanceCounter((LARGE_INTEGER *)&g_Time))
		return false;

	ImGuiIO& io = ImGui::GetIO();

	std::string hlaeFolderPath(GetHlaeFolder());
	
	m_IniFilename = hlaeFolderPath;
	m_IniFilename.append("\\AfxHookSource_imgui.ini");

	io.IniFilename = m_IniFilename.c_str();

	m_LogFileName = hlaeFolderPath;
	m_LogFileName.append("\\AfxHookmource_imgui_log.txt");
	io.LogFilename = m_LogFileName.c_str();

	io.KeyMap[ImGuiKey_Tab] = VK_TAB;                       // Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array that we will update during the application lifetime.
	io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
	io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
	io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
	io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
	io.KeyMap[ImGuiKey_PageUp] = VK_PRIOR;
	io.KeyMap[ImGuiKey_PageDown] = VK_NEXT;
	io.KeyMap[ImGuiKey_Home] = VK_HOME;
	io.KeyMap[ImGuiKey_End] = VK_END;
	io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
	io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
	io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
	io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
	io.KeyMap[ImGuiKey_A] = 'A';
	io.KeyMap[ImGuiKey_C] = 'C';
	io.KeyMap[ImGuiKey_V] = 'V';
	io.KeyMap[ImGuiKey_X] = 'X';
	io.KeyMap[ImGuiKey_Y] = 'Y';
	io.KeyMap[ImGuiKey_Z] = 'Z';

	io.ImeWindowHandle = g_hWnd;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
	ImGui::StyleColorsDark();

	DX9_CreateDeviceObjects();

	return true;
}

void DX9_InvalidateDeviceObjects()
{
	if (!g_pd3dDevice)
		return;

	m_ImGuiRernderCacheManager.Dx9_Destroy();

	// At this point note that we set ImGui::GetIO().Fonts->TexID to be == g_FontTexture, so clear both.
	ImGuiIO& io = ImGui::GetIO();
	if (g_FontTexture)
		g_FontTexture->Release();
	g_FontTexture = NULL;
	io.Fonts->TexID = NULL;
}

void DoFrame()
{
	DrawCampathGraph();
	UpdateGraphInputOwnership();
}

void On_Direct3DDevice9_Shutdown()
{
	if (!IsSupported())
		return;

	SetCampathGraphOpen(false);
	DX9_InvalidateDeviceObjects();
	g_pd3dDevice = NULL;
	g_hWnd = 0;
}


void On_Direct3DDevice9_EndScene()
{
	if (!IsSupported())
		return;

	if (m_FirstEndScene && m_Active && g_FontTexture)
	{
		m_FirstEndScene = false;

		m_ImGuiRernderCacheManager.Dx9_Render();
	}
}

void On_Direct3DDevice9_Present(bool deviceLost)
{
	if (!IsSupported())
		return;

	m_FirstEndScene = true;
}

void On_Direct3DDevice9_Reset_Before()
{
	if (!IsSupported())
		return;

	DX9_InvalidateDeviceObjects();
}

void On_Direct3DDevice9_Reset_After()
{
	if (!IsSupported())
		return;

	DX9_CreateDeviceObjects();
}


bool OnGameFrameRenderEnd()
{
	if (!IsSupported())
		return false;

	UpdateGraphInputOwnership();
	if (!m_Active || !g_hWnd || !g_pd3dDevice || !g_FontTexture)
		return false;

	ImGuiIO& io = ImGui::GetIO();

	// Setup time step
	INT64 current_time;
	QueryPerformanceCounter((LARGE_INTEGER *)&current_time);
	io.DeltaTime = (float)(current_time - g_Time) / g_TicksPerSecond;
	g_Time = current_time;

	// Setup display size (every frame to accommodate for window resizing)
	RECT rect;
	GetClientRect(g_hWnd, &rect);
	io.DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));

	// Read keyboard modifiers inputs
	io.KeyCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	io.KeyShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	io.KeyAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
	io.KeySuper = false;
	// io.KeysDown : filled by WM_KEYDOWN/WM_KEYUP events
	// io.MousePos : filled by WM_MOUSEMOVE events
	// io.MouseDown : filled by WM_*BUTTON* events
	// io.MouseWheel : filled by WM_MOUSEWHEEL events

	// Set OS mouse position if requested last frame by io.WantSetMousePos flag (used when io.NavMovesTrue is enabled by
	// user and using directional navigation)
	if (m_GraphInputOwned && io.WantSetMousePos)
	{
		POINT pos = { (int)io.MousePos.x, (int)io.MousePos.y };
		ClientToScreen(g_hWnd, &pos);
		SetCursorPos(pos.x, pos.y);
	}

	// Start the frame. This call will update the io.WantCaptureMouse, io.WantCaptureKeyboard flag that you can use to dispatch inputs (or not) to your application.
	ImGui::NewFrame();

	DoFrame();
	if (m_GraphInputOwned)
	{
		m_OwnCursor = m_Cursors.GetCursor(ImGui::GetMouseCursor());
		SetCursor(m_OwnCursor);
	}

	ImGui::EndFrame();

	ImGui::Render();

	CacheRenderDrawData(ImGui::GetDrawData());

	return true;
}

} // namespace Gui {
} // namespace AfxHookSource {

#else

#include "Gui.h"

namespace AfxHookSource {
namespace Gui {

void DllProcessAttach(void)
{
}

void DllProcessDetach(void)
{
}

bool SetCampathGraphOpen(bool)
{
	return false;
}
bool GetCampathGraphOpen()
{
	return false;
}
void FinishCampathGraphEdit()
{
}

bool WantsKeyboardInput()
{
	return false;
}

bool WndProcHandler(HWND hwnd, UINT msg, WPARAM & wParam, LPARAM & lParam)
{
	return false;
}

bool OnSetCursorPos(__in int X, __in int Y)
{
	return false;
}

bool OnGetCursorPos(__out LPPOINT lpPoint)
{
	return false;
}

bool OnSetCursor(__in_opt HCURSOR hCursor, HCURSOR & result)
{
	return false;
}

bool OnSetCapture(HWND hWnd, HWND & result)
{
	return false;
}

bool OnReleaseCapture()
{
	return false;
}

bool OnClipCursor(const RECT *)
{
	return false;
}
bool OnShowCursor(BOOL, int &)
{
	return false;
}

bool OnGameFrameRenderEnd()
{
	return false;
}

bool On_Direct3DDevice9_Init(void* hwnd, IDirect3DDevice9* device)
{
	return false;
}

void On_Direct3DDevice9_Shutdown()
{

}

void On_Direct3DDevice9_EndScene()
{

}

void On_Direct3DDevice9_Present(bool deviceLost)
{

}

void On_Direct3DDevice9_Reset_Before()
{

}

void On_Direct3DDevice9_Reset_After()
{

}



} // namespace Gui {
} // namespace AfxHookSource {


#endif
