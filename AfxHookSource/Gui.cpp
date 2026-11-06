#include "stdafx.h"

// TODO:
// - Lots.
// - Maybe handle RawInput mouse buttons in drag mode (so the game doesn't see them, but currently it doesn't care about them anyway.).

#include "Gui.h"

#ifndef AFX_ENABLE_GUI
#define AFX_ENABLE_GUI 0
#endif

#if AFX_ENABLE_GUI

#include "CameraOverlay.h"
#include "CampathDrawer.h"
#include "RenderView.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
extern void MirvCampathPreview_ReleaseKeys();
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

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

bool SetOverlayOpen(bool value);
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
	return ImGui::GetCurrentContext() != nullptr &&
		   (g_SourceSdkVer == SourceSdkVer_TF2 || g_SourceSdkVer == SourceSdkVer_TF2Classified);
}

class CImGuiRenderCacheManager
{
public:
  ~CImGuiRenderCacheManager()
  {
	  Dx9_Destroy();
	  for (auto *cache : m_FreeCaches)
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

		CImGuiRenderCache *stale = nullptr;
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

namespace
{
bool g_OverlayWindowFocused = true;
bool SourceOwnsUi()
{
	bool available = false, visible = false;
	g_Hook_VClient_RenderView.UiCursorStatus(available, visible);
	return g_Hook_VClient_RenderView.IsSourceUiActive() || (available && visible);
}

bool GraphInteractive()
{
	return CameraOverlay::IsOpen() && !CameraOverlay::IsMouseLook() && g_OverlayWindowFocused && g_hWnd &&
		   g_FontTexture && !SourceOwnsUi();
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
	MirvCampathPreview_ReleaseKeys();
	if (auto *input = g_Hook_VClient_RenderView.m_MirvInput)
		input->ClearMovementInput();
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
		if (GetCursor() != m_OwnCursor)
			SetCursor(m_OwnCursor);
		POINT client = m_GameCursorPos;
		ScreenToClient(g_hWnd, &client);
		ImGui::GetIO().MousePos = ImVec2(float(client.x), float(client.y));
	}
	else
	{
		m_GraphInputOwned = false;
		CameraOverlay::FinishEdit();
		if (GetCapture() == g_hWnd)
			ReleaseCapture();
		// Restore the game's latest requests, including requests made while the
		// graph owned the cursor. Never warp/capture another foreground window.
		if (g_OverlayWindowFocused)
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
		auto &io = ImGui::GetIO();
		std::fill(std::begin(io.MouseDown), std::end(io.MouseDown), false);
		std::fill(std::begin(io.KeysDown), std::end(io.KeysDown), false);
	}
}
} // namespace

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
		g_OverlayWindowFocused = false;
		CameraOverlay::SetMouseLook(false);
		CameraOverlay::FinishEdit();
		std::fill(std::begin(m_KeyDownEaten), std::end(m_KeyDownEaten), false);
		ImGui::GetIO().AddFocusEvent(false);
	}
	if (msg == WM_SETFOCUS || (msg == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE))
	{
		g_OverlayWindowFocused = true;
		ImGui::GetIO().AddFocusEvent(true);
	}
	UpdateGraphInputOwnership();
	if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wParam < 256 && m_KeyDownEaten[wParam])
		return true;
	if ((msg == WM_KEYUP || msg == WM_SYSKEYUP) && wParam < 256 && m_KeyDownEaten[wParam])
	{
		m_KeyDownEaten[wParam] = false;
		if (g_hWnd)
			ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
		return true;
	}
	if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wParam == VK_F8 && !SourceOwnsUi())
	{
		if (!(lParam & (1LL << 30)))
			SetOverlayOpen(!CameraOverlay::IsOpen());
		m_KeyDownEaten[wParam] = true;
		return true;
	}
	if ((msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) &&
		(wParam == VK_OEM_3 || wParam == VK_F2 || wParam == VK_SHIFT || wParam == VK_LSHIFT ||
		 wParam == VK_RSHIFT))
	{
		if (GraphInteractive())
			ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
		return false;
	}
	if (msg == WM_RBUTTONDOWN && CameraOverlay::IsOpen() && !SourceOwnsUi() &&
		!ImGui::GetIO().WantCaptureMouse && g_Hook_VClient_RenderView.m_MirvInput &&
		g_Hook_VClient_RenderView.m_MirvInput->GetCameraControlMode())
	{
		CameraOverlay::SetMouseLook(true);
		UpdateGraphInputOwnership();
		return false;
	}
	if (msg == WM_RBUTTONUP && CameraOverlay::IsMouseLook())
	{
		CameraOverlay::SetMouseLook(false);
		UpdateGraphInputOwnership();
		return false;
	}
	if ((GetKeyState(VK_MENU) & 0x8000) && (wParam == VK_TAB || wParam == VK_F4) &&
		(msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP))
		return false;

	if (!m_Active || !GraphInteractive())
		return false;

	if (msg == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT)
	{
		SetCursor(m_OwnCursor ? m_OwnCursor : m_Cursors.GetCursor(ImGuiMouseCursor_Arrow));
			return true;
		}

	ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
	if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wParam < 256)
		m_KeyDownEaten[wParam] = true;
	if (msg == WM_INPUT)
	{
		RAWINPUT input = {};
		UINT size = sizeof(input);
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));
		if (input.header.dwType == RIM_TYPEKEYBOARD &&
			(input.data.keyboard.VKey == VK_SHIFT || input.data.keyboard.VKey == VK_LSHIFT ||
			 input.data.keyboard.VKey == VK_RSHIFT || input.data.keyboard.VKey == VK_F2 ||
			 input.data.keyboard.VKey == VK_OEM_3))
		return false;
		return input.header.dwType == RIM_TYPEMOUSE || !(input.data.keyboard.Flags & RI_KEY_BREAK);
	}
	return msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP ||
		   msg == WM_CHAR || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN ||
		   msg == WM_RBUTTONUP || msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP || msg == WM_MOUSEWHEEL ||
		   msg == WM_MOUSEMOVE;
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

bool OnSetCursor(__in_opt HCURSOR cursor, HCURSOR &result)
{
	if (!IsSupported())
		return false;
	UpdateGraphInputOwnership();
	if (!m_GraphInputOwned)
		return false;
	result = m_GameCursor;
	m_GameCursor = cursor;
	if (GetCursor() != m_OwnCursor)
	SetCursor(m_OwnCursor);
	return true;
}

bool OnSetCapture(HWND window, HWND &result)
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

bool OnClipCursor(const RECT *rect)
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

bool OnShowCursor(BOOL show, int &result)
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
	SetOverlayOpen(false);
	if (ImGui::GetIO().BackendPlatformUserData)
		ImGui_ImplWin32_Shutdown();
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

	if (io.BackendPlatformUserData)
		ImGui_ImplWin32_Shutdown();
	ImGui_ImplWin32_Init(g_hWnd);
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
	ImGui::StyleColorsDark();
	CameraOverlay::Initialize();

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

bool SetOverlayOpen(bool value)
{
	if (value && !IsSupported())
		return false;
	CameraOverlay::SetOpen(value);
	UpdateGraphInputOwnership();
	return value == CameraOverlay::IsOpen();
}
void DoFrame()
{
	if (!SourceOwnsUi())
		CameraOverlay::Draw();
	else
		CameraOverlay::SetMouseLook(false);
	UpdateGraphInputOwnership();
}

void On_Direct3DDevice9_Shutdown()
{
	if (!IsSupported())
		return;

	SetOverlayOpen(false);
	DX9_InvalidateDeviceObjects();
	if (ImGui::GetIO().BackendPlatformUserData)
		ImGui_ImplWin32_Shutdown();
	g_pd3dDevice = NULL;
	g_hWnd = 0;
}


void On_Direct3DDevice9_EndScene()
{
	if (!IsSupported())
		return;

	if (m_FirstEndScene && m_Active && g_FontTexture && CameraOverlay::IsOpen())
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

	ImGui_ImplWin32_NewFrame();
	// Set OS mouse position if requested last frame by io.WantSetMousePos flag (used when io.NavMovesTrue is
	// enabled by user and using directional navigation)
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
		if (GetCursor() != m_OwnCursor)
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

bool SetOverlayOpen(bool)
{
	return false;
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
