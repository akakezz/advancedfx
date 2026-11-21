// Native observer override for POV demos. Called only at render-frame boundaries;
// network updates and the recorded player's state retain their original values.
#include <atomic>
#include <cmath>
#include <vector>
#include <map>
namespace {
struct PovObserverFields { int Mode = -1, Target = -1, Pitch = -1, Yaw = -1;
 int Fov = -1, FovStart = -1, DefaultFov = -1, Punch[3] = {-1,-1,-1}, ViewOffset[3] = {-1,-1,-1}; };
PovObserverFields PovFindFields(const Tf2FixRecvTable *table)
{
 PovObserverFields result;
 std::set<const Tf2FixRecvTable *> active;
 std::function<void(const Tf2FixRecvTable *, int, int)> visit = [&](const auto *node, int base, int depth) {
  if (!node || depth >= 32 || !active.insert(node).second) return;
  if (node->Count >= 0 && node->Count <= 4096 && (!node->Count || node->Props))
   for (int i = 0; i < node->Count; ++i) {
    const auto &p = node->Props[i];
    if (!p.Name || p.Offset < 0 || p.Offset > 65532 - base) continue;
    const int offset = base + p.Offset;
    if (p.Type == 0 && !std::strcmp(p.Name, "m_iObserverMode")) result.Mode = offset;
    else if (p.Type == 0 && !std::strcmp(p.Name, "m_hObserverTarget")) result.Target = offset;
    else if (p.Type == 1 && !std::strcmp(p.Name, "m_angEyeAngles[0]")) result.Pitch = offset;
    else if (p.Type == 1 && !std::strcmp(p.Name, "m_angEyeAngles[1]")) result.Yaw = offset;
    else if ((p.Type == 2 || p.Type == 3) && !std::strcmp(p.Name, "m_angEyeAngles")) { result.Pitch = offset; result.Yaw = offset + 4; }
    else if (p.Type == 0 && !std::strcmp(p.Name, "m_iFOV")) result.Fov = offset;
    else if (p.Type == 0 && !std::strcmp(p.Name, "m_iFOVStart")) result.FovStart = offset;
    else if (p.Type == 0 && !std::strcmp(p.Name, "m_iDefaultFOV")) result.DefaultFov = offset;
    else if (p.Type == 2 && offset <= 65524 && !std::strcmp(p.Name, "m_vecPunchAngle")) for (int j=0;j<3;++j) result.Punch[j]=offset+4*j;
    else if (p.Type == 2 && offset <= 65524 && !std::strcmp(p.Name, "m_vecViewOffset")) for (int j=0;j<3;++j) result.ViewOffset[j]=offset+4*j;
    else if (p.Type == 1) {
     for (int j=0;j<3;++j) {
      const char *punchNames[] = {"m_vecPunchAngle[0]","m_vecPunchAngle[1]","m_vecPunchAngle[2]"};
      const char *viewNames[] = {"m_vecViewOffset[0]","m_vecViewOffset[1]","m_vecViewOffset[2]"};
      if (!std::strcmp(p.Name,punchNames[j])) result.Punch[j]=offset;
      if (!std::strcmp(p.Name,viewNames[j])) result.ViewOffset[j]=offset;
     }
    }
    else if (p.Type == 6) visit(p.Table, offset, depth + 1);
   }
  active.erase(node);
 };
 visit(table, 0, 0);
 return result;
}
PovObserverFields PovEntityFields(void *entity)
{
 if (!entity) return {};
#ifdef _WIN64
 using InterfaceFn = void *(*)(void *);
#else
 using InterfaceFn = void *(__thiscall *)(void *);
#endif
 // IClientUnknown::GetClientNetworkable / IClientNetworkable::GetClientClass.
 const auto network = reinterpret_cast<InterfaceFn>((*static_cast<void ***>(entity))[4])(entity);
 if (!network) return {};
 const auto cls = static_cast<const Tf2FixClientClass *>(reinterpret_cast<InterfaceFn>((*static_cast<void ***>(network))[2])(network));
 return cls ? PovFindFields(cls->Table) : PovObserverFields{};
}
bool PovTargetAvailable(void *entity)
{
 if (!entity) return false;
#ifdef _WIN64
 using InterfaceFn = void *(*)(void *);
 using DormantFn = bool (*)(void *);
#else
 using InterfaceFn = void *(__thiscall *)(void *);
 using DormantFn = bool (__thiscall *)(void *);
#endif
 auto network = reinterpret_cast<InterfaceFn>((*static_cast<void ***>(entity))[4])(entity);
 return network && !reinterpret_cast<DormantFn>((*static_cast<void ***>(network))[8])(network);
}
#ifdef _WIN64
using PovGetViewAnglesFn = void (*)(void *, SOURCESDK::QAngle &);
#else
using PovGetViewAnglesFn = void (__fastcall *)(void *, void *, SOURCESDK::QAngle &);
#endif
std::map<void **, PovGetViewAnglesFn> g_PovOriginalAngles;
bool PovReadLook(void *target, SOURCESDK::QAngle &angles, const PovObserverFields *known = nullptr);
// Reuse the registered cvar interfaces (WrpConVarRef is CS:GO-only).
// TF2's own taunt-first-person option blocks recorder taunts from switching cameras
// without clearing taunt conditions, cancelling gestures, or changing player animation.
bool PovTauntFirstPerson(float &value, bool write)
{
 auto access = [&](auto *iface) {
  if (!iface) return false;
  auto var = iface->FindVar("tf_taunt_first_person");
  if (!var) return false;
  if (write) var->SetValue(value); else value=var->GetFloat();
  return true;
 };
 if (SOURCESDK::L4D2::g_pCVar) return access(SOURCESDK::L4D2::g_pCVar);
 return access(SOURCESDK::CSS::g_pCVar);
}
bool g_PovTauntGuard = false;
float g_PovTauntPreference = 0;
void PovGuardRecorderCamera(bool enabled)
{
 if (!enabled) {
  if (g_PovTauntGuard) PovTauntFirstPerson(g_PovTauntPreference, true);
  g_PovTauntGuard=false;
  return;
 }
 float current=0;
 if (!PovTauntFirstPerson(current,false)) return;
 if (!g_PovTauntGuard || current!=1) g_PovTauntPreference=current;
 float firstPerson=1;
 g_PovTauntGuard=PovTauntFirstPerson(firstPerson,true);
}
int g_PovSpectateUser = 0;
std::atomic<bool> g_PovThirdPerson{false};
std::atomic<const char *> g_PovSpectateStatus = "Native POV spectating is off.";
struct PovObserverScope {
 void *Object = nullptr;
 int Index = -1, ModeOffset = -1, TargetOffset = -1, Mode = 0;
 unsigned Handle = 0xffffffffu, Target = 0xffffffffu, AppliedTarget = 0xffffffffu;
 PovObserverFields LookFields;
 std::vector<std::pair<int,unsigned>> CameraFields;
} g_PovObserverScope;
template<typename T> T PovReadField(const void *object, int offset, T fallback)
{
 if (offset>=0) std::memcpy(&fallback,static_cast<const unsigned char*>(object)+offset,sizeof(T));
 return fallback;
}
template<typename T> void PovPatchCameraField(PovObserverScope &scope, int offset, T value)
{
 static_assert(sizeof(T)==4,"Native camera fields are 32-bit");
 if (offset<0) return;
 unsigned original;
 auto bytes=static_cast<unsigned char*>(scope.Object)+offset;
 std::memcpy(&original,bytes,4);scope.CameraFields.emplace_back(offset,original);
 std::memcpy(bytes,&value,4);
}
void PovRestoreCameraFields(PovObserverScope &scope)
{
 if (!scope.Object || Mirv_Tf2_DiagnosticEntity(scope.Index)!=scope.Object || Mirv_Tf2_EntityHandle(scope.Object)!=scope.Handle) return;
 for (auto i=scope.CameraFields.rbegin();i!=scope.CameraFields.rend();++i)
  std::memcpy(static_cast<unsigned char*>(scope.Object)+i->first,&i->second,4);
 scope.CameraFields.clear();
}
bool PovSpectatePlayback()
{
 return (g_SourceSdkVer == SourceSdkVer_TF2 || g_SourceSdkVer == SourceSdkVer_TF2Classified) &&
  g_VEngineClient && g_VEngineClient->IsPlayingDemo() && !g_VEngineClient->IsHLTV();
}
}
void MirvPovSpectate_FrameEnd()
{
 auto saved = std::move(g_PovObserverScope);
 g_PovObserverScope = {};
 PovRestoreCameraFields(saved);
 // Never restore into a deleted entity or a reused player slot.
 if (!saved.Object || Mirv_Tf2_DiagnosticEntity(saved.Index) != saved.Object ||
     Mirv_Tf2_EntityHandle(saved.Object) != saved.Handle) return;
 auto bytes = static_cast<unsigned char *>(saved.Object);
 std::memcpy(bytes + saved.ModeOffset, &saved.Mode, sizeof(saved.Mode));
 std::memcpy(bytes + saved.TargetOffset, &saved.Target, sizeof(saved.Target));
}
bool MirvPovSpectate_SetPlayer(int index)
{
 MirvPovSpectate_FrameEnd();
 if (index == 0) {
  g_PovSpectateUser = 0;
  g_PovThirdPerson = false;
  PovGuardRecorderCamera(false);
  g_PovSpectateStatus = "Native POV spectating is off.";
  return true;
 }
 if (!PovSpectatePlayback()) {
  g_PovSpectateStatus = "Native POV spectating requires a TF2/TF2C POV demo.";
  return false;
 }
 int user = 0;
 auto local = Mirv_Tf2_DiagnosticEntity(g_VEngineClient->GetLocalPlayer());
 const auto fields = PovEntityFields(local);
 auto target = Mirv_Tf2_DiagnosticEntity(index);
 if (index < 1 || !Mirv_Tf2_GetPlayerInfo(index, nullptr, 0, &user) || user <= 0 || !PovTargetAvailable(target) ||
     Mirv_Tf2_EntityHandle(target) == 0xffffffffu || fields.Mode < 0 || fields.Target < 0) {
  g_PovSpectateStatus = "Player or native observer properties are unavailable in this demo.";
  return false;
 }
 g_PovSpectateUser = index == g_VEngineClient->GetLocalPlayer() ? 0 : user;
 PovGuardRecorderCamera(g_PovSpectateUser!=0);
 g_PovSpectateStatus = g_PovSpectateUser ? "Native in-eye POV spectating; no camera attachment." : "Viewing the recorded player.";
 return true;
}
int MirvPovSpectate_Player()
{
 return g_PovSpectateUser ? Mirv_Tf2_FindCameraTarget(std::uint64_t(g_PovSpectateUser), {}) : 0;
}
const char *MirvPovSpectate_Status() { return g_PovSpectateStatus.load(); }
void MirvPovSpectate_FrameBegin()
{
 MirvPovSpectate_FrameEnd();
 if (!g_PovSpectateUser) return;
 if (!PovSpectatePlayback()) { MirvPovSpectate_SetPlayer(0); return; }
 PovGuardRecorderCamera(true);
 const int targetIndex = MirvPovSpectate_Player();
 const int localIndex = g_VEngineClient->GetLocalPlayer();
 auto local = Mirv_Tf2_DiagnosticEntity(localIndex);
 auto target = Mirv_Tf2_DiagnosticEntity(targetIndex);
 const auto fields = PovEntityFields(local);
 if (!local || !PovTargetAvailable(target) || fields.Mode < 0 || fields.Target < 0) {
  g_PovSpectateStatus = "Waiting for the selected player's recorded entity.";
  return;
 }
 SOURCESDK::QAngle look{};
 const auto lookFields = g_PovThirdPerson.load() ? PovEntityFields(target) : PovObserverFields{};
 if (g_PovThirdPerson.load() && (g_PovOriginalAngles.empty() || !PovReadLook(target, look, &lookFields))) {
  g_PovSpectateStatus = "Third-person view needs the engine angle hook and recorded target eye angles.";
  return;
 }
 const unsigned targetHandle = Mirv_Tf2_EntityHandle(target);
 if (targetHandle == 0xffffffffu) return;
 auto bytes = static_cast<unsigned char *>(local);
 auto &saved = g_PovObserverScope;
 saved.Object = local; saved.Index = localIndex; saved.Handle = Mirv_Tf2_EntityHandle(local);
 saved.ModeOffset = fields.Mode; saved.TargetOffset = fields.Target;
 std::memcpy(&saved.Mode, bytes + fields.Mode, sizeof(saved.Mode));
 std::memcpy(&saved.Target, bytes + fields.Target, sizeof(saved.Target));
 const int observerMode = g_PovThirdPerson.load() ? 5 : 4; // OBS_MODE_CHASE / OBS_MODE_IN_EYE.
 std::memcpy(bytes + fields.Mode, &observerMode, sizeof(observerMode));
 std::memcpy(bytes + fields.Target, &targetHandle, sizeof(targetHandle));
 saved.AppliedTarget = targetHandle;
 saved.LookFields = lookFields;
 // CalcInEyeCamView otherwise reads the RECORDER's recoil, zoom and eye height.
 // Supply recorded target inputs only for view setup; restore before tools,
 // animation/simulation and scene-cache capture consume the recorder's data.
 const auto targetFields=PovEntityFields(target);
 int defaultFov=PovReadField(target,targetFields.DefaultFov,90);
 if (defaultFov<1 || defaultFov>179) defaultFov=90;
 int fov=PovReadField(target,targetFields.Fov,0);
 if (fov<0 || fov>179) fov=0;
 saved.CameraFields.reserve(9);
 PovPatchCameraField(saved,fields.DefaultFov,defaultFov);
 PovPatchCameraField(saved,fields.Fov,fov);
 PovPatchCameraField(saved,fields.FovStart,fov?fov:defaultFov);
 for(int j=0;j<3;++j) {
  float punch=PovReadField(target,targetFields.Punch[j],0.0f);
  if(!std::isfinite(punch)) punch=0;
  PovPatchCameraField(saved,fields.Punch[j],punch);
  float height=PovReadField(target,targetFields.ViewOffset[j],j==2?64.0f:0.0f);
  if(!std::isfinite(height) || (j==2&&(height<=0||height>4096))) height=j==2?64.0f:0.0f;
  PovPatchCameraField(saved,fields.ViewOffset[j],height);
 }
 g_PovSpectateStatus = "Native in-eye POV spectating; no camera attachment.";
}

// Supply native chase input at the actual GetViewAngles call, after Source's
// interpolation and before CalcChaseCamView. Never overwrite recorder angle state.
namespace {
bool PovReadLook(void *target, SOURCESDK::QAngle &angles, const PovObserverFields *known)
{
 if (!PovTargetAvailable(target)) return false;
 const auto fields = known ? *known : PovEntityFields(target);
 if (fields.Pitch < 0 || fields.Yaw < 0) return false;
 angles = {};
 std::memcpy(&angles.x, static_cast<const unsigned char *>(target) + fields.Pitch, sizeof(float));
 std::memcpy(&angles.y, static_cast<const unsigned char *>(target) + fields.Yaw, sizeof(float));
 return std::isfinite(angles.x) && std::isfinite(angles.y);
}
void __fastcall PovGetViewAngles(void *self,
#ifndef _WIN64
 void *edx,
#endif
 SOURCESDK::QAngle &angles)
{
 const auto original = g_PovOriginalAngles.find(*static_cast<void ***>(self));
 if (original == g_PovOriginalAngles.end()) { angles = {}; return; }
 original->second(self,
#ifndef _WIN64
 edx,
#endif
 angles);
 const auto &scope = g_PovObserverScope;
 if (!scope.Object || !g_PovThirdPerson.load() || !PovSpectatePlayback()) return;
 auto target = Mirv_Tf2_DiagnosticEntity(MirvPovSpectate_Player());
 if (!target || Mirv_Tf2_EntityHandle(target) != scope.AppliedTarget) return;
 SOURCESDK::QAngle look{};
 if (PovReadLook(target, look, &scope.LookFields)) {
  angles = look;
  g_PovSpectateStatus = "Native third-person chase; following the target's recorded look direction.";
 }
}
}
bool MirvPovSpectate_ThirdPerson() { return g_PovThirdPerson.load(); }
bool MirvPovSpectate_SetMode(bool thirdPerson)
{
 MirvPovSpectate_FrameEnd();
 g_PovThirdPerson = thirdPerson;
 return true;
}
void MirvPovSpectate_InstallEngine(void *engine)
{
 if (!engine) return;
 auto table = *static_cast<void ***>(engine);
 if (g_PovOriginalAngles.count(table)) return; // Versions can alias one vtable.
 // TF2's client requests 014; HLAE may use 015. All share GetViewAngles at 18.
 auto &original = g_PovOriginalAngles[table];
 if (!AfxDetourPtr(reinterpret_cast<PVOID *>(&table[18]), reinterpret_cast<PVOID>(PovGetViewAngles), reinterpret_cast<PVOID *>(&original)))
  g_PovOriginalAngles.erase(table);
}

// Source's local-player visibility and viewmodel selection already consult this
// tools-camera flag. Observer mode alone still treats the recorder as first person.
bool MirvPovSpectate_DrawRecorderWorldModel()
{
 const auto &scope = g_PovObserverScope;
 if (!scope.Object || !PovSpectatePlayback()) return false;
 auto target = Mirv_Tf2_DiagnosticEntity(MirvPovSpectate_Player());
 return target && Mirv_Tf2_EntityHandle(target) == scope.AppliedTarget;
}

// The shared visibility flag also makes the observed player's mesh eligible.
// Retain native in-eye behavior by excluding their world mesh/gear at render time.
int MirvPovSpectate_HiddenPlayer()
{
 return !g_PovThirdPerson.load() && MirvPovSpectate_DrawRecorderWorldModel() ? MirvPovSpectate_Player() : 0;
}

// Tools' SetupEngineView runs after native CalcView and ClientMode::OverrideView.
void MirvPovSpectate_ViewDone() { PovRestoreCameraFields(g_PovObserverScope); }
