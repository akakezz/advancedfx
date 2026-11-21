// Source SDK 2013 VClient017 / VEngineModel016 adapter. Encoding, capture,
// render targets and D3D state overrides remain owned by existing HLAE systems.
namespace {
#ifdef _WIN64
using Tf2RenderFn = void (*)(void *, const void *, int, int);
using Tf2ModelFn = void (*)(void *, const void *, const void *, void *);
using Tf2FrameFn = void (*)(void *, void *);
using Tf2ViewFn = bool (*)(void *, void *);
#else
using Tf2RenderFn = void (__fastcall *)(void *, void *, const void *, int, int);
using Tf2ModelFn = void (__fastcall *)(void *, void *, const void *, const void *, void *);
using Tf2FrameFn = void (__fastcall *)(void *, void *, void *);
using Tf2ViewFn = bool (__fastcall *)(void *, void *, void *);
#endif
Tf2RenderFn g_Tf2OriginalRender = nullptr;
Tf2ModelFn g_Tf2OriginalModel = nullptr;
Tf2FrameFn g_Tf2OriginalFrame = nullptr;
Tf2ViewFn g_Tf2PlayerView = nullptr;
int g_Tf2CapturedFrame = -1;
Tf2RenderPass g_Tf2ActivePass = Tf2RenderPass::Count;
std::map<const void *, unsigned> g_Tf2RenderGroups;
// Source SDK ModelRenderInfo_t prefix; the rest is forwarded untouched.
struct Tf2ModelInfoPrefix {
 float Origin[3], Angles[3];
 const void *Renderable, *Model, *ModelToWorld, *LightingOffset, *LightingOrigin;
 int Flags, Entity;
};
static_assert(offsetof(Tf2ModelInfoPrefix, Renderable) == 24, "TF2 model info ABI");
class Tf2PassState : public CMaterialSystemFunctor {
 int Op;
public:
 explicit Tf2PassState(int op) : Op(op) {}
 void operator()() override {
  switch (Op) {
  case 0: AfxD3D9PushOverrideState(false); AfxD3D9OverrideBegin_D3DRS_COLORWRITEENABLE(0); break;
  case 1: AfxD3D9OverrideBegin_D3DRS_COLORWRITEENABLE(15); break;
  case 2: AfxD3D9OverrideBegin_D3DRS_COLORWRITEENABLE(0); break;
  case 3: AfxD3D9OverrideEnd_D3DRS_COLORWRITEENABLE(); AfxD3D9PopOverrideState(); break;
  }
 }
};
void Tf2PassColor(int op) { MaterialSystem_ExecuteOnRenderThread(new Tf2PassState(op)); }
void __fastcall Tf2PassDrawModel(void *self,
#ifndef _WIN64
 void *edx,
#endif
 const void *state, const void *info, void *bones)
{
 bool isolate = g_Tf2ActivePass == Tf2RenderPass::Players || g_Tf2ActivePass == Tf2RenderPass::Viewmodel;
 bool draw = true;
 if (g_Tf2ActivePass == Tf2RenderPass::World || isolate) {
  const auto &model = *static_cast<const Tf2ModelInfoPrefix *>(info);
  const auto found = g_Tf2RenderGroups.find(model.Renderable);
  const unsigned group = found == g_Tf2RenderGroups.end() ? 0 : found->second;
  draw = g_Tf2ActivePass == Tf2RenderPass::World ? !(group & 3) :
   g_Tf2ActivePass == Tf2RenderPass::Players ? bool(group & 1) : bool(group & 2);
 }
 if (!draw) return;
 if (isolate) Tf2PassColor(1);
 g_Tf2OriginalModel(self,
#ifndef _WIN64
 edx,
#endif
 state, info, bones);
 if (isolate) Tf2PassColor(2);
}
void __fastcall Tf2PassRenderFrame(void *self,
#ifndef _WIN64
 void *edx,
#endif
 void *rect)
{
 g_Tf2OriginalFrame(self,
#ifndef _WIN64
  edx,
#endif
  rect);
 // Normal TF2 frames enter View_Render, not the tools-only RenderView entry.
 // GetPlayerView copies Source SDK 2013's 200-byte CViewSetup into bounded,
 // generously sized aligned storage; it is forwarded opaquely, never cast to CS:GO.
 alignas(16) unsigned char view[512] = {};
 auto render = [&](int clearFlags, int drawFlags) {
  g_Tf2OriginalRender(self,
#ifndef _WIN64
   edx,
#endif
   view, clearFlags, drawFlags);
 };
 // Nested monitor/portal views retain their parent's pass; never launch another batch.
 if (g_Tf2ActivePass != Tf2RenderPass::Count) return;
 if (!rect) return;
 int rectangle[4]; std::memcpy(rectangle, rect, sizeof(rectangle));
 if (rectangle[2] <= 0 || rectangle[3] <= 0) return;
 bool any = false;
 for (unsigned i = 0; i < unsigned(Tf2RenderPass::Count); ++i)
  any |= g_AfxStreams.Tf2PassRecording(Tf2RenderPass(i));
 if (any && Tf2RenderPassSupported()) {
  auto globals = g_Hook_VClient_RenderView.GetGlobals();
  if (!globals || globals->framecount_get() == g_Tf2CapturedFrame) return;
  if (!g_Tf2PlayerView(self,
#ifndef _WIN64
   edx,
#endif
   view)) return;
  g_Tf2CapturedFrame = globals->framecount_get();
  Mirv_Tf2_RenderGroups(g_Tf2RenderGroups);
  for (unsigned i = 0; i < unsigned(Tf2RenderPass::Count); ++i) {
   const auto pass = Tf2RenderPass(i);
   if (!g_AfxStreams.Tf2PassRecording(pass)) continue;
   g_Tf2ActivePass = pass;
   g_AfxStreams.EngineThread_PassBegin(pass);
   if (pass != Tf2RenderPass::World) Tf2PassColor(0);
   // Clear black in the existing capture target, then retain it as the matte
   // background. World depth still occludes player/viewmodel color normally.
   render(2, (pass == Tf2RenderPass::Hud ? 2 : pass == Tf2RenderPass::Viewmodel ? 1 : 0) | 4);
   if (pass != Tf2RenderPass::World) Tf2PassColor(3);
   g_AfxStreams.EngineThread_PassEnd(pass);
   g_Tf2ActivePass = Tf2RenderPass::Count;
  }
  g_Tf2RenderGroups.clear();
 }
 // Offscreen pass targets are popped before normal screen recording/presentation.
}
}
bool Tf2RenderPassSupported() { return g_Tf2OriginalFrame && g_Tf2OriginalRender && g_Tf2OriginalModel && g_Tf2PlayerView; }
void Tf2RenderPassInstallClient(void *client)
{
 if (!client || g_Tf2OriginalRender) return;
 auto table = *static_cast<void ***>(client);
 g_Tf2OriginalRender = reinterpret_cast<Tf2RenderFn>(table[27]);
 g_Tf2PlayerView = reinterpret_cast<Tf2ViewFn>(table[59]);
 AfxDetourPtr(reinterpret_cast<PVOID *>(&table[26]), reinterpret_cast<PVOID>(Tf2PassRenderFrame), reinterpret_cast<PVOID *>(&g_Tf2OriginalFrame));
}
void Tf2RenderPassInstallModel(void *model)
{
 if (!model || g_Tf2OriginalModel) return;
 auto table = *static_cast<void ***>(model);
 AfxDetourPtr(reinterpret_cast<PVOID *>(&table[19]), reinterpret_cast<PVOID>(Tf2PassDrawModel), reinterpret_cast<PVOID *>(&g_Tf2OriginalModel));
}
void Tf2RenderPassHud(bool begin)
{
 if (g_Tf2ActivePass == Tf2RenderPass::Hud) Tf2PassColor(begin ? 1 : 2);
}
