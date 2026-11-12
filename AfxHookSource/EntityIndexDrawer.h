#pragma once

#include <d3d9.h>

#include <stdint.h>
#include <vector>

/// <summary>Draws TF2 entity indices in the game view.</summary>
/// <remarks>Collects entities on the engine thread and draws labels on the render thread.</remarks>
class CEntityIndexDrawer
{
public:
	void BeginDevice(IDirect3DDevice9 * device);
	void EndDevice(void);

	void OnPostRenderAllTools_EngineThread(void);

	bool Enabled_get(void) { return m_Enabled; }
	void Enabled_set(bool value) { m_Enabled = value; }

	// Label categories: player, projectile, weapon, cosmetic, other, gib.
	bool Filter_get(int category) const
	{
		return 0 <= category && category < 6 && m_CategoryFilter[category];
	}
	void Filter_set(int category, bool value)
	{
		if (0 <= category && category < 6)
			m_CategoryFilter[category] = value;
	}
	void FilterAll_set(bool value)
	{
		for (int i = 0; i < 6; ++i)
			m_CategoryFilter[i] = value;
	}

	/// <summary>Vertical offset from the label anchor in world units.</summary>
	float Height_get(void) { return m_Height; }
	void Height_set(float value) { m_Height = value; }

	/// <summary>Height of the digits in pixels.</summary>
	float Size_get(void) { return m_Size; }
	void Size_set(float value) { m_Size = value < 6.0f ? 6.0f : (200.0f < value ? 200.0f : value); }

	float GibSize_get(void) const
	{
		return m_GibSize;
	}
	void GibSize_set(float value)
	{
		m_GibSize = value < 6 ? 6 : (value > 200 ? 200 : value);
	}

	/// <summary>0 means no limit.</summary>
	float MaxDistance_get(void) { return m_MaxDistance; }
	void MaxDistance_set(float value) { m_MaxDistance = value < 0.0f ? 0.0f : value; }

	int MaxLabels_get(void) { return m_MaxLabels; }
	void MaxLabels_set(int value) { m_MaxLabels = value < 1 ? 1 : (2048 < value ? 2048 : value); }

	void CategoryColor_set(int category, int r, int g, int b);
	void CategoryColor_get(int category, int & r, int & g, int & b) const;

	// Shift overlapping labels only if their world-space anchors are nearby.
	bool StackLabels_get(void) const { return m_StackLabels; }
	void StackLabels_set(bool value) { m_StackLabels = value; }
	float StackWorldDistance_get(void) const { return m_StackWorldDistance; }
	void StackWorldDistance_set(float value) { m_StackWorldDistance = value < 0.0f ? 0.0f : value; }

	/// <summary>If true (default) only the main view is drawn on, not i.e. the 3D skybox view.</summary>
	bool ViewCheck_get(void) { return m_ViewCheck; }
	void ViewCheck_set(bool value) { m_ViewCheck = value; }


	void Stats_get(unsigned int & outCalls, unsigned int & outMainViewCalls, int & outLastFound, int & outLastPosted, double & outLastEyeDelta)
	{
		outCalls = m_StatCalls;
		outMainViewCalls = m_StatMainViewCalls;
		outLastFound = m_StatLastFound;
		outLastPosted = m_StatLastPosted;
		outLastEyeDelta = m_StatLastEyeDelta;
	}

	struct Label
	{
		int index;
		int category;
		float x, y, z;
	};

	struct DrawData
	{
		float worldToScreen[4][4] = {};
		float size = 32.0f;
		float gibSize = 16.0f;
		bool stackLabels = true;
		float stackWorldDistance = 32.0f;
		uint32_t colors[6] = {0xFF42C8FFu, 0xFFFF6A55u, 0xFF46EB8Cu, 0xFFCE80FFu, 0xFFFFE600u, 0xFFFFE600u};
		std::vector<Label> labels;
	};

	/// <summary>Draws the labels, called on the render thread.</summary>
	void OnPostRenderAllTools_DrawingThread(const DrawData & data);

private:
	IDirect3DDevice9 * m_Device = nullptr;
	IDirect3DTexture9 * m_DigitsTexture = nullptr;

	bool m_Enabled = false;
	bool m_CategoryFilter[6] = {true, true, true, true, true, true};
	float m_Height = 0.0f;
	float m_Size = 32.0f;
	float m_GibSize = 16.0f;
	float m_MaxDistance = 0.0f;
	int m_MaxLabels = 64;
	int m_CategoryColors[6][3] = {{66, 200, 255},  {255, 106, 85}, {70, 235, 140},
	                              {206, 128, 255}, {255, 230, 0},  {255, 230, 0}};
	bool m_ViewCheck = true;
	bool m_StackLabels = true;
	float m_StackWorldDistance = 32.0f;

	unsigned int m_StatCalls = 0;
	unsigned int m_StatMainViewCalls = 0;
	int m_StatLastFound = 0;
	int m_StatLastPosted = 0;
	double m_StatLastEyeDelta = -1.0;
};

extern CEntityIndexDrawer g_EntityIndexDrawer;
