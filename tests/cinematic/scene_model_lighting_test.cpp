#include "shared/SceneModelLighting.h"
#include <cassert>
using namespace advancedfx::SceneLighting;
int main(){
    float ambient[24]{};ambient[0]=.4f;ambient[3]=1;
    Descriptor raw{};raw[0]=1;SetWord(raw,1,2);SetWord(raw,4,100);SetWord(raw,10,20);SetWord(raw,12,1);raw[19]=9;
    // The native converter doesn't initialize the unused angular or
    // private derived fields. Never capture them as arbitrary floats.
    raw[11]=raw[15]=raw[16]=raw[17]=raw[18]=raw[20]=raw[21]=0x7fc00000u;
    Sample point;assert(Sample::Capture(16,ambient,1,&raw,point)&&point.Valid());
    assert(FloatWord(point.Lights[0],1)==2&&FloatWord(point.Lights[0],4)==100&&FloatWord(point.Lights[0],21)==400);
    assert(point.Lights[0][11]==0&&point.Lights[0][15]==0&&point.Lights[0][20]==0);
    auto spot=raw;spot[0]=3;SetWord(spot,11,5);SetWord(spot,15,.5f);SetWord(spot,16,1);
    SetWord(spot,17,.875f);SetWord(spot,18,.625f);
    Sample s;assert(Sample::Capture(17,ambient,1,&spot,s)&&s.Valid());
    assert(FloatWord(s.Lights[0],20)==4&&s.AmbientWords()==18);
    auto previous=s;spot[19]=0;assert(!Sample::Capture(17,ambient,1,&spot,s)&&s==previous);
    assert(!Sample::Capture(15,ambient,0,nullptr,s)&&!Sample::Capture(16,ambient,5,&raw,s));
    ambient[0]=NAN;assert(!Sample::Capture(16,ambient,0,nullptr,s));ambient[0]=.4f;
    assert(Sample::Capture(16,ambient,0,nullptr,s)&&s.Valid()&&s.Count==0);
    auto invalid=raw;SetWord(invalid,10,(std::numeric_limits<float>::max)());assert(!Sample::Capture(16,ambient,1,&invalid,s));
    Key key;key.Checksum=12;key.Skin=1;key.Body=2;key.Origin={1,2,3};float near[3]={1.1f,2,3},far[3]={2,2,3};
    assert(key.Matches(12,1,2,near)&&!key.Matches(12,1,2,far)&&!key.Matches(12,2,2,near)&&!key.Matches(13,1,2,near));
}
