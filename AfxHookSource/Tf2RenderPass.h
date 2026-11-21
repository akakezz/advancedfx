#pragma once
#include <map>
// The TF2 adapter feeds the existing CAfxStreams capture/output pipeline.
enum class Tf2RenderPass { World, Players, Viewmodel, Hud, Depth, Count };
inline const char *Tf2RenderPassName(Tf2RenderPass pass) {
 static const char *names[] = {"world", "players", "viewmodel", "hud", "depth"};
 return unsigned(pass) < unsigned(Tf2RenderPass::Count) ? names[unsigned(pass)] : "invalid";
}
bool Tf2RenderPassSupported();
void Tf2RenderPassInstallClient(void *client);
void Tf2RenderPassInstallModel(void *model);
void Tf2RenderPassHud(bool begin);
// Engine-thread snapshot, reusing the entity tools list and category metadata.
void Mirv_Tf2_RenderGroups(std::map<const void *, unsigned> &groups);
