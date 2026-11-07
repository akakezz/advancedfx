#pragma once

#include <d3d9.h>

namespace AfxHookSource {
namespace Gui {

// Read-only access to the existing font atlas for world text in the classic campath drawer.
struct WorldGlyph
{
	float X0, Y0, X1, Y1, U0, V0, U1, V1, Advance;
};
bool GetWorldGlyph(unsigned int codepoint, WorldGlyph &glyph);
IDirect3DTexture9 *GetWorldFontTexture();

//bool SetActive(bool value);

void DllProcessAttach(void);
void DllProcessDetach(void);

/// <returns>Whether the event was already handled or not.</returns>
bool WndProcHandler(HWND hwnd, UINT msg, WPARAM & wParam, LPARAM & lParam);
// Expose existing GUI ownership so camera/editor input can yield between events.
bool WantsKeyboardInput();
bool SetOverlayOpen(bool value);
bool OnSetCursorPos(__in int X, __in int Y);
bool OnGetCursorPos(__out LPPOINT lpPoint);
bool OnGetCursor(HCURSOR &result);
bool OnSetCursor(__in_opt HCURSOR hCursor, HCURSOR & result);
bool OnSetCapture(HWND hWnd, HWND & result);
bool OnReleaseCapture();
bool OnClipCursor(const RECT *rect);
bool OnShowCursor(BOOL show, int &result);
bool OnGameFrameRenderEnd();

bool On_Direct3DDevice9_Init(void* hwnd, IDirect3DDevice9* device);
void On_Direct3DDevice9_Shutdown();

void On_Direct3DDevice9_EndScene();
void On_Direct3DDevice9_Present(bool deviceLost);
void On_Direct3DDevice9_Reset_Before();
void On_Direct3DDevice9_Reset_After();


} // namespace Gui {
} // namespace AfxHookSource {

