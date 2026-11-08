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
	}

	void Dx9_Destroy()
	{
		std::unique_lock<std::mutex> freeLock(m_FreeCachesMutex);
		std::unique_lock<std::mutex> queuedLock(m_QueuedCachesMutex);

		if (m_CurrentCache)
		{
			m_FreeCaches.push_back(m_CurrentCache);
			m_CurrentCache = nullptr;
		}

		while (!m_QueuedCaches.empty())
		{
			m_FreeCaches.push_back(m_QueuedCaches.front());
			m_QueuedCaches.pop_front();
		}

		if (m_pIB)
		{
			m_pIB->Release();
			m_pIB = nullptr;
		}
		if (m_pVB)
		{
			m_pVB->Release();
			m_pVB = nullptr;
		}

		m_Device = 0;
	}

	bool Dx9_Render()
	{
		if (!m_Device) return false;
		// Source may end offscreen scenes before the main scene. Drawing into
		// a material/shadow/atlas texture here corrupts subsequent game frames.
		IDirect3DSurface9 *target = nullptr, *backbuffer = nullptr;
		const bool mainTarget = m_Device->GetRenderTarget(0, &target) >= 0 &&
			m_Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer) >= 0 &&
			target && target == backbuffer;
		if (target) target->Release();
		if (backbuffer) backbuffer->Release();
		if (!mainTarget) return false;
		CImGuiRenderCache *cache = nullptr;
		std::list<CImGuiRenderCache *> stale;
		{
			std::unique_lock<std::mutex> queuedLock(m_QueuedCachesMutex);
			if (!m_QueuedCaches.empty())
			{
				cache = m_QueuedCaches.back();
				m_QueuedCaches.pop_back();
				stale.splice(stale.end(), m_QueuedCaches);
			}
		}
		// Keep the last immutable snapshot when rendering gets ahead of the UI producer.
		// A missing update must not turn the overlay off for one presented frame.
		if (cache)
		{
			if (m_CurrentCache)
				stale.push_back(m_CurrentCache);
			m_CurrentCache = cache;
		}
		if (!stale.empty())
		{
			std::unique_lock<std::mutex> freeLock(m_FreeCachesMutex);
			m_FreeCaches.splice(m_FreeCaches.end(), stale);
		}
		if (m_CurrentCache)
			m_CurrentCache->Render(*this);
		return true;
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
		void CacheRender(ImDrawData* draw_data)
		{
			m_DrawListCount = 0;

			if (!draw_data || draw_data->TotalVtxCount == 0)
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

			// Only CPU memory is touched on the game thread. The queued cache owns its data.
			m_Vertices.resize(draw_data->TotalVtxCount);
			m_Indices.resize(draw_data->TotalIdxCount);
			CUSTOMVERTEX *vtx_dst = m_Vertices.data();
			ImDrawIdx *idx_dst = m_Indices.data();

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

		void Render(CImGuiRenderCacheManager &owner)
		{
			// CPU snapshots share one renderer-owned buffer pair, including across device resets.
			auto *m_Device = owner.m_Device;
			auto &m_pVB = owner.m_pVB;
			auto &m_pIB = owner.m_pIB;
			auto &m_VertexBufferSize = owner.m_VertexBufferSize;
			auto &m_IndexBufferSize = owner.m_IndexBufferSize;
			if (!m_Device)
				return;

			if (0 == m_DrawListCount)
				return;

			// D3D9 allocation and locks belong to the consuming render thread.
			const int vertexCount = (int)m_Vertices.size(), indexCount = (int)m_Indices.size();
			// Create and grow buffers if needed
			if (!m_pVB || m_VertexBufferSize < vertexCount)
			{
				if (m_pVB)
				{
					m_pVB->Release();
					m_pVB = NULL;
				}
				m_VertexBufferSize = vertexCount + 5000;
				if (m_Device->CreateVertexBuffer(m_VertexBufferSize * sizeof(CUSTOMVERTEX),
												 D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_CUSTOMVERTEX,
												 D3DPOOL_DEFAULT, &m_pVB, NULL) < 0)
					return;
			}
			if (!m_pIB || m_IndexBufferSize < indexCount)
			{
				if (m_pIB)
				{
					m_pIB->Release();
					m_pIB = NULL;
				}
				m_IndexBufferSize = indexCount + 10000;
				if (m_Device->CreateIndexBuffer(m_IndexBufferSize * sizeof(ImDrawIdx),
												D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
												sizeof(ImDrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32,
												D3DPOOL_DEFAULT, &m_pIB, NULL) < 0)
					return;
			}

			void *vertices = nullptr, *indices = nullptr;
			if (m_pVB->Lock(0, (UINT)(vertexCount * sizeof(CUSTOMVERTEX)), &vertices, D3DLOCK_DISCARD) < 0)
				return;
			if (m_pIB->Lock(0, (UINT)(indexCount * sizeof(ImDrawIdx)), &indices, D3DLOCK_DISCARD) < 0)
			{
				m_pVB->Unlock();
				return;
			}
			memcpy(vertices, m_Vertices.data(), vertexCount * sizeof(CUSTOMVERTEX));
			memcpy(indices, m_Indices.data(), indexCount * sizeof(ImDrawIdx));
			m_pIB->Unlock();
			m_pVB->Unlock();

			// Backup the DX9 state
			IDirect3DStateBlock9* d3d9_state_block = NULL;
			if (m_Device->CreateStateBlock(D3DSBT_ALL, &d3d9_state_block) < 0)
				return;
			// Match ImGui's DX9 backend: explicitly capture, and preserve fixed
			// transforms separately (some implementations omit them from blocks).
			D3DMATRIX lastWorld, lastView, lastProjection;
			if (d3d9_state_block->Capture() < 0 ||
				m_Device->GetTransform(D3DTS_WORLD, &lastWorld) < 0 ||
				m_Device->GetTransform(D3DTS_VIEW, &lastView) < 0 ||
				m_Device->GetTransform(D3DTS_PROJECTION, &lastProjection) < 0)
			{
				d3d9_state_block->Release();
				return;
			}

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
			m_Device->SetTransform(D3DTS_WORLD, &lastWorld);
			m_Device->SetTransform(D3DTS_VIEW, &lastView);
			m_Device->SetTransform(D3DTS_PROJECTION, &lastProjection);
			d3d9_state_block->Apply();
			d3d9_state_block->Release();
		}

	private:

		struct CDrawList
		{
			int m_DrawCommandCount;
			int m_DrawCommandSize = 0;
			std::vector<ImDrawCmd> m_DrawCommands;
			int m_VtxBuffer_Size;
			int m_IdxBuffer_Size;
		};

		int m_DrawListCount = 0;
		int m_DrawListSize = 0;
		std::vector<CUSTOMVERTEX> m_Vertices;
		std::vector<ImDrawIdx> m_Indices;
		std::vector<CDrawList> m_DrawLists;
		float m_Width;
		float m_Height;

	};

	// Owned exclusively by the render thread; never returned to the producer while displayed.
	CImGuiRenderCache *m_CurrentCache = nullptr;
	std::mutex m_FreeCachesMutex;
	std::list<CImGuiRenderCache *> m_FreeCaches;
	std::mutex m_QueuedCachesMutex;
	std::list<CImGuiRenderCache *> m_QueuedCaches;
	LPDIRECT3DVERTEXBUFFER9 m_pVB = nullptr;
	LPDIRECT3DINDEXBUFFER9 m_pIB = nullptr;
	int m_VertexBufferSize = 5000, m_IndexBufferSize = 10000;
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
// ImGui renders overlay cursor shapes; the OS cursor is restored when ownership ends.
static bool m_PreviousDrawCursor = false;

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
		m_PreviousDrawCursor = ImGui::GetIO().MouseDrawCursor;
		ImGui::GetIO().MouseDrawCursor = true;
		SetCursor(nullptr);
		POINT client = m_GameCursorPos;
		ScreenToClient(g_hWnd, &client);
		ImGui::GetIO().MousePos = ImVec2(float(client.x), float(client.y));
	}
	else
	{
		m_GraphInputOwned = false;
		ImGui::GetIO().MouseDrawCursor = m_PreviousDrawCursor;
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

	// WM_SETCURSOR is a hot/reentrant Windows callback. Ownership is refreshed by
	// normal window events and frames, never by querying Source from this callback.
	if (msg == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT && m_GraphInputOwned)
	{
		if (GetCursor() != nullptr)
			SetCursor(nullptr);
		return true;
	}

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
		MirvCampathPreview_SetActive(false);
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
	if (!m_GraphInputOwned)
		return false;
	m_GameCursorPos = {X, Y};
	return true;
}

bool OnGetCursorPos(__out LPPOINT point)
{
	if (!IsSupported())
		return false;
	if (!m_GraphInputOwned || !point)
		return false;
	*point = m_GameCursorPos;
	return true;
}

// Called from Source's cursor routines: never re-enter the engine UI or the OS here.
bool OnGetCursor(HCURSOR &result)
{
	if (!IsSupported() || !m_GraphInputOwned)
		return false;
	result = m_GameCursor;
	return true;
}
bool OnSetCursor(__in_opt HCURSOR cursor, HCURSOR &result)
{
	if (!IsSupported())
		return false;
	if (!m_GraphInputOwned)
		return false;
	result = m_GameCursor;
	m_GameCursor = cursor;
	return true;
}

bool OnSetCapture(HWND window, HWND &result)
{
	if (!IsSupported())
		return false;
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
	if (!m_GraphInputOwned)
		return false;
	m_GameCapture = nullptr;
	return true;
}

bool OnClipCursor(const RECT *rect)
{
	if (!IsSupported())
		return false;
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
	if (!m_GraphInputOwned)
		return false;
	m_GameCursorDisplayCount += show ? 1 : -1;
	result = m_GameCursorDisplayCount;
	return true;
}

bool GetWorldGlyph(unsigned int codepoint, WorldGlyph &out)
{
	if (!ImGui::GetCurrentContext())
		return false;
	auto *atlas = ImGui::GetIO().Fonts;
	if (!atlas || !atlas->IsBuilt() || atlas->Fonts.empty())
		return false;
	auto *font = atlas->Fonts[0];
	const ImFontGlyph *glyph = font->FindGlyph((ImWchar)codepoint);
	if (!glyph || font->FontSize <= 0)
		return false;
	const float scale = 1.0f / font->FontSize;
	out = {glyph->X0 * scale, glyph->Y0 * scale, glyph->X1 * scale, glyph->Y1 * scale,		glyph->U0,
		   glyph->V0,		  glyph->U1,		 glyph->V1,			glyph->AdvanceX * scale};
	return true;
}
IDirect3DTexture9 *GetWorldFontTexture()
{
	return g_FontTexture;
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
	CameraOverlay::FlushRecovery();
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
	// HLAE owns the OS cursor through its existing import hooks. The Win32
	// backend must not reset its shape during NewFrame / WM_SETCURSOR.
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
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
		if (m_ImGuiRernderCacheManager.Dx9_Render())
			m_FirstEndScene = false;
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

bool GetWorldGlyph(unsigned int, WorldGlyph &)
{
	return false;
}
IDirect3DTexture9 *GetWorldFontTexture()
{
	return nullptr;
}

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

bool OnGetCursor(HCURSOR &)
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
