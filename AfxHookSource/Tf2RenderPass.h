#pragma once
#include <map>
// The TF2 adapter feeds the existing CAfxStreams capture/output pipeline.
enum class Tf2RenderPass { World, Players, Viewmodel, Hud, Depth, Count };
inline const char *Tf2RenderPassName(Tf2RenderPass pass) {
 static const char *names[] = {"base", "players", "viewmodel", "hud", "depth"};
 return unsigned(pass) < unsigned(Tf2RenderPass::Count) ? names[unsigned(pass)] : "invalid";
}
inline bool Tf2RenderPassIsolated(Tf2RenderPass pass) {
 return pass == Tf2RenderPass::Players || pass == Tf2RenderPass::Viewmodel || pass == Tf2RenderPass::Hud;
}
bool Tf2RenderPassSupported();
void Tf2RenderPassInstallClient(void *client);
void Tf2RenderPassInstallModel(void *model);
void Tf2RenderPassInstallRenderView(void *render);
// Engine-thread snapshot, reusing the entity tools list and category metadata.
// Bits: player=1, viewmodel=2, hide in native in-eye observation=4.
void Mirv_Tf2_RenderGroups(std::map<const void *, unsigned> &groups, int hiddenPlayer = 0, bool markStudio = false);
