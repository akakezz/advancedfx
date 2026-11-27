// Exact-engine experimental surface history. Only ordinary immediate demo views.
// No historical native address is retained in a Frame.
#ifdef _WIN64
#include <intrin.h>
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
static void *g_SurfaceShadowManager=nullptr;
static ProjectFn g_SurfaceProject=nullptr;
static DestroyFn g_SurfaceDestroy=nullptr;
static BiasFn g_SurfaceBias=nullptr;
static DWORD g_SurfaceThread=0;
static bool g_SurfaceHooks=false,g_SurfaceOwn=false,g_SurfaceReplayActive=false,g_SurfaceCaptureArmed=false;
static std::map<ShadowHandle,Shadow> g_SurfaceObserved;
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
static thread_local std::size_t g_SurfaceDecalDrawSkips=0;
// Observe only ordinary immediate main-world geometry dispatch. Native
// addresses here are transient observations, discarded at capture end.
using DecalDispatchFn=void(*)(void*,int,float);
using DecalDrawFn=void(*)(const void*,void*,void*,int,const void*,float);
static DecalDispatchFn g_SurfaceDecalDispatch=nullptr;
static bool g_SurfaceDispatchInstalled=false;
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
static unsigned g_SurfaceDrawnGroups=0;
static std::size_t g_SurfaceDecalsSubmitted=0,g_SurfaceDecalsRejected=0,g_SurfaceOpacitySubmitted=0,g_SurfaceOpacityNotObserved=0;
static void SurfaceClearGeometryObservations(){
    g_SurfaceDecalObservations.clear();g_SurfaceObservedVertices=0;
}
static void SurfaceResetGeometry(){
    SurfaceClearGeometryObservations();g_SurfaceLastDecals.reset();
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
            if(appearance){get(0x4c,s.UV);get(0x70,s.Material);get(0x78,s.ModelMaterial);s.Appearance=true;}
        }else {
            if(!SurfaceGet(item+0x150,back)||!SurfaceGet(item+0x152,next)||!SurfaceGet(item+0x88,s.Flags)||!SurfaceGet(item+0x5c,s.Bias))return false;
            if(appearance)s.Appearance=SurfaceGet(item+0x4c,s.UV)&&SurfaceGet(item+0x70,s.Material)&&SurfaceGet(item+0x78,s.ModelMaterial);
        }
        if(back!=prev)return false;
        out.push_back(s);prev=i;i=next;
    }
    SurfaceAddress after=0;int afterCount=0,afterHead=0,afterTail=0;
    return visited.size()==static_cast<unsigned>(count)&&prev==tail&&
        SurfaceGet(a+0x10,after)&&after==data&&SurfaceGet(a+0x2c,afterCount)&&afterCount==count&&
        SurfaceGet(a+0x20,afterHead)&&afterHead==head&&SurfaceGet(a+0x24,afterTail)&&afterTail==tail;
}
static void SurfaceProjectHook(void *self,ShadowHandle handle,const float *origin,const float *dir,const float *matrix,const float *size,int count,const int *leaves,float height,float offset,float amount,const float *caster){
    g_SurfaceProject(self,handle,origin,dir,matrix,size,count,leaves,height,offset,amount,caster);
    if(GetCurrentThreadId()!=g_SurfaceThread||g_SurfaceOwn||g_SurfaceReplayActive||!g_SurfaceCaptureArmed||self!=g_SurfaceShadowManager)return;
    auto base=SurfaceEngine();NativeWorld world;
    if(!base||!SurfaceWorld(base,world)){g_SurfaceObserved.clear();return;}
    if(!SurfaceSameWorld(world,g_SurfaceWorld)){g_SurfaceWorld=world;++g_SurfaceGeneration;g_SurfaceObserved.clear();SurfaceResetGeometry();}
    Shadow s;s.Height=height;s.FalloffOffset=offset;s.FalloffAmount=amount;
    if(count<=0||count>8192||!leaves){g_SurfaceObserved.erase(handle);return;}
    s.Leaves.resize(count);
    if(!SurfaceGet(reinterpret_cast<SurfaceAddress>(origin),s.Origin)||!SurfaceGet(reinterpret_cast<SurfaceAddress>(dir),s.Direction)||
       !SurfaceGet(reinterpret_cast<SurfaceAddress>(matrix),s.Matrix)||!SurfaceGet(reinterpret_cast<SurfaceAddress>(size),s.Size)||
       !SurfaceGet(reinterpret_cast<SurfaceAddress>(caster),s.Caster)||!SurfaceRead(reinterpret_cast<SurfaceAddress>(leaves),s.Leaves.data(),count*sizeof(int))||!s.Valid()){
        g_SurfaceObserved.erase(handle);return;
    }
    if(g_SurfaceObserved.size()>=2048&&!g_SurfaceObserved.count(handle))return;
    g_SurfaceObserved[handle]=std::move(s);
}
static void SurfaceDestroyHook(void *self,ShadowHandle handle){
    if(!g_SurfaceOwn&&GetCurrentThreadId()==g_SurfaceThread&&self==g_SurfaceShadowManager){g_SurfaceObserved.erase(handle);g_SurfaceSavedBias.erase(handle);g_SurfaceOwned.erase(std::remove(g_SurfaceOwned.begin(),g_SurfaceOwned.end(),handle),g_SurfaceOwned.end());}
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
       !SurfaceMatch(base,0x12c1ca,{0xf3,0x0f,0x11,0x52,0x4c,0xf3,0x0f,0x11,0x5a,0x50,0xf3,0x0f,0x11,0x42,0x54,0xf3,0x0f,0x11,0x4a,0x58,0xc3}))return false;
    using Factory=void*(*)(const char*,int*);
    auto factory=reinterpret_cast<Factory>(GetProcAddress(reinterpret_cast<HMODULE>(base),"CreateInterface"));
    if(!factory)return false;
    void *manager=factory("VEngineShadowMgr002",nullptr);
    SurfaceAddress vt=0;std::array<SurfaceAddress,20> slots{};
    if(!manager||!SurfaceGet(reinterpret_cast<SurfaceAddress>(manager),vt)||vt!=base+0x3959f0||!SurfaceGet(vt,slots)||
       slots[0]!=base+0x124900||slots[1]!=base+0x124b00||slots[3]!=base+0x127de0||slots[9]!=base+0x12c1b0||slots[15]!=base+0x12b7e0)return false;
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
// Resolve the engine's exact material system, immediate context and lookup
// before the detached native draw. The native routine consumes the copied
// polygon and color/lightmap scratch synchronously; it never creates decals.
static bool SurfaceNativeDrawContext(SurfaceAddress engine,void *context,void *&system,SurfaceAddress &find){
    const auto module=reinterpret_cast<SurfaceAddress>(GetModuleHandleA("materialsystem.dll"));
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    SurfaceAddress ms=0,vt=0,ctxVt=0,getter=0;
    if(!module||!SurfaceGet(module,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x1000||
       !SurfaceGet(module+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||
       nt.FileHeader.TimeDateStamp!=0x6ac56a08||nt.OptionalHeader.SizeOfImage!=0x141000||
       !SurfaceGet(engine+0x7aac50,ms)||!ms||!SurfaceGet(ms,vt)||vt!=module+0xb7470||
       !SurfaceGet(vt+0x238,find)||find!=module+0x15720||!SurfaceGet(vt+0x310,getter)||getter!=module+0x16c20||
       !SurfaceGet(reinterpret_cast<SurfaceAddress>(context),ctxVt)||ctxVt!=module+0xbcab0||
       !SurfaceMatch(module,0x15720,{0x48,0x83,0xec,0x38,0x48,0x8b,0x44,0x24,0x60,0x4c,0x8b,0x11})||
       !SurfaceMatch(module,0x16c20,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20}))return false;
    system=reinterpret_cast<void*>(ms);return true;
}
static void SurfacePreparePendingDecals(SurfaceAddress base,void *context,int group,float blend,std::vector<PreparedDecal> &prepared){
    if(group<0||group>=3||g_SurfacePendingDecals.empty()||(g_SurfaceDrawnGroups&(1u<<group)))return;
    if(!std::isfinite(blend)||blend<=0||blend>1)return;
    g_SurfaceDrawnGroups|=1u<<group;
    NativeWorld world;float visible=0;void *system=nullptr;SurfaceAddress find=0;
    if(!SurfaceWorld(base,world)||!SurfaceSameWorld(world,g_SurfaceWorld)||
       !Mirv_Tf2_SceneDrawVisibility("r_drawdecals",visible,false)||!visible||
       !SurfaceNativeDrawContext(base,context,system,find))return;
    using Find=void*(*)(void*,const char*,const char*,bool,const char*);
    std::map<std::string,void*> materials;
    for(const auto &pending:g_SurfacePendingDecals){
        const auto &d=pending.Value;
        if(d.GeometryGroup!=group)continue;
        if(!d.Visible()||!d.HasGeometry()||d.Surface>=static_cast<unsigned>(world.Count)){++g_SurfaceDecalsRejected;continue;}
        const auto surface=world.Surfaces+SurfaceAddress(d.Surface)*64;
        std::uint32_t flags=0;std::int16_t sort=0;SurfaceAddress sorts=0;int lightmap=0;float offset=0;
        std::memcpy(&offset,&d.LightmapOffset,sizeof(offset));
        if(!SurfaceGet(surface,flags)||(flags&0x800)||!SurfaceGet(surface+0x16,sort)||sort<0||
           !SurfaceGet(base+0x6a1530,sorts)||!sorts||!SurfaceGet(sorts+SurfaceAddress(sort)*16+8,lightmap)||lightmap<0||
           !std::isfinite(offset)){++g_SurfaceDecalsRejected;continue;}
        auto entry=materials.try_emplace(d.Material,nullptr);
        if(entry.second){
            auto mat=reinterpret_cast<Find>(find)(system,d.Material.c_str(),"Decal textures",false,nullptr);
            std::string name;
            if(mat&&SurfaceMaterialName(reinterpret_cast<SurfaceAddress>(mat),name)&&name==d.Material)entry.first->second=mat;
        }
        if(!entry.first->second){++g_SurfaceDecalsRejected;continue;}
        prepared.push_back({&pending,surface,entry.first->second,false});
    }
}
static void SurfaceDrawPreparedDecals(SurfaceAddress base,void *context,float blend,const std::vector<PreparedDecal> &prepared){
    if(prepared.empty())return;
    // Recheck the current world and immediate context after native dispatch.
    // No pointer survives the dispatcher; a failed recheck loses at most
    // this frame's replacement, never mutates the underlying decal pool.
    NativeWorld world;void *system=nullptr;SurfaceAddress find=0;
    if(!MaterialSystem_IsImmediate()||!SurfaceWorld(base,world)||!SurfaceSameWorld(world,g_SurfaceWorld)||
       !SurfaceNativeDrawContext(base,context,system,find)){
        g_SurfaceDecalsRejected+=prepared.size();return;
    }
    const auto draw=reinterpret_cast<DecalDrawFn>(base+0x11e6c0);
    for(const auto &item:prepared){
        const auto &pending=*item.Pending;const auto &d=pending.Value;
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
    const int old=g_SurfaceGeometryGroup;
    NativeWorld captureWorld;
    if(world&&g_SurfaceCaptureArmed&&!g_SurfaceReplayActive&&SurfaceWorld(base,captureWorld)&&SurfaceSameWorld(captureWorld,g_SurfaceWorld))g_SurfaceGeometryGroup=group;
    else g_SurfaceGeometryGroup=-1;
    std::vector<PreparedDecal> prepared;
    std::map<SurfaceAddress,PreparedDecal*> replacements;
    if(world&&g_SurfaceReplayActive&&g_SurfaceDecalGate){
        SurfacePreparePendingDecals(base,context,group,blend,prepared);
        for(auto &item:prepared)if(item.Pending->LiveObject)replacements.emplace(item.Pending->LiveObject,&item);
    }
    auto *previousGates=g_SurfaceReplacementGates;
    g_SurfaceReplacementGates=replacements.empty()?nullptr:&replacements;
    g_SurfaceDecalDispatch(context,group,blend);
    g_SurfaceReplacementGates=previousGates;
    g_SurfaceGeometryGroup=old;
    if(!prepared.empty())SurfaceDrawPreparedDecals(base,context,blend,prepared);
}
static bool SurfaceWorldDecalHooks(SurfaceAddress base){
    if(!SurfaceDecalHooks(base))return false;
    if(g_SurfaceDispatchInstalled)return true;
    if(!SurfaceMatch(base,0x116ad0,{0x40,0x53,0x57,0x48,0x83,0xec,0x58,0x48,0x8b,0x05,0x6a,0xe8,0x58,0})||
       !SurfaceMatch(base,0xe728e,{0x41,0x0f,0x28,0xd0,0x41,0x8b,0xd6,0x48,0x8b,0xcb,0xe8,0x33,0xf8,0x02,0,0x48,0x8b,0x0d,0xc4})||
       !SurfaceMatch(base,0x11e6c0,{0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x48,0x08,0x55,0x56,0x57,0x41,0x54})||
       !SurfaceMatch(base,0x11e76f,{0x4d,0x8b,0x46,0x58,0x48,0x8b,0xcb,0xff,0x50,0x48})||
       !SurfaceMatch(base,0x11eaf0,{0xf3,0x41,0x0f,0x10,0x76,0x20,0x45,0x85,0xed})||
       !SurfaceMatch(base,0x11b502,{0x48,0x83,0xc2,0x20,0x48,0x8b,0x8d,0x30,0x02})||
       !SurfaceMatch(base,0x11ed86,{0xff,0x50,0x18,0x48,0x8b,0x03,0x48,0x8b,0xcb,0xff,0x50,0x08}))return false;
    g_SurfaceDecalDispatch=reinterpret_cast<DecalDispatchFn>(base+0x116ad0);
    if(DetourTransactionBegin()!=NO_ERROR)return false;
    LONG err=DetourUpdateThread(GetCurrentThread());
    if(err==NO_ERROR)err=DetourAttach(reinterpret_cast<PVOID*>(&g_SurfaceDecalDispatch),reinterpret_cast<PVOID>(SurfaceDecalDispatchHook));
    if(err!=NO_ERROR){DetourTransactionAbort();return false;}
    if(DetourTransactionCommit()!=NO_ERROR)return false;
    g_SurfaceDispatchInstalled=true;return true;
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
    std::vector<LiveDecal> decals;
    sample.DecalsReady=ReadWorldDecals(base,world,decals,sample.DecalsUnsupported,sample.DecalStatus,sample.UnsupportedDecals);
    if(sample.DecalsReady){
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
                if(s.Appearance){
                    const auto &worldName=name(s.Material),&modelName=name(s.ModelMaterial);
                    if(!worldName.empty()&&!modelName.empty()){
                        if(worldName=="decals/simpleshadow"&&modelName=="decals/simpleshadow"){
                            if(ValidShadowTextureRect(s.UV)){projection.Texture=ShadowTexture::Simple;projection.UV=s.UV;}
                        }else projection.Texture=ShadowTexture::Other;
                    }
                }
                shadows->push_back(std::move(projection));
            }
            sample.Shadows=shadows;sample.ShadowsReady=true;
        }
    }
#endif
    return sample;
}
void Mirv_Tf2_SetSurfaceObservation(bool armed){
#ifdef _WIN64
    if(!armed&&g_SurfaceCaptureArmed){g_SurfaceObserved.clear();SurfaceResetGeometry();}
    g_SurfaceCaptureArmed=armed;
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
    if(!requested||!base||!Mirv_Tf2_ParticleIsolationReady()||!SurfaceWorld(base,world)||!SurfaceSameWorld(world,g_SurfaceWorld)||sample.World!=g_SurfaceGeneration||g_SurfaceReplayActive)return 0;
    unsigned active=0;std::ostringstream report;
    g_SurfacePendingDecals.clear();g_SurfaceDrawnGroups=0;
    g_SurfaceDecalsSubmitted=g_SurfaceDecalsRejected=g_SurfaceOpacitySubmitted=g_SurfaceOpacityNotObserved=0;
    if((requested&1)&&sample.ShadowsReady&&sample.Shadows&&g_SurfaceHooks&&GetCurrentThreadId()==g_SurfaceThread){
        float visible=0;std::vector<ShadowSlot> slots;
        auto material=Mirv_Tf2_SceneDrawVisibility("r_shadows",visible,false)&&visible!=0?SurfaceSimpleShadowMaterial():nullptr;
        if(material&&ShadowSlots(g_SurfaceShadowManager,slots)){
            auto vt=*reinterpret_cast<void***>(g_SurfaceShadowManager);
            auto create=reinterpret_cast<CreateFn>(vt[0]);auto tex=reinterpret_cast<TexFn>(vt[9]);
            std::size_t simple=0,other=0,unknown=0;
            g_SurfaceOwn=true;
            for(const auto &s:*sample.Shadows){
                if(!s.Valid())continue;
                auto handle=create(g_SurfaceShadowManager,material,material,reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xffff)),0);
                if(handle==ShadowInvalid)continue;
                g_SurfaceOwned.push_back(handle);
                const auto uv=s.ReplayTextureRect();
                tex(g_SurfaceShadowManager,handle,uv[0],uv[1],uv[2],uv[3]);
                if(s.HasSimpleTexture())++simple;
                else if(s.Texture==ShadowTexture::Other)++other;
                else ++unknown;
                g_SurfaceProject(g_SurfaceShadowManager,handle,s.Origin.data(),s.Direction.data(),s.Matrix.data(),s.Size.data(),static_cast<int>(s.Leaves.size()),s.Leaves.data(),s.Height,s.FalloffOffset,s.FalloffAmount,s.Caster.data());
                g_SurfaceBias(g_SurfaceShadowManager,handle,s.Bias);
            }
            g_SurfaceOwn=false;
            // Fail open on incomplete observation or creation. Keep live
            // shadows if replacement coverage cannot be established.
            const bool replace=sample.ShadowsUnobserved==0&&g_SurfaceOwned.size()==sample.Shadows->size();
            if(replace){
                for(const auto &s:slots)if(!(s.Flags&6)&&s.Bias!=255){g_SurfaceSavedBias[s.Handle]=s.Bias;g_SurfaceBias(g_SurfaceShadowManager,s.Handle,255);}
            }
            if(replace||!g_SurfaceOwned.empty())active|=1;
            report<<"Historical projected blobs "<<g_SurfaceOwned.size()<<"/"<<sample.Shadows->size()<<"; simple texture rect "<<simple<<"; non-simple approximations "<<other<<"; unknown appearance approximations "<<unknown<<"; unobserved "<<sample.ShadowsUnobserved<<"; live bias gates "<<g_SurfaceSavedBias.size()<<"; "<<(replace?"replacement":"partial overlay")<<". ";
        }else report<<"Shadow material, manager, or r_shadows unavailable. ";
    }
    if(requested&2){
        if(!sample.DecalsReady||!sample.Decals)report<<"Historical decal capture unavailable: "<<DecalReadLabel(sample.DecalStatus)<<". ";
        else if(!SurfaceDecalHooks(base))report<<"Decal geometry gate unavailable (ABI or installation). ";
        else {
            std::vector<LiveDecal> current;std::size_t unsupported=0;DecalRead status=DecalRead::NotRun;DecalCoverage coverage;
            const bool detached=(requested&12)&&SurfaceWorldDecalHooks(base);
            std::set<std::uint16_t> excludedSlots;
            if(ReadWorldDecals(base,world,current,unsupported,status,coverage,detached?&excludedSlots:nullptr)){
                std::size_t matched=0,hidden=0,failed=0,fading=0,fadeUnknown=0;
                g_SurfaceHiddenDecals.clear();g_SurfaceDecalDrawSkips=0;
                for(const auto &live:current){
                    const auto &v=live.Value;
                    auto it=std::lower_bound(sample.Decals->begin(),sample.Decals->end(),v.Slot,[](const Decal &a,std::uint16_t s){return a.Slot<s;});
                    const bool same=it!=sample.Decals->end()&&it->Same(v);matched+=same;
                    fading+=v.Fading();
                    // If any historical fade record was unreadable, an
                    // unmatched live fade may be that record; fail open.
                    if(!same&&v.Fading()&&sample.UnsupportedDecals.FadeInvalid){++fadeUnknown;continue;}
                    if(!same||!it->Visible()){
                        // Use the pointers from the validated record; the hook
                        // still checks surface/material/slot before suppression.
                        DecalGate gate;gate.Slot=v.Slot;gate.Surface=live.Surface;gate.Material=live.Material;
                        if(g_SurfaceHiddenDecals.emplace(live.Object,gate).second)++hidden;
                        else ++failed;
                    }
                }
                std::size_t geometryMissing=0,excludedMissing=0,opacityPlanned=0,opacityUnavailable=0,removedPlanned=0;
                if(detached)for(const auto &d:*sample.Decals){
                    if(!d.Visible())continue;
                    auto it=std::lower_bound(current.begin(),current.end(),d.Slot,[](const LiveDecal &x,std::uint16_t s){return x.Value.Slot<s;});
                    if(it!=current.end()&&it->Value.Same(d)){
                        if(!(requested&8)||d.ReplayAlpha()==it->Value.ReplayAlpha())continue;
                        if(!it->GeometryEligible||!d.NeedsOpacityReplay(it->Value)||g_SurfacePendingDecals.size()>=1024){++opacityUnavailable;continue;}
                        PendingDecal pending;pending.Value=d;pending.LiveObject=it->Object;
                        pending.LiveGate={it->Surface,it->Material,it->Value.Slot};pending.LiveVisible=it->Value.Visible();
                        g_SurfacePendingDecals.push_back(std::move(pending));++opacityPlanned;continue;
                    }
                    if(!(requested&4))continue;
                    if(excludedSlots.count(d.Slot)){++excludedMissing;continue;}
                    if(!d.HasGeometry()||g_SurfacePendingDecals.size()>=1024){++geometryMissing;continue;}
                    PendingDecal pending;pending.Value=d;g_SurfacePendingDecals.push_back(std::move(pending));++removedPlanned;
                }
                if(requested&4)report<<"Detached removed decals "<<(detached?"enabled":"guard unavailable")<<"; planned "<<removedPlanned<<"; missing geometry/bounded "<<geometryMissing<<"; unknown live slot "<<excludedMissing<<". ";
                if(requested&8)report<<"Surviving opacity replacement "<<(detached?"enabled":"guard unavailable")<<"; planned "<<opacityPlanned<<"; unavailable "<<opacityUnavailable<<". ";
                g_SurfaceDecalGate=true;active|=2;
                report<<"Historical surviving decals "<<matched<<"/"<<sample.Decals->size()<<"; geometry gates "<<hidden<<"; missing "<<sample.Decals->size()-matched<<"; unsupported live "<<unsupported<<" ("<<coverage.Describe()<<"); rejected gates "<<failed<<"; live fading "<<fading<<"; unknown historical fades left live "<<fadeUnknown<<". Unreplaced surviving marks retain live opacity. ";
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
    const bool gated=g_SurfaceDecalGate;
    const auto decalSkips=g_SurfaceDecalDrawSkips;
    g_SurfaceDecalGate=false;g_SurfaceHiddenDecals.clear();g_SurfaceDecalDrawSkips=0;
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
        (pending?" Detached decal submissions "+std::to_string(g_SurfaceDecalsSubmitted)+"/"+std::to_string(pending)+", rejected "+std::to_string(g_SurfaceDecalsRejected)+", surviving opacity "+std::to_string(g_SurfaceOpacitySubmitted)+", surviving live geometry not gated "+std::to_string(g_SurfaceOpacityNotObserved)+".":"")+
        " Scope cleanup warnings "+std::to_string(failed)+".";
#else
    return {};
#endif
}
