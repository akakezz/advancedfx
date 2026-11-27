#pragma once
#include <array>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace advancedfx { namespace SceneLighting {
// Exact Source 2013 LightDesc_t, 88 bytes. Keep enum/flags as raw words;
// do not interpret them as float values. No engine pointers.
using Descriptor=std::array<std::uint32_t,22>;
static_assert(sizeof(Descriptor)==88,"Source light descriptor ABI");
inline float FloatWord(const Descriptor &d,unsigned i){
    float f=0;std::memcpy(&f,&d[i],sizeof(f));return f;
}
inline void SetWord(Descriptor &d,unsigned i,float f){std::memcpy(&d[i],&f,sizeof(f));}
// Native world-light conversion leaves type-unused and private derived words
// uninitialized. Copy the meaningful fields, reconstruct derived values using
// the SDK's LightDesc_t rules, and never retain those uninitialized words.
inline bool Normalize(Descriptor &d){
    if(d[0]<1||d[0]>3)return false;
    for(unsigned i=1;i<=14;++i){
        if(i==11&&d[0]!=3){d[i]=0;continue;}
        if(!std::isfinite(FloatWord(d,i)))return false;
    }
    const float range=FloatWord(d,10);
    const double squared=double(range)*range;
    if(range<0||squared>(std::numeric_limits<float>::max)())return false;
    unsigned flags=8;
    for(unsigned i=12;i<=14;++i)if(FloatWord(d,i)!=0)flags|=1u<<(i-12);
    if(d[19]!=flags)return false;
    if(d[0]==3){
        for(unsigned i=15;i<=18;++i)if(!std::isfinite(FloatWord(d,i)))return false;
        const float theta=FloatWord(d,15),phi=FloatWord(d,16),inner=FloatWord(d,17),outer=FloatWord(d,18);
        if(theta<0||phi<theta||phi>6.283186f||std::abs(inner)>1||std::abs(outer)>1)return false;
        const float spread=inner-outer;
        SetWord(d,20,spread>1e-10f?1.f/spread:1.f);
    }else{
        for(unsigned i=15;i<=18;++i)d[i]=0;
        d[20]=0;
    }
    SetWord(d,21,static_cast<float>(squared));
    return true;
}
// Verified TF2 x64 CStudioRenderContext slots, not guessed from overload order.
inline constexpr int AmbientRgbSlot=16,AmbientRgbaSlot=17;
inline constexpr unsigned AmbientWordCount(int slot){return slot==AmbientRgbSlot?18u:slot==AmbientRgbaSlot?24u:0u;}
struct Sample {
    int AmbientSlot=0,Count=0; // slot 16 = six float3, slot 17 = six float4
    std::array<float,24> Ambient{};
    std::array<Descriptor,4> Lights{};
    unsigned AmbientWords()const{return AmbientWordCount(AmbientSlot);}
    bool Valid()const{
        const auto n=AmbientWords();
        if(!n||Count<0||Count>4)return false;
        for(unsigned i=0;i<n;++i)if(!std::isfinite(Ambient[i])||std::abs(Ambient[i])>1e6f)return false;
        for(int i=0;i<Count;++i){auto copy=Lights[i];if(!Normalize(copy)||copy!=Lights[i])return false;}
        return true;
    }
    bool operator==(const Sample &o)const{
        return AmbientSlot==o.AmbientSlot&&Count==o.Count&&
            !std::memcmp(Ambient.data(),o.Ambient.data(),sizeof(Ambient))&&Lights==o.Lights;
    }
    static bool Capture(int slot,const float *ambient,int count,const void *lights,Sample &out){
        Sample next;next.AmbientSlot=slot;next.Count=count;
        const auto n=next.AmbientWords();
        if(!n||!ambient||count<0||count>4||(count&&!lights))return false;
        std::memcpy(next.Ambient.data(),ambient,n*sizeof(float));
        if(count)std::memcpy(next.Lights.data(),lights,std::size_t(count)*sizeof(Descriptor));
        for(int i=0;i<count;++i)if(!Normalize(next.Lights[i]))return false;
        if(!next.Valid())return false;
        out=next;return true;
    }
};
// Read-only model decal inventory for the actually drawn LOD. Values only;
// no native handles, material pointers, geometry or replay authorization.
struct ModelDecalCoverage {
    int Lod=-1;
    std::uint32_t Batches=0,Vertices=0,Indices=0;
    bool Known()const{return Lod>=0&&Lod<32;}
    bool operator==(const ModelDecalCoverage &o)const{return Lod==o.Lod&&Batches==o.Batches&&Vertices==o.Vertices&&Indices==o.Indices;}
};
struct IgnoreDecalBatch {void operator()(const std::array<unsigned char,0x80>&)const{}};
template<class Read,class Batch=IgnoreDecalBatch>
bool ReadModelDecalCoverage(Read read,std::uint64_t renderer,std::uint64_t hardware,std::uint64_t handle,int lod,ModelDecalCoverage &out,Batch batch={}){

    ModelDecalCoverage next;
    if(lod<0||lod>=32||!hardware||!renderer)return false;
    int hardwareLods=0;
    if(!read(hardware+4,hardwareLods)||hardwareLods<=lod||hardwareLods>32)return false;
    next.Lod=lod;
    if(!handle){out=next;return true;}
    std::uint64_t owner=0,levels=0,pool=0;
    int count=0,capacity=0;
    std::uint16_t active=0,head=0xffff;
    if(!read(handle,owner)||owner!=hardware||!read(handle+8,levels)||!levels||
       !read(handle+0x10,count)||count!=hardwareLods||
       !read(levels+std::uintptr_t(lod)*0x30,head)||
       !read(renderer+0x10,pool)||!read(renderer+0x18,capacity)||capacity<0||capacity>65535||
       !read(renderer+0x26,active)||active>capacity)return false;
    std::array<std::uint16_t,256> visited{};
    auto index=head;
    unsigned traversed=0;
    while(index!=0xffff){
        if(!pool||index>=capacity||traversed>=visited.size()||traversed>=active)return false;
        for(unsigned i=0;i<traversed;++i)if(visited[i]==index)return false;
        visited[traversed++]=index;
        const auto record=pool+std::uintptr_t(index)*0x80;
        std::array<unsigned char,0x80> bytes{};
        if(!read(record,bytes))return false;
        std::uint64_t material=0,indices=0,vertices=0;
        int indexCount=0,indexCapacity=0,vertexCapacity=0;
        std::uint16_t vertexCount=0;
        std::memcpy(&material,bytes.data(),8);
        std::memcpy(&indices,bytes.data()+8,8);
        std::memcpy(&indexCapacity,bytes.data()+0x10,4);
        std::memcpy(&indexCount,bytes.data()+0x18,4);
        std::memcpy(&vertices,bytes.data()+0x28,8);
        std::memcpy(&vertexCapacity,bytes.data()+0x30,4);
        std::memcpy(&vertexCount,bytes.data()+0x3e,2);
        std::memcpy(&index,bytes.data()+0x7a,2);
        if(indexCount<0||indexCount>indexCapacity||indexCount>65535||indexCount%3||
           vertexCapacity<0||vertexCount>vertexCapacity||
           (indexCount&&(!material||!indices||!vertices||!vertexCount)))return false;
        if(indexCount){++next.Batches;next.Indices+=indexCount;next.Vertices+=vertexCount;batch(bytes);}
    }
    // Detect pool/list replacement during observation; this is not a lock.
    std::uint64_t poolAgain=0,levelsAgain=0,ownerAgain=0;
    std::uint16_t headAgain=0xffff,activeAgain=0;
    int countAgain=0,capacityAgain=0;
    if(!read(handle,ownerAgain)||ownerAgain!=owner||!read(handle+8,levelsAgain)||levelsAgain!=levels||
       !read(handle+0x10,countAgain)||countAgain!=count||
       !read(levels+std::uintptr_t(lod)*0x30,headAgain)||headAgain!=head||
       !read(renderer+0x10,poolAgain)||poolAgain!=pool||!read(renderer+0x18,capacityAgain)||capacityAgain!=capacity||
       !read(renderer+0x26,activeAgain)||activeAgain!=active)return false;
    out=next;return true;
}
// Pointer-free native vertex values. Six model selectors include flex
// fields; preserve them without interpreting them as bone indices.
struct ModelDecalVertex {
    std::array<float,8> Values{};
    std::array<std::uint16_t,6> Selectors{};
    bool operator==(const ModelDecalVertex &o)const{return Values==o.Values&&Selectors==o.Selectors;}
};
// Source-vertex inputs for the native multi-bone decal writer. Capture
// only directly mapped fat vertices from a validated studio asset.
struct ModelDecalSkin {
    std::array<float,3> Position{},Normal{},Weights{};
    std::array<unsigned char,3> Bones{};
    unsigned char Count=0;
    bool Valid(int boneCount)const{
        if(boneCount<1||boneCount>128||Count<1||Count>3)return false;
        for(float f:Position)if(!std::isfinite(f)||std::abs(f)>1e7f)return false;
        float len=0;for(float f:Normal){if(!std::isfinite(f)||std::abs(f)>2.f)return false;len+=f*f;}
        if(len<.25f||len>4.f)return false;
        float sum=0;
        for(unsigned i=0;i<Count;++i){
            if(Bones[i]>=boneCount||!std::isfinite(Weights[i])||Weights[i]<0||Weights[i]>1.001f)return false;
            sum+=Weights[i];
        }
        return std::abs(sum-1.f)<.01f;
    }
    bool operator==(const ModelDecalSkin &o)const{return Position==o.Position&&Normal==o.Normal&&Weights==o.Weights&&Bones==o.Bones&&Count==o.Count;}
};
struct ModelDecalBatch {
    std::string Material;
    std::vector<ModelDecalVertex> Vertices;
    std::vector<std::uint16_t> Indices;
    std::vector<ModelDecalSkin> Skin; // empty = unsupported or not captured; otherwise one per vertex
    bool operator==(const ModelDecalBatch &o)const{return Material==o.Material&&Vertices==o.Vertices&&Indices==o.Indices&&Skin==o.Skin;}
};
struct ModelDecalGeometry {
    std::vector<ModelDecalBatch> Batches;
    std::size_t Bytes()const{
        std::size_t n=sizeof(*this)+Batches.capacity()*sizeof(ModelDecalBatch);
        for(const auto &b:Batches)n+=b.Material.capacity()+1+b.Vertices.capacity()*sizeof(ModelDecalVertex)+b.Indices.capacity()*sizeof(std::uint16_t)+b.Skin.capacity()*sizeof(ModelDecalSkin);
        return n;
    }
    bool operator==(const ModelDecalGeometry &o)const{return Batches==o.Batches;}
    bool Valid(const ModelDecalCoverage &coverage)const{
        if(!coverage.Known()||Batches.size()!=coverage.Batches||Batches.size()>64||Bytes()>256*1024)return false;
        std::size_t v=0,i=0;
        for(const auto &b:Batches){
            if(b.Material.empty()||b.Material.size()>255||b.Vertices.empty()||b.Indices.empty()||b.Indices.size()%3)return false;
            v+=b.Vertices.size();i+=b.Indices.size();
            if(v>4096||i>12288)return false;
            for(const auto &vert:b.Vertices)for(float f:vert.Values)if(!std::isfinite(f))return false;
            for(auto index:b.Indices)if(index>=b.Vertices.size())return false;
            if(!b.Skin.empty()&&b.Skin.size()!=b.Vertices.size())return false;
            for(const auto &s:b.Skin)if(!s.Valid(128))return false;
        }
        return v==coverage.Vertices&&i==coverage.Indices;
    }
};
template<class T,std::size_t N> T DecalField(const std::array<unsigned char,N> &b,std::size_t offset){
    T result{};std::memcpy(&result,b.data()+offset,sizeof(result));return result;
}
template<class ReadBytes,class MaterialName>
bool CaptureModelDecalBatch(ReadBytes read,MaterialName name,const std::array<unsigned char,0x80> &record,ModelDecalBatch &out){
    const auto material=DecalField<std::uint64_t>(record,0),indexData=DecalField<std::uint64_t>(record,8),
        vertexData=DecalField<std::uint64_t>(record,0x28),segmentData=DecalField<std::uint64_t>(record,0x50);
    const int ni=DecalField<int>(record,0x18),nvCapacity=DecalField<int>(record,0x30),nsCapacity=DecalField<int>(record,0x58);
    const auto nv=DecalField<std::uint16_t>(record,0x3e),ns=DecalField<std::uint16_t>(record,0x66);
    if(!material||!indexData||!vertexData||!segmentData||ni<=0||ni>12288||ni%3||
       ni>DecalField<int>(record,0x10)||!nv||nv>4096||nv>nvCapacity||nvCapacity>8192||
       !ns||ns>512||ns>nsCapacity||nsCapacity>1024)return false;
    ModelDecalBatch next;
    if(!name(material,next.Material)||next.Material.empty()||next.Material.size()>255)return false;
    std::vector<std::array<unsigned char,0x30>> vertices(nvCapacity);
    std::vector<std::array<unsigned char,0x18>> segments(nsCapacity);
    std::vector<std::uint16_t> indices(ni);
    if(!read(vertexData,vertices.data(),vertices.size()*0x30)||!read(segmentData,segments.data(),segments.size()*0x18)||
       !read(indexData,indices.data(),indices.size()*2))return false;
    next.Vertices.reserve(nv);next.Indices.reserve(ni);
    auto head=DecalField<std::uint16_t>(record,0x38);
    std::uint16_t previous=0xffff;
    for(unsigned n=0;n<nv;++n){
        if(head>=vertices.size())return false;
        const auto &v=vertices[head];
        if(DecalField<std::uint16_t>(v,0x2c)!=previous)return false;
        ModelDecalVertex value;std::memcpy(value.Values.data(),v.data(),32);
        std::memcpy(value.Selectors.data(),v.data()+0x20,12);
        for(float f:value.Values)if(!std::isfinite(f))return false;
        next.Vertices.push_back(value);previous=head;head=DecalField<std::uint16_t>(v,0x2e);
    }
    if(head!=0xffff||previous!=DecalField<std::uint16_t>(record,0x3a))return false;
    head=DecalField<std::uint16_t>(record,0x60);previous=0xffff;
    std::size_t base=0,offset=0;
    for(unsigned n=0;n<ns;++n){
        if(head>=segments.size())return false;
        const auto &seg=segments[head];
        if(DecalField<std::uint16_t>(seg,0x14)!=previous)return false;
        const int ic=DecalField<int>(seg,0),vc=DecalField<int>(seg,4);
        if(ic<=0||ic%3||vc<=0||std::size_t(ic)>indices.size()-offset||std::size_t(vc)>next.Vertices.size()-base)return false;
        for(int j=0;j<ic;++j){const auto index=indices[offset++];if(index>=vc)return false;next.Indices.push_back(std::uint16_t(base+index));}
        base+=vc;previous=head;head=DecalField<std::uint16_t>(seg,0x16);
    }
    if(head!=0xffff||previous!=DecalField<std::uint16_t>(record,0x62)||base!=next.Vertices.size()||offset!=indices.size())return false;
    out=std::move(next);return true;
}
// Verified SDK-2013 x64 studio layout and exact native 0x8180 lookup.
// All relative metadata ranges must remain inside the current IDST asset.
// Never load an asset or call CacheVertexData here.
template<class Read>
bool ReadModelDecalSkin(Read read,std::uint64_t header,std::uint32_t checksum,int body,int bones,
                       const ModelDecalVertex &vertex,ModelDecalSkin &out){
    if(!header||header>(std::numeric_limits<std::uint64_t>::max)()-32*1024*1024||
       body<0||bones<2||bones>128)return false;
    const unsigned vi=vertex.Selectors[0],bi=vertex.Selectors[1],mi=vertex.Selectors[2],si=vertex.Selectors[3];
    if(vi==0xffff||bi==0xffff||mi==0xffff||si==0xffff)return false;
    std::array<std::uint32_t,3> signature{};
    int length=0,nativeBones=0,parts=0,partOffset=0;
    if(!read(header,signature)||signature[0]!=0x54534449||signature[1]!=48||signature[2]!=checksum||
       !read(header+0x4c,length)||length<0x198||length>32*1024*1024||
       !read(header+0x9c,nativeBones)||nativeBones!=bones||
       !read(header+0xe8,parts)||parts<1||parts>128||bi>=static_cast<unsigned>(parts)||
       !read(header+0xec,partOffset)||partOffset<0x198)return false;
    auto inside=[&](std::uint64_t offset,std::uint64_t bytes){return offset<=static_cast<unsigned>(length)&&bytes<=static_cast<unsigned>(length)-offset;};
    if(!inside(partOffset,std::uint64_t(parts)*16))return false;
    const std::uint64_t part=std::uint64_t(partOffset)+bi*16;
    std::array<int,4> p{};
    if(!read(header+part,p)||p[1]<1||p[1]>256||p[2]<=0||p[3]<=0||mi>=static_cast<unsigned>(p[1])||
       static_cast<unsigned>((body/p[2])%p[1])!=mi||!inside(part+std::uint64_t(p[3]),std::uint64_t(p[1])*0x94))return false;
    const auto model=part+std::uint64_t(p[3])+mi*0x94;
    std::array<unsigned char,0x94> m{};
    if(!read(header+model,m))return false;
    const int meshCount=DecalField<int>(m,0x48),meshOffset=DecalField<int>(m,0x4c),
        modelVerts=DecalField<int>(m,0x50),vertexOffset=DecalField<int>(m,0x54);
    if(meshCount<1||meshCount>256||si>=static_cast<unsigned>(meshCount)||meshOffset<=0||
       modelVerts<1||modelVerts>65536||vertexOffset<0||vertexOffset%48||
       !inside(model+std::uint64_t(meshOffset),std::uint64_t(meshCount)*0x74))return false;
    const auto mesh=model+std::uint64_t(meshOffset)+si*0x74;
    std::array<unsigned char,0x74> s{};
    if(!read(header+mesh,s))return false;
    const int modelBack=DecalField<int>(s,4),meshVerts=DecalField<int>(s,8),meshStart=DecalField<int>(s,0xc);
    if(std::int64_t(mesh)+modelBack!=std::int64_t(model)||meshVerts<1||meshVerts>modelVerts||
       vi>=static_cast<unsigned>(meshVerts)||meshStart<0||meshStart>modelVerts-meshVerts||
       DecalField<std::uint64_t>(s,0x54)!=header+model+0x6c)return false;
    const auto data=DecalField<std::uint64_t>(m,0x6c);
    const std::uint64_t offset=std::uint64_t(vertexOffset)+48*(std::uint64_t(meshStart)+vi);
    if(!data||data>(std::numeric_limits<std::uint64_t>::max)()-offset-48)return false;
    std::array<unsigned char,48> raw{};
    if(!read(data+offset,raw))return false;
    ModelDecalSkin next;next.Count=raw[15];
    std::memcpy(next.Weights.data(),raw.data(),12);std::memcpy(next.Bones.data(),raw.data()+12,3);
    std::memcpy(next.Position.data(),raw.data()+16,12);std::memcpy(next.Normal.data(),raw.data()+28,12);
    if(!next.Valid(bones))return false;
    // Discard unused native weight/bone bytes before equality and retention.
    for(unsigned i=next.Count;i<3;++i){next.Weights[i]=0;next.Bones[i]=0;}
    out=next;return true;
}
template<class Read>
bool CaptureModelDecalSkin(Read read,std::uint64_t header,std::uint32_t checksum,int body,int bones,ModelDecalBatch &batch){
    batch.Skin.clear();
    if(batch.Vertices.empty()||batch.Vertices.size()>4096)return false;
    std::vector<ModelDecalSkin> skin;skin.reserve(batch.Vertices.size());
    for(const auto &vertex:batch.Vertices){
        ModelDecalSkin next;
        if(!ReadModelDecalSkin(read,header,checksum,body,bones,vertex,next))return false;
        skin.push_back(next);
    }
    batch.Skin=std::move(skin);return true;
}
// Native software decal position/normal transformation, evaluated against
// the studio renderer's pose-to-world (not uncorrected bone-to-world).
inline bool SkinModelDecalBatch(const ModelDecalBatch &source,const std::array<float,12> *matrices,int bones,ModelDecalBatch &out){
    if(!matrices||bones<2||bones>128||source.Skin.empty()||source.Skin.size()!=source.Vertices.size())return false;
    ModelDecalBatch next;next.Material=source.Material;next.Indices=source.Indices;next.Vertices=source.Vertices;
    for(unsigned i=0;i<source.Skin.size();++i){
        const auto &skin=source.Skin[i];if(!skin.Valid(bones))return false;
        std::array<float,3> pos{},normal{};
        for(unsigned j=0;j<skin.Count;++j){
            const auto &m=matrices[skin.Bones[j]];
            const float weight=skin.Count==1?1.f:skin.Weights[j];
            for(unsigned axis=0;axis<3;++axis){
                const auto off=axis*4;
                float p=m[off+3],n=0;
                for(unsigned k=0;k<3;++k){if(!std::isfinite(m[off+k]))return false;p+=m[off+k]*skin.Position[k];n+=m[off+k]*skin.Normal[k];}
                pos[axis]+=weight*p;normal[axis]+=weight*n;
            }
        }
        const float norm=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
        if(!std::isfinite(norm)||norm<1e-8f)return false;
        for(unsigned axis=0;axis<3;++axis){
            if(skin.Count>1)normal[axis]/=norm;
            pos[axis]+=.1f*normal[axis]; // exact studio software writer constant
            if(!std::isfinite(pos[axis])||std::abs(pos[axis])>1e7f)return false;
            next.Vertices[i].Values[axis]=pos[axis];next.Vertices[i].Values[3+axis]=normal[axis];
        }
    }
    out=std::move(next);return true;
}
// Private native draw-batch storage, never a registered studio decal list.
// Only the rigid/no-flex writer is authorized to consume this representation.
// Flattened historical indices become one native segment, with its base zero.
struct RigidDecalScratch {
    std::vector<std::array<unsigned char,0x30>> Vertices;
    std::vector<std::uint16_t> Indices;
    std::array<unsigned char,0x18> Segment{};
    std::array<unsigned char,0x80> Record{};
    template<std::size_t N,class T> static void Put(std::array<unsigned char,N> &bytes,std::size_t offset,const T &value){
        std::memcpy(bytes.data()+offset,&value,sizeof(value));
    }
    bool Build(const ModelDecalBatch &batch){
        if(batch.Vertices.empty()||batch.Vertices.size()>4096||batch.Indices.empty()||batch.Indices.size()>12288||batch.Indices.size()%3)return false;
        for(const auto &v:batch.Vertices)for(float f:v.Values)if(!std::isfinite(f))return false;
        for(auto i:batch.Indices)if(i>=batch.Vertices.size())return false;
        Vertices.assign(batch.Vertices.size(),{});Indices=batch.Indices;
        for(unsigned i=0;i<Vertices.size();++i){
            auto &v=Vertices[i];const auto &src=batch.Vertices[i];
            std::memcpy(v.data(),src.Values.data(),32);
            std::memcpy(v.data()+0x20,src.Selectors.data(),12);
            Put(v,0x2c,std::uint16_t(i?i-1:0xffff));
            Put(v,0x2e,std::uint16_t(i+1<Vertices.size()?i+1:0xffff));
        }
        Segment={};Put(Segment,0,int(Indices.size()));Put(Segment,4,int(Vertices.size()));
        Put(Segment,0x14,std::uint16_t(0xffff));Put(Segment,0x16,std::uint16_t(0xffff));
        return true;
    }
    // Bind addresses only after final placement, immediately before the
    // synchronous native material-batch call. No queued use or retention.
    void Bind(std::uint64_t material){
        Record={};
        Put(Record,0,material);Put(Record,8,std::uint64_t(reinterpret_cast<std::uintptr_t>(Indices.data())));
        Put(Record,0x10,int(Indices.size()));Put(Record,0x18,int(Indices.size()));
        Put(Record,0x28,std::uint64_t(reinterpret_cast<std::uintptr_t>(Vertices.data())));
        Put(Record,0x30,int(Vertices.size()));Put(Record,0x38,std::uint16_t(0));
        Put(Record,0x3a,std::uint16_t(Vertices.size()-1));Put(Record,0x3e,std::uint16_t(Vertices.size()));
        Put(Record,0x50,std::uint64_t(reinterpret_cast<std::uintptr_t>(&Segment)));
        Put(Record,0x58,int(1));Put(Record,0x60,std::uint16_t(0));Put(Record,0x62,std::uint16_t(0));
        Put(Record,0x66,std::uint16_t(1));Put(Record,0x7a,std::uint16_t(0xffff));
    }
};
inline bool RigidDecalReplayEligible(const ModelDecalCoverage &coverage,int actualLod,int bones,int flexes,bool immediate,bool staticLighting){
    return immediate&&!staticLighting&&coverage.Known()&&coverage.Batches&&coverage.Lod==actualLod&&bones==1&&flexes==0;
}
inline bool SkinnedDecalReplayEligible(const ModelDecalCoverage &coverage,int actualLod,int bones,int flexes,bool immediate,bool staticLighting){
    return immediate&&!staticLighting&&coverage.Known()&&coverage.Batches&&coverage.Lod==actualLod&&
        bones>1&&bones<=128&&flexes>=0&&flexes<=1024;
}
struct Key {
    std::uint32_t Checksum=0;
    int Skin=-1,Body=-1;
    std::array<float,3> Origin{};
    bool Matches(std::uint32_t checksum,int skin,int body,const float *origin)const{
        if(!origin||Checksum!=checksum||Skin!=skin||Body!=body)return false;
        for(unsigned i=0;i<3;++i)if(!std::isfinite(Origin[i])||!std::isfinite(origin[i])||std::abs(Origin[i]-origin[i])>.25f)return false;
        return true;
    }
};
}} // namespace
