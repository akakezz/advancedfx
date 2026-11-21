// Native observer override for POV demos. Called only at render-frame boundaries;
// network updates and the recorded player's state retain their original values.
#include <atomic>
namespace {
struct PovObserverFields { int Mode = -1, Target = -1; };
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
int g_PovSpectateUser = 0;
std::atomic<const char *> g_PovSpectateStatus = "Native POV spectating is off.";
struct PovObserverScope {
 void *Object = nullptr;
 int Index = -1, ModeOffset = -1, TargetOffset = -1, Mode = 0;
 unsigned Handle = 0xffffffffu, Target = 0xffffffffu;
} g_PovObserverScope;
bool PovSpectatePlayback()
{
 return (g_SourceSdkVer == SourceSdkVer_TF2 || g_SourceSdkVer == SourceSdkVer_TF2Classified) &&
  g_VEngineClient && g_VEngineClient->IsPlayingDemo() && !g_VEngineClient->IsHLTV();
}
}
void MirvPovSpectate_FrameEnd()
{
 auto saved = g_PovObserverScope;
 g_PovObserverScope = {};
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
 const int targetIndex = MirvPovSpectate_Player();
 const int localIndex = g_VEngineClient->GetLocalPlayer();
 auto local = Mirv_Tf2_DiagnosticEntity(localIndex);
 auto target = Mirv_Tf2_DiagnosticEntity(targetIndex);
 const auto fields = PovEntityFields(local);
 if (!local || !PovTargetAvailable(target) || fields.Mode < 0 || fields.Target < 0) {
  g_PovSpectateStatus = "Waiting for the selected player's recorded entity.";
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
 const int inEye = 4; // OBS_MODE_IN_EYE, Source SDK shareddefs.h.
 std::memcpy(bytes + fields.Mode, &inEye, sizeof(inEye));
 std::memcpy(bytes + fields.Target, &targetHandle, sizeof(targetHandle));
 g_PovSpectateStatus = "Native in-eye POV spectating; no camera attachment.";
}
