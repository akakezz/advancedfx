#pragma once
#include "WrpConsole.h"
#include <d3d9.h>
#include <shared/CinematicPath.h>

// Called only on the engine/UI thread. Rendering consumes immutable snapshots.
cinematic::Editor &MirvCinematic_Editor();
bool MirvCinematic_Command(IWrpCommandArgs *args);
bool MirvCinematic_Override(float &x, float &y, float &z, float &pitch, float &yaw, float &roll, float &fov);
bool MirvCinematic_OwnsCamera();
bool MirvCinematic_IsEditing();
void MirvCinematic_Update();
bool MirvCinematic_KeyEvent(UINT message, WPARAM key, LPARAM flags);
void MirvCinematic_ReleaseKeys();
void MirvCinematic_Draw();
void MirvCinematic_BeginDevice(IDirect3DDevice9 *device);
void MirvCinematic_EndDevice();
bool MirvCinematic_DrawEnabled(int layer);
