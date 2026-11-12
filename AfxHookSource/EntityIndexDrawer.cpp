#include "stdafx.h"

#include "EntityIndexDrawer.h"

#include "addresses.h"
#include "MaterialSystemHooks.h"
#include "MirvCalcs.h"
#include "RenderView.h"
#include "SourceInterfaces.h"
#include "WrpVEngineClient.h"
#include "d3d9Hooks.h"
#include "hlaeFolder.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string>
#include <utility>

extern WrpVEngineClient * g_VEngineClient;

CEntityIndexDrawer g_EntityIndexDrawer;

// Entity index helpers ////////////////////////////////////////////////////////

/// <summary>Projects a world position with the engine's world to screen matrix (like the Source SDK's ScreenTransform)
/// into the viewport (left, top, width, height) in pixels.</summary>
/// <returns>false if the point is behind the camera.</returns>
static bool EntityIndex_Project(const float m[4][4], float x, float y, float z, float left, float top, float width, float height, float & outX, float & outY)
{
	const float w = m[3][0] * x + m[3][1] * y + m[3][2] * z + m[3][3];

	if (w < 0.001f)
		return false;

	const float invW = 1.0f / w;
	const float sx = (m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3]) * invW;
	const float sy = (m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3]) * invW;

	outX = left + 0.5f * (1.0f + sx) * width;
	outY = top + 0.5f * (1.0f - sy) * height;

	return true;
}

/// <summary>Finds the eye position of the camera the world to screen matrix belongs to:
/// It is the point where clip x, clip y and w (rows 0, 1 and 3 times (x, y, z, 1)) are all zero.</summary>
/// <returns>false if the matrix is degenerate.</returns>
static bool EntityIndex_SolveEye(const float m[4][4], double outEye[3])
{
	const double a[3][3] = {
		{ m[0][0], m[0][1], m[0][2] },
		{ m[1][0], m[1][1], m[1][2] },
		{ m[3][0], m[3][1], m[3][2] }
	};
	const double b[3] = { -(double)m[0][3], -(double)m[1][3], -(double)m[3][3] };

	const double det =
		a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
		- a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
		+ a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);

	if (fabs(det) < 1e-9)
		return false;

	// Cramer's rule
	for (int c = 0; c < 3; ++c)
	{
		double t[3][3];
		for (int r = 0; r < 3; ++r)
			for (int k = 0; k < 3; ++k)
				t[r][k] = k == c ? b[r] : a[r][k];

		const double d =
			t[0][0] * (t[1][1] * t[2][2] - t[1][2] * t[2][1])
			- t[0][1] * (t[1][0] * t[2][2] - t[1][2] * t[2][0])
			+ t[0][2] * (t[1][0] * t[2][1] - t[1][1] * t[2][0]);

		outEye[c] = d / det;
	}

	return true;
}

// The same digit atlas used by CampathDrawer.cpp (resources/hexfont.tga).
// XYZRHW vertices keep digits a constant size on screen while the anchor is
// obtained from each entity's world-space position.
struct EntityIndex_GlyphVertex
{
	float x, y, z, rhw;
	float u, v;
};

static void EntityIndex_DrawGlyph(IDirect3DDevice9 * device, float x, float y, float w, float h, int digit)
{
	// Campath's atlas has 32x64 cells in a 256x128 texture.
	const float u0 = (32.0f * (digit % 8)) / 256.0f;
	const float v0 = (64.0f * (digit / 8)) / 128.0f;
	const float u1 = u0 + 32.0f / 256.0f;
	const float v1 = v0 + 64.0f / 128.0f;
	EntityIndex_GlyphVertex quad[4] = {
		{x - 0.5f,     y - 0.5f,     0.0f, 1.0f, u0, v0},
		{x+w - 0.5f,   y - 0.5f,     0.0f, 1.0f, u1, v0},
		{x - 0.5f,     y+h - 0.5f,   0.0f, 1.0f, u0, v1},
		{x+w - 0.5f,   y+h - 0.5f,   0.0f, 1.0f, u1, v1}
	};
	device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(EntityIndex_GlyphVertex));
}

static void EntityIndex_DrawNumber(IDirect3DDevice9 * device, int number, float x, float bottomY, float height, uint32_t color)
{
	char digits[16];
	int count = 0;
	if (number < 0)
		number = 0;
	do
	{
		digits[count++] = (char)(number % 10);
		number /= 10;
	}
	while (number && count < 15);

	// Use the campath glyph texture and modulate it with the category color.
	device->SetRenderState(D3DRS_TEXTUREFACTOR, color);
	const float width = 0.5f * height; // Hexfont's 32:64 cell proportions.
	float left = x - 0.5f * width * count;
	for (int i = count - 1; i >= 0; --i)
	{
		EntityIndex_DrawGlyph(device, left, bottomY-height, width, height, digits[i]);
		left += width;
	}
}

// CEntityIndexDrawer ////////////////////////////////////////////////////////

class CEntityIndexDrawerFunctor : public CMaterialSystemFunctor
{
public:
	CEntityIndexDrawerFunctor(CEntityIndexDrawer::DrawData && data)
		: m_Data(std::move(data))
	{
	}

	virtual void operator()()
	{
		g_EntityIndexDrawer.OnPostRenderAllTools_DrawingThread(m_Data);
	}

private:
	CEntityIndexDrawer::DrawData m_Data;
};

void CEntityIndexDrawer::BeginDevice(IDirect3DDevice9 * device)
{
	m_Device = device;
}

void CEntityIndexDrawer::EndDevice(void)
{
	if (m_DigitsTexture)
	{
		m_DigitsTexture->Release();
		m_DigitsTexture = nullptr;
	}
	m_Device = nullptr;
}

void CEntityIndexDrawer::CategoryColor_set(int category, int r, int g, int b)
{
	if (category < 0 || 6 <= category)
		return;

	const int values[3] = { r, g, b };
	for (int i = 0; i < 3; ++i)
		m_CategoryColors[category][i] = (std::max)(0, (std::min)(255, values[i]));
}

void CEntityIndexDrawer::CategoryColor_get(int category, int & r, int & g, int & b) const
{
	if (category < 0 || 6 <= category)
		category = 4;

	r = m_CategoryColors[category][0];
	g = m_CategoryColors[category][1];
	b = m_CategoryColors[category][2];
}

void CEntityIndexDrawer::OnPostRenderAllTools_EngineThread(void)
{
	// Warning: This is called multiple times per frame (i.e. after the 3D skybox view and after the world view).

	if (!m_Enabled)
		return;

	if (!(SourceSdkVer_TF2 == g_SourceSdkVer || SourceSdkVer_TF2Classified == g_SourceSdkVer))
		return;

	if (!g_VEngineClient)
		return;

	++m_StatCalls;

	const SOURCESDK::VMatrix & matrix = g_VEngineClient->WorldToScreenMatrix();

	DrawData data;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			data.worldToScreen[r][c] = matrix.m[r][c];

	const double camX = g_Hook_VClient_RenderView.LastCameraOrigin[0];
	const double camY = g_Hook_VClient_RenderView.LastCameraOrigin[1];
	const double camZ = g_Hook_VClient_RenderView.LastCameraOrigin[2];

	if (m_ViewCheck)
	{
		// Only draw on the main view: The eye of the matrix has to be where the camera is.
		double eye[3];

		if (EntityIndex_SolveEye(data.worldToScreen, eye))
		{
			const double dx = eye[0] - camX;
			const double dy = eye[1] - camY;
			const double dz = eye[2] - camZ;

			m_StatLastEyeDelta = sqrt(dx * dx + dy * dy + dz * dz);

			if (2.0 < m_StatLastEyeDelta)
				return;
		}
	}

	++m_StatMainViewCalls;

	static MirvTf2EntityLabel_t found[8192];
	const int numFound = Mirv_Tf2_CollectEntityLabels(false, found, 8192);

	m_StatLastFound = numFound;

	struct Candidate
	{
		double distSq;
		Label label;
	};

	std::vector<Candidate> candidates;
	candidates.reserve(numFound);

	const double maxDistSq = (double)m_MaxDistance * (double)m_MaxDistance;

	for (int i = 0; i < numFound; ++i)
	{
		const int category = found[i].category;
		if (category < 0 || category >= 6 || !m_CategoryFilter[category])
			continue;
		const double dx = found[i].origin[0] - camX;
		const double dy = found[i].origin[1] - camY;
		const double dz = found[i].origin[2] - camZ;
		const double distSq = dx * dx + dy * dy + dz * dz;

		if (0.0f < m_MaxDistance && maxDistSq < distSq)
			continue;

		Candidate c;
		c.distSq = distSq;
		c.label.index = found[i].index;
		c.label.category = found[i].category;
		c.label.x = found[i].origin[0];
		c.label.y = found[i].origin[1];
		c.label.z = found[i].origin[2] + m_Height;
		candidates.push_back(c);
	}

	// Nearest first, keep at most m_MaxLabels.
	std::sort(candidates.begin(), candidates.end(), [](const Candidate & a, const Candidate & b) { return a.distSq < b.distSq; });

	if ((size_t)m_MaxLabels < candidates.size())
		candidates.resize((size_t)m_MaxLabels);

	m_StatLastPosted = (int)candidates.size();

	if (candidates.empty())
		return;

	data.size = m_Size;
	data.gibSize = m_GibSize;
	data.stackLabels = m_StackLabels;
	data.stackWorldDistance = m_StackWorldDistance;
	for (int category = 0; category < 6; ++category)
		data.colors[category] = 0xFF000000u | ((uint32_t)m_CategoryColors[category][0] << 16) | ((uint32_t)m_CategoryColors[category][1] << 8) | (uint32_t)m_CategoryColors[category][2];

	data.labels.reserve(candidates.size());
	for (size_t i = candidates.size(); 0 < i; --i)
		data.labels.push_back(candidates[i - 1].label); // far to near, so near labels end up on top

	MaterialSystem_ExecuteOnRenderThread(new CEntityIndexDrawerFunctor(std::move(data)));
}

void CEntityIndexDrawer::OnPostRenderAllTools_DrawingThread(const DrawData & data)
{

	IDirect3DDevice9 * device = m_Device;
	if (!device || data.labels.empty())
		return;

	D3DVIEWPORT9 vp;
	if (FAILED(device->GetViewport(&vp)) || !vp.Width || !vp.Height)
		return;

	struct ScreenLabel
	{
		const Label * label;
		float x, y;
		float left, top, right, bottom;
	};
	std::vector<ScreenLabel> labels;
	labels.reserve(data.labels.size());

	for (size_t i = 0; i < data.labels.size(); ++i)
	{
		const Label & label = data.labels[i];
		float x, y;
		if (!EntityIndex_Project(data.worldToScreen, label.x, label.y, label.z,
			(float)vp.X, (float)vp.Y, (float)vp.Width, (float)vp.Height, x, y))
			continue;
		if (x < vp.X || x >= vp.X + vp.Width || y < vp.Y || y >= vp.Y + vp.Height)
			continue;
		int digits = 1;
		for (int n = label.index; n >= 10; n /= 10)
			++digits;
		const float size = label.category == 5 ? data.gibSize : data.size;
		float w = digits * 0.5f * size;
		labels.push_back({&label, x, y, x - 0.5f * w, y - size, x + 0.5f * w, y});
	}
	if (labels.empty())
		return;

	// Deterministic order for coincident entities.
	std::sort(labels.begin(), labels.end(), [](const ScreenLabel & a, const ScreenLabel & b) {
		if (a.label->category != b.label->category)
			return a.label->category < b.label->category;
		return a.label->index < b.label->index;
	});

	// Only shift overlapping labels if their world-space anchors are nearby.
	if (data.stackLabels)
	{
		std::vector<ScreenLabel> placed;
		placed.reserve(labels.size());
		for (size_t i = 0; i < labels.size(); ++i)
		{
			ScreenLabel candidate = labels[i];
			const float step = candidate.bottom - candidate.top + 5.0f;
			for (size_t attempt = 0; attempt <= placed.size(); ++attempt)
			{
				bool overlap = false;
				float shift = step;
				for (size_t j = 0; j < placed.size(); ++j)
				{
					const ScreenLabel & other = placed[j];
					// Projection alone is insufficient: two entities separated by
					// hundreds of world units can occupy the same screen pixels.
					const float dx = candidate.label->x - other.label->x;
					const float dy = candidate.label->y - other.label->y;
					const float dz = candidate.label->z - other.label->z;
					const float maxDist = data.stackWorldDistance;
					const bool nearby = dx*dx + dy*dy + dz*dz <= maxDist*maxDist;
					if (nearby && candidate.left < other.right && other.left < candidate.right &&
						candidate.top < other.bottom && other.top < candidate.bottom)
					{
						overlap = true;
						shift = (std::max)(step, candidate.bottom - other.top + 5.0f);
						break;
					}
				}
				if (!overlap)
					break;
				candidate.y -= shift;
				candidate.top -= shift;
				candidate.bottom -= shift;
			}
			placed.push_back(candidate);
		}
		labels.swap(placed);
	}

	// Reuse the campath digit texture. Release it when the device is reset.
	if (!m_DigitsTexture)
	{
		IDirect3DTexture9 * texture = nullptr;
		if (SUCCEEDED(device->CreateTexture(256, 128, 1, D3DUSAGE_DYNAMIC,
			D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, nullptr)) && texture)
		{
			std::wstring filename = GetHlaeFolderW();
			filename += L"resources\\hexfont.tga";
			FILE * file = nullptr;
			_wfopen_s(&file, filename.c_str(), L"rb");
			D3DLOCKED_RECT rect;
			if (file && SUCCEEDED(texture->LockRect(0, &rect, nullptr, 0)))
			{
				fseek(file, 18, SEEK_SET); // Match the campath atlas loader.
				bool ok = true;
				for (int row = 0; row < 128; ++row)
					if (fread((unsigned char*)rect.pBits + row * rect.Pitch, 1, 256*4, file) != 256*4)
						ok = false;
				texture->UnlockRect(0);
				if (ok) m_DigitsTexture = texture;
			}
			if (file) fclose(file);
			if (!m_DigitsTexture) texture->Release();
		}
	}
	if (!m_DigitsTexture)
		return;

	// Preserve the rendering state, as done by CampathDrawer.
	AfxD3D9BeginCleanState();

	device->SetVertexShader(nullptr);
	device->SetPixelShader(nullptr);
	device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
	device->SetTexture(0, m_DigitsTexture);
	device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
	device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->SetRenderState(D3DRS_ZENABLE, FALSE);
	device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);

	for (size_t i = 0; i < labels.size(); ++i)
	{
		const ScreenLabel & label = labels[i];
		int cat = label.label->category;
		if (cat < 0 || cat >= 6)
			cat = 4;
		EntityIndex_DrawNumber(device, label.label->index, label.x, label.y,
		                       cat == 5 ? data.gibSize : data.size, data.colors[cat]);
	}
	AfxD3D9EndCleanState();
}
