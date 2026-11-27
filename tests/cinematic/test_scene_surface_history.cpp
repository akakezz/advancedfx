#include "../../shared/SceneSurfaceHistory.h"
#include <cassert>
using namespace advancedfx::SceneSurfaces;
int main(){
    DecalCoverage coverage;coverage.Attached=2;coverage.OutsideWorld=3;coverage.Displacement=4;assert(coverage.Total()==9&&coverage.Describe().find("attached 2")!=std::string::npos);
    Frame f;f.Tick=1;f.World=1;f.DecalsReady=true;
    Decal d;d.Material="decals/test";d.Alpha=123;
    f.Decals=std::make_shared<const std::vector<Decal>>(std::vector<Decal>{d});
    History h;assert(h.Append(f));auto bytes=h.Bytes;
    f.Tick=2;f.Decals=std::make_shared<const std::vector<Decal>>(*f.Decals);
    assert(h.Append(f));assert(h.Bytes==bytes+sizeof(Frame));assert(h.At(1)->Decals==h.At(2)->Decals);
    d.Alpha=0;assert(d.Same(f.Decals->front()));assert(!(d==f.Decals->front()));
    f.Tick=3;f.Decals=std::make_shared<const std::vector<Decal>>(std::vector<Decal>{d});
    assert(h.Append(f));assert(h.Bytes>bytes+2*sizeof(Frame));assert(!h.At(4));
    History small(bytes+sizeof(Frame));f.Tick=1;assert(small.Append(f));f.Tick=2;assert(small.Append(f));f.Tick=3;assert(small.Append(f));assert(small.Frames.size()==2);assert(small.Evicted==1);assert(small.Bytes==bytes+sizeof(Frame));
    f.World=2;f.Tick=1;assert(h.Append(f));assert(h.Frames.size()==1);
    h.Clear();assert(!h.Bytes&&!h.At(1));
    f.Tick=10;assert(h.Append(f));f.Tick=13;assert(h.Append(f));f.Tick=17;assert(h.Append(f));
    assert(!h.At(11)&&h.ForPreview(11)->Tick==10&&h.ForPreview(12)->Tick==10);
    assert(!h.ForPreview(9)&&!h.ForPreview(16)&&!h.ForPreview(18)&&h.ForPreview(13)->Tick==13);
    assert(std::string(DecalReadLabel(DecalRead::Slot))=="slot identity mismatch");
    // Unavailable and oversized captures cannot destroy a valid history.
    const auto retainedBytes=h.Bytes, retainedFrames=h.Frames.size();
    Frame unavailable;unavailable.Tick=17;
    assert(!h.Append(unavailable)&&h.Bytes==retainedBytes&&h.Frames.size()==retainedFrames&&h.At(17));
    unavailable.World=2;assert(!h.Append(unavailable)&&h.At(17));
    unavailable.Tick=1;assert(!h.Append(unavailable)&&h.At(17));
    Frame oversize=f;oversize.Tick=3;oversize.World=99;
    Decal huge;huge.Material=std::string(4096,'x');
    oversize.Decals=std::make_shared<const std::vector<Decal>>(std::vector<Decal>{huge});
    const auto smallBytes=small.Bytes,smallFrames=small.Frames.size();
    assert(!small.Append(oversize)&&small.Bytes==smallBytes&&small.Frames.size()==smallFrames&&small.At(3));
    Shadow sh;assert(!sh.Valid());sh.Size={32,32};sh.Height=100;sh.Leaves={1,2};assert(sh.Valid());
    sh.Leaves.push_back(-1);assert(!sh.Valid());
}
