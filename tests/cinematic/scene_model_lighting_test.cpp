#include "shared/SceneModelLighting.h"
#include <cassert>
using namespace advancedfx::SceneLighting;
int main(){
    static_assert(AmbientRgbSlot==16&&AmbientRgbaSlot==17,"verified TF2 overload order");
    static_assert(AmbientWordCount(16)==18&&AmbientWordCount(17)==24&&AmbientWordCount(15)==0,"exact native input lengths");
    float rgb[18]{};for(unsigned i=0;i<18;++i)rgb[i]=float(i+1)/64;
    float ambient[24]{};for(unsigned i=0;i<24;++i)ambient[i]=float(i+1)/32;
    Sample rgbOnly;assert(Sample::Capture(16,rgb,0,nullptr,rgbOnly)&&rgbOnly.AmbientWords()==18);
    assert(rgbOnly.Ambient[17]==rgb[17]&&rgbOnly.Ambient[18]==0&&rgbOnly.Ambient[23]==0);
    Sample rgbaOnly;assert(Sample::Capture(17,ambient,0,nullptr,rgbaOnly)&&rgbaOnly.AmbientWords()==24);
    assert(rgbaOnly.Ambient[23]==ambient[23]);
    // Synthetic exact-layout model decal list: two material batches, one empty.
    std::array<unsigned char,0x1000> mem{};
    auto put=[&](std::size_t at,const auto &value){std::memcpy(mem.data()+at,&value,sizeof(value));};
    auto read=[&](std::uintptr_t at,auto &value){
        if(at>mem.size()||sizeof(value)>mem.size()-at)return false;
        std::memcpy(&value,mem.data()+at,sizeof(value));return true;
    };
    const std::uintptr_t renderer=0x100,hardware=0x200,handle=0x300,levels=0x400,pool=0x500;
    put(hardware+4,int(2));put(handle,hardware);put(handle+8,levels);put(handle+0x10,int(2));
    put(levels,std::uint16_t(0));put(levels+0x30,std::uint16_t(0xffff));
    put(renderer+0x10,pool);put(renderer+0x18,int(2));put(renderer+0x26,std::uint16_t(2));
    put(pool,std::uintptr_t(0x900));put(pool+8,std::uintptr_t(0xa00));put(pool+0x10,int(6));put(pool+0x18,int(6));
    put(pool+0x28,std::uintptr_t(0xb00));put(pool+0x30,int(4));put(pool+0x3e,std::uint16_t(4));put(pool+0x7a,std::uint16_t(1));
    put(pool+0x80+0x7a,std::uint16_t(0xffff));
    ModelDecalCoverage decals;
    assert(ReadModelDecalCoverage(read,renderer,hardware,0,0,decals)&&decals.Known()&&!decals.Batches);
    assert(ReadModelDecalCoverage(read,renderer,hardware,handle,0,decals)&&decals.Batches==1&&decals.Vertices==4&&decals.Indices==6);
    auto good=decals;
    put(pool+0x80+0x7a,std::uint16_t(0));assert(!ReadModelDecalCoverage(read,renderer,hardware,handle,0,decals)&&decals==good);
    put(pool+0x80+0x7a,std::uint16_t(0xffff));
    put(pool+0x18,int(7));assert(!ReadModelDecalCoverage(read,renderer,hardware,handle,0,decals));
    assert(ReadModelDecalCoverage(read,renderer,hardware,handle,1,decals)&&decals.Lod==1&&!decals.Batches);
    assert(!ReadModelDecalCoverage(read,renderer,hardware,handle,2,decals));
    // Two decal segments with distinct local index spaces; linked vertices
    // need not be stored contiguously or in numerical index order.
    std::array<unsigned char,0x80> record{};
    auto field=[&](std::size_t at,const auto &v){std::memcpy(record.data()+at,&v,sizeof(v));};
    const std::uint64_t vd=0x600,sd=0x800,id=0x900;
    field(0,std::uint64_t(1));field(8,id);field(0x10,int(6));field(0x18,int(6));
    field(0x28,vd);field(0x30,int(6));field(0x38,std::uint16_t(5));field(0x3a,std::uint16_t(0));field(0x3e,std::uint16_t(6));
    field(0x50,sd);field(0x58,int(2));field(0x60,std::uint16_t(0));field(0x62,std::uint16_t(1));field(0x66,std::uint16_t(2));
    for(unsigned j=0;j<6;++j){
        put(vd+j*0x30,float(j));put(vd+j*0x30+0x20,std::uint16_t(j+1));
        put(vd+j*0x30+0x2c,std::uint16_t(j==5?0xffff:j+1));
        put(vd+j*0x30+0x2e,std::uint16_t(j==0?0xffff:j-1));
    }
    for(unsigned j=0;j<2;++j){
        put(sd+j*0x18,int(3));put(sd+j*0x18+4,int(3));
        put(sd+j*0x18+0x14,std::uint16_t(j==0?0xffff:0));
        put(sd+j*0x18+0x16,std::uint16_t(j==1?0xffff:1));
    }
    for(unsigned j=0;j<6;++j)put(id+j*2,std::uint16_t(j%3));
    auto readBytes=[&](std::uint64_t at,void *data,std::size_t size){if(at>mem.size()||size>mem.size()-at)return false;std::memcpy(data,mem.data()+at,size);return true;};
    auto name=[](std::uint64_t material,std::string &out){if(material!=1)return false;out="decals/test";return true;};
    ModelDecalBatch batch;assert(CaptureModelDecalBatch(readBytes,name,record,batch));
    assert(batch.Vertices.size()==6&&batch.Vertices.front().Values[0]==5&&batch.Vertices.front().Selectors[0]==6);
    assert((batch.Indices==std::vector<std::uint16_t>{0,1,2,3,4,5}));
    ModelDecalGeometry geometry;geometry.Batches.push_back(batch);ModelDecalCoverage cov;cov.Lod=0;cov.Batches=1;cov.Vertices=6;cov.Indices=6;
    assert(geometry.Valid(cov));
    put(id+8,std::uint16_t(3));assert(!CaptureModelDecalBatch(readBytes,name,record,batch)&&batch==geometry.Batches[0]);
    put(id+8,std::uint16_t(1));put(vd+0x2e,std::uint16_t(5));assert(!CaptureModelDecalBatch(readBytes,name,record,batch));
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
    assert(FloatWord(s.Lights[0],20)==4&&s.AmbientWords()==24);
    auto previous=s;spot[19]=0;assert(!Sample::Capture(17,ambient,1,&spot,s)&&s==previous);
    assert(!Sample::Capture(15,ambient,0,nullptr,s)&&!Sample::Capture(16,ambient,5,&raw,s));
    ambient[0]=NAN;assert(!Sample::Capture(16,ambient,0,nullptr,s));ambient[0]=.4f;
    assert(Sample::Capture(16,ambient,0,nullptr,s)&&s.Valid()&&s.Count==0);
    auto invalid=raw;SetWord(invalid,10,(std::numeric_limits<float>::max)());assert(!Sample::Capture(16,ambient,1,&invalid,s));
    Key key;key.Checksum=12;key.Skin=1;key.Body=2;key.Origin={1,2,3};float near[3]={1.1f,2,3},far[3]={2,2,3};
    assert(key.Matches(12,1,2,near)&&!key.Matches(12,1,2,far)&&!key.Matches(12,2,2,near)&&!key.Matches(13,1,2,near));
}
