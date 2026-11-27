// Exact-engine experimental surface history. Ordinary immediate demo views and limited queued decal replay.
// No historical native address is retained in a Frame.
#ifdef _WIN64
#include <intrin.h>
extern bool GetCurrentDemoTick(int &);
namespace {
using namespace advancedfx::SceneSurfaces;
using SurfaceAddress=std::uint64_t;
using ShadowHandle=std::uint16_t;
constexpr ShadowHandle ShadowInvalid=0xffff;
static bool SurfaceRead(SurfaceAddress a,void *out,std::size_t n){
    SIZE_T got=0;return a>=0x10000&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(a),out,n,&got)&&got==n;
}
template<class T> static bool SurfaceGet(SurfaceAddress a,T &v){return SurfaceRead(a,&v,sizeof(v));}
static bool SurfaceMatch(SurfaceAddress base,SurfaceAddress r,std::initializer_list<unsigned char> bytes){
    unsigned char data[32]{};return bytes.size()<=sizeof(data)&&SurfaceRead(base+r,data,bytes.size())&&std::equal(bytes.begin(),bytes.end(),data);
}
static SurfaceAddress SurfaceEngineModule(){
    const auto base=reinterpret_cast<SurfaceAddress>(GetModuleHandleA("engine.dll"));
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(!base||!SurfaceGet(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x1000||
       !SurfaceGet(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||
       nt.FileHeader.TimeDateStamp!=0x6ac41507||nt.OptionalHeader.SizeOfImage!=0x822000)return 0;
    return base;
}
static SurfaceAddress SurfaceEngine(){
    if(g_SourceSdkVer!=SourceSdkVer_TF2||!g_VEngineClient||!g_VEngineClient->IsPlayingDemo()||
       g_VEngineClient->IsSkippingDemo()||g_VEngineClient->IsDrawingLoadingImage()||MirvCampathPreview_IsSeeking())return 0;
    return SurfaceEngineModule();
}
static bool SurfaceMaterialName(SurfaceAddress material,std::string &name){
    SurfaceAddress table=0,fn=0;
    MEMORY_BASIC_INFORMATION mbi{};
    auto module=GetModuleHandleA("materialsystem.dll");
    if(!module||!SurfaceGet(material,table)||!SurfaceGet(table,fn)||!VirtualQuery(reinterpret_cast<void*>(fn),&mbi,sizeof(mbi))||
       mbi.AllocationBase!=module||!(mbi.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
    using Name=const char*(*)(void*);
    auto ptr=reinterpret_cast<Name>(fn)(reinterpret_cast<void*>(material));
    const auto address=reinterpret_cast<SurfaceAddress>(ptr);name.clear();
    // Read bounded chunks rather than one system call per character. If a
    // chunk crosses unreadable memory, fall back to a single byte so a
    // valid terminator just before that boundary remains accepted.
    for(unsigned i=0;i<260;){
        char bytes[32]{};
        auto n=(std::min)(unsigned(sizeof(bytes)),260-i);
        if(!SurfaceRead(address+i,bytes,n)){n=1;if(!SurfaceRead(address+i,bytes,n))return false;}
        for(unsigned j=0;j<n;++j){
            const auto ch=static_cast<unsigned char>(bytes[j]);
            if(!ch)return !name.empty();
            if(ch<32||ch>126)return false;
            name+=bytes[j];
        }
        i+=n;
    }
    return false;
}
struct NativeWorld {SurfaceAddress World=0,Surfaces=0;int Count=0;};
static bool SurfaceWorld(SurfaceAddress base,NativeWorld &w){
    return SurfaceGet(base+0x755738,w.World)&&w.World&&SurfaceGet(w.World+0xe8,w.Surfaces)&&w.Surfaces&&SurfaceGet(w.World+0xd8,w.Count)&&w.Count>0&&w.Count<=1048576;
}
static NativeWorld g_SurfaceWorld;
static std::uint64_t g_SurfaceGeneration=1;
static bool SurfaceSameWorld(const NativeWorld &a,const NativeWorld &b){return a.World==b.World&&a.Surfaces==b.Surfaces&&a.Count==b.Count;}
// Read the exact client-state clock used by both native creation and
// ordinary decal fading. No engine call or global clock mutation.
static bool SurfaceDecalClock(SurfaceAddress base,float &now){
    if(!SurfaceMatch(base,0xa4320,{0x80,0xb9,0x64,0x8c,0,0,0,0x66,0x0f,0x6e,0x81,0xb4,0x01,0,0,0x0f,0x5b,0xc0,0xf3,0x0f,0x59,0x05,0x06,0x14,0x6b,0,0x75,0x08})||
       !SurfaceMatch(base,0xa433c,{0xf3,0x0f,0x58,0x81,0x6c,0x8c,0,0,0xc3})||
       !SurfaceMatch(base,0x11ac29,{0x48,0x8d,0x0d,0x30,0xdc,0x41,0,0xe8,0xeb,0x96,0xf8,0xff})||
       !SurfaceMatch(base,0x11b33c,{0xb9,0,0x01,0,0,0x66,0x85,0x4b,0x68,0x74,0x5f,0xf3,0x0f,0x10,0x53,0x4c}))return false;
    int tick=0;float interval=0,delta=0;unsigned char mode=0;
    const auto state=base+0x538860;
    if(!SurfaceGet(state+0x1b4,tick)||!SurfaceGet(base+0x755740,interval)||!SurfaceGet(state+0x8c64,mode)||
       !std::isfinite(interval)||interval<=0||interval>1||tick<0)return false;
    if(!mode&&(!SurfaceGet(state+0x8c6c,delta)||!std::isfinite(delta)))return false;
    now=float(tick)*interval+delta;return std::isfinite(now);
}
struct LiveDecal {Decal Value;SurfaceAddress Object=0,Surface=0,Material=0;bool GeometryEligible=false;};
static bool ReadWorldDecals(SurfaceAddress base,const NativeWorld &world,std::vector<LiveDecal> &out,std::size_t &unsupported,DecalRead &status,DecalCoverage &coverage,std::set<std::uint16_t> *excludedSlots=nullptr){
    coverage={};unsupported=0;
    status=DecalRead::Guard;
    if(!SurfaceMatch(base,0x117c42,{0x48,0x89,0x04,0xd1,0x66,0x89,0x58,0x66})||
       !SurfaceMatch(base,0x118700,{0x48,0x8b,0x80,0xe8,0x00,0x00,0x00,0x48,0x8d,0x49,0x40})||
       !SurfaceMatch(base,0x11677c,{0x48,0x8b,0x4a,0x18,0x48,0x8b,0x01,0xff,0x10})||
       !SurfaceMatch(base,0x11b326,{0x0f,0xb6,0x43,0x57,0x74,0x10}))return false;
    status=DecalRead::Table;
    SurfaceAddress table=0;int count=0,capacity=0;
    if(!SurfaceGet(base+0x6a4e60,table)||!SurfaceGet(base+0x6a4e70,count)||!SurfaceGet(base+0x6a4e68,capacity)||
       count<0||count>8192||capacity<count||(count&&!table))return false;
    std::vector<SurfaceAddress> slots(count);
    if(count&&!SurfaceRead(table,slots.data(),slots.size()*sizeof(SurfaceAddress)))return false;
    std::map<SurfaceAddress,std::string> materials;
    std::map<SurfaceAddress,std::uint32_t> surfaceFlagsCache;
    bool clockChecked=false,clockReady=false;float clock=0;
    for(int i=0;i<count;++i){
        auto a=slots[i];if(!a)continue;
        std::array<unsigned char,0x6c> raw{};
        status=DecalRead::Record;
        if(!SurfaceGet(a,raw))return false;
        auto get=[&](std::size_t off,auto &v){std::memcpy(&v,raw.data()+off,sizeof(v));};
        LiveDecal live;auto &v=live.Value;live.Object=a;
        SurfaceAddress surface=0,material=0,attachment=0;
        get(0x10,surface);get(0x18,material);get(0x58,attachment);get(0x66,v.Slot);get(0x68,v.Flags);get(0x6a,v.NameIndex);
        if(v.Slot!=i){status=DecalRead::Slot;return false;}
        if(excludedSlots)excludedSlots->insert(v.Slot);
        if(attachment){++unsupported;++coverage.Attached;continue;}
        if(v.Flags&0x1000){++unsupported;++coverage.Flagged;continue;}
        if(!surface||surface<world.Surfaces||(surface-world.Surfaces)%64||(surface-world.Surfaces)/64>=static_cast<unsigned>(world.Count)){
            ++unsupported;++coverage.OutsideWorld;continue;
        }
        auto sf=surfaceFlagsCache.try_emplace(surface,0);
        if(sf.second&&!SurfaceGet(surface,sf.first->second)){status=DecalRead::Surface;return false;}
        if(sf.first->second&0x800){++unsupported;++coverage.Displacement;continue;}
        v.Surface=static_cast<std::uint32_t>((surface-world.Surfaces)/64);
        std::memcpy(v.Placement.data(),raw.data()+0x24,12);
        if(v.Flags&0x80)std::memcpy(v.Placement.data()+3,raw.data()+0x30,12);
        get(0x44,v.Placement[6]);get(0x54,v.Color);get(0x57,v.Alpha);get(0x20,v.LightmapOffset);
        live.GeometryEligible=!(v.Flags&0x3000);
        if(v.Fading()){
            if(!clockChecked){clockChecked=true;clockReady=SurfaceDecalClock(base,clock);}
            float duration=0,start=0;get(0x4c,duration);get(0x50,start);
            if(!clockReady||!v.SetFade(duration,start,clock)){++unsupported;++coverage.FadeInvalid;continue;}
        }
        v.Flags&=0x0181; // retain fade identity, exclude transient draw/clip flags
        auto found=materials.find(material);
        if(found==materials.end()){
            std::string name;if(!SurfaceMaterialName(material,name)){++unsupported;++coverage.Material;continue;}
            found=materials.emplace(material,std::move(name)).first;
        }
        v.Material=found->second;
        live.Surface=surface;live.Material=material;
        if(excludedSlots)excludedSlots->erase(v.Slot);
        out.push_back(std::move(live));
    }
    SurfaceAddress after=0;int n=0;NativeWorld now;
    const bool stable=SurfaceGet(base+0x6a4e60,after)&&after==table&&SurfaceGet(base+0x6a4e70,n)&&n==count&&SurfaceWorld(base,now)&&SurfaceSameWorld(now,world);
    status=stable?DecalRead::Ready:DecalRead::Changed;return stable;
}
// Native engine shadow manager, SDK VEngineShadowMgr002; exact vtable and
// project/destroy/bias function bodies checked before installing observers.
using ProjectFn=void(*)(void*,ShadowHandle,const float*,const float*,const float*,const float*,int,const int*,float,float,float,const float*);
using DestroyFn=void(*)(void*,ShadowHandle);
using BiasFn=void(*)(void*,ShadowHandle,unsigned char);
using CreateFn=ShadowHandle(*)(void*,void*,void*,void*,int);
using TexFn=void(*)(void*,ShadowHandle,float,float,float,float);
using ClearClipFn=void(*)(void*,ShadowHandle);
using AddClipFn=void(*)(void*,ShadowHandle,const float*,float);
static void *g_SurfaceShadowManager=nullptr;
static ProjectFn g_SurfaceProject=nullptr;
static DestroyFn g_SurfaceDestroy=nullptr;
static BiasFn g_SurfaceBias=nullptr;
static DWORD g_SurfaceThread=0;
static bool g_SurfaceHooks=false,g_SurfaceOwn=false,g_SurfaceReplayActive=false,g_SurfaceCaptureArmed=false;
static std::map<ShadowHandle,Shadow> g_SurfaceObserved;
static bool g_SurfaceAtlasRequested=false,g_SurfaceAtlasArmed=false;
static std::uint64_t g_SurfaceShadowRevision=0;
static void SurfaceClearAtlasObservation();
static std::map<ShadowHandle,unsigned char> g_SurfaceSavedBias;
static std::vector<ShadowHandle> g_SurfaceOwned;
static std::string g_SurfaceReport;
// Return null from the native decal geometry lookup only for future-only
// surviving records in an active ordinary preview. No live record writes.
// Restrict to verified draw callsites; geometry callers outside drawing
// must retain their original behavior.
using DecalGeometryFn=void*(*)(void*,void*,void*,void*);
static DecalGeometryFn g_SurfaceDecalGeometry=nullptr;
static SurfaceAddress g_SurfaceDecalBase=0;
static DWORD g_SurfaceDecalThread=0;
static bool g_SurfaceDecalHookInstalled=false;
static thread_local bool g_SurfaceDecalGate=false;
struct DecalGate {SurfaceAddress Surface=0,Material=0;std::uint16_t Slot=0;};
static thread_local std::map<SurfaceAddress,DecalGate> g_SurfaceHiddenDecals;
// Queued visibility uses only the engine's producer-side gather vector.
// Never pass a cache pointer, temporary record or historical geometry to
// the native queue. Fading marks retain their native lifecycle bookkeeping.
using DecalGatherFn=void(*)(void*,int,int,void*);
static DecalGatherFn g_SurfaceGatherPlain=nullptr,g_SurfaceGatherSorted=nullptr;
static bool g_SurfaceGatherInstalled=false;
static thread_local bool g_SurfaceQueueRequested=false;
static thread_local int g_SurfaceQueuedGroup=-1;
static thread_local std::map<SurfaceAddress,LiveDecal> g_SurfaceQueuedHidden;
// Material witnesses belong only to this ordinary view; native live records
// already retain these materials. Never resolve/load an absent material for queued replay.
static thread_local std::map<std::string,LiveDecal> g_SurfaceQueuedMaterials;
static std::size_t g_SurfaceQueuedRemoved=0,g_SurfaceQueuedRejected=0,g_SurfaceQueuedFades=0,g_SurfaceQueuedGroups=0;
static thread_local std::size_t g_SurfaceDecalDrawSkips=0;
// Observe only ordinary immediate main-world geometry dispatch. Native
// addresses here are transient observations, discarded at capture end.
using DecalDispatchFn=void(*)(void*,int,float);
using DecalDrawFn=void(*)(const void*,void*,void*,int,const void*,float);
static DecalDispatchFn g_SurfaceDecalDispatch=nullptr;
static bool g_SurfaceDispatchInstalled=false;
using WorldDrawFn=void(*)(void*,int,float);
static WorldDrawFn g_SurfaceWorldDraw=nullptr;
static bool g_SurfaceWorldDrawInstalled=false;
static thread_local SurfaceAddress g_SurfaceCurrentWorldList=0;
static thread_local int g_SurfaceGeometryGroup=-1;
struct DecalObservation {
    SurfaceAddress Object=0,Surface=0,Material=0;
    std::array<unsigned char,0x6c> Raw{};
    int Group=-1;
    std::shared_ptr<const std::vector<DecalVertex>> Geometry;
};
static std::map<std::uint16_t,DecalObservation> g_SurfaceDecalObservations;
static std::size_t g_SurfaceObservedVertices=0;
static std::shared_ptr<const std::vector<Decal>> g_SurfaceLastDecals;
struct PendingDecal {
    Decal Value;
    SurfaceAddress LiveObject=0;
    DecalGate LiveGate;
    bool LiveVisible=false;
    LiveDecal LiveWitness; // populated only for a queued surviving replacement
};
struct PreparedDecal {
    const PendingDecal *Pending=nullptr;
    SurfaceAddress Surface=0;
    void *Material=nullptr;
    bool Suppressed=false;
};
static std::vector<PendingDecal> g_SurfacePendingDecals;
// A pointer to the current dispatch's stack-local map, never retained.
static thread_local std::map<SurfaceAddress,PreparedDecal*> *g_SurfaceReplacementGates=nullptr;
struct QueuedReplacementScope {
    std::map<SurfaceAddress,PreparedDecal*> Items;
    QueuedReplacementCoverage Coverage;
};
static thread_local QueuedReplacementScope *g_SurfaceQueuedReplacementScope=nullptr;
static unsigned g_SurfaceDrawnGroups=0;
static std::size_t g_SurfaceDecalsSubmitted=0,g_SurfaceDecalsRejected=0,g_SurfaceOpacitySubmitted=0,g_SurfaceOpacityNotObserved=0;
static std::size_t g_SurfaceReceiverCulled=0,g_SurfaceReceiverUnknown=0;
static void SurfaceClearGeometryObservations(){
    g_SurfaceDecalObservations.clear();g_SurfaceObservedVertices=0;
}
static void SurfaceResetGeometry(){
    SurfaceClearGeometryObservations();g_SurfaceLastDecals.reset();SurfaceClearAtlasObservation();
}
static bool SurfaceObservationMatches(const DecalObservation &o,const LiveDecal &live){
    if(o.Object!=live.Object||o.Surface!=live.Surface||o.Material!=live.Material)return false;
    const auto &d=live.Value;const auto *raw=o.Raw.data();
    auto eq=[&](std::size_t offset,const auto &v){return !std::memcmp(raw+offset,&v,sizeof(v));};
    std::uint16_t flags=0;std::memcpy(&flags,raw+0x68,sizeof(flags));
    return (flags&0x0181)==d.Flags&&eq(0x66,d.Slot)&&eq(0x6a,d.NameIndex)&&eq(0x20,d.LightmapOffset)&&
        !std::memcmp(raw+0x24,d.Placement.data(),12)&&(!(flags&0x80)||!std::memcmp(raw+0x30,d.Placement.data()+3,12))&&
        eq(0x44,d.Placement[6])&&eq(0x54,d.Color)&&
        (!d.Fading()||(eq(0x4c,d.FadeDuration)&&eq(0x50,d.FadeStart)));
}
static void SurfaceObserveDecal(void *geometry,void *decal,void *surface,void *material){
    if(!geometry||g_SurfaceGeometryGroup<0||g_SurfaceGeometryGroup>=3||g_SurfaceObservedVertices>=65536)return;
    const auto a=reinterpret_cast<SurfaceAddress>(decal),s=reinterpret_cast<SurfaceAddress>(surface);
    if(!g_SurfaceWorld.Surfaces||s<g_SurfaceWorld.Surfaces||(s-g_SurfaceWorld.Surfaces)%64||
       (s-g_SurfaceWorld.Surfaces)/64>=static_cast<unsigned>(g_SurfaceWorld.Count))return;
    DecalObservation o;o.Object=a;o.Surface=s;o.Material=reinterpret_cast<SurfaceAddress>(material);o.Group=g_SurfaceGeometryGroup;
    if(!SurfaceGet(a,o.Raw))return;
    auto get=[&](std::size_t off,auto &v){std::memcpy(&v,o.Raw.data()+off,sizeof(v));};
    std::uint16_t flags=0,slot=0,count=0;SurfaceAddress receiver=0,mat=0,attachment=0;
    get(0x10,receiver);get(0x18,mat);get(0x58,attachment);get(0x62,count);get(0x66,slot);get(0x68,flags);
    if(receiver!=s||mat!=o.Material||attachment||(flags&0x3000)||count<3||count>64||
       g_SurfaceDecalObservations.count(slot)||count>65536-g_SurfaceObservedVertices)return;
    auto vertices=std::make_shared<std::vector<DecalVertex>>(count);
    if(!SurfaceRead(reinterpret_cast<SurfaceAddress>(geometry),vertices->data(),count*sizeof(DecalVertex)))return;
    for(auto &v:*vertices)v[3]=0; // word not consumed by verified draw
    if(!ValidDecalGeometry(*vertices))return;
    o.Geometry=vertices;g_SurfaceObservedVertices+=count;g_SurfaceDecalObservations.emplace(slot,std::move(o));
}
static void *SurfaceDecalGeometryHook(void *scratch,void *decal,void *surface,void *material){
    if(g_SurfaceDecalGate&&GetCurrentThreadId()==g_SurfaceDecalThread){
        const auto caller=reinterpret_cast<SurfaceAddress>(_ReturnAddress())-g_SurfaceDecalBase;
        if(caller==0x116fe9||caller==0x11ae61||caller==0x11bd83||caller==0x11c96c||caller==0x11d708){
            const auto a=reinterpret_cast<SurfaceAddress>(decal);
            auto it=g_SurfaceHiddenDecals.find(a);
            std::uint16_t slot=0;
            if(it!=g_SurfaceHiddenDecals.end()&&it->second.Surface==reinterpret_cast<SurfaceAddress>(surface)&&
               it->second.Material==reinterpret_cast<SurfaceAddress>(material)&&SurfaceGet(a+0x66,slot)&&slot==it->second.Slot){
                ++g_SurfaceDecalDrawSkips;return nullptr;
            }
            if(g_SurfaceReplacementGates){
                auto replacement=g_SurfaceReplacementGates->find(a);
                if(replacement!=g_SurfaceReplacementGates->end()){
                    auto *prepared=replacement->second;
                    const auto &gate=prepared->Pending->LiveGate;
                    if(gate.Surface==reinterpret_cast<SurfaceAddress>(surface)&&gate.Material==reinterpret_cast<SurfaceAddress>(material)&&
                       SurfaceGet(a+0x66,slot)&&slot==gate.Slot){
                        prepared->Suppressed=true;++g_SurfaceDecalDrawSkips;return nullptr;
                    }
                }
            }
        }
    }
    auto geometry=g_SurfaceDecalGeometry(scratch,decal,surface,material);
    if(g_SurfaceGeometryGroup>=0&&GetCurrentThreadId()==g_SurfaceDecalThread&&g_SurfaceCaptureArmed&&!g_SurfaceReplayActive)
        SurfaceObserveDecal(geometry,decal,surface,material);
    return geometry;
}
// Only the exact verified scratch vector can be edited, and only while
// the native producer owns it. Require committed writable pages across
// the entire destination; never attempt partial system writes.
static bool SurfaceWritable(SurfaceAddress address,std::size_t bytes){
    if(!bytes)return true;
    const auto max=(std::numeric_limits<SurfaceAddress>::max)();
    if(address<0x10000||address>max-bytes)return false;
    const auto end=address+bytes;
    while(address<end){
        MEMORY_BASIC_INFORMATION mbi{};
        if(!VirtualQuery(reinterpret_cast<const void*>(address),&mbi,sizeof(mbi))||mbi.State!=MEM_COMMIT||
           (mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS))||
           !(mbi.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
        const auto start=reinterpret_cast<SurfaceAddress>(mbi.BaseAddress);
        if(start>address||start>max-mbi.RegionSize||start+mbi.RegionSize<=address)return false;
        address=start+mbi.RegionSize;
    }
    return true;
}
struct SurfaceGatherVector {
    SurfaceAddress Data=0;
    int Capacity=0,Grow=0,Count=0,Padding=0;
    SurfaceAddress Elements=0;
};
static_assert(sizeof(SurfaceGatherVector)==32&&offsetof(SurfaceGatherVector,Count)==16&&offsetof(SurfaceGatherVector,Elements)==24,"native gather vector ABI");
static bool SurfaceGatherRead(SurfaceAddress address,SurfaceGatherVector &v){
    return SurfaceGet(address,v)&&v.Count>=0&&v.Capacity>=v.Count&&v.Capacity<=262144&&v.Count<=65536&&
        (!v.Count||(v.Data>=0x10000&&v.Data==v.Elements&&v.Data<=(std::numeric_limits<SurfaceAddress>::max)()-std::uint64_t(v.Count)*8));
}
static bool SurfaceQueuedLiveIdentity(const LiveDecal &live){
    const auto address=live.Object;
    SurfaceAddress table=0,actual=0;int count=0;
    // A readable old allocation is not proof that it is still a live slot.
    if(!address||!SurfaceGet(g_SurfaceDecalBase+0x6a4e60,table)||!table||
       !SurfaceGet(g_SurfaceDecalBase+0x6a4e70,count)||count<0||count>8192||
       live.Value.Slot>=count||!SurfaceGet(table+SurfaceAddress(live.Value.Slot)*8,actual)||actual!=address)return false;
    DecalObservation o;o.Object=address;o.Surface=live.Surface;o.Material=live.Material;
    if(!SurfaceGet(address,o.Raw))return false;
    SurfaceAddress attachment=0,receiver=0,material=0;std::uint16_t flags=0;
    std::memcpy(&attachment,o.Raw.data()+0x58,8);std::memcpy(&receiver,o.Raw.data()+0x10,8);
    std::memcpy(&material,o.Raw.data()+0x18,8);std::memcpy(&flags,o.Raw.data()+0x68,2);
    return !attachment&&receiver==live.Surface&&material==live.Material&&!(flags&0x3100)&&SurfaceObservationMatches(o,live);
}
static bool SurfaceQueuedReplacementIdentity(const LiveDecal &live){
    unsigned char alpha=0;
    return !live.Value.Fading()&&SurfaceQueuedLiveIdentity(live)&&SurfaceGet(live.Object+0x57,alpha)&&alpha==live.Value.Alpha;
}
static bool SurfaceQueuedIdentity(SurfaceAddress address){
    auto it=g_SurfaceQueuedHidden.find(address);
    return it!=g_SurfaceQueuedHidden.end()&&SurfaceQueuedLiveIdentity(it->second);
}
static void SurfaceGather(DecalGatherFn original,bool sorted,SurfaceAddress caller,void *context,int group,int category,void *vector){
    const auto base=g_SurfaceDecalBase,address=reinterpret_cast<SurfaceAddress>(vector);
    const bool callsite=sorted?(category==0?caller==base+0x116bb5:category==1?caller==base+0x116bd3:category==2&&caller==base+0x116bf4):
                               (category==0?caller==base+0x116bfb:category==1?caller==base+0x116c19:category==2&&caller==base+0x116c3a);
    SurfaceGatherVector before,after;
    const bool scoped=GetCurrentThreadId()==g_SurfaceDecalThread&&g_SurfaceQueuedGroup==group&&group>=0&&group<3&&
        callsite&&address==base+0x6c55b8;
    auto *replacements=scoped?g_SurfaceQueuedReplacementScope:nullptr;
    const bool candidate=scoped&&(!g_SurfaceQueuedHidden.empty()||(replacements&&!replacements->Items.empty()));
    const bool eligible=candidate&&SurfaceGatherRead(address,before);
    auto reject=[&](){++g_SurfaceQueuedRejected;if(replacements)replacements->Coverage.Observe(category,false);};
    auto accepted=[&](){if(replacements)replacements->Coverage.Observe(category,true);};
    original(context,group,category,vector);
    if(!eligible){if(candidate)reject();return;}
    // The original collector may grow its allocation. Re-read the header
    // and copy just this category's newly appended tail. Prior categories
    // were already counted by the native dispatcher and must stay intact.
    NativeWorld world;
    if(!SurfaceWorld(base,world)||!SurfaceSameWorld(world,g_SurfaceWorld)||!SurfaceGatherRead(address,after)||after.Count<before.Count){reject();return;}
    if(after.Count==before.Count){accepted();return;}
    const auto tail=after.Data+std::uint64_t(before.Count)*8;
    std::vector<SurfaceAddress> input(after.Count-before.Count),output;
    if(!SurfaceRead(tail,input.data(),input.size()*8)){reject();return;}
    std::size_t removed=0;
    std::vector<PreparedDecal*> replaced;
    bool invalidReplacement=false;
    auto hide=[&](SurfaceAddress a){
        if(replacements&&!replacements->Coverage.Failed){
            const auto it=replacements->Items.find(a);
            if(it!=replacements->Items.end()){
                if(!SurfaceQueuedReplacementIdentity(it->second->Pending->LiveWitness)){invalidReplacement=true;return false;}
                replaced.push_back(it->second);return true;
            }
        }
        return SurfaceQueuedIdentity(a);
    };
    // Merely visiting a candidate in the parser does not prove removal.
    // Set Suppressed only after the validated tail and count are committed.
    if(!FilterQueuedDecalTail(input,sorted,hide,output,removed)||invalidReplacement){reject();return;}
    if(!removed){accepted();return;}
    SurfaceGatherVector check;
    if(output.size()>input.size()||!SurfaceGatherRead(address,check)||check.Data!=after.Data||check.Elements!=after.Elements||
       check.Count!=after.Count||check.Capacity!=after.Capacity||!SurfaceWritable(address+16,sizeof(int))||
       !SurfaceWritable(tail,output.size()*8)){reject();return;}
    if(!output.empty())std::memcpy(reinterpret_cast<void*>(tail),output.data(),output.size()*8);
    const int count=before.Count+static_cast<int>(output.size());
    std::memcpy(reinterpret_cast<void*>(address+16),&count,sizeof(count));
    g_SurfaceQueuedRemoved+=removed;
    for(auto *item:replaced)item->Suppressed=true;
    accepted();
}
static void SurfaceGatherPlainHook(void *context,int group,int category,void *vector){
    SurfaceGather(g_SurfaceGatherPlain,false,reinterpret_cast<SurfaceAddress>(_ReturnAddress()),context,group,category,vector);
}
static void SurfaceGatherSortedHook(void *context,int group,int category,void *vector){
    SurfaceGather(g_SurfaceGatherSorted,true,reinterpret_cast<SurfaceAddress>(_ReturnAddress()),context,group,category,vector);
}
static bool SurfaceQueuedContext(void *context){
    const auto base=reinterpret_cast<SurfaceAddress>(GetModuleHandleA("materialsystem.dll"));
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};SurfaceAddress vt=0,slot=0;
    return base&&SurfaceGet(base,dos)&&dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0&&dos.e_lfanew<=0x1000&&
        SurfaceGet(base+dos.e_lfanew,nt)&&nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&
        nt.FileHeader.TimeDateStamp==0x6ac56a08&&nt.OptionalHeader.SizeOfImage==0x141000&&
        SurfaceGet(reinterpret_cast<SurfaceAddress>(context),vt)&&vt==base+0xb6c28&&SurfaceGet(vt+0x498,slot)&&slot==base+0x2c4a0&&
        SurfaceMatch(base,0x2c4a0,{0x48,0x8d,0x81,0xa8,0x03,0,0,0xc3});
}
static bool SurfaceQueuedGatherHooks(SurfaceAddress base){
    if(g_SurfaceGatherInstalled)return base==g_SurfaceDecalBase&&GetCurrentThreadId()==g_SurfaceDecalThread;
    if(!SurfaceMatch(base,0x116b23,{0xff,0x90,0x98,0x04,0,0,0x4c,0x8b,0xf0})||
       !SurfaceMatch(base,0x11c670,{0x48,0x8b,0xc4,0x53,0x48,0x83,0xec,0x50,0x4c,0x63,0x15,0x29,0x8e,0x58,0})||
       !SurfaceMatch(base,0x11c749,{0xff,0x43,0x10,0x8b,0x43,0x10,0x48,0x8b,0x0b,0x2b,0xc6,0xff,0xc8,0x48,0x89,0x4b,0x18})||
       !SurfaceMatch(base,0x11c775,{0x48,0x8b,0x03,0x48,0x89,0x2c,0xf0})||
       !SurfaceMatch(base,0x11d220,{0x4c,0x8b,0xdc,0x53,0x55,0x48,0x83,0xec,0x58,0x48,0x63,0x05,0x78,0x82,0x58,0})||
       !SurfaceMatch(base,0x11d2d7,{0x48,0x8b,0x03,0x48,0xc7,0x04,0xf8,0,0,0,0})||
       !SurfaceMatch(base,0x11d398,{0x48,0x8b,0x03,0xba,0xff,0xff,0xff,0xff,0x48,0x89,0x14,0xf0})||
       !SurfaceMatch(base,0x11d3f9,{0x48,0x8b,0x03,0x48,0x89,0x2c,0xf0})||
       !SurfaceMatch(base,0x116bb0,{0xe8,0x6b,0x66,0,0})||!SurfaceMatch(base,0x116bce,{0xe8,0x4d,0x66,0,0})||
       !SurfaceMatch(base,0x116bef,{0xe8,0x2c,0x66,0,0})||!SurfaceMatch(base,0x116bf6,{0xe8,0x75,0x5a,0,0})||
       !SurfaceMatch(base,0x116c14,{0xe8,0x57,0x5a,0,0})||!SurfaceMatch(base,0x116c35,{0xe8,0x36,0x5a,0,0})||
       !SurfaceMatch(base,0x116c3a,{0x8b,0x15,0x88,0xe9,0x5a,0,0x44,0x8b,0xfa,0x45,0x2b,0xfc,0x45,0x2b,0xfd,0x85,0xd2})||
       !SurfaceMatch(base,0x116c64,{0x4c,0x63,0x05,0x5d,0xe9,0x5a,0,0x48,0x8b,0xc8,0x48,0x8b,0x15,0x43,0xe9,0x5a,0})||
       !SurfaceMatch(base,0x116d02,{0xc7,0x05,0xbc,0xe8,0x5a,0,0,0,0,0})||
       !SurfaceMatch(base,0x11d670,{0x48,0x8b,0x1c,0xc1,0x41,0xb8,0xff,0xff,0xff,0xff,0x48,0x85,0xdb,0x0f,0x84,0x75,0x09,0,0,0x49,0x3b,0xd8,0x0f,0x84,0x6c,0x09,0,0})||
       !SurfaceMatch(base,0x11e1cb,{0x48,0xff,0xc0,0x48,0x3b,0xc6,0x74,0x22,0x4c,0x8b,0x2c,0xc1,0x4d,0x85,0xed}))return false;
    g_SurfaceGatherPlain=reinterpret_cast<DecalGatherFn>(base+0x11c670);
    g_SurfaceGatherSorted=reinterpret_cast<DecalGatherFn>(base+0x11d220);
    if(DetourTransactionBegin()!=NO_ERROR)return false;
    LONG err=DetourUpdateThread(GetCurrentThread());
    if(err==NO_ERROR)err=DetourAttach(reinterpret_cast<PVOID*>(&g_SurfaceGatherPlain),reinterpret_cast<PVOID>(SurfaceGatherPlainHook));
    if(err==NO_ERROR)err=DetourAttach(reinterpret_cast<PVOID*>(&g_SurfaceGatherSorted),reinterpret_cast<PVOID>(SurfaceGatherSortedHook));
    if(err!=NO_ERROR){DetourTransactionAbort();return false;}
    if(DetourTransactionCommit()!=NO_ERROR)return false;
    g_SurfaceGatherInstalled=true;return true;
}
static bool SurfaceDecalHooks(SurfaceAddress base){
    if(g_SurfaceDecalHookInstalled)return base==g_SurfaceDecalBase&&GetCurrentThreadId()==g_SurfaceDecalThread;
    if(!SurfaceMatch(base,0x119490,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56})||
       !SurfaceMatch(base,0x116fe4,{0xe8,0xa7,0x24,0,0,0x48,0x85,0xc0,0x74,0x27})||
       !SurfaceMatch(base,0x11ae5c,{0xe8,0x2f,0xe6,0xff,0xff,0x4c,0x8b,0xf8,0x48,0x85,0xc0,0x0f,0x84})||
       !SurfaceMatch(base,0x11bd7e,{0xe8,0x0d,0xd7,0xff,0xff,0x4c,0x8b,0xf8,0x48,0x85,0xc0,0x0f,0x84})||
       !SurfaceMatch(base,0x11c967,{0xe8,0x24,0xcb,0xff,0xff,0x4c,0x8b,0xf0,0x48,0x85,0xc0,0x0f,0x84})||
       !SurfaceMatch(base,0x11d703,{0xe8,0x88,0xbd,0xff,0xff,0x48,0x89,0x45,0xa0,0x48,0x85,0xc0,0x0f,0x84}))return false;
    g_SurfaceDecalGeometry=reinterpret_cast<DecalGeometryFn>(base+0x119490);
    if(DetourTransactionBegin()!=NO_ERROR)return false;
    LONG err=DetourUpdateThread(GetCurrentThread());
    if(err==NO_ERROR)err=DetourAttach(reinterpret_cast<PVOID*>(&g_SurfaceDecalGeometry),reinterpret_cast<PVOID>(SurfaceDecalGeometryHook));
    if(err!=NO_ERROR){DetourTransactionAbort();return false;}
    if(DetourTransactionCommit()!=NO_ERROR)return false;
    g_SurfaceDecalBase=base;g_SurfaceDecalThread=GetCurrentThreadId();g_SurfaceDecalHookInstalled=true;return true;
}
struct ShadowSlot {
    ShadowHandle Handle=ShadowInvalid;unsigned char Bias=0;std::uint16_t Flags=0;
    bool Appearance=false;
    std::array<float,4> UV{};
    SurfaceAddress Material=0,ModelMaterial=0;
    bool ClipReady=false;
    std::uint16_t ClipCount=0;
    std::array<float,12> ClipNormals{};
    std::array<float,4> ClipDistances{};
};
static bool ShadowSlots(void *manager,std::vector<ShadowSlot> &out,bool appearance=false){
    const auto a=reinterpret_cast<SurfaceAddress>(manager);
    SurfaceAddress data=0;int capacity=0,head=0,tail=0,count=0;
    if(!SurfaceGet(a+0x10,data)||!SurfaceGet(a+0x18,capacity)||!SurfaceGet(a+0x20,head)||!SurfaceGet(a+0x24,tail)||!SurfaceGet(a+0x2c,count)||
       capacity<0||capacity>65536||count<0||count>2048||count>capacity||(count&&!data))return false;
    std::set<int> visited;int prev=ShadowInvalid,i=head;
    while(i!=ShadowInvalid){
        if(i<0||i>=capacity||visited.size()>=static_cast<unsigned>(count)||!visited.insert(i).second)return false;
        const auto item=data+SurfaceAddress(i)*0x160;ShadowSlot s;s.Handle=static_cast<ShadowHandle>(i);
        std::uint16_t back=0,next=0;
        // One bounded read of the verified record; narrow fallback keeps
        // the prior traversal behavior if the bulk read is unavailable.
        std::array<unsigned char,0x160> raw{};
        if(SurfaceGet(item,raw)){
            auto get=[&](std::size_t off,auto &v){std::memcpy(&v,raw.data()+off,sizeof(v));};
            get(0x150,back);get(0x152,next);get(0x88,s.Flags);get(0x5c,s.Bias);
            if(appearance){
                get(0x4c,s.UV);get(0x70,s.Material);get(0x78,s.ModelMaterial);s.Appearance=true;
                get(0xf8,s.ClipCount);get(0xfc,s.ClipNormals);get(0x12c,s.ClipDistances);s.ClipReady=s.ClipCount<=4;
            }
        }else {
            if(!SurfaceGet(item+0x150,back)||!SurfaceGet(item+0x152,next)||!SurfaceGet(item+0x88,s.Flags)||!SurfaceGet(item+0x5c,s.Bias))return false;
            if(appearance){
                s.Appearance=SurfaceGet(item+0x4c,s.UV)&&SurfaceGet(item+0x70,s.Material)&&SurfaceGet(item+0x78,s.ModelMaterial);
                s.ClipReady=SurfaceGet(item+0xf8,s.ClipCount)&&s.ClipCount<=4&&(!s.ClipCount||
                    (SurfaceRead(item+0xfc,s.ClipNormals.data(),s.ClipCount*3*sizeof(float))&&SurfaceRead(item+0x12c,s.ClipDistances.data(),s.ClipCount*sizeof(float))));
            }
        }
        if(back!=prev)return false;
        out.push_back(s);prev=i;i=next;
    }
    SurfaceAddress after=0;int afterCount=0,afterHead=0,afterTail=0;
    return visited.size()==static_cast<unsigned>(count)&&prev==tail&&
        SurfaceGet(a+0x10,after)&&after==data&&SurfaceGet(a+0x2c,afterCount)&&afterCount==count&&
        SurfaceGet(a+0x20,afterHead)&&afterHead==head&&SurfaceGet(a+0x24,afterTail)&&afterTail==tail;
}
// Native shadow atlas is bound by the exact client call at 248a6e and
// popped at 248cc6. Snapshot only before the latter, on the ordinary
// synchronous rendering thread, with an exact texture/COM witness.
struct AtlasObservation {
    int Frame=-1,Tick=-1;
    std::uint64_t World=0,Revision=0;
    AtlasRead Status=AtlasRead::Missing;
    std::vector<ShadowSlot> Slots;
    std::shared_ptr<const ShadowAtlasImage> Image;
};
static AtlasObservation g_SurfaceAtlasObservation;
static void SurfaceClearAtlasObservation(){g_SurfaceAtlasObservation={};}
using AtlasPopFn=void(*)(void*);
static AtlasPopFn g_SurfaceAtlasPop=nullptr;
static SurfaceAddress g_SurfaceAtlasClient=0,g_SurfaceAtlasMaterial=0;
static DWORD g_SurfaceAtlasThread=0;
static bool SurfaceExactModule(const char *name,DWORD timestamp,DWORD image,SurfaceAddress &base){
    base=reinterpret_cast<SurfaceAddress>(GetModuleHandleA(name));IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    return base&&SurfaceGet(base,dos)&&dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0&&dos.e_lfanew<=0x1000&&
        SurfaceGet(base+dos.e_lfanew,nt)&&nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&
        nt.FileHeader.TimeDateStamp==timestamp&&nt.OptionalHeader.SizeOfImage==image;
}
// The exact client constructor initializes the singleton at client+0x10e4800.
// Confirm that this is its current atlas, not merely a similarly named texture.
static bool SurfaceAtlasOwner(SurfaceAddress texture,SurfaceAddress &worldMaterial,SurfaceAddress &modelMaterial){
    const auto client=g_SurfaceAtlasClient;SurfaceAddress vt=0,current=0;unsigned char ready=0;
    if(!client||!SurfaceGet(client+0x10e4800,vt)||vt!=client+0xb36880||
       !SurfaceGet(client+0x10e4918,ready)||!ready||
       !SurfaceGet(client+0x10e4860,current)||current!=texture||
       !SurfaceGet(client+0x10e4820,worldMaterial)||!worldMaterial||
       !SurfaceGet(client+0x10e4828,modelMaterial)||!modelMaterial)return false;
    std::string worldName,modelName;
    return SurfaceMaterialName(worldMaterial,worldName)&&SurfaceMaterialName(modelMaterial,modelName)&&
        worldName=="decals/rendershadow"&&modelName=="decals/rendermodelshadow";
}
static bool SurfaceAtlasTexture(void *context,SurfaceAddress &texture,SurfaceAddress &d3dTexture){
    const auto a=reinterpret_cast<SurfaceAddress>(context);
    SurfaceAddress ms=0,api=0,vt=0,stack=0,fn=0,handleArray=0,handle=0,shader=0;
    int depth=0;unsigned flags=0;std::uint16_t width=0,height=0;unsigned char copies=0,textureFlags=0;
    if(!SurfaceExactModule("materialsystem.dll",0x6ac56a08,0x141000,ms)||ms!=g_SurfaceAtlasMaterial||
       !SurfaceExactModule("shaderapidx9.dll",0x6ac412f6,0xdb000,api)||
       !SurfaceGet(a,vt)||vt!=ms+0xbcab0||
       !SurfaceGet(vt+0x20,fn)||fn!=ms+0x323b0||
       !SurfaceGet(a+0x78,stack)||!stack||!SurfaceGet(a+0x88,depth)||depth<1||depth>64||
       !SurfaceGet(stack+SurfaceAddress(depth-1)*0x38,texture)||!texture||
       !SurfaceGet(texture,vt)||vt!=ms+0xbdef0||!SurfaceGet(vt,fn)||fn!=ms+0xd660||
       !SurfaceGet(texture+0x18,flags)||!(flags&0x8000)||
       !SurfaceGet(texture+0x30,width)||!SurfaceGet(texture+0x32,height)||width!=1024||height!=1024||
       !SurfaceGet(texture+0x88,handleArray)||!handleArray||!SurfaceGet(handleArray,handle)||handle<0x10000||
       !SurfaceGet(handle+0x22,copies)||copies!=1||!SurfaceGet(handle+0x5a,textureFlags)||!(textureFlags&1)||
       !SurfaceGet(handle+0x60,d3dTexture)||!d3dTexture||
       !SurfaceGet(ms+0x107248,shader)||!shader||!SurfaceGet(shader,vt)||vt!=api+0x7cce8||
       !SurfaceGet(vt+0x620,fn)||fn!=api+0x22da0||
       !SurfaceMatch(ms,0x30bba,{0x8b,0x81,0x88,0,0,0,0x48,0x8b,0xf9,0xff,0xc8,0x48,0x6b,0xf0,0x38})||
       !SurfaceMatch(ms,0xd660,{0x48,0x83,0xc1,0x14,0xe9,0x07,0x94,0x06,0})||
       !SurfaceMatch(ms,0x3f28e,{0x48,0x8b,0x89,0x88,0,0,0,0x48,0xc7,0xc0,0xff,0xff,0xff,0xff})||
       !SurfaceMatch(ms,0x3f2e9,{0x41,0xff,0x92,0x20,0x06,0,0})||
       !SurfaceMatch(api,0x22e5f,{0xf6,0x47,0x5a,0x01})||
       !SurfaceMatch(api,0x22e6e,{0x80,0x7f,0x22,0x01,0x75,0x06,0x48,0x8b,0x4f,0x60})||
       !SurfaceMatch(api,0x22e99,{0xff,0x90,0x90,0,0,0}))return false;
    std::string name;return SurfaceMaterialName(texture,name)&&!_stricmp(name.c_str(),"_rt_Shadows");
}
struct AtlasRelease {template<class T> void operator()(T *p)const{if(p)p->Release();}};
static AtlasRead SurfaceReadAtlasPixels(SurfaceAddress expectedTexture,ShadowAtlasImage &image){
    auto *device=AfxGetDirect3DDevice9();if(!device)return AtlasRead::Readback;
    IDirect3DSurface9 *raw=nullptr;
    if(FAILED(device->GetRenderTarget(0,&raw))||!raw)return AtlasRead::Readback;
    std::unique_ptr<IDirect3DSurface9,AtlasRelease> source(raw);
    D3DSURFACE_DESC desc{};
    if(FAILED(source->GetDesc(&desc))||desc.Width!=1024||desc.Height!=1024||
       desc.Pool!=D3DPOOL_DEFAULT||desc.MultiSampleType!=D3DMULTISAMPLE_NONE||!(desc.Usage&D3DUSAGE_RENDERTARGET)||
       (desc.Format!=D3DFMT_A8R8G8B8&&desc.Format!=D3DFMT_X8R8G8B8))return AtlasRead::Guard;
    IDirect3DTexture9 *rawTexture=nullptr;
    if(FAILED(source->GetContainer(__uuidof(IDirect3DTexture9),reinterpret_cast<void**>(&rawTexture)))||!rawTexture)return AtlasRead::Guard;
    std::unique_ptr<IDirect3DTexture9,AtlasRelease> container(rawTexture);
    // Compare the actual native device's current RT container to the
    // shader record. A wrapper/alias mismatch is a conservative rejection.
    if(reinterpret_cast<SurfaceAddress>(container.get())!=expectedTexture)return AtlasRead::Guard;
    raw=nullptr;
    if(FAILED(device->CreateOffscreenPlainSurface(1024,1024,desc.Format,D3DPOOL_SYSTEMMEM,&raw,nullptr))||!raw)return AtlasRead::Readback;
    std::unique_ptr<IDirect3DSurface9,AtlasRelease> staging(raw);
    if(FAILED(device->GetRenderTargetData(source.get(),staging.get())))return AtlasRead::Readback;
    std::vector<std::uint32_t> pixels(ShadowAtlasImage::Pixels);
    D3DLOCKED_RECT lock{};
    if(FAILED(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY)))return AtlasRead::Readback;
    const bool pitchOk=lock.pBits&&lock.Pitch>=4096&&lock.Pitch<=1024*1024;
    if(pitchOk)for(unsigned y=0;y<1024;++y)std::memcpy(pixels.data()+y*1024,static_cast<const unsigned char*>(lock.pBits)+std::size_t(y)*lock.Pitch,4096);
    if(FAILED(staging->UnlockRect())||!pitchOk)return AtlasRead::Readback;
    return ShadowAtlasImage::Encode(pixels,desc.Format==D3DFMT_A8R8G8B8?ShadowAtlasImage::Format::Argb:ShadowAtlasImage::Format::Xrgb,image)?AtlasRead::Ready:AtlasRead::TooLarge;
}
static bool SurfaceSameAtlasSlots(const std::vector<ShadowSlot> &a,const std::vector<ShadowSlot> &b){
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i){
        const auto &x=a[i],&y=b[i];
        if(x.Handle!=y.Handle||x.Flags!=y.Flags||x.Bias!=y.Bias||x.Appearance!=y.Appearance||
           x.Material!=y.Material||x.ModelMaterial!=y.ModelMaterial||x.UV!=y.UV||
           x.ClipReady!=y.ClipReady||x.ClipCount!=y.ClipCount)return false;
        if(x.ClipReady)for(unsigned j=0;j<x.ClipCount;++j){
            if(x.ClipDistances[j]!=y.ClipDistances[j])return false;
            for(unsigned k=0;k<3;++k)if(x.ClipNormals[j*3+k]!=y.ClipNormals[j*3+k])return false;
        }
    }
    return true;
}
static void SurfaceAtlasPopHook(void *context){
    const auto caller=reinterpret_cast<SurfaceAddress>(_ReturnAddress());
    if(g_SurfaceAtlasArmed&&g_SurfaceCaptureArmed&&!g_SurfaceReplayActive&&GetCurrentThreadId()==g_SurfaceAtlasThread&&
       caller==g_SurfaceAtlasClient+0x248ccc&&MaterialSystem_IsImmediate()&&Mirv_Tf2_SceneRenderViewId()==0&&g_SurfaceHooks&&
       GetCurrentThreadId()==g_SurfaceThread){
        const auto engine=SurfaceEngine();NativeWorld world;int tick=-1;
        auto *globals=g_Hook_VClient_RenderView.GetGlobals();
        if(globals&&engine&&SurfaceWorld(engine,world)&&SurfaceSameWorld(world,g_SurfaceWorld)&&GetCurrentDemoTick(tick)&&tick>=0){
            const int frame=globals->framecount_get();
            // Multiple ordinary atlas passes in one frame are ambiguous.
            if(g_SurfaceAtlasObservation.Frame==frame&&g_SurfaceAtlasObservation.Tick==tick){
                g_SurfaceAtlasObservation.Image.reset();g_SurfaceAtlasObservation.Status=AtlasRead::Changed;
            }else{
                SurfaceClearAtlasObservation();auto &o=g_SurfaceAtlasObservation;
                o.Frame=frame;o.Tick=tick;o.World=g_SurfaceGeneration;o.Revision=g_SurfaceShadowRevision;o.Status=AtlasRead::Guard;
                SurfaceAddress texture=0,d3dTexture=0,worldMaterial=0,modelMaterial=0;
                if(SurfaceAtlasTexture(context,texture,d3dTexture)&&SurfaceAtlasOwner(texture,worldMaterial,modelMaterial)&&
                   ShadowSlots(g_SurfaceShadowManager,o.Slots,true)){
                    // Avoid a blocking readback when there is no retained projection
                    // that could use these exact atlas pixels.
                    const bool useful=std::any_of(o.Slots.begin(),o.Slots.end(),[&](const ShadowSlot &s){
                        return !(s.Flags&6)&&s.Bias!=255&&s.Appearance&&s.Material==worldMaterial&&s.ModelMaterial==modelMaterial&&
                            ValidShadowTextureRect(s.UV)&&g_SurfaceObserved.count(s.Handle);
                    });
                    if(!useful){o.Status=AtlasRead::Unused;g_SurfaceAtlasPop(context);return;}
                    // Pop's verified first action is Flush(false). Perform
                    // it before the readback while the atlas is still bound;
                    // the original pop will flush again and restore state.
                    reinterpret_cast<void(*)(void*,bool)>(g_SurfaceAtlasMaterial+0x323b0)(context,false);
                    SurfaceAddress again=0,d3dAgain=0;
                    ShadowAtlasImage image;
                    if(SurfaceAtlasTexture(context,again,d3dAgain)&&again==texture&&d3dAgain==d3dTexture){
                        o.Status=SurfaceReadAtlasPixels(d3dTexture,image);
                        int after=-1;std::vector<ShadowSlot> slots;
                        if(o.Status==AtlasRead::Ready&&(!GetCurrentDemoTick(after)||after!=tick||globals->framecount_get()!=frame||
                           g_SurfaceShadowRevision!=o.Revision||!ShadowSlots(g_SurfaceShadowManager,slots,true)||!SurfaceSameAtlasSlots(o.Slots,slots)))o.Status=AtlasRead::Changed;
                        if(o.Status==AtlasRead::Ready)o.Image=std::make_shared<const ShadowAtlasImage>(std::move(image));
                    }
                }
            }
        }
    }
    g_SurfaceAtlasPop(context);
}
static bool SurfaceInstallAtlasCapture(){
    SurfaceAddress client=0,ms=0;SurfaceAddress fn=0;
    if(!SurfaceExactModule("client.dll",0x6ad26f17,0x133a000,client)||
       !SurfaceExactModule("materialsystem.dll",0x6ac56a08,0x141000,ms))return false;
    if(g_SurfaceAtlasPop)return client==g_SurfaceAtlasClient&&ms==g_SurfaceAtlasMaterial&&GetCurrentThreadId()==g_SurfaceAtlasThread;
    if(!MaterialSystem_IsImmediate()||
       !SurfaceMatch(client,0xe054,{0x48,0x8d,0x0d,0xa5,0x67,0x0d,0x01,0xe8,0x80,0x6b,0x23,0})||
       !SurfaceMatch(client,0x248a6a,{0x49,0x8b,0x56,0x60,0xff,0x90,0x58,0x03,0,0})||
       !SurfaceMatch(client,0x248cc0,{0x48,0x8b,0x03,0x48,0x8b,0xcb,0xff,0x90,0x68,0x03,0,0})||
       !SurfaceGet(ms+0xbcab0+0x358,fn)||fn!=ms+0x34dd0||
       !SurfaceGet(ms+0xbcab0+0x368,fn)||fn!=ms+0x34a30||
       !SurfaceMatch(ms,0x34dd0,{0x40,0x53,0x48,0x83,0xec,0x60,0x0f,0x57,0xc0,0x48,0x89,0x54,0x24,0x20})||
       !SurfaceMatch(ms,0x34a30,{0x40,0x53,0x48,0x83,0xec,0x20,0x83,0xb9,0x88,0,0,0,0})||
       !SurfaceMatch(ms,0x34a42,{0x48,0x8b,0x01,0x33,0xd2,0xff,0x50,0x20})||
       !SurfaceMatch(ms,0x323b0,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x0d,0x8b,0x4e,0x0d,0,0x0f,0xb6,0xda}))return false;
    g_SurfaceAtlasPop=reinterpret_cast<AtlasPopFn>(ms+0x34a30);
    if(DetourTransactionBegin()!=NO_ERROR){g_SurfaceAtlasPop=nullptr;return false;}
    LONG error=DetourUpdateThread(GetCurrentThread());
    if(error==NO_ERROR)error=DetourAttach(reinterpret_cast<PVOID*>(&g_SurfaceAtlasPop),reinterpret_cast<PVOID>(SurfaceAtlasPopHook));
    if(error!=NO_ERROR){DetourTransactionAbort();g_SurfaceAtlasPop=nullptr;return false;}
    if(DetourTransactionCommit()!=NO_ERROR){g_SurfaceAtlasPop=nullptr;return false;}
    g_SurfaceAtlasClient=client;g_SurfaceAtlasMaterial=ms;g_SurfaceAtlasThread=GetCurrentThreadId();return true;
}
static void SurfaceProjectHook(void *self,ShadowHandle handle,const float *origin,const float *dir,const float *matrix,const float *size,int count,const int *leaves,float height,float offset,float amount,const float *caster){
    g_SurfaceProject(self,handle,origin,dir,matrix,size,count,leaves,height,offset,amount,caster);
    if(GetCurrentThreadId()!=g_SurfaceThread||g_SurfaceOwn||g_SurfaceReplayActive||!g_SurfaceCaptureArmed||self!=g_SurfaceShadowManager)return;
    ++g_SurfaceShadowRevision;
    auto base=SurfaceEngine();NativeWorld world;
    if(!base||!SurfaceWorld(base,world)){g_SurfaceObserved.clear();SurfaceClearAtlasObservation();return;}
    if(!SurfaceSameWorld(world,g_SurfaceWorld)){g_SurfaceWorld=world;++g_SurfaceGeneration;g_SurfaceObserved.clear();SurfaceResetGeometry();}
    Shadow s;s.Height=height;s.FalloffOffset=offset;s.FalloffAmount=amount;
    if(count<=0||count>8192||!leaves){g_SurfaceObserved.erase(handle);return;}
    std::vector<int> leafValues(count);
    const auto previous=g_SurfaceObserved.find(handle);
    if(!SurfaceGet(reinterpret_cast<SurfaceAddress>(origin),s.Origin)||!SurfaceGet(reinterpret_cast<SurfaceAddress>(dir),s.Direction)||
       !SurfaceGet(reinterpret_cast<SurfaceAddress>(matrix),s.Matrix)||!SurfaceGet(reinterpret_cast<SurfaceAddress>(size),s.Size)||
       !SurfaceGet(reinterpret_cast<SurfaceAddress>(caster),s.Caster)||!SurfaceRead(reinterpret_cast<SurfaceAddress>(leaves),leafValues.data(),count*sizeof(int))||
       !s.SetLeaves(std::move(leafValues),previous!=g_SurfaceObserved.end()?previous->second.Leaves:nullptr)||!s.Valid()){
        g_SurfaceObserved.erase(handle);return;
    }
    if(g_SurfaceObserved.size()>=2048&&!g_SurfaceObserved.count(handle))return;
    g_SurfaceObserved[handle]=std::move(s);
}
static void SurfaceDestroyHook(void *self,ShadowHandle handle){
    if(!g_SurfaceOwn&&GetCurrentThreadId()==g_SurfaceThread&&self==g_SurfaceShadowManager){++g_SurfaceShadowRevision;g_SurfaceObserved.erase(handle);g_SurfaceSavedBias.erase(handle);g_SurfaceOwned.erase(std::remove(g_SurfaceOwned.begin(),g_SurfaceOwned.end(),handle),g_SurfaceOwned.end());}
    g_SurfaceDestroy(self,handle);
}
static void SurfaceBiasHook(void *self,ShadowHandle handle,unsigned char bias){
    if(!g_SurfaceOwn&&g_SurfaceReplayActive&&GetCurrentThreadId()==g_SurfaceThread&&self==g_SurfaceShadowManager){
        auto it=g_SurfaceSavedBias.find(handle);if(it!=g_SurfaceSavedBias.end()){it->second=bias;bias=255;}
    }
    g_SurfaceBias(self,handle,bias);
}
static bool SurfaceShadowHooks(SurfaceAddress base){
    if(g_SurfaceHooks)return GetCurrentThreadId()==g_SurfaceThread;
    if(!SurfaceMatch(base,0x127de0,{0x4c,0x8b,0xdc,0x41,0x54,0x41,0x57,0x48,0x81,0xec,0xc8,0,0,0})||
       !SurfaceMatch(base,0x124b00,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10})||
       !SurfaceMatch(base,0x12b7e0,{0x0f,0xb7,0xc2,0x48,0x69,0xd0,0x60,0x01,0,0})||
       !SurfaceMatch(base,0x12a8ff,{0x80,0x7c,0x01,0x5c,0xff})||
       !SurfaceMatch(base,0x12bb0b,{0x48,0x89,0x5a,0x78,0x49,0x8b,0xf8,0x48,0x89,0x82,0x80,0,0,0,0x4c,0x8b,0xf2,0x4c,0x89,0x42,0x70})||
       !SurfaceMatch(base,0x1237c0,{0x0f,0xb7,0xc2,0x48,0x69,0xd0,0x60,0x01,0,0,0x48,0x8b,0x41,0x10,0x33,0xc9,0x66,0x89,0x8c,0x02,0xf8,0,0,0})||
       !SurfaceMatch(base,0x1217ce,{0x0f,0xb7,0x82,0xf8,0,0,0,0x48,0x83,0xc0,0x15,0x48,0x8d,0x0c,0x40})||
       !SurfaceMatch(base,0x1217f3,{0x0f,0xb7,0x82,0xf8,0,0,0,0xf3,0x0f,0x11,0x9c,0x82,0x2c,0x01,0,0,0x66,0xff,0x82,0xf8,0,0,0})||
       !SurfaceMatch(base,0x123a62,{0x49,0x8d,0xb6,0x2c,0x01,0,0,0x49,0x8d,0xbe,0x04,0x01,0,0})||
       !SurfaceMatch(base,0x12c1ca,{0xf3,0x0f,0x11,0x52,0x4c,0xf3,0x0f,0x11,0x5a,0x50,0xf3,0x0f,0x11,0x42,0x54,0xf3,0x0f,0x11,0x4a,0x58,0xc3}))return false;
    using Factory=void*(*)(const char*,int*);
    auto factory=reinterpret_cast<Factory>(GetProcAddress(reinterpret_cast<HMODULE>(base),"CreateInterface"));
    if(!factory)return false;
    void *manager=factory("VEngineShadowMgr002",nullptr);
    SurfaceAddress vt=0;std::array<SurfaceAddress,20> slots{};
    if(!manager||!SurfaceGet(reinterpret_cast<SurfaceAddress>(manager),vt)||vt!=base+0x3959f0||!SurfaceGet(vt,slots)||
       slots[0]!=base+0x124900||slots[1]!=base+0x124b00||slots[3]!=base+0x127de0||slots[9]!=base+0x12c1b0||slots[12]!=base+0x1237c0||slots[13]!=base+0x1217c0||slots[15]!=base+0x12b7e0)return false;
    g_SurfaceProject=reinterpret_cast<ProjectFn>(slots[3]);g_SurfaceDestroy=reinterpret_cast<DestroyFn>(slots[1]);g_SurfaceBias=reinterpret_cast<BiasFn>(slots[15]);
    if(DetourTransactionBegin()!=NO_ERROR)return false;
    LONG err=DetourUpdateThread(GetCurrentThread());
    if(err==NO_ERROR)err=DetourAttach(reinterpret_cast<PVOID*>(&g_SurfaceProject),reinterpret_cast<PVOID>(SurfaceProjectHook));
    if(err==NO_ERROR)err=DetourAttach(reinterpret_cast<PVOID*>(&g_SurfaceDestroy),reinterpret_cast<PVOID>(SurfaceDestroyHook));
    if(err==NO_ERROR)err=DetourAttach(reinterpret_cast<PVOID*>(&g_SurfaceBias),reinterpret_cast<PVOID>(SurfaceBiasHook));
    if(err!=NO_ERROR){DetourTransactionAbort();return false;}
    if(DetourTransactionCommit()!=NO_ERROR)return false;
    g_SurfaceShadowManager=manager;g_SurfaceThread=GetCurrentThreadId();g_SurfaceHooks=true;return true;
}
static void *SurfaceSimpleShadowMaterial(){
    if(!g_AppSystemFactory)return nullptr;
    auto system=g_AppSystemFactory("VMaterialSystem082",nullptr);
    SurfaceAddress vt=0,fn=0;MEMORY_BASIC_INFORMATION mbi{};
    if(!system||!SurfaceGet(reinterpret_cast<SurfaceAddress>(system),vt)||!SurfaceGet(vt+0x238,fn)||
       !VirtualQuery(reinterpret_cast<void*>(fn),&mbi,sizeof(mbi))||mbi.AllocationBase!=GetModuleHandleA("materialsystem.dll"))return nullptr;
    using Find=void*(*)(void*,const char*,const char*,bool,const char*);
    auto material=reinterpret_cast<Find>(fn)(system,"decals/simpleshadow","Decal textures",false,nullptr);
    std::string name;
    return material&&SurfaceMaterialName(reinterpret_cast<SurfaceAddress>(material),name)&&name=="decals/simpleshadow"?material:nullptr;
}
// Resolve the engine's exact material system, context and lookup before
// the detached native draw. Queued mode has additional independent guards.
static bool SurfaceNativeDrawContext(SurfaceAddress engine,void *context,void *&system,SurfaceAddress &find,bool queued=false){
    const auto module=reinterpret_cast<SurfaceAddress>(GetModuleHandleA("materialsystem.dll"));
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    SurfaceAddress ms=0,vt=0,ctxVt=0,getter=0;
    if(!module||!SurfaceGet(module,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x1000||
       !SurfaceGet(module+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||
       nt.FileHeader.TimeDateStamp!=0x6ac56a08||nt.OptionalHeader.SizeOfImage!=0x141000||
       !SurfaceGet(engine+0x7aac50,ms)||!ms||!SurfaceGet(ms,vt)||vt!=module+0xb7470||
       !SurfaceGet(vt+0x238,find)||find!=module+0x15720||!SurfaceGet(vt+0x310,getter)||getter!=module+0x16c20||
       !SurfaceGet(reinterpret_cast<SurfaceAddress>(context),ctxVt)||ctxVt!=module+(queued?0xb6c28:0xbcab0)||
       !SurfaceMatch(module,0x15720,{0x48,0x83,0xec,0x38,0x48,0x8b,0x44,0x24,0x60,0x4c,0x8b,0x11})||
       !SurfaceMatch(module,0x16c20,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20}))return false;
    system=reinterpret_cast<void*>(ms);return true;
}
// A queued bind can run material proxies on the producer. Restrict this
// experiment to an already-precached, proxy-free ordinary CMaterial, or
// its exact queue-friendly wrapper. No new material is loaded or retained.
static bool SurfaceQueuedMaterialReady(SurfaceAddress material){
    const auto base=reinterpret_cast<SurfaceAddress>(GetModuleHandleA("materialsystem.dll"));
    SurfaceAddress vt=0,real=material,error=0,fn=0;
    if(!base||!SurfaceGet(material,vt))return false;
    if(vt==base+0xb4ba0){
        if(!SurfaceGet(vt+0xa8,fn)||fn!=base+0xae40||
           !SurfaceMatch(base,0xae40,{0x48,0x8b,0x49,0x08,0x48,0x8b,0x01,0x48,0xff,0xa0,0xa8,0,0,0})||
           !SurfaceGet(material+8,real)||!SurfaceGet(real,vt))return false;
    }
    unsigned char flags=0;int proxies=-1;
    return vt==base+0xb4ec0&&SurfaceGet(vt+0xa8,fn)&&fn==base+0x79f0&&
        SurfaceMatch(base,0x7a12,{0x83,0x7b,0x40,0,0x0f,0x9f,0xc0})&&
        SurfaceGet(vt+0x1c0,fn)&&fn==base+0x82d0&&
        SurfaceMatch(base,0x82d0,{0x0f,0xb6,0x41,0x28,0xd0,0xe8,0x24,0x01,0xc3})&&
        SurfaceGet(real+0x28,flags)&&(flags&2)&&SurfaceGet(real+0x40,proxies)&&proxies==0&&
        SurfaceGet(base+0xf2538,error)&&error&&real!=error&&material!=error;
}
// Exact producer-side mesh staging contract, also used by the verified
// legacy emitter path. The engine direct decal draw binds a page/material,
// locks a dynamic mesh, fills vertices and indices, unlocks and draws it.
// All historical bytes are consumed before returning to our caller.
static bool SurfaceQueuedMeshIdle(SurfaceAddress base,void *context){
    const auto ctx=reinterpret_cast<SurfaceAddress>(context);
    SurfaceAddress ctxVt=0,mesh=0,vt=0,owner=0,vertices=0,indices=0,vo=0,io=0;
    return base&&SurfaceGet(ctx,ctxVt)&&ctxVt==base+0xb6c28&&
        SurfaceGet(ctx+0x310,mesh)&&mesh&&SurfaceGet(mesh,vt)&&vt==base+0xbb7d0&&
        SurfaceGet(mesh+0x28,owner)&&owner==ctx&&SurfaceGet(mesh+0x50,vertices)&&!vertices&&
        SurfaceGet(mesh+0x58,indices)&&!indices&&SurfaceGet(mesh+0x70,vo)&&!vo&&SurfaceGet(mesh+0x78,io)&&!io;
}
static bool SurfaceQueuedDebugMaterialOff(SurfaceAddress engine){
    SurfaceAddress debug1=0,debug2=0;int d1=-1,d2=-1;
    // The native alternate material is selected only when BOTH are nonzero.
    return SurfaceGet(engine+0x731d68,debug1)&&SurfaceGet(debug1+0x58,d1)&&
        SurfaceGet(engine+0x6a0678,debug2)&&SurfaceGet(debug2+0x58,d2)&&!(d1&&d2);
}
static bool SurfaceQueuedDirectReady(SurfaceAddress engine,void *context){
    if(!SurfaceQueuedContext(context)||!MaterialSystem_CanExecuteOnRenderThread()||MaterialSystem_IsImmediate()||
       !SurfaceQueuedDebugMaterialOff(engine))return false;
    const auto base=reinterpret_cast<SurfaceAddress>(GetModuleHandleA("materialsystem.dll"));
    SurfaceAddress fn=0;
    if(!SurfaceMatch(engine,0x11e765,{0x41,0xff,0x50,0x50})||
       !SurfaceMatch(engine,0x11e79d,{0x41,0xff,0x92,0xc0,0x01,0,0})||
       !SurfaceMatch(engine,0x11e847,{0xff,0x90,0x90,0,0,0})||
       !SurfaceMatch(engine,0x11ed1e,{0xe8,0xfd,0x1f,0xf5,0xff})||
       !SurfaceMatch(engine,0x11ed32,{0xff,0x50,0x68})||
       !SurfaceMatch(base,0x11a92,{0x4c,0x8b,0xbb,0x18,0x03,0,0,0x4c,0x8d,0xb3,0xc0,0x02,0,0})||
       !SurfaceMatch(base,0x11afd,{0x89,0x6b,0x30,0x48,0x89,0x43,0x20})||
       !SurfaceMatch(base,0x2aa33,{0x49,0x8b,0xd6,0x48,0x8b,0xc8,0x4c,0x8b,0,0x41,0xff,0x90,0xa0,0x01,0,0})||
       !SurfaceMatch(base,0x2aaae,{0x48,0x89,0x7b,0x30,0x48,0x89,0x43,0x20,0x4c,0x89,0x73,0x38,0xe8,0x01,0xef,0xfd,0xff})||
       !SurfaceMatch(base,0x2c53a,{0x48,0x8b,0x8e,0x10,0x03,0,0})||
       !SurfaceMatch(base,0x2cd6c,{0x44,0x0f,0xb7,0x41,0x68,0x48,0x8b,0x49,0x28,0xe8,0x56,0xd8,0xff,0xff})||
       !SurfaceMatch(base,0x2cdb7,{0x48,0x8b,0x4f,0x28,0x8b,0xd6,0xe8,0x5e,0xd6,0xff,0xff,0x48,0x89,0x47,0x58})||
       !SurfaceMatch(base,0x2df5a,{0x48,0x89,0x5f,0x30,0x48,0x8b,0x43,0x50,0x48,0x89,0x47,0x38,0x8b,0x43,0x60,0x89,0x47,0x40,0x44,0x89,0x77,0x44,0x48,0x8b,0x43,0x58})||
       !SurfaceMatch(base,0x2df78,{0x8b,0x43,0x64,0x89,0x47,0x50,0xe8,0x3d,0xba,0xfd,0xff})||
       !SurfaceMatch(base,0x2df88,{0x48,0xc7,0x43,0x50,0,0,0,0,0x48,0xc7,0x43,0x58,0,0,0,0}))return false;
    const std::pair<unsigned,SurfaceAddress> contextMethods[]={{8,0x1ac20},{0x10,0x2a940},{0x18,0x2ba90},{0x48,0x2a9d0},{0x50,0x11a70},{0x1c0,0x2c4c0}};
    for(const auto &m:contextMethods)if(!SurfaceGet(base+0xb6c28+m.first,fn)||fn!=base+m.second)return false;
    if(!SurfaceQueuedMeshIdle(base,context))return false;
    const std::pair<unsigned,SurfaceAddress> methods[]={{0x58,0x2eac0},{0x68,0x2b510},{0x90,0x2cd30},{0xa8,0x2edf0},{0xc8,0x2ce20}};
    for(const auto &m:methods)if(!SurfaceGet(base+0xbb7d0+m.first,fn)||fn!=base+m.second)return false;
    return true;
}
static void SurfacePreparePendingDecals(SurfaceAddress base,void *context,int group,float blend,std::vector<PreparedDecal> &prepared,bool queued=false){
    if(group<0||group>=3||g_SurfacePendingDecals.empty()||(g_SurfaceDrawnGroups&(1u<<group)))return;
    if(!std::isfinite(blend)||blend<=0||blend>1)return;
    g_SurfaceDrawnGroups|=1u<<group;
    NativeWorld world;float visible=0;void *system=nullptr;SurfaceAddress find=0;
    if(!SurfaceWorld(base,world)||!SurfaceSameWorld(world,g_SurfaceWorld)||
       !Mirv_Tf2_SceneDrawVisibility("r_drawdecals",visible,false)||!visible||
       !SurfaceNativeDrawContext(base,context,system,find,queued)||(queued&&!SurfaceQueuedDirectReady(base,context)))return;
    std::set<std::uint32_t> wanted,visibleReceivers;
    for(const auto &pending:g_SurfacePendingDecals)if(pending.Value.GeometryGroup==group)wanted.insert(pending.Value.Surface);
    if(wanted.empty())return;
    if(!g_SurfaceCurrentWorldList||!ResolveWorldReceivers(g_SurfaceCurrentWorldList,group,world.Surfaces,world.Count,wanted,visibleReceivers,SurfaceRead)){
        g_SurfaceReceiverUnknown+=wanted.size();return; // no replacement gates on unknown visibility
    }
    using Find=void*(*)(void*,const char*,const char*,bool,const char*);
    std::map<std::string,void*> materials;
    for(const auto &pending:g_SurfacePendingDecals){
        const auto &d=pending.Value;
        if(d.GeometryGroup!=group)continue;
        if(queued&&pending.LiveObject&&!SurfaceQueuedReplacementIdentity(pending.LiveWitness)){++g_SurfaceDecalsRejected;continue;}
        if(!visibleReceivers.count(d.Surface)){++g_SurfaceReceiverCulled;continue;}
        if(!d.Visible()||!d.HasGeometry()||d.Surface>=static_cast<unsigned>(world.Count)){++g_SurfaceDecalsRejected;continue;}
        const auto surface=world.Surfaces+SurfaceAddress(d.Surface)*64;
        std::uint32_t flags=0;std::int16_t sort=0;SurfaceAddress sorts=0;int lightmap=0;float offset=0;
        std::memcpy(&offset,&d.LightmapOffset,sizeof(offset));
        if(!SurfaceGet(surface,flags)||(flags&0x800)||!SurfaceGet(surface+0x16,sort)||sort<0||
           !SurfaceGet(base+0x6a1530,sorts)||!sorts||!SurfaceGet(sorts+SurfaceAddress(sort)*16+8,lightmap)||lightmap<0||
           !std::isfinite(offset)){++g_SurfaceDecalsRejected;continue;}
        auto entry=materials.try_emplace(d.Material,nullptr);
        if(entry.second){
            if(queued){
                auto witness=g_SurfaceQueuedMaterials.find(d.Material);
                if(witness!=g_SurfaceQueuedMaterials.end()&&SurfaceQueuedLiveIdentity(witness->second)&&SurfaceQueuedMaterialReady(witness->second.Material))
                    entry.first->second=reinterpret_cast<void*>(witness->second.Material);
            }else{
                auto mat=reinterpret_cast<Find>(find)(system,d.Material.c_str(),"Decal textures",false,nullptr);
                std::string name;
                if(mat&&SurfaceMaterialName(reinterpret_cast<SurfaceAddress>(mat),name)&&name==d.Material)entry.first->second=mat;
            }
        }
        if(!entry.first->second||(queued&&pending.LiveObject&&
           reinterpret_cast<SurfaceAddress>(entry.first->second)!=pending.LiveWitness.Material)){++g_SurfaceDecalsRejected;continue;}
        prepared.push_back({&pending,surface,entry.first->second,false});
    }
}
static void SurfaceDrawPreparedDecals(SurfaceAddress base,void *context,float blend,const std::vector<PreparedDecal> &prepared,bool queued=false){
    if(prepared.empty())return;
    // Recheck the current world and immediate context after native dispatch.
    // No pointer survives the dispatcher; a failed recheck loses at most
    // this frame's replacement, never mutates the underlying decal pool.
    NativeWorld world;void *system=nullptr;SurfaceAddress find=0;
    if(MaterialSystem_IsImmediate()==queued||!SurfaceWorld(base,world)||!SurfaceSameWorld(world,g_SurfaceWorld)||
       !SurfaceNativeDrawContext(base,context,system,find,queued)||(queued&&!SurfaceQueuedDirectReady(base,context))){
        g_SurfaceDecalsRejected+=prepared.size();return;
    }
    const auto draw=reinterpret_cast<DecalDrawFn>(base+0x11e6c0);
    const auto materialBase=queued?reinterpret_cast<SurfaceAddress>(GetModuleHandleA("materialsystem.dll")):0;
    for(const auto &item:prepared){
        const auto &pending=*item.Pending;const auto &d=pending.Value;
        if(queued){
            if(pending.LiveObject&&!item.Suppressed){++g_SurfaceOpacityNotObserved;continue;}
            auto witness=g_SurfaceQueuedMaterials.find(d.Material);
            if((pending.LiveObject&&!SurfaceQueuedReplacementIdentity(pending.LiveWitness))||witness==g_SurfaceQueuedMaterials.end()||
               witness->second.Material!=reinterpret_cast<SurfaceAddress>(item.Material)||
               !SurfaceQueuedLiveIdentity(witness->second)||!SurfaceQueuedMaterialReady(witness->second.Material)||
               !SurfaceQueuedDebugMaterialOff(base)||!SurfaceQueuedMeshIdle(materialBase,context)){++g_SurfaceDecalsRejected;continue;}
        }
        if(!ShouldSubmitDetached(pending.LiveObject!=0,pending.LiveVisible,item.Suppressed)){
            ++g_SurfaceOpacityNotObserved;continue;
        }
        // Only fields read by the verified direct mesh routine. Clear
        // attachment and fade bits; historical alpha is already evaluated.
        alignas(16) unsigned char record[0x6c]{};
        std::memcpy(record+0x20,&d.LightmapOffset,sizeof(d.LightmapOffset));
        std::memcpy(record+0x54,d.Color.data(),d.Color.size());record[0x57]=d.ReplayAlpha();
        draw(d.Geometry->data(),item.Material,reinterpret_cast<void*>(item.Surface),static_cast<int>(d.Geometry->size()),record,blend);
        ++g_SurfaceDecalsSubmitted;if(pending.LiveObject)++g_SurfaceOpacitySubmitted;
    }
}
static void SurfaceDecalDispatchHook(void *context,int group,float blend){
    const auto base=g_SurfaceDecalBase;
    const auto caller=reinterpret_cast<SurfaceAddress>(_ReturnAddress())-base;
    // This return address is the ordinary world pass, not brush/flashlight
    // calls. Group 3 uses an optional translated water model matrix.
    const bool world=caller==0xe729d&&group>=0&&group<3&&GetCurrentThreadId()==g_SurfaceDecalThread&&
        (g_SurfaceCaptureArmed||g_SurfaceReplayActive)&&MaterialSystem_IsImmediate()&&Mirv_Tf2_SceneRenderViewId()==0;
    const int old=g_SurfaceGeometryGroup,oldQueued=g_SurfaceQueuedGroup;
    g_SurfaceQueuedGroup=-1;
    if(caller==0xe729d&&group>=0&&group<3&&GetCurrentThreadId()==g_SurfaceDecalThread&&
       g_SurfaceQueueRequested&&g_SurfaceReplayActive&&g_SurfaceDecalGate&&
       Mirv_Tf2_SceneRenderViewId()==0&&SurfaceQueuedContext(context)){
        g_SurfaceQueuedGroup=group;++g_SurfaceQueuedGroups;
    }
    const bool queuedWorld=g_SurfaceQueuedGroup==group&&group>=0&&group<3;
    NativeWorld captureWorld;
    if(world&&g_SurfaceCaptureArmed&&!g_SurfaceReplayActive&&SurfaceWorld(base,captureWorld)&&SurfaceSameWorld(captureWorld,g_SurfaceWorld))g_SurfaceGeometryGroup=group;
    else g_SurfaceGeometryGroup=-1;
    std::vector<PreparedDecal> prepared;
    std::map<SurfaceAddress,PreparedDecal*> replacements;
    QueuedReplacementScope queuedReplacements;
    if((world||queuedWorld)&&g_SurfaceReplayActive&&g_SurfaceDecalGate){
        SurfacePreparePendingDecals(base,context,group,blend,prepared,queuedWorld);
        for(auto &item:prepared)if(item.Pending->LiveObject){
            if(queuedWorld)queuedReplacements.Items.emplace(item.Pending->LiveObject,&item);
            else replacements.emplace(item.Pending->LiveObject,&item);
        }
    }
    auto *previousGates=g_SurfaceReplacementGates;
    auto *previousQueuedGates=g_SurfaceQueuedReplacementScope;
    g_SurfaceReplacementGates=replacements.empty()?nullptr:&replacements;
    g_SurfaceQueuedReplacementScope=queuedReplacements.Items.empty()?nullptr:&queuedReplacements;
    g_SurfaceDecalDispatch(context,group,blend);
    g_SurfaceReplacementGates=previousGates;
    g_SurfaceQueuedReplacementScope=previousQueuedGates;
    g_SurfaceGeometryGroup=old;g_SurfaceQueuedGroup=oldQueued;
    if(queuedWorld)for(auto &item:prepared)if(item.Pending->LiveObject)
        item.Suppressed=queuedReplacements.Coverage.CanSubmit(item.Suppressed);
    if(!prepared.empty())SurfaceDrawPreparedDecals(base,context,blend,prepared,queuedWorld);
}
static void SurfaceWorldDrawHook(void *list,int flags,float waterZ){
    const auto saved=g_SurfaceCurrentWorldList;
    // A nested non-main or special pass cannot borrow the outer list.
    g_SurfaceCurrentWorldList=0;
    if(GetCurrentThreadId()==g_SurfaceDecalThread&&reinterpret_cast<SurfaceAddress>(_ReturnAddress())==g_SurfaceDecalBase+0xe10e7&&
       g_SurfaceReplayActive&&g_SurfaceDecalGate&&!g_SurfacePendingDecals.empty()&&!(flags&0x840)&&Mirv_Tf2_SceneRenderViewId()==0&&
       (MaterialSystem_IsImmediate()||g_SurfaceQueueRequested))
        g_SurfaceCurrentWorldList=reinterpret_cast<SurfaceAddress>(list);
    g_SurfaceWorldDraw(list,flags,waterZ);
    g_SurfaceCurrentWorldList=saved;
}
static bool SurfaceWorldDecalHooks(SurfaceAddress base){
    if(!SurfaceDecalHooks(base))return false;
    if(g_SurfaceDispatchInstalled&&g_SurfaceWorldDrawInstalled)return true;
    if(!SurfaceMatch(base,0xe7010,{0x40,0x53,0x48,0x83,0xec,0x70,0x48,0x89,0xb4,0x24,0x90,0,0,0})||
       !SurfaceMatch(base,0xe10da,{0x0f,0x28,0xd6,0x8b,0xd6,0x48,0x8b,0xcd,0xe8,0x29,0x5f,0,0})||
       !SurfaceMatch(base,0xd240b,{0x8b,0x47,0x08,0xba,0x01,0,0,0,0x48,0x89,0x6c,0x24,0x50,0x8b,0x6f,0x10})||
       !SurfaceMatch(base,0xd2480,{0x89,0x4f,0x08,0x48,0x8b,0x07,0x48,0x63,0xc9,0x48,0xc1,0xe1,0x07})||
       !SurfaceMatch(base,0xd24a7,{0xff,0x47,0x10,0x8b,0x47,0x10,0x4c,0x8b,0x0f})||
       !SurfaceMatch(base,0xd2571,{0x4c,0x8b,0x03,0x48,0xc1,0xe1,0x03})||
       !SurfaceMatch(base,0xd2592,{0x8b,0x43,0x10,0x4c,0x8b,0x0b,0xff,0xc0,0x89,0x43,0x10})||
       !SurfaceMatch(base,0xe71c8,{0x41,0x8b,0xd6,0x48,0x8b,0xce,0xe8,0xcd,0x43,0xff,0xff})||
       !SurfaceMatch(base,0xdb5b3,{0x4c,0x8d,0x41,0x18,0x48,0x63,0xda,0x4c,0x89,0x44,0x24,0x20,0x48,0x8b,0xd3,0x48,0x8d,0x43,0x03,0x48,0xc1,0xe0,0x05,0x49,0x03,0xc0})||
       !SurfaceMatch(base,0xdb620,{0x48,0x8b,0x00,0x48,0x8b,0x0c,0xc8,0x44,0x0f,0xb7,0x21})||
       !SurfaceMatch(base,0xdb640,{0x45,0x0f,0xbf,0xec,0x45,0x8b,0xfb,0x49,0xc1,0xe5,0x07,0x4d,0x03,0x28,0x66,0x45,0x3b,0x5d,0x02})||
       !SurfaceMatch(base,0xdb7d7,{0x49,0x8b,0x00,0x41,0x0f,0xbf,0xcc,0x48,0xc1,0xe1,0x07,0x44,0x0f,0xb7,0x24,0x01})||
       !SurfaceMatch(base,0x116ad0,{0x40,0x53,0x57,0x48,0x83,0xec,0x58,0x48,0x8b,0x05,0x6a,0xe8,0x58,0})||
       !SurfaceMatch(base,0xe728e,{0x41,0x0f,0x28,0xd0,0x41,0x8b,0xd6,0x48,0x8b,0xcb,0xe8,0x33,0xf8,0x02,0,0x48,0x8b,0x0d,0xc4})||
       !SurfaceMatch(base,0x11e6c0,{0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x48,0x08,0x55,0x56,0x57,0x41,0x54})||
       !SurfaceMatch(base,0x11e76f,{0x4d,0x8b,0x46,0x58,0x48,0x8b,0xcb,0xff,0x50,0x48})||
       !SurfaceMatch(base,0x11eaf0,{0xf3,0x41,0x0f,0x10,0x76,0x20,0x45,0x85,0xed})||
       !SurfaceMatch(base,0x11b502,{0x48,0x83,0xc2,0x20,0x48,0x8b,0x8d,0x30,0x02})||
       !SurfaceMatch(base,0x11ed86,{0xff,0x50,0x18,0x48,0x8b,0x03,0x48,0x8b,0xcb,0xff,0x50,0x08}))return false;
    g_SurfaceDecalDispatch=reinterpret_cast<DecalDispatchFn>(base+0x116ad0);
    g_SurfaceWorldDraw=reinterpret_cast<WorldDrawFn>(base+0xe7010);
    if(DetourTransactionBegin()!=NO_ERROR)return false;
    LONG err=DetourUpdateThread(GetCurrentThread());
    if(err==NO_ERROR)err=DetourAttach(reinterpret_cast<PVOID*>(&g_SurfaceDecalDispatch),reinterpret_cast<PVOID>(SurfaceDecalDispatchHook));
    if(err==NO_ERROR)err=DetourAttach(reinterpret_cast<PVOID*>(&g_SurfaceWorldDraw),reinterpret_cast<PVOID>(SurfaceWorldDrawHook));
    if(err!=NO_ERROR){DetourTransactionAbort();return false;}
    if(DetourTransactionCommit()!=NO_ERROR)return false;
    g_SurfaceDispatchInstalled=g_SurfaceWorldDrawInstalled=true;return true;
}
} // namespace
#endif
advancedfx::SceneSurfaces::Frame Mirv_Tf2_CaptureSurfaces(int tick){
    advancedfx::SceneSurfaces::Frame sample;sample.Tick=tick;
#ifdef _WIN64
    auto base=SurfaceEngine();NativeWorld world;
    if(!base||!MaterialSystem_IsImmediate()||!SurfaceWorld(base,world)){SurfaceClearGeometryObservations();return sample;}
    if(!SurfaceSameWorld(world,g_SurfaceWorld)){g_SurfaceWorld=world;++g_SurfaceGeneration;g_SurfaceObserved.clear();SurfaceResetGeometry();}
    SurfaceWorldDecalHooks(base); // observe subsequent ordinary main-world draws
    sample.World=g_SurfaceGeneration;
    if(g_SurfaceAtlasRequested)sample.AtlasStatus=g_SurfaceAtlasArmed?AtlasRead::Missing:AtlasRead::Guard;
    std::vector<LiveDecal> decals;std::set<std::uint16_t> excludedSlots;
    sample.DecalsReady=ReadWorldDecals(base,world,decals,sample.DecalsUnsupported,sample.DecalStatus,sample.UnsupportedDecals,&excludedSlots);
    if(sample.DecalsReady){
        if(!excludedSlots.empty())sample.UncertainDecalSlots=std::make_shared<const std::vector<std::uint16_t>>(excludedSlots.begin(),excludedSlots.end());
        auto values=std::make_shared<std::vector<Decal>>();values->reserve(decals.size());
        for(auto &d:decals){
            auto &v=d.Value;
            if(d.GeometryEligible){
                const Decal *previous=nullptr;
                if(g_SurfaceLastDecals){
                    auto it=std::lower_bound(g_SurfaceLastDecals->begin(),g_SurfaceLastDecals->end(),v.Slot,[](const Decal &x,std::uint16_t s){return x.Slot<s;});
                    if(it!=g_SurfaceLastDecals->end()&&it->Same(v)&&it->Geometry)previous=&*it;
                }
                auto obs=g_SurfaceDecalObservations.find(v.Slot);
                if(obs!=g_SurfaceDecalObservations.end()&&SurfaceObservationMatches(obs->second,d)){
                    v.GeometryGroup=obs->second.Group;v.Geometry=obs->second.Geometry;
                    if(previous&&previous->GeometryGroup==v.GeometryGroup&&*previous->Geometry==*v.Geometry)v.Geometry=previous->Geometry;
                }else if(previous){v.GeometryGroup=previous->GeometryGroup;v.Geometry=previous->Geometry;}
            }
            values->push_back(std::move(v));
        }
        sample.Decals=values;g_SurfaceLastDecals=values;
    }
    SurfaceClearGeometryObservations();
    if(SurfaceShadowHooks(base)){
        std::vector<ShadowSlot> slots;
        if(ShadowSlots(g_SurfaceShadowManager,slots,true)){
            auto shadows=std::make_shared<std::vector<Shadow>>();
            std::map<SurfaceAddress,std::string> names;
            auto name=[&](SurfaceAddress address)->const std::string&{
                auto entry=names.try_emplace(address);
                if(entry.second&&!SurfaceMaterialName(address,entry.first->second))entry.first->second.clear();
                return entry.first->second;
            };
            for(const auto &s:slots)if(!(s.Flags&6)&&s.Bias!=255){
                auto it=g_SurfaceObserved.find(s.Handle);
                if(it==g_SurfaceObserved.end()||shadows->size()>=128){++sample.ShadowsUnobserved;continue;}
                auto projection=it->second;projection.Bias=s.Bias;
                if(s.ClipReady)projection.SetClipPlanes(s.ClipCount,s.ClipNormals,s.ClipDistances);
                if(s.Appearance){
                    const auto &worldName=name(s.Material),&modelName=name(s.ModelMaterial);
                    if(!worldName.empty()&&!modelName.empty()){
                        if(worldName=="decals/simpleshadow"&&modelName=="decals/simpleshadow"){
                            if(ValidShadowTextureRect(s.UV)){projection.Texture=ShadowTexture::Simple;projection.UV=s.UV;}
                        }else if(worldName=="decals/rendershadow"&&modelName=="decals/rendermodelshadow"){
                            if(ValidShadowTextureRect(s.UV)){projection.Texture=ShadowTexture::Atlas;projection.UV=s.UV;}
                        }else projection.Texture=ShadowTexture::Other;
                    }
                }
                shadows->push_back(std::move(projection));
            }
            sample.Shadows=shadows;sample.ShadowsReady=true;
            if(g_SurfaceAtlasArmed){
                auto &o=g_SurfaceAtlasObservation;auto *globals=g_Hook_VClient_RenderView.GetGlobals();
                sample.AtlasStatus=AtlasRead::Missing;
                if(globals&&o.Frame==globals->framecount_get()&&o.Tick==tick&&o.World==sample.World){
                    sample.AtlasStatus=o.Status;
                    if(o.Image&&o.Status==AtlasRead::Ready){
                        if(o.Revision==g_SurfaceShadowRevision&&SurfaceSameAtlasSlots(o.Slots,slots)&&
                           std::any_of(shadows->begin(),shadows->end(),[](const Shadow &s){return s.HasAtlasRect();}))sample.Atlas=o.Image;
                        else sample.AtlasStatus=AtlasRead::Changed;
                    }
                }
            }
        }
    }
#endif
    return sample;
}
void Mirv_Tf2_SetSurfaceObservation(bool armed,bool atlas){
#ifdef _WIN64
    if(!armed&&g_SurfaceCaptureArmed){g_SurfaceObserved.clear();SurfaceResetGeometry();}
    g_SurfaceCaptureArmed=armed;
    const bool wantAtlas=armed&&atlas;
    g_SurfaceAtlasRequested=wantAtlas;
    if(!wantAtlas&&g_SurfaceAtlasArmed)SurfaceClearAtlasObservation();
    g_SurfaceAtlasArmed=wantAtlas&&SurfaceInstallAtlasCapture();
#endif
}
void Mirv_Tf2_ResetSurfaces(){
#ifdef _WIN64
    if(!g_SurfaceReplayActive){g_SurfaceObserved.clear();SurfaceResetGeometry();g_SurfaceWorld={};++g_SurfaceGeneration;}
#endif
}
unsigned Mirv_Tf2_BeginSurfaceReplay(const advancedfx::SceneSurfaces::Frame &sample,unsigned requested,std::string &status){
    status="Historical surfaces unavailable or not eligible.";
#ifdef _WIN64
    const auto base=SurfaceEngine();NativeWorld world;
    const bool immediate=MaterialSystem_IsImmediate();
    if(!requested||!base||!Mirv_Tf2_SceneStationaryReady()||
       (!immediate&&(!(requested&16)||!(requested&2)||!MaterialSystem_CanExecuteOnRenderThread()))||
       !SurfaceWorld(base,world)||!SurfaceSameWorld(world,g_SurfaceWorld)||sample.World!=g_SurfaceGeneration||g_SurfaceReplayActive)return 0;
    unsigned active=0;std::ostringstream report;
    g_SurfaceQueueRequested=false;g_SurfaceQueuedGroup=-1;g_SurfaceQueuedHidden.clear();g_SurfaceQueuedMaterials.clear();
    g_SurfaceQueuedRemoved=g_SurfaceQueuedRejected=g_SurfaceQueuedFades=g_SurfaceQueuedGroups=0;
    if(!immediate)report<<"Queued experiment: surviving nonfading visibility/opacity and limited removed geometry (existing proxy-free materials only); surviving fading opacity and historical shadows remain immediate-only. ";
    g_SurfacePendingDecals.clear();g_SurfaceDrawnGroups=0;
    g_SurfaceDecalsSubmitted=g_SurfaceDecalsRejected=g_SurfaceOpacitySubmitted=g_SurfaceOpacityNotObserved=0;
    g_SurfaceReceiverCulled=g_SurfaceReceiverUnknown=0;g_SurfaceCurrentWorldList=0;
    if(immediate&&(requested&1)&&sample.ShadowsReady&&sample.Shadows&&g_SurfaceHooks&&GetCurrentThreadId()==g_SurfaceThread){
        float visible=0;std::vector<ShadowSlot> slots;
        auto material=Mirv_Tf2_SceneDrawVisibility("r_shadows",visible,false)&&visible!=0?SurfaceSimpleShadowMaterial():nullptr;
        if(material&&ShadowSlots(g_SurfaceShadowManager,slots)){
            auto vt=*reinterpret_cast<void***>(g_SurfaceShadowManager);
            auto create=reinterpret_cast<CreateFn>(vt[0]);auto tex=reinterpret_cast<TexFn>(vt[9]);
            auto clearClip=reinterpret_cast<ClearClipFn>(vt[12]);auto addClip=reinterpret_cast<AddClipFn>(vt[13]);
            std::size_t simple=0,atlas=0,other=0,unknown=0,clipPlanes=0,clipUnknown=0;
            g_SurfaceOwn=true;
            for(const auto &s:*sample.Shadows){
                if(!s.Valid())continue;
                auto handle=create(g_SurfaceShadowManager,material,material,reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xffff)),0);
                if(handle==ShadowInvalid)continue;
                g_SurfaceOwned.push_back(handle);
                const auto uv=s.ReplayTextureRect();
                tex(g_SurfaceShadowManager,handle,uv[0],uv[1],uv[2],uv[3]);
                if(s.HasSimpleTexture())++simple;
                else if(s.HasAtlasRect())++atlas;
                else if(s.Texture==ShadowTexture::Other)++other;
                else ++unknown;
                g_SurfaceProject(g_SurfaceShadowManager,handle,s.Origin.data(),s.Direction.data(),s.Matrix.data(),s.Size.data(),static_cast<int>(s.Leaves->size()),s.Leaves->data(),s.Height,s.FalloffOffset,s.FalloffAmount,s.Caster.data());
                // Native client projects first, then computes extra planes.
                // Only call the bounded API for our newly owned shadow.
                if(s.ClipKnown){clearClip(g_SurfaceShadowManager,handle);for(unsigned i=0;i<s.ClipCount;++i){const auto &p=s.ClipPlanes[i];addClip(g_SurfaceShadowManager,handle,p.data(),p[3]);++clipPlanes;}}
                else ++clipUnknown;
                g_SurfaceBias(g_SurfaceShadowManager,handle,s.Bias);
            }
            g_SurfaceOwn=false;
            // Fail open on incomplete observation or creation. Keep live
            // shadows if replacement coverage cannot be established.
            const bool replace=sample.ShadowsUnobserved==0&&clipUnknown==0&&g_SurfaceOwned.size()==sample.Shadows->size();
            if(replace){
                for(const auto &s:slots)if(!(s.Flags&6)&&s.Bias!=255){g_SurfaceSavedBias[s.Handle]=s.Bias;g_SurfaceBias(g_SurfaceShadowManager,s.Handle,255);}
            }
            if(replace||!g_SurfaceOwned.empty())active|=1;
            report<<"Historical projected blobs "<<g_SurfaceOwned.size()<<"/"<<sample.Shadows->size()<<"; simple texture rect "<<simple<<"; atlas rectangles (pixels not retained; blob fallback) "<<atlas<<"; non-simple approximations "<<other<<"; unknown appearance approximations "<<unknown<<"; restored clip planes "<<clipPlanes<<"; unknown clip coverage "<<clipUnknown<<"; unobserved "<<sample.ShadowsUnobserved<<"; live bias gates "<<g_SurfaceSavedBias.size()<<"; "<<(replace?"replacement":"partial overlay")<<". ";
        }else report<<"Shadow material, manager, or r_shadows unavailable. ";
    }
    if(requested&2){
        if(!sample.DecalsReady||!sample.Decals)report<<"Historical decal capture unavailable: "<<DecalReadLabel(sample.DecalStatus)<<". ";
        else if(immediate?!SurfaceDecalHooks(base):!(SurfaceWorldDecalHooks(base)&&SurfaceQueuedGatherHooks(base)))report<<"Decal gate unavailable (ABI or installation). ";
        else {
            std::vector<LiveDecal> current;std::size_t unsupported=0;DecalRead status=DecalRead::NotRun;DecalCoverage coverage;
            const bool detached=(immediate?(requested&12):((requested&16)&&(requested&12)))&&SurfaceWorldDecalHooks(base);
            std::set<std::uint16_t> excludedSlots;
            if(ReadWorldDecals(base,world,current,unsupported,status,coverage,detached?&excludedSlots:nullptr)){
                std::size_t matched=0,hidden=0,failed=0,fading=0,fadeUnknown=0,slotUnknown=0;
                g_SurfaceHiddenDecals.clear();g_SurfaceDecalDrawSkips=0;
                for(const auto &live:current){
                    const auto &v=live.Value;
                    if(!immediate&&detached&&live.GeometryEligible&&!v.Fading()){
                        auto witness=g_SurfaceQueuedMaterials.try_emplace(v.Material,live);
                        // Ambiguous material names cannot authorize queued replay.
                        if(!witness.second&&witness.first->second.Material!=live.Material)witness.first->second={};
                    }
                    auto it=std::lower_bound(sample.Decals->begin(),sample.Decals->end(),v.Slot,[](const Decal &a,std::uint16_t s){return a.Slot<s;});
                    const bool same=it!=sample.Decals->end()&&it->Same(v);matched+=same;
                    fading+=v.Fading();
                    if(!same&&sample.DecalSlotUncertain(v.Slot)){++slotUnknown;continue;}
                    // If any historical fade record was unreadable, an
                    // unmatched live fade may be that record; fail open.
                    if(!same&&v.Fading()&&sample.UnsupportedDecals.FadeInvalid){++fadeUnknown;continue;}
                    if(!same||!it->Visible()){
                        // Use the pointers from the validated record; the hook
                        // still checks surface/material/slot before suppression.
                        if(!immediate){
                            if(v.Fading()){++g_SurfaceQueuedFades;continue;}
                            if(g_SurfaceQueuedHidden.emplace(live.Object,live).second)++hidden;else ++failed;
                        }else{
                            DecalGate gate;gate.Slot=v.Slot;gate.Surface=live.Surface;gate.Material=live.Material;
                            if(g_SurfaceHiddenDecals.emplace(live.Object,gate).second)++hidden;else ++failed;
                        }
                    }
                }
                std::size_t geometryMissing=0,excludedMissing=0,opacityPlanned=0,opacityUnavailable=0,removedPlanned=0;
                if(detached)for(const auto &d:*sample.Decals){
                    if(!d.Visible())continue;
                    auto it=std::lower_bound(current.begin(),current.end(),d.Slot,[](const LiveDecal &x,std::uint16_t s){return x.Value.Slot<s;});
                    if(it!=current.end()&&it->Value.Same(d)){
                        if(d.ReplayAlpha()==it->Value.ReplayAlpha())continue;
                        if(!CanPlanDetached(immediate,requested,true,false,d.Fading()||it->Value.Fading())){if(requested&8)++opacityUnavailable;continue;}
                        if(!it->GeometryEligible||!d.NeedsOpacityReplay(it->Value)||g_SurfacePendingDecals.size()>=1024){++opacityUnavailable;continue;}
                        PendingDecal pending;pending.Value=d;pending.LiveObject=it->Object;
                        pending.LiveGate={it->Surface,it->Material,it->Value.Slot};pending.LiveVisible=it->Value.Visible();
                        if(!immediate)pending.LiveWitness=*it;
                        g_SurfacePendingDecals.push_back(std::move(pending));++opacityPlanned;continue;
                    }
                    if(!(requested&4))continue;
                    if(!CanPlanDetached(immediate,requested,false,excludedSlots.count(d.Slot)!=0)){++excludedMissing;continue;}
                    if(!d.HasGeometry()||g_SurfacePendingDecals.size()>=1024){++geometryMissing;continue;}
                    PendingDecal pending;pending.Value=d;g_SurfacePendingDecals.push_back(std::move(pending));++removedPlanned;
                }
                if(requested&4)report<<"Detached removed decals "<<(detached?"enabled":"guard unavailable")<<"; planned "<<removedPlanned<<"; missing geometry/bounded "<<geometryMissing<<"; unknown live slot "<<excludedMissing<<". ";
                if(requested&8)report<<"Surviving opacity replacement "<<(detached?(immediate?"enabled":"nonfading queued only"):"guard unavailable")<<"; planned "<<opacityPlanned<<"; unavailable "<<opacityUnavailable<<". ";
                g_SurfaceDecalGate=true;g_SurfaceQueueRequested=!immediate;active|=2;
                report<<"Historical surviving decals "<<matched<<"/"<<sample.Decals->size()<<"; geometry gates "<<hidden<<"; missing "<<sample.Decals->size()-matched<<"; unsupported live "<<unsupported<<" ("<<coverage.Describe()<<"); rejected gates "<<failed<<"; live fading "<<fading<<"; unknown historical fades left live "<<fadeUnknown<<"; unsupported historical slots left live "<<slotUnknown<<". Unreplaced surviving marks retain live opacity. ";
            }else report<<"Live decal traversal failed: "<<DecalReadLabel(status)<<". ";
        }
    }
    g_SurfaceReplayActive=active!=0||!g_SurfaceOwned.empty();
    g_SurfaceReport=report.str();if(!g_SurfaceReport.empty())status=g_SurfaceReport;return active;
#else
    return 0;
#endif
}
std::string Mirv_Tf2_EndSurfaceReplay(){
#ifdef _WIN64
    const bool gated=g_SurfaceDecalGate,queued=g_SurfaceQueueRequested;
    const auto decalSkips=g_SurfaceDecalDrawSkips;
    g_SurfaceDecalGate=false;g_SurfaceQueueRequested=false;g_SurfaceQueuedGroup=-1;g_SurfaceQueuedHidden.clear();g_SurfaceQueuedMaterials.clear();
    g_SurfaceHiddenDecals.clear();g_SurfaceDecalDrawSkips=0;
    if(!g_SurfaceReplayActive)return {};
    std::size_t failed=0;const auto base=SurfaceEngineModule();NativeWorld world;
    if(base&&SurfaceWorld(base,world)&&SurfaceSameWorld(world,g_SurfaceWorld)){
        if(!g_SurfaceOwned.empty()||!g_SurfaceSavedBias.empty()){
            std::vector<ShadowSlot> slots;
            if(ShadowSlots(g_SurfaceShadowManager,slots)){
                g_SurfaceOwn=true;
                for(const auto &s:slots){
                    auto it=g_SurfaceSavedBias.find(s.Handle);
                    if(it!=g_SurfaceSavedBias.end()&&s.Bias==255)g_SurfaceBias(g_SurfaceShadowManager,s.Handle,it->second);
                }
                for(auto handle:g_SurfaceOwned)if(std::any_of(slots.begin(),slots.end(),[&](const ShadowSlot &s){return s.Handle==handle;}))g_SurfaceDestroy(g_SurfaceShadowManager,handle);
                g_SurfaceOwn=false;
            }else failed+=g_SurfaceOwned.size()+g_SurfaceSavedBias.size();
        }
    }else failed+=g_SurfaceOwned.size()+g_SurfaceSavedBias.size();
    g_SurfaceOwned.clear();g_SurfaceSavedBias.clear();g_SurfaceReplayActive=false;
    const auto pending=g_SurfacePendingDecals.size();g_SurfacePendingDecals.clear();
    return g_SurfaceReport+(gated?" Native decal geometry skips "+std::to_string(decalSkips)+".":"")+
        (queued?" Queued world groups "+std::to_string(g_SurfaceQueuedGroups)+", filtered pointer submissions "+std::to_string(g_SurfaceQueuedRemoved)+", rejected gathers "+std::to_string(g_SurfaceQueuedRejected)+", fading candidates left native "+std::to_string(g_SurfaceQueuedFades)+".":"")+
        (pending?" Detached decal submissions "+std::to_string(g_SurfaceDecalsSubmitted)+"/"+std::to_string(pending)+", rejected "+std::to_string(g_SurfaceDecalsRejected)+", surviving opacity "+std::to_string(g_SurfaceOpacitySubmitted)+", surviving live geometry not gated "+std::to_string(g_SurfaceOpacityNotObserved)+", receiver culled decals "+std::to_string(g_SurfaceReceiverCulled)+", unknown receiver candidates "+std::to_string(g_SurfaceReceiverUnknown)+".":"")+
        " Scope cleanup warnings "+std::to_string(failed)+".";
#else
    return {};
#endif
}
